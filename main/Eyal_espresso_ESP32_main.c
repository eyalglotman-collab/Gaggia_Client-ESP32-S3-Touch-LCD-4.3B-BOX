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
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui_screen.h"
#include "hardware_init.h"
#include "peripherals_manager.h"

static const char *TAG = "espresso";
#define UI_INIT_STATUS_SHOW_MS (300)

/**
 * @brief Update the initialization splash with the current startup step.
 *
 * @details Uses the LVGL lock to safely replace the splash status text while
 * initialization work is running on the main task.
 *
 * @param[in] status_text New initialization step text.
 */
static void ui_set_init_status(const char *status_text)
{
    if (lvgl_port_lock(-1)) {
        ui_screen_set_init_status(status_text);
        lvgl_port_unlock();
    }
}

/**
 * @brief Update the splash status text and yield so it becomes visible.
 *
 * @details Lets the LVGL worker run for a short interval after each status
 * change so the operator can see each initialization step before the next
 * blocking startup action begins.
 *
 * @param[in] status_text New initialization step text.
 */
static void ui_set_init_status_and_yield(const char *status_text)
{
    ui_set_init_status(status_text);
    vTaskDelay(pdMS_TO_TICKS(UI_INIT_STATUS_SHOW_MS));
}

/**
 * @brief Run a lightweight internal ESP32 health check.
 *
 * @details Verifies that core runtime services such as flash size discovery,
 * PSRAM reporting, and free heap availability are in a sane state before the
 * application begins peripheral initialization.
 *
 * @return
 *      - ESP_OK: Internal checks passed
 *      - ESP_FAIL: One or more internal checks failed
 */
static esp_err_t espresso_run_internal_checks(void)
{
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Internal check failed: flash size read");
        return ESP_FAIL;
    }

    size_t internal_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,
             "Internal check: flash=%" PRIu32 "MB internal_heap=%u psram_heap=%u",
             flash_size / (uint32_t)(1024 * 1024),
             (unsigned)internal_heap,
             (unsigned)psram_heap);

    if (internal_heap == 0) {
        ESP_LOGE(TAG, "Internal check failed: no internal heap");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Execute the staged client initialization flow.
 *
 * @details Shows the splash progress steps, runs internal and peripheral checks,
 * requests controller initialization status, and then returns the UI to the
 * main screen.
 */
static void espresso_run_client_initialization_sequence(void)
{
    ui_set_init_status_and_yield("Testing internal ESP32 components...");
    esp_err_t internal_ret = espresso_run_internal_checks();
    if (internal_ret != ESP_OK) {
        ESP_LOGW(TAG, "Internal ESP32 checks reported failure");
    }

    ui_set_init_status_and_yield("Initializing RTC...");
    esp_err_t rtc_ret = peripherals_manager_init_rtc_now();
    if (rtc_ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC initialized successfully");
    } else {
        ESP_LOGW(TAG, "RTC initialization failed: %s", esp_err_to_name(rtc_ret));
    }

    ui_set_init_status_and_yield("Initializing TF card...");
    esp_err_t tf_ret = peripherals_manager_init_tf_card();
    if (tf_ret == ESP_OK) {
        ESP_LOGI(TAG, "TF card initialized successfully");
    } else {
        ESP_LOGW(TAG, "TF card initialization failed: %s", esp_err_to_name(tf_ret));
    }

    ui_set_init_status_and_yield("Initializing Wi-Fi...");
    esp_err_t wifi_ret = peripherals_manager_init_wifi();
    if (wifi_ret == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi initialized successfully");
    } else {
        ESP_LOGW(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(wifi_ret));
    }

    ui_set_init_status_and_yield("Requesting Gaggia Controller status...");
    peripherals_controller_status_t controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    esp_err_t controller_ret = peripherals_manager_request_controller_init(&controller_status);
    if (controller_ret == ESP_OK) {
        ESP_LOGI(TAG,
                 "Controller initialization status: %s",
                 peripherals_manager_controller_status_to_string(controller_status));
    } else {
        ESP_LOGW(TAG, "Controller initialization request failed: %s", esp_err_to_name(controller_ret));
    }

    char final_status[80];
    snprintf(final_status,
             sizeof(final_status),
             "Controller status: %s",
             peripherals_manager_controller_status_to_string(controller_status));
    ui_set_init_status_and_yield(final_status);

    if (lvgl_port_lock(-1)) {
        ui_screen_show_main();
        lvgl_port_unlock();
    }
}

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
    uint32_t flash_size = 0;
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

            /* Create initialization splash before running staged startup checks. */
            if (lvgl_port_lock(-1)) {
                ui_screen_create();
                lvgl_port_unlock();
            }

            espresso_run_client_initialization_sequence();
        } else {
            ESP_LOGE(TAG, "LVGL initialization failed");
        }
    } else {
        ESP_LOGW(TAG, "LCD/touch initialization not available");
        ESP_LOGW(TAG, "Application will continue without LVGL display");
    }

    /* Keep the app running */
    while (1) {
        if (ui_screen_take_reinit_request()) {
            ESP_LOGI(TAG, "Re-running client initialization sequence");
            espresso_run_client_initialization_sequence();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
