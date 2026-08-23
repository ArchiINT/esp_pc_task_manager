/*
 * ESP32-S3 PC Monitor
 *
 * Renders live telemetry (uptime, CPU, RAM, NVIDIA GPU) pushed by a small agent
 * running on the PC to a 240x240 IPS ST7789 panel.
 *
 *   host agent  --USB CDC / BLE NUS-->  pcmon_link  -->  pcmon_ui  -->  ST7789
 *
 * Startup order matters: the display owns the LVGL context, the link must be up
 * before the UI subscribes to it, and the UI task takes over LVGL afterwards.
 */
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "pcmon_display.h"
#include "pcmon_link.h"
#include "pcmon_ui.h"

static const char *TAG = "pcmon";

/** NVS is required by the BLE stack (bonding info) and harmless otherwise. */
static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "PC Monitor starting, transport: %s", pcmon_link_transport_name());

    nvs_init();

    ESP_ERROR_CHECK(pcmon_display_init());
    ESP_ERROR_CHECK(pcmon_link_start());
    ESP_ERROR_CHECK(pcmon_ui_start());

    ESP_LOGI(TAG, "init done");
}
