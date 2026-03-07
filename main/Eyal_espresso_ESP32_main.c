/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui_screen.h"
#include "hardware_init.h"

static const char *TAG = "espresso";

/**
 * @brief Forward RGB VSYNC events to LVGL port synchronization.
 *
 * @details Called by the ESP LCD RGB panel ISR context to unblock LVGL flush
 * completion waiting logic implemented in demo-based `lvgl_port.c`.
 */
IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel,
                                             const esp_lcd_rgb_panel_event_data_t *edata,
                                             void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    return lvgl_port_notify_rgb_vsync();
}

/**
 * @brief Initialize display and touch hardware resources.
 *
 * @details Delegates board-specific peripheral setup to `hardware_init()`.
 * This keeps application startup orchestration local while isolating pin and
 * panel-controller logic in the hardware abstraction module.
 */
static esp_err_t lcd_and_touch_init(esp_lcd_panel_handle_t *lcd_handle, 
                                     esp_lcd_touch_handle_t *tp_handle)
{
    return hardware_init(lcd_handle, tp_handle);
}

/**
 * @brief Application entry point for firmware startup.
 *
 * @details Reports chip/runtime diagnostics, initializes display + LVGL stack,
 * constructs the initial UI, and keeps the main task alive for background
 * framework processing.
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Eyal_espresso_ESP32!");
    esp_log_level_set("GT911", ESP_LOG_ERROR);

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(TAG, "silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }

    ESP_LOGI(TAG, "%" PRIu32 "MB %s flash", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG, "Minimum free heap size: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());

    /* Initialize LCD and touchpad */
    esp_lcd_panel_handle_t lcd_handle = NULL;
    esp_lcd_touch_handle_t tp_handle = NULL;
    
    if (lcd_and_touch_init(&lcd_handle, &tp_handle) == ESP_OK && lcd_handle != NULL) {
        /* Initialize LVGL */
        if (lvgl_port_init(lcd_handle, tp_handle) == ESP_OK) {
            ESP_LOGI(TAG, "LVGL initialized successfully");

            /* With bounce buffering enabled, signal LVGL after bounce-frame completion. */
            esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if HARDWARE_LCD_RGB_BOUNCE_BUFFER_HEIGHT > 0
                .on_bounce_frame_finish = rgb_lcd_on_vsync_event,
#else
                .on_vsync = rgb_lcd_on_vsync_event,
#endif
            };
            esp_err_t cb_ret = esp_lcd_rgb_panel_register_event_callbacks(lcd_handle, &cbs, NULL);
            if (cb_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to register RGB VSYNC callback: %s", esp_err_to_name(cb_ret));
            }
            
            /* Create UI screen */
            if (lvgl_port_lock(-1)) {
                ui_screen_create();
                lvgl_port_unlock();
            }
        } else {
            ESP_LOGE(TAG, "LVGL initialization failed");
        }
    } else {
        ESP_LOGW(TAG, "LCD/touch initialization not available");
        ESP_LOGW(TAG, "Application will continue without LVGL display");
    }

    /* Keep the app running */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
