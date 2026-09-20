#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
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
#define HTTP_TASK_STACK_SIZE    12288   // /api/messages 局部 snapshot+freq+vbuf 较大
#define VI_JSON_MAX_POINTS      400     // 单次响应最多返回的曲线点数
#define VI_BATCH_STR            3072    // vi 行攒发缓冲大小
#define EXPORT_READ_BATCH       64      // 导出每批读取条数
#define EXPORT_FLUSH_INTERVAL_MS 10     // 每批发送间隔（让出 CPU 给 RX/其他连接）
#define EXPORT_LINE_BUF         4096

// /api/messages 忙闸：同一时刻只处理一个 poll 请求，后续请求快速返回 busy
// （C3）。若上一个 poll 客户端中途断开导致标志未释放，3 秒看门狗强制放行，
// 防止整个轮询被永久卡死。
static volatile bool s_busy_msgs = false;
static volatile uint32_t s_busy_since_ms = 0;

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
    // C3 忙闸：已有 poll 在处理中时快速返回，让客户端沿用本地数据。
    // 看门狗：busy 超过 3 秒视为泄漏，强制放行重新处理。
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (s_busy_msgs && (uint32_t)(now_ms - s_busy_since_ms) < 3000) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_sendstr(req, "{\"busy\":1}");
        return ESP_OK;
    }
    s_busy_msgs = true;
    s_busy_since_ms = now_ms;

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

    for (uint32_t i = 0; i < count; i++) {
        char entry[160];
        int n = snprintf(entry, sizeof(entry),
            "%s{\"t\":%lu,\"id\":\"0x%lX\",\"dlc\":%d,\"ext\":%s,\"data\":\"",
            (i > 0) ? "," : "",
            (unsigned long)snapshot[i].timestamp_ms,
            (unsigned long)snapshot[i].id,
            snapshot[i].dlc,
            snapshot[i].extended ? "true" : "false");

        for (int j = 0; j < snapshot[i].dlc && j < 8; j++) {
            n += snprintf(entry + n, sizeof(entry) - n, "%02X", snapshot[i].data[j]);
            if (j < snapshot[i].dlc - 1) {
                entry[n++] = ' ';
            }
        }
        snprintf(entry + n, sizeof(entry) - n, "\"}");
        httpd_resp_sendstr_chunk(req, entry);
    }

    // 电压/电流曲线数据（0x18FF0282 解码值，t=ms, c=电流A*10, v=电压V*10, f=电流哨兵故障）
    // C1：攒批发送（本地缓冲，无跨请求共享问题），syscall 数从 400+ 降到 ~10
    {
        char vbuf[VI_BATCH_STR];
        size_t vused = 0;
        int vi_total = sig_vi_count();
        if (vi_total > VI_JSON_MAX_POINTS) vi_total = VI_JSON_MAX_POINTS;
        httpd_resp_sendstr_chunk(req, "],\"vi\":[");
        for (int i = vi_total - 1; i >= 0; i--) {
            sig_vi_point_t vp;
            if (!sig_vi_get_back(i, &vp)) continue;
            // i==vi_total-1 是最旧一条（第一个输出），不加前导逗号
            int n = snprintf(vbuf + vused, sizeof(vbuf) - vused,
                     "%s{\"t\":%lu,\"c\":%d,\"v\":%lu,\"f\":%d,\"q\":%d,\"r\":%d,\"m\":%d}",
                     (i == vi_total - 1) ? "" : ",",
                     (unsigned long)vp.t,
                     (int)vp.current_x10,
                     (unsigned long)vp.voltage_x10,
                     (int)vp.fault,
                     (int)vp.torque,
                     (int)vp.rpm,
                     (int)vp.motor_v);
            if (n < 0 || (size_t)n >= sizeof(vbuf) - vused) {
                // 缓冲将满：刷出已攒部分后重写该点
                esp_err_t cerr = httpd_resp_send_chunk(req, vbuf, vused);
                if (cerr != ESP_OK) {
                    ESP_LOGE(TAG, "vi chunk send fail 0x%x", (int)cerr);
                    s_busy_msgs = false;
                    return ESP_FAIL;
                }
                vused = 0;
                n = snprintf(vbuf, sizeof(vbuf) - vused, "{\"t\":%lu,\"c\":%d,\"v\":%lu,\"f\":%d,\"q\":%d,\"r\":%d,\"m\":%d}",
                     (unsigned long)vp.t,
                     (int)vp.current_x10,
                     (unsigned long)vp.voltage_x10,
                     (int)vp.fault,
                     (int)vp.torque,
                     (int)vp.rpm,
                     (int)vp.motor_v);
            }
            vused += n;

            if (vused > sizeof(vbuf) / 2) {
                if (httpd_resp_send_chunk(req, vbuf, vused) != ESP_OK) {
                    s_busy_msgs = false;
                    return ESP_FAIL;
                }
                vused = 0;
            }
        }
        if (vused > 0) {
            if (httpd_resp_send_chunk(req, vbuf, vused) != ESP_OK) {
                s_busy_msgs = false;
                return ESP_FAIL;
            }
        }
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    s_busy_msgs = false;
    return ESP_OK;
}

// POST /api/send — 发送 CAN 帧
// D2：改用 cJSON 解析（IDF 内置），比手写 strstr 更健壮；
// 兼容旧语义：id 可为 "0x7E0" 或数字，data 为空格分隔 hex 字符串
static esp_err_t api_send_handler(httpd_req_t *req)
{
    char body[256];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

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
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

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
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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
    config.max_uri_handlers = 12;
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

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &messages);
    httpd_register_uri_handler(s_server, &send);
    httpd_register_uri_handler(s_server, &clear);
    httpd_register_uri_handler(s_server, &rec_start);
    httpd_register_uri_handler(s_server, &rec_stop);
    httpd_register_uri_handler(s_server, &rec_clear);
    httpd_register_uri_handler(s_server, &export);
    httpd_register_uri_handler(s_server, &time);

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
