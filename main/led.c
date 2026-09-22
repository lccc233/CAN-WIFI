#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"
#include "wifi.h"
#include "led.h"

static const char *TAG = "led";

#define LED_GPIO   48
#define LED_NUM    1
#define LED_BRIGHTNESS  20   // 最大亮度 0-255，20 比较柔和，可自行调整

static led_strip_handle_t s_strip;

// HSV 转 RGB：用于炫彩效果（hue 0~359，全饱和，亮度受 LED_BRIGHTNESS 限制）
static void hsv_to_rgb(uint32_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint32_t region = hue / 60;
    uint32_t rem = (hue % 60) * LED_BRIGHTNESS / 60;
    uint32_t q = LED_BRIGHTNESS - rem;
    switch (region) {
    case 0: *r = LED_BRIGHTNESS; *g = rem;   *b = 0;   break;
    case 1: *r = q;   *g = LED_BRIGHTNESS;   *b = 0;   break;
    case 2: *r = 0;   *g = LED_BRIGHTNESS;   *b = rem; break;
    case 3: *r = 0;   *g = q;                *b = LED_BRIGHTNESS; break;
    case 4: *r = rem; *g = 0;                *b = LED_BRIGHTNESS; break;
    default:*r = LED_BRIGHTNESS; *g = 0;     *b = q;   break;
    }
}

static void led_task(void *arg)
{
    uint32_t hue = 0;
    while (1) {
        if (!wifi_is_connected()) {
            // 未连上路由器：红灯常亮
            led_strip_set_pixel(s_strip, 0, LED_BRIGHTNESS, 0, 0);
            led_strip_refresh(s_strip);
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // 已连上路由器：炫彩（色相循环）
            uint8_t r, g, b;
            hsv_to_rgb(hue, &r, &g, &b);
            led_strip_set_pixel(s_strip, 0, r, g, b);
            led_strip_refresh(s_strip);
            hue = (hue + 2) % 360;
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

esp_err_t led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_NUM,
        // 其余取默认：WS2812、GRB 像素格式
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "WS2812 ready (GPIO=%d, %d LED): disconnected=red, connected=rainbow",
             LED_GPIO, LED_NUM);

    xTaskCreate(led_task, "led_task", 4096, NULL, 5, NULL);
    return ESP_OK;
}
