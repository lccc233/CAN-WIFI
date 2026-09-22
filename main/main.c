#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "wifi.h"
#include "can.h"
#include "can_logger.h"
#include "web_server.h"
#include "led.h"

static const char *TAG = "main";

void app_main(void)
{
    // NVS 初始化（WiFi 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // WiFi STA 模式 — 连接现有路由器（SSID/密码见 wifi.h）
    ESP_ERROR_CHECK(wifi_init_sta());

    // CAN 总线初始化（TWAI + RX 任务 + 心跳任务）
    ESP_ERROR_CHECK(can_init());

    // PSRAM 历史数据记录器（失败仅禁用录制，不影响监控）
    ESP_ERROR_CHECK(can_log_init());

    // HTTP 服务器
    ESP_ERROR_CHECK(web_server_start());

    // WS2812 状态灯（未连上路由器=红灯，连上=炫彩）
    ESP_ERROR_CHECK(led_init());

    ESP_LOGI(TAG, "=============================================");
    ESP_LOGI(TAG, "  CAN Bus Monitor Ready!");
    ESP_LOGI(TAG, "  WiFi STA: connecting to \"%s\"", WIFI_STA_SSID);
    ESP_LOGI(TAG, "  Web:  http://%s.local (IP 见启动日志 / wifi.h 静态配置)", MDNS_HOSTNAME);
    ESP_LOGI(TAG, "=============================================");

    // 空闲 — 所有工作在 FreeRTOS 任务中完成
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
