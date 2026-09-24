#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "wifi.h"

static const char *TAG = "wifi";

// ---- NVS 持久化配置（命名空间 "wificfg"，与 webui/signals 互不影响）----
#define CFG_NS     "wificfg"
#define KEY_MODE   "mode"     // u8: wifi_cfg_mode_t
#define KEY_IPMODE "ipmode"   // u8: wifi_ip_mode_t
#define KEY_SSID   "ssid"     // str: STA SSID
#define KEY_PASS   "pass"     // str: STA 密码
#define KEY_IP     "ip"       // str: 固定 IP（STATIC 模式）

// 自动绑定 .250 的主机位
#define AUTO_IP_HOST_OCTET 250u

static wifi_cfg_mode_t s_cfg_mode = WIFI_CFG_MODE_STA;
static wifi_ip_mode_t  s_ip_mode  = WIFI_IP_AUTO_250;
static char s_sta_ssid[33]  = WIFI_DEFAULT_STA_SSID;
static char s_sta_pass[65]  = WIFI_DEFAULT_STA_PASS;
static char s_static_ip[16] = "";      // 仅 WIFI_IP_STATIC 时有效

static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static volatile bool s_sta_got_ip = false;   // 已连上 AP 且拿到 IP
static volatile bool s_ap_running = false;   // 热点已启动
static volatile bool s_wifi_started = false; // esp_wifi_start 已调用（配置热生效用）
static volatile bool s_ip_rebinding = false; // 正在 GOT_IP 事件里改绑 IP，防止递归
static int s_retry_count = 0;
static esp_timer_handle_t s_reconnect_timer; // 断线退避重连定时器

// ================= 配置存取 =================

static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NS, NVS_READONLY, &h) != ESP_OK) {
        return;   // 首次烧录：全部用默认值
    }
    uint8_t u8;
    if (nvs_get_u8(h, KEY_MODE, &u8) == ESP_OK && u8 <= WIFI_CFG_MODE_AP) {
        s_cfg_mode = (wifi_cfg_mode_t)u8;
    }
    if (nvs_get_u8(h, KEY_IPMODE, &u8) == ESP_OK && u8 <= WIFI_IP_STATIC) {
        s_ip_mode = (wifi_ip_mode_t)u8;
    }
    size_t len;
    len = sizeof(s_sta_ssid);
    if (nvs_get_str(h, KEY_SSID, s_sta_ssid, &len) == ESP_OK && s_sta_ssid[0] == '\0') {
        strlcpy(s_sta_ssid, WIFI_DEFAULT_STA_SSID, sizeof(s_sta_ssid));
    }
    len = sizeof(s_sta_pass);
    nvs_get_str(h, KEY_PASS, s_sta_pass, &len);
    len = sizeof(s_static_ip);
    nvs_get_str(h, KEY_IP, s_static_ip, &len);
    nvs_close(h);
}

static esp_err_t cfg_save_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t cfg_save_str(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CFG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

wifi_cfg_mode_t wifi_cfg_get_mode(void)          { return s_cfg_mode; }
wifi_ip_mode_t  wifi_cfg_get_ip_mode(void)       { return s_ip_mode; }

void wifi_cfg_get_sta_ssid(char *out, size_t len)   { strlcpy(out, s_sta_ssid, len); }
void wifi_cfg_get_sta_pass(char *out, size_t len)   { strlcpy(out, s_sta_pass, len); }
void wifi_cfg_get_static_ip(char *out, size_t len)  { strlcpy(out, s_static_ip, len); }

void wifi_cfg_get_ap_ssid(char *out, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, len, "CAN-Monitor-%02X%02X", mac[4], mac[5]);
}

esp_err_t wifi_cfg_set_mode(wifi_cfg_mode_t mode)
{
    if (mode != WIFI_CFG_MODE_STA && mode != WIFI_CFG_MODE_AP) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg_mode = mode;
    return cfg_save_u8(KEY_MODE, (uint8_t)mode);
}

esp_err_t wifi_cfg_set_sta_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid || strlen(ssid) > 32) return ESP_ERR_INVALID_ARG;
    if (!pass || strlen(pass) < 8 || strlen(pass) > 63) return ESP_ERR_INVALID_ARG;

    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));
    strlcpy(s_sta_pass, pass, sizeof(s_sta_pass));
    esp_err_t e1 = cfg_save_str(KEY_SSID, s_sta_ssid);
    esp_err_t e2 = cfg_save_str(KEY_PASS, s_sta_pass);
    if (e1 != ESP_OK || e2 != ESP_OK) return ESP_FAIL;

    // 立即生效：更新 STA 配置并断开重连（DISCONNECTED 事件的前 5 次即时重试接管）
    if (s_wifi_started && s_cfg_mode == WIFI_CFG_MODE_STA) {
        wifi_config_t c = {
            .sta = { .threshold.authmode = WIFI_AUTH_WPA_PSK },
        };
        strlcpy((char *)c.sta.ssid, s_sta_ssid, sizeof(c.sta.ssid));
        strlcpy((char *)c.sta.password, s_sta_pass, sizeof(c.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &c);
        esp_wifi_disconnect();
        ESP_LOGI(TAG, "STA credentials updated, reconnecting to \"%s\"", s_sta_ssid);
    }
    return ESP_OK;
}

esp_err_t wifi_cfg_set_ip_mode(wifi_ip_mode_t mode, const char *static_ip)
{
    char ip[16] = "";
    if (mode == WIFI_IP_STATIC) {
        ip4_addr_t tmp;
        if (!static_ip || ip4addr_aton(static_ip, &tmp) <= 0) {
            return ESP_ERR_INVALID_ARG;
        }
        strlcpy(ip, static_ip, sizeof(ip));
    }
    s_ip_mode = mode;
    strlcpy(s_static_ip, ip, sizeof(s_static_ip));
    esp_err_t e1 = cfg_save_u8(KEY_IPMODE, (uint8_t)mode);
    esp_err_t e2 = cfg_save_str(KEY_IP, s_static_ip);
    if (e1 != ESP_OK || e2 != ESP_OK) return ESP_FAIL;

    // 立即生效：STA 在线则重连，GOT_IP 事件按新规则重新绑定
    if (s_wifi_started && s_cfg_mode == WIFI_CFG_MODE_STA && s_sta_got_ip) {
        esp_wifi_disconnect();
        ESP_LOGI(TAG, "IP rule updated (%s), rebinding on reconnect...",
                 mode == WIFI_IP_AUTO_250 ? "auto .250" : s_static_ip);
    }
    return ESP_OK;
}

// ================= 事件处理 =================

// 在 GOT_IP 事件里按规则改绑 IP：
//   AUTO_250: 保留 DHCP 下发的网段/网关/DNS，仅把主机位改成 .250
//   STATIC:   使用固定 IP，掩码 /24，网关取同网段 .1
static void wifi_rebind_ip(const esp_netif_ip_info_t *dhcp_info)
{
    if (s_ip_rebinding) return;

    esp_netif_ip_info_t info = *dhcp_info;
    if (s_ip_mode == WIFI_IP_AUTO_250) {
        info.ip.addr = (info.ip.addr & info.netmask.addr) | (AUTO_IP_HOST_OCTET << 24);
        if (info.ip.addr == dhcp_info->ip.addr) return;   // 已经是 .250
    } else {
        ip4_addr_t ip, mask;
        if (ip4addr_aton(s_static_ip, &ip) <= 0 || ip4addr_aton("255.255.255.0", &mask) <= 0) {
            return;   // 配置异常，保持 DHCP 地址
        }
        info.ip.addr = ip.addr;
        info.netmask.addr = mask.addr;
        info.gw.addr = (ip.addr & mask.addr) | (1u << 24);
        if (info.ip.addr == dhcp_info->ip.addr) return;
    }

    s_ip_rebinding = true;
    // DHCP 分配的 DNS 先保存下来，改绑静态 IP 后继续可用
    esp_netif_dns_info_t dns = {0};
    esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);

    esp_err_t err = esp_netif_dhcpc_stop(s_sta_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc stop failed: %s", esp_err_to_name(err));
        s_ip_rebinding = false;
        return;
    }
    err = esp_netif_set_ip_info(s_sta_netif, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set static ip failed: %s（保留 DHCP 地址）", esp_err_to_name(err));
        s_ip_rebinding = false;
        return;
    }
    esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    ESP_LOGI(TAG, "IP rebound to " IPSTR " (gw " IPSTR ")",
             IP2STR(&info.ip), IP2STR(&info.gw));
    // s_ip_rebinding 由下一次 GOT_IP 事件清除
}

static void wifi_reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"...", s_sta_ssid);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)data;
        s_sta_got_ip = false;
        s_retry_count++;
        // 无限重连但带退避：前 5 次立即重试（覆盖路由器快速重启/信号抖动），
        // 之后 1s→30s 指数退避，避免路由器长期不在时空转刷日志
        uint32_t delay_ms = 0;
        if (s_retry_count > 5) {
            uint32_t shift = (uint32_t)(s_retry_count - 6);
            if (shift > 5) shift = 5;
            delay_ms = 1000u << shift;         // 1s,2s,4s,8s,16s,32s
            if (delay_ms > 30000) delay_ms = 30000;
        }
        if (delay_ms == 0) {
            ESP_LOGW(TAG, "Disconnected (reason=%d), retry #%d...",
                     event->reason, s_retry_count);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Disconnected (reason=%d), retry #%d in %u ms",
                     event->reason, s_retry_count, (unsigned)delay_ms);
            esp_timer_stop(s_reconnect_timer);   // 已在计时则重置；未运行返回错误，忽略
            esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000u);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_sta_got_ip = true;
        s_retry_count = 0;
        esp_timer_stop(s_reconnect_timer);   // 丢弃可能残留的退避重连（未在运行则无操作）
        if (s_ip_rebinding) {
            s_ip_rebinding = false;   // 改绑完成，这次是最终 IP
        } else {
            ESP_LOGI(TAG, "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            wifi_rebind_ip(&event->ip_info);
        }
        ESP_LOGI(TAG, "Web: http://" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        s_ap_running = true;
        ESP_LOGI(TAG, "AP started");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        s_ap_running = false;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "AP client joined: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)data;
        ESP_LOGI(TAG, "AP client left: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    }
}

bool wifi_is_connected(void)
{
    return s_cfg_mode == WIFI_CFG_MODE_AP ? s_ap_running : s_sta_got_ip;
}

// ================= 初始化 =================

static void wifi_start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(MDNS_HOSTNAME);
        mdns_instance_name_set("ESP32 CAN Monitor");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS: http://%s.local", MDNS_HOSTNAME);
    }
}

static esp_err_t wifi_init_sta(void)
{
    s_sta_netif = esp_netif_create_default_wifi_sta();

    // 断线退避重连定时器（事件在 esp_wifi_start 之后才会到来）
    const esp_timer_create_args_t reconnect_args = {
        .callback = wifi_reconnect_timer_cb,
        .name = "wifi_reconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_args, &s_reconnect_timer));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_PSK,   // 不回落到开放网络
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, s_sta_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, s_sta_pass, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    // 实时轮询场景：关闭 Modem Sleep，降低 HTTP 延迟
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "STA mode: connecting to \"%s\"", s_sta_ssid);
    wifi_start_mdns();
    return ESP_OK;
}

static esp_err_t wifi_init_ap(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();   // 默认 192.168.4.1

    char ap_ssid[33];
    wifi_cfg_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ap_ssid);
    strlcpy((char *)ap_config.ap.password, WIFI_DEFAULT_AP_PASS, sizeof(ap_config.ap.password));
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    ESP_LOGI(TAG, "AP mode: SSID=\"%s\" pass=\"%s\"", ap_ssid, WIFI_DEFAULT_AP_PASS);
    ESP_LOGI(TAG, "AP mode: web at http://192.168.4.1");
    wifi_start_mdns();
    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    cfg_load();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    return s_cfg_mode == WIFI_CFG_MODE_AP ? wifi_init_ap() : wifi_init_sta();
}

// ================= 状态查询 =================

static void ip_to_str(uint32_t addr, char *out, size_t len)
{
    snprintf(out, len, "%u.%u.%u.%u",
             (unsigned)(addr & 0xff), (unsigned)((addr >> 8) & 0xff),
             (unsigned)((addr >> 16) & 0xff), (unsigned)(addr >> 24));
}

void wifi_get_status(wifi_status_t *st)
{
    memset(st, 0, sizeof(*st));
    st->mode = s_cfg_mode;
    st->link_up = wifi_is_connected();

    if (s_cfg_mode == WIFI_CFG_MODE_AP) {
        wifi_cfg_get_ap_ssid(st->ssid, sizeof(st->ssid));
        if (s_ap_netif) {
            esp_netif_ip_info_t info = {0};
            if (esp_netif_get_ip_info(s_ap_netif, &info) == ESP_OK) {
                ip_to_str(info.ip.addr, st->ip, sizeof(st->ip));
                ip_to_str(info.netmask.addr, st->netmask, sizeof(st->netmask));
                ip_to_str(info.gw.addr, st->gw, sizeof(st->gw));
            }
        }
        esp_wifi_get_mac(WIFI_IF_AP, st->mac);
    } else {
        strlcpy(st->ssid, s_sta_ssid, sizeof(st->ssid));
        if (s_sta_netif) {
            esp_netif_ip_info_t info = {0};
            if (esp_netif_get_ip_info(s_sta_netif, &info) == ESP_OK) {
                ip_to_str(info.ip.addr, st->ip, sizeof(st->ip));
                ip_to_str(info.netmask.addr, st->netmask, sizeof(st->netmask));
                ip_to_str(info.gw.addr, st->gw, sizeof(st->gw));
            }
        }
        esp_wifi_get_mac(WIFI_IF_STA, st->mac);
        if (s_sta_got_ip) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                st->rssi = ap.rssi;
            }
        }
    }

    wifi_second_chan_t second;
    esp_wifi_get_channel(&st->channel, &second);

    if (s_cfg_mode == WIFI_CFG_MODE_AP && s_ap_running) {
        wifi_sta_list_t list;
        if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
            st->ap_clients = list.num;
        }
    }
}
