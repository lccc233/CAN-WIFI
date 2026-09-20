#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "mdns.h"
#include "wifi.h"

static const char *TAG = "wifi";

static bool s_ap_ready = false;
static volatile int s_sta_count = 0;   // 当前连接的客户端数

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)data;
        s_sta_count++;
        ESP_LOGI(TAG, "Station connected: " MACSTR " AID=%d (total=%d)",
                 MAC2STR(event->mac), event->aid, s_sta_count);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)data;
        if (s_sta_count > 0) s_sta_count--;
        ESP_LOGI(TAG, "Station disconnected: " MACSTR " AID=%d (total=%d)",
                 MAC2STR(event->mac), event->aid, s_sta_count);
    }
}

bool wifi_is_connected(void)
{
    return s_ap_ready;
}

int wifi_get_sta_count(void)
{
    return s_sta_count;
}

// 启动 SoftAP 热点（设备自己发布 WiFi）
esp_err_t wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_AP_PASS,
            .channel = WIFI_AP_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = WIFI_AP_MAX_CONN,
        },
    };
    // 密码为空时使用开放网络
    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // C4：SoftAP 模式下 Modem Sleep 无意义，显式关闭避免驱动默认值变化导致延迟
    esp_wifi_set_ps(WIFI_PS_NONE);

    s_ap_ready = true;
    ESP_LOGI(TAG, "SoftAP started: SSID=\"%s\" IP=192.168.4.1",
             WIFI_SSID);

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
