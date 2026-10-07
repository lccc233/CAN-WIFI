#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "can.h"
#include "can_logger.h"
#include "signal_decode.h"
#include "time_sync.h"
#include "web_page.h"
#include "mail_sender.h"
#include "web_server.h"

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;
static cJSON *rec_json(const can_log_status_t *rec);

static esp_err_t json_reply(httpd_req_t *req, const char *status, cJSON *root)
{
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t ret = httpd_resp_sendstr(req, body);
    free(body);
    return ret;
}

static esp_err_t json_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", message);
    }
    return json_reply(req, status, root);
}

// Read exactly Content-Length; reject truncation/oversize before parsing or persisting.
static cJSON *read_json_body(httpd_req_t *req, size_t limit)
{
    if (!req->content_len || req->content_len > limit) {
        json_error(req, "400 Bad Request", "Empty or oversized JSON body");
        return NULL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) {
        json_error(req, "500 Internal Server Error", "Out of memory");
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            free(body);
            json_error(req, "400 Bad Request", "Incomplete JSON body");
            return NULL;
        }
        got += n;
    }
    body[got] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(body, got + 1, NULL, true);
    free(body);
    if (!root) json_error(req, "400 Bad Request", "Invalid JSON");
    return root;
}

// ---- 集中常量（D3） ----
#define HTTP_TASK_STACK_SIZE    12288   // /api/messages 局部 snapshot+freq 快照较大
#define EXPORT_READ_BATCH       64      // 导出每批读取条数
#define EXPORT_FLUSH_INTERVAL_MS 10     // 每批发送间隔（让出 CPU 给 RX/其他连接）
#define EXPORT_LINE_BUF         4096

// /api/messages 忙闸：同一时刻只处理一个 poll 请求，后续请求快速返回 busy
// （C3）。若上一个 poll 客户端中途断开导致标志未释放，3 秒看门狗强制放行，
// 防止整个轮询被永久卡死。check-then-set 在临界区内完成，消除并发 TOCTOU。
static portMUX_TYPE s_busy_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy_msgs = false;
static uint32_t s_busy_since_ms = 0;

static bool msgs_busy_try_acquire(uint32_t now_ms)
{
    bool acquired = false;
    portENTER_CRITICAL(&s_busy_mux);
    if (!s_busy_msgs || (uint32_t)(now_ms - s_busy_since_ms) >= 3000) {
        s_busy_msgs = true;
        s_busy_since_ms = now_ms;
        acquired = true;
    }
    portEXIT_CRITICAL(&s_busy_mux);
    return acquired;
}

static void msgs_busy_release(void)
{
    portENTER_CRITICAL(&s_busy_mux);
    s_busy_msgs = false;
    portEXIT_CRITICAL(&s_busy_mux);
}

// GET / — 返回 HTML 页面
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    return ESP_OK;
}

// GET /api/messages — 返回 CAN 消息 JSON
static esp_err_t api_messages_handler(httpd_req_t *req)
{
    // C3 忙闸：已有 poll 在处理中时快速返回，让客户端沿用本地数据
    if (!msgs_busy_try_acquire((uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS))) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_sendstr(req, "{\"busy\":1}");
        return ESP_OK;
    }

    can_msg_entry_t snapshot[CAN_RX_RING_SIZE];
    uint32_t seq[CAN_RX_RING_SIZE];
    uint32_t count, total;
    can_get_snapshot_seq(snapshot, seq, CAN_RX_RING_SIZE, &count, &total);

    can_id_freq_t freqs[CAN_FREQ_MAX_IDS];
    int freq_count = can_get_id_freqs(freqs, CAN_FREQ_MAX_IDS);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // 授时状态：前端据此把 t(开机ms) 换算为真实时间并本地格式化
    uint32_t now_boot = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    int64_t now_epoch = 0;
    bool synced = time_sync_get(now_boot, &now_epoch);
    char clk_hdr[128];
    snprintf(clk_hdr, sizeof(clk_hdr),
             "{\"boot\":\"%08lx\",\"clk\":{\"sync\":%s,\"boot\":%llu,\"ep\":%lld},",
             (unsigned long)can_get_boot_id(), synced ? "true" : "false",
             (unsigned long long)now_boot,
             (unsigned long long)now_epoch);
    httpd_resp_sendstr_chunk(req, clk_hdr);

    // 录制状态（并入 messages 响应，前端 200ms 轮询自动携带）
    can_log_status_t rec;
    can_log_get_status(&rec);
    cJSON *rec_status = rec_json(&rec);
    char *rec_body = rec_status ? cJSON_PrintUnformatted(rec_status) : NULL;
    cJSON_Delete(rec_status);
    if (!rec_body) { msgs_busy_release(); return ESP_FAIL; }
    esp_err_t rec_ret = httpd_resp_sendstr_chunk(req, "\"rec\":");
    if (rec_ret == ESP_OK) rec_ret = httpd_resp_sendstr_chunk(req, rec_body);
    free(rec_body);
    char total_hdr[64];
    snprintf(total_hdr, sizeof(total_hdr), ",\"total\":%lu,\"freqs\":[", (unsigned long)total);
    if (rec_ret == ESP_OK) rec_ret = httpd_resp_sendstr_chunk(req, total_hdr);
    if (rec_ret != ESP_OK) { msgs_busy_release(); return ESP_FAIL; }

    for (int i = 0; i < freq_count; i++) {
        char entry[80];
        snprintf(entry, sizeof(entry), "%s{\"id\":\"0x%lX\",\"f\":%lu}",
                 (i > 0) ? "," : "",
                 (unsigned long)freqs[i].id,
                 (unsigned long)freqs[i].rate_x10);
        httpd_resp_sendstr_chunk(req, entry);
    }

    httpd_resp_sendstr_chunk(req, "],\"messages\":[");

    // emitted 而非 i 决定逗号前缀：某条因防御性检查被跳过时 JSON 仍合法
    uint32_t emitted = 0;
    for (uint32_t i = 0; i < count; i++) {
        char entry[160];
        bool ok = true;
        int n = snprintf(entry, sizeof(entry),
            "%s{\"seq\":%lu,\"t\":%lu,\"id\":\"0x%lX\",\"dlc\":%d,\"ext\":%s,\"data\":\"",
            (emitted > 0) ? "," : "",
            (unsigned long)seq[i],
            (unsigned long)snapshot[i].timestamp_ms,
            (unsigned long)snapshot[i].id,
            snapshot[i].dlc,
            snapshot[i].extended ? "true" : "false");
        // 数据段上限 8*3-1=23 字符 + 结尾 "\"}" + NUL 共 26；字段均有界时
        // 正常最大 ~90 字符，远用不到该余量——纯防御，防截断后 n 越界
        if (n < 0 || (size_t)n + 27 > sizeof(entry)) ok = false;
        for (int j = 0; ok && j < snapshot[i].dlc && j < 8; j++) {
            if (j > 0) entry[n++] = ' ';
            if (snprintf(entry + n, sizeof(entry) - n, "%02X", snapshot[i].data[j]) != 2) {
                ok = false;
                break;
            }
            n += 2;
        }
        if (ok) {
            entry[n++] = '"';
            entry[n++] = '}';
            entry[n] = '\0';
            httpd_resp_sendstr_chunk(req, entry);
            emitted++;
        }
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    msgs_busy_release();
    return ESP_OK;
}

// Signal settings stay independent of the immutable configuration of each recording.
#define SIGNALS_NVS_NS "webui"
#define SIGNALS_NVS_KEY "signals"
#define SIGNALS_MAX_BYTES CAN_LOG_CONFIG_MAX_BYTES
#define SIGNALS_MAX_ITEMS CAN_LOG_MAX_IDS

static bool json_integer(const cJSON *v, int low, int high)
{
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) &&
           v->valuedouble >= low && v->valuedouble <= high &&
           floor(v->valuedouble) == v->valuedouble;
}

static bool signals_entry_valid(cJSON *item)
{
    if (!cJSON_IsObject(item)) return false;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(item, "unit");
    cJSON *start = cJSON_GetObjectItemCaseSensitive(item, "start");
    cJSON *len = cJSON_GetObjectItemCaseSensitive(item, "len");
    cJSON *endian = cJSON_GetObjectItemCaseSensitive(item, "endian");
    cJSON *factor = cJSON_GetObjectItemCaseSensitive(item, "factor");
    cJSON *offset = cJSON_GetObjectItemCaseSensitive(item, "offset");
    cJSON *color = cJSON_GetObjectItemCaseSensitive(item, "color");
    if (!cJSON_IsString(id) || strlen(id->valuestring) < 3 || strlen(id->valuestring) > 10 ||
        id->valuestring[0] != '0' || (id->valuestring[1] != 'x' && id->valuestring[1] != 'X')) return false;
    for (const char *v = id->valuestring + 2; *v; v++) if (!isxdigit((unsigned char)*v)) return false;
    unsigned long numeric_id = strtoul(id->valuestring + 2, NULL, 16);
    if (numeric_id > 0x1FFFFFFFUL) return false;
    char canonical_id[11];
    snprintf(canonical_id, sizeof(canonical_id), "0x%lx", numeric_id);
    if (!cJSON_SetValuestring(id, canonical_id)) return false;
    if (!cJSON_IsString(name) || !*name->valuestring || strlen(name->valuestring) > 64 ||
        !cJSON_IsString(unit) || strlen(unit->valuestring) > 32 ||
        !json_integer(start, 0, 63) || !json_integer(len, 1, 64) || !cJSON_IsString(endian) ||
        !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(item, "signed")) ||
        !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(item, "enabled")) ||
        !cJSON_IsNumber(factor) || !isfinite(factor->valuedouble) ||
        !cJSON_IsNumber(offset) || !isfinite(offset->valuedouble)) return false;
    bool has_name = false;
    for (const char *p = name->valuestring; *p; p++) {
        if (!isspace((unsigned char)*p)) { has_name = true; break; }
    }
    if (!has_name) return false;
    bool intel = strcmp(endian->valuestring, "intel") == 0;
    if (!intel && strcmp(endian->valuestring, "moto") != 0) return false;
    int first_bit = intel ? start->valueint : (start->valueint / 8) * 8 + 7 - (start->valueint % 8);
    if (first_bit + len->valueint > 64) return false;
    if (!cJSON_IsString(color) || color->valuestring[0] != '#') return false;
    size_t color_len = strlen(color->valuestring);
    if (color_len != 4 && color_len != 7 && color_len != 9) return false;
    for (const char *v = color->valuestring + 1; *v; v++) if (!isxdigit((unsigned char)*v)) return false;
    return true;
}

static bool signals_valid(cJSON *array)
{
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > SIGNALS_MAX_ITEMS) return false;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        if (!signals_entry_valid(item)) return false;
        for (cJSON *other = array->child; other != item; other = other->next) {
            if (!strcmp(cJSON_GetObjectItemCaseSensitive(other, "id")->valuestring,
                        cJSON_GetObjectItemCaseSensitive(item, "id")->valuestring) &&
                !strcmp(cJSON_GetObjectItemCaseSensitive(other, "name")->valuestring,
                        cJSON_GetObjectItemCaseSensitive(item, "name")->valuestring)) return false;
        }
    }
    return true;
}

static esp_err_t api_signals_handler(httpd_req_t *req)
{
    bool metadata = false;
    char query[32], value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "meta", value, sizeof(value)) == ESP_OK) metadata = !strcmp(value, "1");
    if (req->method == HTTP_POST) {
        cJSON *array = read_json_body(req, SIGNALS_MAX_BYTES);
        if (!array) return ESP_OK;
        if (!signals_valid(array)) {
            cJSON_Delete(array);
            return json_error(req, "400 Bad Request", "Invalid signal definition or duplicate id/name");
        }
        int count = cJSON_GetArraySize(array);
        char *compact = cJSON_PrintUnformatted(array);
        cJSON_Delete(array);
        if (!compact) return json_error(req, "500 Internal Server Error", "Out of memory");
        if (strlen(compact) > SIGNALS_MAX_BYTES) {
            free(compact);
            return json_error(req, "400 Bad Request", "Signal configuration exceeds 3500 bytes");
        }
        nvs_handle_t nvs;
        esp_err_t err = nvs_open(SIGNALS_NVS_NS, NVS_READWRITE, &nvs);
        if (err == ESP_OK) {
            err = nvs_set_blob(nvs, SIGNALS_NVS_KEY, compact, strlen(compact));
            if (err == ESP_OK) err = nvs_commit(nvs);
            nvs_close(nvs);
        }
        free(compact);
        if (err != ESP_OK) return json_error(req, "500 Internal Server Error", "NVS write failed; local draft retained");
        cJSON *root = cJSON_CreateObject();
        if (root) { cJSON_AddBoolToObject(root, "ok", true); cJSON_AddNumberToObject(root, "n", count); }
        return json_reply(req, "200 OK", root);
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SIGNALS_NVS_NS, NVS_READONLY, &nvs);
    bool configured = false;
    cJSON *array = NULL;
    if (err == ESP_OK) {
        size_t size = 0;
        err = nvs_get_blob(nvs, SIGNALS_NVS_KEY, NULL, &size);
        if (err == ESP_OK && size > 0 && size <= SIGNALS_MAX_BYTES) {
            char *stored = malloc(size + 1);
            if (!stored) { nvs_close(nvs); return json_error(req, "500 Internal Server Error", "Out of memory"); }
            err = nvs_get_blob(nvs, SIGNALS_NVS_KEY, stored, &size);
            if (err == ESP_OK) {
                stored[size] = 0;
                array = cJSON_ParseWithLengthOpts(stored, size + 1, NULL, true);
                configured = true;
            }
            free(stored);
        } else if (err == ESP_OK) {
            err = ESP_ERR_INVALID_SIZE;
        }
        nvs_close(nvs);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        cJSON_Delete(array);
        return json_error(req, "500 Internal Server Error", "Stored configuration unavailable");
    }
    if (configured && (!array || !signals_valid(array))) {
        cJSON_Delete(array);
        return json_error(req, "500 Internal Server Error", "Stored configuration is invalid; no cache migration performed");
    }
    if (!array) array = cJSON_CreateArray();
    if (!metadata) return json_reply(req, "200 OK", array);
    cJSON *root = cJSON_CreateObject();
    if (!root || !array) { cJSON_Delete(root); cJSON_Delete(array); return json_error(req, "500 Internal Server Error", "Out of memory"); }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "configured", configured);
    cJSON_AddItemToObject(root, "signals", array);
    return json_reply(req, "200 OK", root);
}

// POST /api/send — 发送 CAN 帧
// D2：改用 cJSON 解析（IDF 内置），比手写 strstr 更健壮；
// 兼容旧语义：id 可为 "0x7E0" 或数字，data 为空格分隔 hex 字符串
static esp_err_t api_send_handler(httpd_req_t *req)
{
    char body[256];
    // 循环读满 body：TCP 分段时一次 recv 可能只拿到半截（对齐 /api/signals 读法）
    int total = 0, recvd;
    while (total < (int)(sizeof(body) - 1) &&
           (recvd = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0) {
        total += recvd;
    }
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }
    cJSON *jid = cJSON_GetObjectItem(root, "id");
    cJSON *jdlc = cJSON_GetObjectItem(root, "dlc");
    cJSON *jext = cJSON_GetObjectItem(root, "extended");
    cJSON *jdata = cJSON_GetObjectItem(root, "data");

    uint32_t id = jid ? (cJSON_IsString(jid)
                        ? (uint32_t)strtoul(jid->valuestring, NULL, 0)
                        : (uint32_t)jid->valueint) : 0;
    uint8_t dlc = jdlc && jdlc->valueint >= 0 ? (uint8_t)jdlc->valueint : 0;
    if (dlc > 8) dlc = 8;
    bool extended = cJSON_IsBool(jext) && cJSON_IsTrue(jext);

    uint8_t data[8] = {0};
    if (cJSON_IsString(jdata) && jdata->valuestring) {
        // 逐个 hex token 解析（容忍多空格，忽略引号边界）
        const char *pc = jdata->valuestring;
        for (int i = 0; i < dlc && i < 8 && *pc; i++) {
            data[i] = (uint8_t)strtoul(pc, (char **)&pc, 16);
            while (*pc == ' ') pc++;
        }
    } else {
        // 无 data 字段（如 dlc==0）：保持 0
    }
    cJSON_Delete(root);

    if (id == 0 && dlc == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid message");
        return ESP_FAIL;
    }

    esp_err_t ret = can_send_message(id, extended, dlc, data);
    if (ret == ESP_ERR_INVALID_ARG) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid id/dlc");
        return ESP_FAIL;
    }
    if (ret != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"TX failed\"}");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/clear — 清空消息缓冲
static esp_err_t api_clear_handler(httpd_req_t *req)
{
    can_clear_ring();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/time — 浏览器授时（epoch_ms 为 UTC Unix 毫秒，tz 为浏览器时区偏移分钟）
static esp_err_t api_time_handler(httpd_req_t *req)
{
    char body[128];
    // 循环读满 body（同 /api/send）
    int total = 0, recvd;
    while (total < (int)(sizeof(body) - 1) &&
           (recvd = httpd_req_recv(req, body + total, sizeof(body) - 1 - total)) > 0) {
        total += recvd;
    }
    if (total <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[total] = '\0';

    char *p = strstr(body, "\"epoch_ms\"");
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing epoch_ms");
        return ESP_FAIL;
    }
    p = strchr(p, ':');
    if (!p) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_FAIL;
    }
    int64_t epoch_ms = (int64_t)strtoll(p + 1, NULL, 10);

    int tz_min = 0;
    p = strstr(body, "\"tz\"");
    if (p) {
        p = strchr(p, ':');
        if (p) tz_min = atoi(p + 1);
    }
    // A3：钳位到 ±14h，防异常值产生怪时间
    if (tz_min < -840) tz_min = -840;
    if (tz_min > 840) tz_min = 840;

    // 合理性：Unix 时刻应在 2000-01-01 ~ 2100-01-01 之间
    if (epoch_ms < 946684800000LL || epoch_ms > 4102444800000LL) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"epoch out of range\"}");
        return ESP_OK;
    }

    time_sync_set(epoch_ms, tz_min);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static cJSON *rec_json(const can_log_status_t *rec)
{
    cJSON *out = cJSON_CreateObject();
    if (!out) return NULL;
    cJSON_AddStringToObject(out, "session", rec->session);
    cJSON_AddBoolToObject(out, "on", rec->recording);
    cJSON_AddNumberToObject(out, "cnt", rec->count);
    cJSON_AddNumberToObject(out, "cap", rec->capacity);
    cJSON_AddNumberToObject(out, "drop", rec->dropped);
    cJSON_AddNumberToObject(out, "ms", rec->duration_ms);
    cJSON_AddBoolToObject(out, "psram", rec->psram_ok);
    cJSON_AddBoolToObject(out, "full", rec->full);
    cJSON_AddBoolToObject(out, "protected", rec->protected_record);
    cJSON_AddNumberToObject(out, "rx_lost", rec->rx_lost);
    cJSON_AddBoolToObject(out, "quality_known", rec->rx_quality_known);
    return out;
}

static esp_err_t record_reply(httpd_req_t *req, const can_log_status_t *rec)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *status = rec_json(rec);
    char *config = malloc(CAN_LOG_CONFIG_MAX_BYTES + 1);
    if (!root || !status || !config) {
        cJSON_Delete(root); cJSON_Delete(status); free(config);
        return json_error(req, "500 Internal Server Error", "Out of memory");
    }
    esp_err_t err = *rec->session ? can_log_copy_config(rec->session, config, CAN_LOG_CONFIG_MAX_BYTES + 1) : ESP_OK;
    if (!*rec->session) strcpy(config, "[]");
    cJSON *signals = err == ESP_OK ? cJSON_Parse(config) : NULL;
    free(config);
    if (!signals) {
        cJSON_Delete(root); cJSON_Delete(status);
        return json_error(req, "409 Conflict", "Recording changed; reload status");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddItemToObject(root, "rec", status);
    cJSON_AddItemToObject(root, "signals", signals);
    return json_reply(req, "200 OK", root);
}

static bool get_hex_token(cJSON *root, const char *key, const char **value)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) != 16) return false;
    for (const char *p = item->valuestring; *p; p++) if (!isxdigit((unsigned char)*p)) return false;
    *value = item->valuestring;
    return true;
}

static bool get_session(cJSON *root, const char **session)
{
    return get_hex_token(root, "session", session);
}

static esp_err_t api_rec_status_handler(httpd_req_t *req)
{
    can_log_status_t rec;
    can_log_get_status(&rec);
    return record_reply(req, &rec);
}

// New records freeze signal definitions and select only IDs required by enabled signals.
static esp_err_t api_rec_start_handler(httpd_req_t *req)
{
    can_log_status_t rec;
    if (!req->content_len) {
        esp_err_t err = can_log_start();
        if (err != ESP_OK) return json_error(req, "409 Conflict", "Recorder unavailable or previous record protected");
        can_log_get_status(&rec);
        return record_reply(req, &rec);
    }
    cJSON *root = read_json_body(req, CAN_LOG_CONFIG_MAX_BYTES + 128);
    if (!root) return ESP_OK;
    cJSON *signals = cJSON_GetObjectItemCaseSensitive(root, "signals");
    if (!signals_valid(signals)) {
        cJSON_Delete(root);
        return json_error(req, "400 Bad Request", "Invalid recording signal definitions");
    }
    uint32_t ids[CAN_LOG_MAX_IDS];
    size_t id_count = 0;
    cJSON *signal = NULL;
    cJSON_ArrayForEach(signal, signals) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(signal, "enabled"))) continue;
        uint32_t id = strtoul(cJSON_GetObjectItemCaseSensitive(signal, "id")->valuestring + 2, NULL, 16);
        bool found = false;
        for (size_t i = 0; i < id_count; i++) if (ids[i] == id) found = true;
        if (!found) ids[id_count++] = id;
    }
    char *config = cJSON_PrintUnformatted(signals);
    cJSON_Delete(root);
    if (!config) return json_error(req, "500 Internal Server Error", "Out of memory");
    if (!id_count || strlen(config) > CAN_LOG_CONFIG_MAX_BYTES) {
        free(config);
        return json_error(req, "400 Bad Request", "Need enabled signals within configuration size limit");
    }
    esp_err_t err = can_log_start_config(config, ids, id_count, &rec);
    free(config);
    if (err != ESP_OK) return json_error(req, "409 Conflict", "Recorder unavailable, active, or previous record protected");
    return record_reply(req, &rec);
}

static esp_err_t api_rec_stop_handler(httpd_req_t *req)
{
    can_log_status_t rec;
    if (!req->content_len) {
        can_log_get_status(&rec);
        if (rec.protected_record) return json_error(req, "409 Conflict", "Session required for protected recording");
        if (can_log_stop() != ESP_OK) return json_error(req, "409 Conflict", "Recorder unavailable");
        can_log_get_status(&rec);
    } else {
        cJSON *root = read_json_body(req, 128);
        if (!root) return ESP_OK;
        const char *session = NULL;
        esp_err_t err = get_session(root, &session) ? can_log_stop_session(session, &rec) : ESP_ERR_INVALID_ARG;
        cJSON_Delete(root);
        if (err != ESP_OK) return json_error(req, "409 Conflict", "Recording identity mismatch");
    }
    return record_reply(req, &rec);
}

static esp_err_t api_rec_release_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req, 256);
    if (!root) return ESP_OK;
    const char *session = NULL;
    const char *client = NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (get_session(root, &session)) {
        err = get_hex_token(root, "client", &client) ? can_log_unprotect_client(session, client) : can_log_unprotect(session);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) return json_error(req, "409 Conflict", "另一页面正在同步或导出；请完成同步后再开始新记录");
    can_log_status_t rec;
    can_log_get_status(&rec);
    return record_reply(req, &rec);
}

static esp_err_t api_rec_ack_handler(httpd_req_t *req)
{
    cJSON *root = read_json_body(req, 256);
    if (!root) return ESP_OK;
    const char *session = NULL, *client = NULL;
    cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
    esp_err_t err = ESP_ERR_INVALID_ARG;
    cJSON *receipt = NULL;
    if (get_session(root, &session) && get_hex_token(root, "client", &client) && json_integer(count, 0, INT32_MAX)) {
        err = can_log_sync_done(session, client, count->valueint);
        if (err == ESP_OK) {
            receipt = cJSON_CreateObject();
            if (receipt) {
                cJSON_AddBoolToObject(receipt, "ok", true);
                cJSON_AddStringToObject(receipt, "session", session);
                cJSON_AddNumberToObject(receipt, "count", count->valueint);
            }
        }
    }
    cJSON_Delete(root);
    if (err != ESP_OK) return json_error(req, "409 Conflict", "Record identity or final count changed; sync not acknowledged");
    return json_reply(req, "200 OK", receipt);
}

static esp_err_t api_rec_clear_handler(httpd_req_t *req)
{
    if (can_log_clear() != ESP_OK) return json_error(req, "409 Conflict", "Record active or protected; explicit release required");
    can_log_status_t rec;
    can_log_get_status(&rec);
    return record_reply(req, &rec);
}

static esp_err_t api_rec_data_handler(httpd_req_t *req)
{
    char query[96], session[17], client[17], offset_str[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "session", session, sizeof(session)) != ESP_OK || strlen(session) != 16 ||
        httpd_query_key_value(query, "client", client, sizeof(client)) != ESP_OK || strlen(client) != 16 ||
        httpd_query_key_value(query, "from", offset_str, sizeof(offset_str)) != ESP_OK || !*offset_str) {
        return json_error(req, "400 Bad Request", "Need session, client and from index");
    }
    for (const char *p = offset_str; *p; p++) if (!isdigit((unsigned char)*p)) return json_error(req, "400 Bad Request", "Invalid from index");
    errno = 0;
    unsigned long offset = strtoul(offset_str, NULL, 10);
    if (errno || offset > UINT32_MAX) return json_error(req, "400 Bad Request", "Invalid from index");
    can_msg_entry_t frames[CAN_LOG_READ_MAX_ENTRIES];
    uint32_t count = 0;
    can_log_status_t rec;
    esp_err_t ret = can_log_read_client(session, client, offset, frames, CAN_LOG_READ_MAX_ENTRIES, &count, &rec);
    if (ret != ESP_OK) return json_error(req, "409 Conflict", "Recording changed or from index unavailable");
    cJSON *status = rec_json(&rec);
    char *status_json = status ? cJSON_PrintUnformatted(status) : NULL;
    cJSON_Delete(status);
    if (!status_json) return json_error(req, "500 Internal Server Error", "Out of memory");
    char out[4096];
    int n = snprintf(out, sizeof(out), "{\"ok\":true,\"rec\":%s,\"from\":%lu,\"next\":%lu,\"frames\":[",
                     status_json, offset, offset + count);
    free(status_json);
    if (n < 0 || n >= sizeof(out)) return ESP_FAIL;
    size_t used = n;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    for (uint32_t i = 0; i < count; i++) {
        if (sizeof(out) - used < 180) {
            if (httpd_resp_send_chunk(req, out, used) != ESP_OK) return ESP_FAIL;
            used = 0;
        }
        const can_msg_entry_t *m = &frames[i];
        n = snprintf(out + used, sizeof(out) - used,
                     "%s{\"seq\":%lu,\"t\":%lu,\"id\":\"0x%lx\",\"dlc\":%u,\"ext\":%s,\"data\":\"",
                     i ? "," : "", offset + i, (unsigned long)m->timestamp_ms,
                     (unsigned long)m->id, m->dlc, m->extended ? "true" : "false");
        if (n < 0 || (size_t)n >= sizeof(out) - used) return ESP_FAIL;
        used += n;
        for (uint8_t j = 0; j < m->dlc && j < 8; j++) {
            n = snprintf(out + used, sizeof(out) - used, "%s%02X", j ? " " : "", m->data[j]);
            if (n < 0 || (size_t)n >= sizeof(out) - used) return ESP_FAIL;
            used += n;
        }
        out[used++] = '"'; out[used++] = '}';
    }
    out[used++] = ']'; out[used++] = '}';
    if (httpd_resp_send_chunk(req, out, used) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

// GET /api/export — 以 CSV 流式下载全部录制数据
typedef struct {
    httpd_req_t *req;
    can_log_status_t record;
    bool raw;
} export_job_t;

static bool s_export_busy;
static portMUX_TYPE s_export_mux = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t export_stream(export_job_t *job)
{
    httpd_req_t *req = job->req;
    can_log_status_t st = job->record;
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"can_log.csv\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // CSV 表头（0x18FF0182 填转矩/转速/故障列，0x18FF0282 填电流/电压列）
    // time 列：已授时为 "2026-09-20 14:35:01.123"，未授时为 "boot+H:MM:SS.mmm"
    httpd_resp_set_hdr(req, "X-Log-Clock", time_sync_is_synced() ? "rtc" : "boot");
    httpd_resp_set_hdr(req, "X-Record-Session", st.session);
    const char *header = job->raw ? "no,time,id,dlc,ext,data\r\n" :
        "no,time,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V\r\n";
    if (httpd_resp_sendstr_chunk(req, header) != ESP_OK) return ESP_FAIL;

    // C1：分批读取 + 行缓冲攒批发送，减少 syscall 与网络小包
    can_msg_entry_t buf[EXPORT_READ_BATCH];
    char line[EXPORT_LINE_BUF];
    size_t used = 0;
    uint32_t sent = 0;
    uint32_t seq = 0;
    bool aborted = false;

    while (sent < st.count && !aborted) {
        uint32_t wanted = st.count - sent;
        if (wanted > EXPORT_READ_BATCH) wanted = EXPORT_READ_BATCH;
        uint32_t to_read = 0;
        can_log_status_t read_status;
        if (can_log_read(st.session, sent, buf, wanted, &to_read, &read_status) != ESP_OK ||
            to_read != wanted) return ESP_FAIL;

        for (uint32_t i = 0; i < to_read; i++) {
            seq++;
            const can_msg_entry_t *m = &buf[i];
            char tstr[40];
            if (!time_sync_format_boot_ms(m->timestamp_ms, tstr, sizeof(tstr))) {
                // 未授时回退：boot+相对秒
                uint32_t s = m->timestamp_ms / 1000;
                uint32_t ms = m->timestamp_ms % 1000;
                snprintf(tstr, sizeof(tstr), "boot+%lu:%02lu:%02lu.%03lu",
                         (unsigned long)(s / 3600),
                         (unsigned long)(s % 3600 / 60),
                         (unsigned long)(s % 60),
                         (unsigned long)ms);
            }
            int n = snprintf(line + used, sizeof(line) - used,
                             "%lu,%s,0x%lX,",
                             (unsigned long)seq,
                             tstr,
                             (unsigned long)m->id);
            if (n < 0 || (size_t)n >= sizeof(line) - used) { aborted = true; break; }
            used += n;

            if (job->raw) {
                n = snprintf(line + used, sizeof(line) - used, "%u,%u,", m->dlc, m->extended);
                if (n < 0 || (size_t)n >= sizeof(line) - used) { aborted = true; break; }
                used += n;
                for (uint8_t j = 0; j < m->dlc && j < 8; j++) {
                    n = snprintf(line + used, sizeof(line) - used, "%s%02X", j ? " " : "", m->data[j]);
                    if (n < 0 || (size_t)n >= sizeof(line) - used) { aborted = true; break; }
                    used += n;
                }
                if (aborted) break;
                n = 0;
            } else if (m->id == SIG_ID_MOTOR_DRIVE) {
                sig_motor_t sg;
                sig_decode_motor(m->data, m->dlc, &sg);
                if (sg.valid) {
                    n = snprintf(line + used, sizeof(line) - used, "%d,%d,%u,%u,,",
                                 (int)sg.torque, (int)sg.speed_rpm,
                                 (unsigned)sg.fault_code, (unsigned)sg.fault_level);
                } else {
                    n = snprintf(line + used, sizeof(line) - used, ",,,,,");
                }
            } else if (m->id == SIG_ID_BUS_VI) {
                sig_bus_vi_t sv;
                sig_decode_bus_vi(m->data, m->dlc, &sv);
                if (sv.valid) {
                    n = snprintf(line + used, sizeof(line) - used, ",,,,%s%d.%d,%d.%d",
                                 sv.current_x10 < 0 ? "-" : "",
                                 (int)(sv.current_x10 < 0 ? -sv.current_x10 : sv.current_x10) / 10,
                                 (int)(sv.current_x10 < 0 ? -sv.current_x10 : sv.current_x10) % 10,
                                 (int)sv.voltage_x10 / 10,
                                 (int)sv.voltage_x10 % 10);
                } else {
                    n = snprintf(line + used, sizeof(line) - used, ",,,,,");
                }
            } else {
                n = snprintf(line + used, sizeof(line) - used, ",,,,,");
            }
            if (n < 0 || (size_t)n >= sizeof(line) - used) { aborted = true; break; }
            used += n;

            if (used + 2 <= sizeof(line)) {
                line[used++] = '\r';
                line[used++] = '\n';
            } else { aborted = true; break; }

            // 攒够一半缓冲就刷出
            if (used > sizeof(line) / 2) {
                if (httpd_resp_send_chunk(req, line, used) != ESP_OK) {
                    return ESP_FAIL; // 客户端断开
                }
                used = 0;
                vTaskDelay(pdMS_TO_TICKS(EXPORT_FLUSH_INTERVAL_MS));
            }
        }
        sent += to_read;
    }

    if (aborted) return ESP_FAIL;
    // 刷出剩余
    if (used > 0) {
        if (httpd_resp_send_chunk(req, line, used) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) return ESP_FAIL;
    ESP_LOGI(TAG, "Exported %u frames as CSV", (unsigned)sent);
    return ESP_OK;
}


static void export_task(void *arg)
{
    export_job_t *job = arg;
    esp_err_t ret = export_stream(job);
    if (ret != ESP_OK) ESP_LOGW(TAG, "CSV export interrupted; source record retained");
    can_log_release(job->record.session);
    int fd = httpd_req_to_sockfd(job->req);
    httpd_handle_t server = job->req->handle;
    httpd_req_async_handler_complete(job->req);
    httpd_sess_trigger_close(server, fd);
    free(job);
    portENTER_CRITICAL(&s_export_mux);
    s_export_busy = false;
    portEXIT_CRITICAL(&s_export_mux);
    vTaskDelete(NULL);
}

static esp_err_t api_export_handler(httpd_req_t *req)
{
    portENTER_CRITICAL(&s_export_mux);
    bool busy = s_export_busy;
    if (!busy) s_export_busy = true;
    portEXIT_CRITICAL(&s_export_mux);
    if (busy) return json_error(req, "409 Conflict", "CSV export already in progress");
    export_job_t *job = calloc(1, sizeof(*job));
    can_log_status_t current;
    can_log_get_status(&current);
    esp_err_t err = job ? can_log_acquire(current.session, &job->record) : ESP_ERR_NO_MEM;
    if (err == ESP_OK && !job->record.count) {
        can_log_release(job->record.session);
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK) {
        char config[CAN_LOG_CONFIG_MAX_BYTES + 1];
        err = can_log_copy_config(job->record.session, config, sizeof(config));
        if (err == ESP_OK) job->raw = strcmp(config, "[]") != 0;
        else can_log_release(job->record.session);
    }
    if (err == ESP_OK) {
        err = httpd_req_async_handler_begin(req, &job->req);
        if (err == ESP_OK && xTaskCreate(export_task, "csv_export", 8192, job, 2, NULL) == pdPASS) return ESP_OK;
        can_log_release(job->record.session);
        if (job->req) {
            json_error(job->req, "503 Service Unavailable", "CSV export task unavailable");
            httpd_req_async_handler_complete(job->req);
            free(job);
            portENTER_CRITICAL(&s_export_mux); s_export_busy = false; portEXIT_CRITICAL(&s_export_mux);
            return ESP_OK;
        }
    }
    free(job);
    portENTER_CRITICAL(&s_export_mux); s_export_busy = false; portEXIT_CRITICAL(&s_export_mux);
    return json_error(req, "409 Conflict", "Stop recording first; retained record required for export");
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 17;
    config.stack_size = HTTP_TASK_STACK_SIZE;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t messages = { .uri = "/api/messages", .method = HTTP_GET, .handler = api_messages_handler };
    httpd_uri_t send = { .uri = "/api/send", .method = HTTP_POST, .handler = api_send_handler };
    httpd_uri_t clear = { .uri = "/api/clear", .method = HTTP_POST, .handler = api_clear_handler };
    httpd_uri_t rec_start = { .uri = "/api/rec/start", .method = HTTP_POST, .handler = api_rec_start_handler };
    httpd_uri_t rec_stop = { .uri = "/api/rec/stop", .method = HTTP_POST, .handler = api_rec_stop_handler };
    httpd_uri_t rec_status = { .uri = "/api/rec/status", .method = HTTP_GET, .handler = api_rec_status_handler };
    httpd_uri_t rec_data = { .uri = "/api/rec/data", .method = HTTP_GET, .handler = api_rec_data_handler };
    httpd_uri_t rec_release = { .uri = "/api/rec/release", .method = HTTP_POST, .handler = api_rec_release_handler };
    httpd_uri_t rec_ack = { .uri = "/api/rec/ack", .method = HTTP_POST, .handler = api_rec_ack_handler };
    httpd_uri_t rec_clear = { .uri = "/api/rec/clear", .method = HTTP_POST, .handler = api_rec_clear_handler };
    httpd_uri_t export = { .uri = "/api/export", .method = HTTP_GET, .handler = api_export_handler };
    httpd_uri_t time = { .uri = "/api/time", .method = HTTP_POST, .handler = api_time_handler };
    httpd_uri_t signals_get = { .uri = "/api/signals", .method = HTTP_GET, .handler = api_signals_handler };
    httpd_uri_t signals_post = { .uri = "/api/signals", .method = HTTP_POST, .handler = api_signals_handler };
    httpd_uri_t mail_send = { .uri = "/api/mail/send", .method = HTTP_POST, .handler = mail_send_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &messages);
    httpd_register_uri_handler(s_server, &send);
    httpd_register_uri_handler(s_server, &clear);
    httpd_register_uri_handler(s_server, &rec_start);
    httpd_register_uri_handler(s_server, &rec_stop);
    httpd_register_uri_handler(s_server, &rec_clear);
    httpd_register_uri_handler(s_server, &rec_status);
    httpd_register_uri_handler(s_server, &rec_data);
    httpd_register_uri_handler(s_server, &rec_release);
    httpd_register_uri_handler(s_server, &rec_ack);
    httpd_register_uri_handler(s_server, &export);
    httpd_register_uri_handler(s_server, &time);
    httpd_register_uri_handler(s_server, &signals_get);
    httpd_register_uri_handler(s_server, &signals_post);
    httpd_register_uri_handler(s_server, &mail_send);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
