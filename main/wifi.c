#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "mdns.h"
#include "wifi.h"

static const char *TAG = "wifi";

static volatile bool s_sta_got_ip = false;   // 已连上 AP 且拿到 IP
static int s_retry_count = 0;

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
        // 无限重连：CAN 工具应始终尝试回到网络
        // （esp_wifi_connect 会在事件回调里立即发起；断开事件本身自带秒级间隔）
        ESP_LOGW(TAG, "Disconnected (reason=%d), retry #%d...",
                 event->reason, s_retry_count);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_sta_got_ip = true;
        s_retry_count = 0;
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

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

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
