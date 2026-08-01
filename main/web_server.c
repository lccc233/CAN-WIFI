#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "can.h"
#include "web_page.h"
#include "web_server.h"

static const char *TAG = "web";
static httpd_handle_t s_server = NULL;

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
    can_msg_entry_t snapshot[CAN_RX_RING_SIZE];
    uint32_t count, total;
    can_get_snapshot(snapshot, CAN_RX_RING_SIZE, &count, &total);

    can_id_freq_t freqs[CAN_FREQ_MAX_IDS];
    int freq_count = can_get_id_freqs(freqs, CAN_FREQ_MAX_IDS);

    httpd_resp_set_type(req, "application/json");

    // 发送分块 JSON：{"total":N,"freqs":[...],"messages":[...]}
    char header[64];
    snprintf(header, sizeof(header), "{\"total\":%lu,\"freqs\":[", (unsigned long)total);
    httpd_resp_sendstr_chunk(req, header);

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

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// POST /api/send — 发送 CAN 帧
static esp_err_t api_send_handler(httpd_req_t *req)
{
    char body[256];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    uint32_t id = 0;
    uint8_t dlc = 0;
    bool extended = false;
    uint8_t data[8] = {0};

    // 解析 "id"
    char *p = strstr(body, "\"id\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '"') p++;
            id = strtoul(p, NULL, 0);
        }
    }

    // 解析 "dlc"
    p = strstr(body, "\"dlc\"");
    if (p) {
        p = strchr(p, ':');
        if (p) dlc = atoi(p + 1);
    }
    if (dlc > 8) dlc = 8;

    // 解析 "extended"
    p = strstr(body, "\"extended\"");
    if (p) {
        p = strchr(p, ':');
        if (p && strstr(p, "true")) extended = true;
    }

    // 解析 "data" — hex 字节
    p = strstr(body, "\"data\"");
    if (p) {
        p = strchr(p, ':');
        if (p) {
            p++;
            while (*p == ' ' || *p == '"') p++;
            for (int i = 0; i < dlc && i < 8; i++) {
                data[i] = (uint8_t)strtoul(p, &p, 16);
                while (*p == ' ') p++;
            }
        }
    }

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

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_uri_t messages = { .uri = "/api/messages", .method = HTTP_GET, .handler = api_messages_handler };
    httpd_uri_t send = { .uri = "/api/send", .method = HTTP_POST, .handler = api_send_handler };
    httpd_uri_t clear = { .uri = "/api/clear", .method = HTTP_POST, .handler = api_clear_handler };

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &messages);
    httpd_register_uri_handler(s_server, &send);
    httpd_register_uri_handler(s_server, &clear);

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
