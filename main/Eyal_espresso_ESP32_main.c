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
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui_screen.h"
#include "hardware_init.h"

static const char *TAG = "espresso";

/**
 * @brief Initialize LCD and touch hardware
 */
static esp_err_t lcd_and_touch_init(esp_lcd_panel_handle_t *lcd_handle, 
                                     esp_lcd_touch_handle_t *tp_handle)
{
    return hardware_init(lcd_handle, tp_handle);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Eyal_espresso_ESP32!");

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
            
            /* Create UI screen */
            if (lvgl_port_lock(0)) {
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
