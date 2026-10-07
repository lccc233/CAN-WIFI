#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include "wifi.h"
#include "mail_sender.h"

/* Protocol matches official agently-cli 1.0.18 --dry-run output.
 * The published service has no versioned MCU SDK: keep URLs and schema together.
 */
#define MAIL_API "https://api.agent.qq.com"
#define MAIL_TOKEN_URL "https://auth.agent.qq.com/oauth/token"
#define RESPONSE_MAX 8192
static const char *TAG = "mail";
static SemaphoreHandle_t s_lock;
static bool s_sending;
typedef struct {
    char recipient[MAIL_ADDRESS_MAX];
    char client_id[MAIL_CLIENT_MAX];
    char refresh_token[MAIL_REFRESH_MAX];
} mail_config_t;
static mail_config_t s_config = {.recipient = MAIL_DEFAULT_TO};
typedef struct {
    httpd_req_t *req;
    mail_config_t config;
    char filename[80];
} mail_job_t;

static bool valid_address(const char *address)
{
    size_t n = strlen(address);
    if (!n || n >= MAIL_ADDRESS_MAX) return false;
    const char *at = strchr(address, '@');
    if (!at || at == address || !strchr(at + 1, '.') || strchr(at + 1, '@')) return false;
    for (const char *p = address; *p; p++) {
        if (!isalnum((unsigned char)*p) && !strchr(".!#$%&'*+-/=?^_`{|}~@", *p)) return false;
    }
    return at[1] && address[n-1] != '.';
}

static esp_err_t save_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("mailcfg", NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

esp_err_t mail_sender_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("mailcfg", NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (ret != ESP_OK) return ret;
    char address[MAIL_ADDRESS_MAX];
    size_t size = sizeof(address);
    if (nvs_get_str(handle, "to", address, &size) == ESP_OK && valid_address(address)) {
        strcpy(s_config.recipient, address);
    }
    char auth[MAIL_CLIENT_MAX + MAIL_REFRESH_MAX + 100];
    size = sizeof(auth);
    if (nvs_get_str(handle, "auth", auth, &size) == ESP_OK) {
        cJSON *root = cJSON_Parse(auth);
        cJSON *client = cJSON_GetObjectItemCaseSensitive(root, "client_id");
        cJSON *refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_token");
        if (cJSON_IsString(client) && strlen(client->valuestring) < MAIL_CLIENT_MAX &&
            cJSON_IsString(refresh) && strlen(refresh->valuestring) < MAIL_REFRESH_MAX) {
            strcpy(s_config.client_id, client->valuestring);
            strcpy(s_config.refresh_token, refresh->valuestring);
        }
        cJSON_Delete(root);
    }
    memset(auth, 0, sizeof(auth));
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t mail_set_recipient(const char *address)
{
    if (!valid_address(address)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = save_string("to", address);
    if (ret == ESP_OK) strcpy(s_config.recipient, address);
    xSemaphoreGive(s_lock);
    return ret;
}

void mail_get_recipient(char *out, size_t size)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(out, size, "%s", s_config.recipient);
    xSemaphoreGive(s_lock);
}

bool mail_is_configured(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool configured = s_config.client_id[0] && s_config.refresh_token[0];
    xSemaphoreGive(s_lock);
    return configured;
}

static esp_err_t save_auth(const char *client, const char *refresh)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    if (!cJSON_AddStringToObject(root, "client_id", client) ||
        !cJSON_AddStringToObject(root, "refresh_token", refresh)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    esp_err_t ret = save_string("auth", json);
    memset(json, 0, strlen(json));
    free(json);
    return ret;
}

esp_err_t mail_import_auth(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *email = cJSON_GetObjectItemCaseSensitive(root, "email");
    cJSON *client = cJSON_GetObjectItemCaseSensitive(root, "client_id");
    cJSON *refresh = cJSON_GetObjectItemCaseSensitive(root, "refresh_token");
    esp_err_t ret = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(email) && !strcmp(email->valuestring, MAIL_SENDER) &&
        cJSON_IsString(client) && client->valuestring[0] && strlen(client->valuestring) < MAIL_CLIENT_MAX &&
        cJSON_IsString(refresh) && refresh->valuestring[0] && strlen(refresh->valuestring) < MAIL_REFRESH_MAX) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_sending) ret = ESP_ERR_INVALID_STATE;
        else {
            ret = save_auth(client->valuestring, refresh->valuestring);
            if (ret == ESP_OK) {
                strcpy(s_config.client_id, client->valuestring);
                strcpy(s_config.refresh_token, refresh->valuestring);
            }
        }
        xSemaphoreGive(s_lock);
    }
    cJSON_Delete(root);
    return ret;
}

static void respond(httpd_req_t *req, const char *status, bool ok, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory"); return; }
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", message);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json ? json : "{\"ok\":false}", HTTPD_RESP_USE_STRLEN);
    free(json);
}

static esp_http_client_handle_t client_open(const char *url, const char *token)
{
    esp_http_client_config_t config = {
        .url = url, .timeout_ms = 30000, .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    if (token) {
        size_t size = strlen(token) + 8;
        char *authorization = malloc(size);
        if (!authorization) { esp_http_client_cleanup(client); return NULL; }
        snprintf(authorization, size, "Bearer %s", token);
        esp_err_t ret = esp_http_client_set_header(client, "Authorization", authorization);
        memset(authorization, 0, size);
        free(authorization);
        if (ret != ESP_OK) { esp_http_client_cleanup(client); return NULL; }
    }
    return client;
}

static bool write_all(esp_http_client_handle_t client, const char *data, size_t len)
{
    while (len) {
        int n = esp_http_client_write(client, data, len);
        if (n <= 0) return false;
        data += n; len -= n;
    }
    return true;
}

/* Fully read bounded JSON responses. Never log OAuth response bodies. */
static cJSON *read_json(esp_http_client_handle_t client, int *status)
{
    if (esp_http_client_fetch_headers(client) < 0) return NULL;
    *status = esp_http_client_get_status_code(client);
    char *body = malloc(RESPONSE_MAX + 1);
    if (!body) return NULL;
    int total = 0;
    while (total < RESPONSE_MAX) {
        int n = esp_http_client_read(client, body + total, RESPONSE_MAX - total);
        if (n < 0) { free(body); return NULL; }
        if (!n) break;
        total += n;
    }
    body[total] = 0;
    cJSON *root = esp_http_client_is_complete_data_received(client) ? cJSON_Parse(body) : NULL;
    memset(body, 0, total);
    free(body);
    return root;
}

static cJSON *json_request(const char *url, const char *token, const char *body, const char *type, int *status)
{
    esp_http_client_handle_t client = client_open(url, token);
    if (!client) return NULL;
    size_t len = body ? strlen(body) : 0;
    if (body) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", type);
    }
    cJSON *root = NULL;
    if (esp_http_client_open(client, len) == ESP_OK && (!body || write_all(client, body, len))) {
        root = read_json(client, status);
    }
    esp_http_client_cleanup(client);
    return root;
}

static char *form_encode(const char *str)
{
    char *out = malloc(strlen(str) * 3 + 1);
    if (!out) return NULL;
    char *p = out;
    for (; *str; str++) {
        unsigned char c = (unsigned char)*str;
        if (isalnum(c) || strchr("-._~", c)) *p++ = c;
        else { sprintf(p, "%%%02X", c); p += 3; }
    }
    *p = 0;
    return out;
}

static char *refresh_access(mail_job_t *job, char *error, size_t error_size)
{
    char *client = form_encode(job->config.client_id);
    char *refresh = form_encode(job->config.refresh_token);
    if (!client || !refresh) { free(client); free(refresh); return NULL; }
    size_t size = strlen(client) + strlen(refresh) + 80;
    char *form = malloc(size);
    if (!form) { free(client); free(refresh); return NULL; }
    snprintf(form, size, "grant_type=refresh_token&client_id=%s&refresh_token=%s", client, refresh);
    memset(refresh, 0, strlen(refresh));
    free(client); free(refresh);
    int status = 0;
    cJSON *root = json_request(MAIL_TOKEN_URL, NULL, form, "application/x-www-form-urlencoded", &status);
    memset(form, 0, strlen(form));
    free(form);
    cJSON *access = cJSON_GetObjectItemCaseSensitive(root, "access_token");
    cJSON *rotated = cJSON_GetObjectItemCaseSensitive(root, "refresh_token");
    char *token = NULL;
    if (status == 200 && cJSON_IsString(access) && access->valuestring[0]) {
        const char *next = cJSON_IsString(rotated) ? rotated->valuestring : job->config.refresh_token;
        if (!next[0] || strlen(next) >= MAIL_REFRESH_MAX) {
            snprintf(error, error_size, "邮箱续期响应无效，请重新授权");
        } else {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            esp_err_t ret = save_auth(job->config.client_id, next);
            if (ret == ESP_OK) strcpy(s_config.refresh_token, next);
            xSemaphoreGive(s_lock);
            if (ret == ESP_OK) token = strdup(access->valuestring);
            else snprintf(error, error_size, "无法保存续期凭据，请重新配置邮箱授权");
        }
    } else if (status == 400 || status == 401 || status == 403) {
        snprintf(error, error_size, "邮箱授权失效，请重新授权并通过串口导入");
    } else {
        snprintf(error, error_size, "邮箱续期失败（HTTP %d），请检查设备网络", status);
    }
    cJSON_Delete(root);
    return token;
}

static bool lookup_alias(const char *token, char *alias, size_t size, size_t csv_size, char *error, size_t error_size)
{
    int status = 0;
    cJSON *root = json_request(MAIL_API "/v1/me", token, NULL, NULL, &status);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    /* CLI wraps API responses; accept the API's unwrapped object as well. */
    if (!cJSON_IsObject(data)) data = root;
    bool found = false;
    cJSON *item;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(data, "aliases")) {
        cJSON *email = cJSON_GetObjectItemCaseSensitive(item, "email");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "alias_id");
        if (status == 200 && cJSON_IsString(email) && !strcmp(email->valuestring, MAIL_SENDER) &&
            cJSON_IsString(id) && id->valuestring[0] && strlen(id->valuestring) < size) {
            found = true;
            for (const char *p = id->valuestring; *p; p++) {
                if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') found = false;
            }
            if (found) strcpy(alias, id->valuestring);
            break;
        }
    }
    if (!found) snprintf(error, error_size, "邮箱身份验证失败，必须授权 " MAIL_SENDER);
    cJSON *limits = cJSON_GetObjectItemCaseSensitive(data, "constraints");
    cJSON *limit = cJSON_GetObjectItemCaseSensitive(limits, "max_attachment_size_bytes");
    double max = cJSON_IsNumber(limit) ? limit->valuedouble :
                 cJSON_IsString(limit) ? strtod(limit->valuestring, NULL) : MAIL_CSV_MAX;
    if (found && (max <= 0 || csv_size > max)) {
        snprintf(error, error_size, "CSV 超过邮箱附件大小限制，请缩短记录");
        found = false;
    }
    cJSON_Delete(root);
    return found;
}

static bool send_csv(mail_job_t *job, const char *token, const char *alias, char *error, size_t error_size)
{
    char url[160];
    snprintf(url, sizeof(url), MAIL_API "/v1/aliases/%s/messages/send", alias);
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "subject", job->filename);
    cJSON_AddStringToObject(root, "body", "CAN 数据记录，详见 CSV 附件。");
    cJSON_AddStringToObject(root, "body_format", "PLAIN");
    cJSON_AddBoolToObject(root, "skip_confirmation", true); /* Human clicked the envelope. */
    cJSON *to = cJSON_AddArrayToObject(root, "to");
    cJSON *address = cJSON_CreateObject();
    cJSON_AddStringToObject(address, "email", job->config.recipient);
    cJSON_AddItemToArray(to, address);
    char *prefix = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!prefix) return false;
    size_t prefix_len = strlen(prefix);
    prefix[--prefix_len] = 0; /* Replace closing } with streaming attachments field. */
    char attachment[256];
    int attachment_len = snprintf(attachment, sizeof(attachment),
        ",\"attachments\":[{\"filename\":\"%s\",\"content_type\":\"text/csv; charset=utf-8\",\"size\":%u,\"content\":\"",
        job->filename, (unsigned)job->req->content_len);
    const size_t tail_len = strlen("\",\"sha1\":\"") + 40 + strlen("\"}]}");
    size_t length = prefix_len + attachment_len + ((job->req->content_len + 2) / 3) * 4 + tail_len;
    esp_http_client_handle_t client = client_open(url, token);
    bool ok = false;
    mbedtls_sha1_context sha;
    mbedtls_sha1_init(&sha);
    if (!client) goto done;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (esp_http_client_open(client, length) != ESP_OK ||
        !write_all(client, prefix, prefix_len) || !write_all(client, attachment, attachment_len)) goto done;
    if (mbedtls_sha1_starts(&sha) != 0) goto done;
    unsigned char raw[768], encoded[1025];
    size_t remaining = job->req->content_len;
    int64_t started = esp_timer_get_time();
    while (remaining) {
        size_t wanted = remaining < sizeof(raw) ? remaining : sizeof(raw);
        size_t got = 0;
        while (got < wanted) {
            int n = httpd_req_recv(job->req, (char *)raw + got, wanted - got);
            if (n <= 0) { snprintf(error, error_size, "CSV 上传中断，请检查连接后重试"); goto done; }
            got += n;
        }
        size_t encoded_size = 0;
        if (mbedtls_sha1_update(&sha, raw, got) != 0 ||
            mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_size, raw, got) != 0 ||
            !write_all(client, (char *)encoded, encoded_size)) goto done;
        remaining -= got;
        if (esp_timer_get_time() - started > 600000000LL) {
            snprintf(error, error_size, "发送超时，请缩短记录后重试"); goto done;
        }
        taskYIELD();
    }
    unsigned char digest[20];
    if (mbedtls_sha1_finish(&sha, digest) != 0) goto done;
    char hex[41];
    for (int i = 0; i < 20; i++) sprintf(hex + i * 2, "%02x", digest[i]);
    char tail[80];
    int size = snprintf(tail, sizeof(tail), "\",\"sha1\":\"%s\"}]}", hex);
    snprintf(error, error_size, "发送结果未确认，请检查邮箱后再决定是否重试");
    if (!write_all(client, tail, size)) goto done;
    int status = 0;
    root = read_json(client, &status);
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data)) data = root;
    cJSON *queued = cJSON_GetObjectItemCaseSensitive(data, "queued");
    ok = status >= 200 && status < 300 && cJSON_IsTrue(queued) &&
         !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok"));
    if (!ok) {
        if (status == 429) snprintf(error, error_size, "邮箱发送额度或频率已达限制，请稍后重试");
        else if (status == 401 || status == 403) snprintf(error, error_size, "邮箱拒绝发送，请检查授权和发信权限");
        else if (!status) snprintf(error, error_size, "发送结果未确认，请检查邮箱后再决定是否重试");
        else snprintf(error, error_size, "邮件服务未确认发送（HTTP %d）", status);
    }
    cJSON_Delete(root);
done:
    mbedtls_sha1_free(&sha);
    if (client) esp_http_client_cleanup(client);
    free(prefix);
    return ok;
}

static void mail_task(void *arg)
{
    mail_job_t *job = arg;
    char error[192] = "发送失败，请检查设备互联网连接";
    char *token = refresh_access(job, error, sizeof(error));
    char alias[96];
    bool ok = token && lookup_alias(token, alias, sizeof(alias), job->req->content_len, error, sizeof(error)) &&
              send_csv(job, token, alias, error, sizeof(error));
    if (token) { memset(token, 0, strlen(token)); free(token); }
    /* Close this upload session: failures can leave an unread request body. */
    httpd_resp_set_hdr(job->req, "Connection", "close");
    respond(job->req, ok ? "200 OK" : "502 Bad Gateway", ok,
            ok ? "邮件服务已接受发送，请留意收件箱" : error);
    unsigned csv_size = job->req->content_len;
    int fd = httpd_req_to_sockfd(job->req);
    httpd_handle_t handle = job->req->handle;
    httpd_req_async_handler_complete(job->req);
    httpd_sess_trigger_close(handle, fd);
    ESP_LOGI(TAG, "CSV mail %s (%u bytes)", ok ? "accepted" : "failed", csv_size);
    memset(job, 0, sizeof(*job));
    free(job);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sending = false;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

/* Verify network, credential renewal and sender identity without sending mail. */
static void mail_check_task(void *arg)
{
    mail_job_t *job = arg;
    char error[192] = "邮箱连接失败，请检查设备网络和页面授时";
    char alias[96];
    char *token = refresh_access(job, error, sizeof(error));
    bool ok = token && lookup_alias(token, alias, sizeof(alias), 0, error, sizeof(error));
    if (token) { memset(token, 0, strlen(token)); free(token); }
    printf("%s：%s\r\n", ok ? "MAILCHECK OK" : "MAILCHECK ERROR",
           ok ? "设备 HTTPS 连接、邮箱身份和自动续期验证通过（未发送邮件）" : error);
    memset(job, 0, sizeof(*job));
    free(job);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sending = false;
    xSemaphoreGive(s_lock);
    vTaskDelete(NULL);
}

esp_err_t mail_check_start(void)
{
    if (wifi_cfg_get_mode() != WIFI_CFG_MODE_STA || !wifi_is_connected()) return ESP_ERR_INVALID_STATE;
    mail_job_t *job = calloc(1, sizeof(*job));
    if (!job) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sending || !s_config.client_id[0] || !s_config.refresh_token[0]) {
        xSemaphoreGive(s_lock);
        free(job);
        return ESP_ERR_INVALID_STATE;
    }
    s_sending = true;
    job->config = s_config;
    xSemaphoreGive(s_lock);
    if (xTaskCreate(mail_check_task, "mail_check", 12288, job, 2, NULL) == pdPASS) return ESP_OK;
    memset(job, 0, sizeof(*job));
    free(job);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sending = false;
    xSemaphoreGive(s_lock);
    return ESP_ERR_NO_MEM;
}

esp_err_t mail_send_handler(httpd_req_t *req)
{
    if (!req->content_len || req->content_len > MAIL_CSV_MAX) {
        respond(req, "413 Payload Too Large", false, "CSV 为空或超过 20MB，请缩短记录");
        return ESP_FAIL; /* Close without trying to drain a large rejected upload. */
    }
    if (wifi_cfg_get_mode() != WIFI_CFG_MODE_STA || !wifi_is_connected()) {
        respond(req, "503 Service Unavailable", false, "设备需连接可上网的路由器或手机热点"); return ESP_FAIL;
    }
    mail_job_t *job = calloc(1, sizeof(*job));
    if (!job) { respond(req, "503 Service Unavailable", false, "设备内存不足"); return ESP_FAIL; }
    char name[80];
    if (httpd_req_get_hdr_value_str(req, "X-CSV-Filename", name, sizeof(name)) != ESP_OK) {
        strcpy(name, "can_signals.csv");
    }
    size_t name_len = strlen(name);
    bool valid = name_len > 4 && !strcmp(name + name_len - 4, ".csv");
    for (const char *p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && !strchr("_.-", *p)) valid = false;
    }
    if (!valid) { free(job); respond(req, "400 Bad Request", false, "CSV 文件名无效"); return ESP_FAIL; }
    strcpy(job->filename, name);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_sending || !s_config.refresh_token[0] || !s_config.client_id[0]) {
        bool busy = s_sending;
        xSemaphoreGive(s_lock);
        free(job);
        respond(req, busy ? "409 Conflict" : "503 Service Unavailable", false,
                busy ? "已有邮件正在发送" : "请先通过串口配置邮箱授权（mailauth）");
        return ESP_FAIL;
    }
    s_sending = true;
    job->config = s_config;
    xSemaphoreGive(s_lock);
    esp_err_t ret = httpd_req_async_handler_begin(req, &job->req);
    if (ret == ESP_OK && xTaskCreate(mail_task, "mail_send", 12288, job, 2, NULL) == pdPASS) return ESP_OK;
    if (job->req) {
        respond(job->req, "503 Service Unavailable", false, "无法启动邮件发送");
        httpd_req_async_handler_complete(job->req);
    } else respond(req, "503 Service Unavailable", false, "无法启动邮件发送");
    memset(job, 0, sizeof(*job));
    free(job);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sending = false;
    xSemaphoreGive(s_lock);
    return ESP_FAIL;
}
