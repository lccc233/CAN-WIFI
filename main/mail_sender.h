#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#define MAIL_SENDER "espdata@agent.qq.com"
#define MAIL_DEFAULT_TO "lichen1435374410@163.com"
#define MAIL_ADDRESS_MAX 128
#define MAIL_REFRESH_MAX 1024
#define MAIL_CLIENT_MAX 128
#define MAIL_CSV_MAX (20 * 1024 * 1024)

esp_err_t mail_sender_init(void);
esp_err_t mail_set_recipient(const char *address);
void mail_get_recipient(char *out, size_t size);
bool mail_is_configured(void);
/* Import only this mailbox's OAuth client_id and refresh_token; never echo secrets. */
esp_err_t mail_import_auth(const char *json);
esp_err_t mail_check_start(void);
esp_err_t mail_send_handler(httpd_req_t *req);
