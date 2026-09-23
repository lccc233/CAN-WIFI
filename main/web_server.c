#include <string.h>
#include <stdlib.h>
#include <ctype.h>
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
#include "web_server.h"

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

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
    uint32_t count, total;
    can_get_snapshot(snapshot, CAN_RX_RING_SIZE, &count, &total);

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
             "{\"clk\":{\"sync\":%s,\"boot\":%llu,\"ep\":%lld},",
             synced ? "true" : "false",
             (unsigned long long)now_boot,
             (unsigned long long)now_epoch);
    httpd_resp_sendstr_chunk(req, clk_hdr);

    // 录制状态（并入 messages 响应，前端 200ms 轮询自动携带）
    can_log_status_t rec;
    can_log_get_status(&rec);
    char rec_hdr[128];
    snprintf(rec_hdr, sizeof(rec_hdr),
             "\"rec\":{\"on\":%s,\"cnt\":%lu,\"cap\":%lu,\"drop\":%lu,\"ms\":%lu,\"psram\":%s},\"total\":%lu,\"freqs\":[",
             rec.recording ? "true" : "false",
             (unsigned long)rec.count,
             (unsigned long)rec.capacity,
             (unsigned long)rec.dropped,
             (unsigned long)rec.duration_ms,
             rec.psram_ok ? "true" : "false",
             (unsigned long)total);
    httpd_resp_sendstr_chunk(req, rec_hdr);

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
            "%s{\"t\":%lu,\"id\":\"0x%lX\",\"dlc\":%d,\"ext\":%s,\"data\":\"",
            (emitted > 0) ? "," : "",
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

// GET/POST /api/signals — 自定义曲线信号配置（设备 NVS 持久化，断电不丢）
// 配置真源在设备：浏览器 localStorage 仅作缓存（页面加载时以设备为准；
// 设备为空时把浏览器现有配置迁移上去）。多浏览器并存时，最后保存者生效。
// 存储格式：紧凑 JSON 数组原文（cJSON 校验后重新序列化），上限 ~3500 字节
#define SIGNALS_NVS_NS     "webui"
#define SIGNALS_NVS_KEY    "signals"
#define SIGNALS_MAX_BYTES  3500
#define SIGNALS_MAX_ITEMS  64

// POST body 的单条配置校验：对象；id 为 0x 开头十六进制字符串（3~16 字符）；
// name 为字符串（≤64 字符）。其余字段不限定（总大小另有 SIGNALS_MAX_BYTES 上限）。
// 该接口局域网内无鉴权可写，畸形数据挡在 NVS 门外（前端渲染另有转义兜底）
static bool signals_entry_valid(const cJSON *item)
{
    if (!cJSON_IsObject(item)) return false;
    const cJSON *jid = cJSON_GetObjectItem(item, "id");
    const cJSON *jname = cJSON_GetObjectItem(item, "name");
    if (!cJSON_IsString(jname) || strlen(jname->valuestring) > 64) return false;
    if (!cJSON_IsString(jid)) return false;
    const char *p = jid->valuestring;
    size_t len = strlen(p);
    if (len < 3 || len > 16 || p[0] != '0' || (p[1] != 'x' && p[1] != 'X')) return false;
    for (p += 2; *p; p++) {
        if (!isxdigit((unsigned char)*p)) return false;
    }
    return true;
}

static esp_err_t api_signals_handler(httpd_req_t *req)
{
    nvs_handle_t nvs;
    if (nvs_open(SIGNALS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS open failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");

    if (req->method == HTTP_POST) {
        // 读完整 body（recv 可能分片）
        size_t cap = SIGNALS_MAX_BYTES + 1;
        char *body = malloc(cap);
        if (!body) {
            nvs_close(nvs);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No mem");
            return ESP_FAIL;
        }
        int total = 0, recvd;
        while (total < (int)(cap - 1) &&
               (recvd = httpd_req_recv(req, body + total, cap - 1 - total)) > 0) {
            total += recvd;
        }
        if (total <= 0) {
            free(body);
            nvs_close(nvs);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
            return ESP_FAIL;
        }
        body[total] = '\0';

        // 校验为 JSON 数组并转紧凑格式
        cJSON *root = cJSON_Parse(body);
        free(body);
        if (!root || !cJSON_IsArray(root)) {
            if (root) cJSON_Delete(root);
            nvs_close(nvs);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
            return ESP_FAIL;
        }
        int n = cJSON_GetArraySize(root);
        bool valid = (n <= SIGNALS_MAX_ITEMS);
        for (int i = 0; valid && i < n; i++) {
            valid = signals_entry_valid(cJSON_GetArrayItem(root, i));
        }
        if (!valid) {
            cJSON_Delete(root);
            nvs_close(nvs);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "Bad signal entry (need id 0x.. and name)");
            return ESP_FAIL;
        }
        char *compact = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        esp_err_t err = ESP_OK;
        if (!compact || strlen(compact) > SIGNALS_MAX_BYTES) {
            err = ESP_ERR_INVALID_SIZE;   // 统一走下方报错
        } else {
            err = nvs_set_blob(nvs, SIGNALS_NVS_KEY, compact, strlen(compact));
            if (err == ESP_OK) err = nvs_commit(nvs);
        }
        if (compact) free(compact);
        nvs_close(nvs);
        if (err != ESP_OK) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                err == ESP_ERR_INVALID_SIZE
                                    ? "{\"ok\":false,\"error\":\"config too large\"}"
                                    : "{\"ok\":false,\"error\":\"NVS write failed\"}");
            return ESP_FAIL;
        }
        char out[40];
        snprintf(out, sizeof(out), "{\"ok\":true,\"n\":%d}", n);
        httpd_resp_sendstr(req, out);
        return ESP_OK;
    }

    // GET：返回存储的配置数组（未存过或异常时返回空数组）
    size_t len = 0;
    if (nvs_get_blob(nvs, SIGNALS_NVS_KEY, NULL, &len) != ESP_OK ||
        len == 0 || len > SIGNALS_MAX_BYTES) {
        nvs_close(nvs);
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    char *buf = malloc(len + 1);
    if (!buf) {
        nvs_close(nvs);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No mem");
        return ESP_FAIL;
    }
    esp_err_t err = nvs_get_blob(nvs, SIGNALS_NVS_KEY, buf, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(buf);
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }
    buf[len] = '\0';
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
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

// POST /api/rec/start — 开始录制
static esp_err_t api_rec_start_handler(httpd_req_t *req)
{
    esp_err_t ret = can_log_start();
    httpd_resp_set_type(req, "application/json");
    if (ret != ESP_OK) {
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"psram unavailable\"}");
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    }
    return ESP_OK;
}

// POST /api/rec/stop — 停止录制
static esp_err_t api_rec_stop_handler(httpd_req_t *req)
{
    can_log_stop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// POST /api/rec/clear — 清空录制数据（A2：录制进行中拒绝，防导出数据被覆盖）
static esp_err_t api_rec_clear_handler(httpd_req_t *req)
{
    if (can_log_is_recording()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"stop recording first\"}");
        return ESP_OK;
    }
    can_log_clear();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// GET /api/export — 以 CSV 流式下载全部录制数据
static esp_err_t api_export_handler(httpd_req_t *req)
{
    can_log_status_t st;
    can_log_get_status(&st);

    if (!st.psram_ok) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Recorder unavailable (no PSRAM)");
        return ESP_FAIL;
    }
    if (st.count == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No recorded data");
        return ESP_FAIL;
    }

    // 实时获取录制缓冲基址和已完成条数（PID 检查在下方钳位）
    can_msg_entry_t *base = NULL;
    uint32_t avail = 0;
    if (can_log_get_buffer(&base, &avail) != ESP_OK || base == NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Recorder buffer unavailable");
        return ESP_FAIL;
    }
    if (st.count > avail) st.count = avail;

    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"can_log.csv\"");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // CSV 表头（0x18FF0182 填转矩/转速/故障列，0x18FF0282 填电流/电压列）
    // time 列：已授时为 "2026-09-20 14:35:01.123"，未授时为 "boot+H:MM:SS.mmm"
    httpd_resp_set_hdr(req, "X-Log-Clock", time_sync_is_synced() ? "rtc" : "boot");
    httpd_resp_sendstr_chunk(req,
        "no,time,id,torque,speed_rpm,fault_code,fault_level,current_A,voltage_V\r\n");

    // C1：分批读取 + 行缓冲攒批发送，减少 syscall 与网络小包
    can_msg_entry_t buf[EXPORT_READ_BATCH];
    char line[EXPORT_LINE_BUF];
    size_t used = 0;
    uint32_t sent = 0;
    uint32_t seq = 0;
    bool aborted = false;

    while (sent < st.count && !aborted) {
        // 分批拷贝：写入方（RX 任务）会后写 count，条数 <= count 的环形
        // 位置必然已写完，无需锁；批量 memcpy 到内部缓冲再逐行格式化
        uint32_t to_read = st.count - sent;
        if (to_read > EXPORT_READ_BATCH) to_read = EXPORT_READ_BATCH;
        memcpy(buf, &base[sent], to_read * sizeof(can_msg_entry_t));

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

            if (m->id == SIG_ID_MOTOR_DRIVE) {
                sig_motor_t sg;
                sig_decode_motor(m->data, m->dlc, &sg);
                if (sg.valid) {
                    n = snprintf(line + used, sizeof(line) - used, "%d,%d,%u,%u,,",
                                 (int)sg.torque, (int)sg.speed_rpm,
                                 (unsigned)sg.fault_code, (unsigned)sg.fault_level);
                } else {
                    n = snprintf(line + used, sizeof(line) - used, ",,,,");
                }
            } else if (m->id == SIG_ID_BUS_VI) {
                sig_bus_vi_t sv;
                sig_decode_bus_vi(m->data, m->dlc, &sv);
                if (sv.valid) {
                    n = snprintf(line + used, sizeof(line) - used, ",,,,%d.%d,%d.%d",
                                 (int)sv.current_x10 / 10,
                                 (int)(sv.current_x10 < 0 ? -sv.current_x10 : sv.current_x10) % 10,
                                 (int)sv.voltage_x10 / 10,
                                 (int)sv.voltage_x10 % 10);
                } else {
                    n = snprintf(line + used, sizeof(line) - used, ",,,");
                }
            } else {
                n = snprintf(line + used, sizeof(line) - used, ",");
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

    // 刷出剩余
    if (used > 0) {
        if (httpd_resp_send_chunk(req, line, used) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "Exported %u frames as CSV", (unsigned)sent);
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 13;
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
    httpd_uri_t rec_clear = { .uri = "/api/rec/clear", .method = HTTP_POST, .handler = api_rec_clear_handler };
    httpd_uri_t export = { .uri = "/api/export", .method = HTTP_GET, .handler = api_export_handler };
    httpd_uri_t time = { .uri = "/api/time", .method = HTTP_POST, .handler = api_time_handler };
    httpd_uri_t signals_get = { .uri = "/api/signals", .method = HTTP_GET, .handler = api_signals_handler };
    httpd_uri_t signals_post = { .uri = "/api/signals", .method = HTTP_POST, .handler = api_signals_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &messages);
    httpd_register_uri_handler(s_server, &send);
    httpd_register_uri_handler(s_server, &clear);
    httpd_register_uri_handler(s_server, &rec_start);
    httpd_register_uri_handler(s_server, &rec_stop);
    httpd_register_uri_handler(s_server, &rec_clear);
    httpd_register_uri_handler(s_server, &export);
    httpd_register_uri_handler(s_server, &time);
    httpd_register_uri_handler(s_server, &signals_get);
    httpd_register_uri_handler(s_server, &signals_post);

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
