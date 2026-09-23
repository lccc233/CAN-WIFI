#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "wifi.h"

static const char *TAG = "wifi";

static volatile bool s_sta_got_ip = false;   // 已连上 AP 且拿到 IP
static int s_retry_count = 0;
static esp_timer_handle_t s_reconnect_timer;   // 断线退避重连定时器

// 在定时器回调里发起重连，而不是在事件回调里 vTaskDelay（会卡住整个事件循环）
static void wifi_reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA started, connecting to \"%s\"...", WIFI_STA_SSID);
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
        ESP_LOGI(TAG, "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

bool wifi_is_connected(void)
{
    return s_sta_got_ip;
}

// STA 模式：连接现有 WiFi 路由器（IP 由 DHCP 或 wifi.h 静态配置决定）
esp_err_t wifi_init_sta(void)
{
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    (void)sta_netif;   // 仅静态 IP 分支使用；DHCP 模式下消除未用变量告警

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 断线退避重连定时器（事件在 esp_wifi_start 之后才会到来）
    const esp_timer_create_args_t reconnect_args = {
        .callback = wifi_reconnect_timer_cb,
        .name = "wifi_reconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_args, &s_reconnect_timer));

#if WIFI_STA_STATIC_IP
    // 固定 IP：在启动前停 DHCP 并写入静态地址（网段配错设备将不可达，
    // 把 wifi.h 的 WIFI_STA_STATIC_IP 改回 0 重烧即可退回 DHCP）
    {
        ip4_addr_t ip, gw, mask;
        if (ip4addr_aton(WIFI_STA_IP, &ip) &&
            ip4addr_aton(WIFI_STA_GATEWAY, &gw) &&
            ip4addr_aton(WIFI_STA_NETMASK, &mask)) {
            esp_netif_ip_info_t info = {0};
            info.ip.addr = ip.addr;
            info.gw.addr = gw.addr;
            info.netmask.addr = mask.addr;
            ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));
            ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &info));
            ESP_LOGI(TAG, "Static IP: %s (gw %s)", WIFI_STA_IP, WIFI_STA_GATEWAY);
        } else {
            ESP_LOGE(TAG, "Bad static IP config \"%s\", falling back to DHCP",
                     WIFI_STA_IP);
        }
    }
#endif

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_STA_SSID,
            .password = WIFI_STA_PASS,
            .threshold.authmode = WIFI_AUTH_WPA_PSK,   // 不回落到开放网络
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 实时轮询场景：关闭 Modem Sleep，降低 HTTP 延迟
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "STA mode: connecting to SSID=\"%s\" (IP via DHCP)",
             WIFI_STA_SSID);

    // mDNS：让 http://can-monitor.local 可访问
    esp_err_t mdns_ret = mdns_init();
    if (mdns_ret == ESP_OK) {
        mdns_hostname_set(MDNS_HOSTNAME);
        mdns_instance_name_set("ESP32 CAN Monitor");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS: http://%s.local", MDNS_HOSTNAME);
    }

    return ESP_OK;
}
