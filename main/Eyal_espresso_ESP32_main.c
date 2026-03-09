/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
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
#include "CommunicationFunctions.h"
#include "EspLogBuffer.h"
#include "peripherals_manager.h"
#include "system_constants.h"

static const char *TAG = "espresso";
#define UI_INIT_STATUS_SHOW_MS (300)
#define UI_INIT_ERROR_TEXT_MAX (320)

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
 * @brief Show an initialization failure and transition to the error screen.
 *
 * @details Updates the splash text with failure styling, waits for the
 * operator's confirm action, and then switches to the persistent error screen.
 *
 * @param[in] step_name Failed initialization step.
 * @param[in] detail_text Failure detail text.
 *
 * @return Always returns `false` for convenient use in failure paths.
 */
static bool espresso_handle_init_failure(const char *step_name, const char *detail_text)
{
    char error_text[UI_INIT_ERROR_TEXT_MAX];
    snprintf(error_text,
             sizeof(error_text),
             "Initialization failed.\n\nStep: %s\n\nDetail: %s",
             (step_name != NULL) ? step_name : "Unknown",
             (detail_text != NULL) ? detail_text : "Unknown");

    if (lvgl_port_lock(-1)) {
        ui_screen_set_init_failed(true);
        ui_screen_set_init_status(error_text);
        ui_screen_show_init_failure_confirm();
        lvgl_port_unlock();
    }

    while (!ui_screen_take_init_failure_confirm()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (lvgl_port_lock(-1)) {
        ui_screen_show_error(error_text);
        lvgl_port_unlock();
    }

    return false;
}

/**
 * @brief Check whether a version string matches a compatibility pattern.
 *
 * @details Supports exact matches and simple trailing `x` wildcards such as
 * `0.2.x` for coarse compatibility checks during startup.
 *
 * @param[in] version Concrete version string.
 * @param[in] pattern Compatibility pattern string.
 *
 * @return `true` when the version satisfies the pattern.
 */
static bool espresso_version_matches_pattern(const char *version, const char *pattern)
{
    if (version == NULL || pattern == NULL) {
        return false;
    }

    const char *wildcard = strchr(pattern, 'x');
    if (wildcard == NULL) {
        return strcmp(version, pattern) == 0;
    }

    size_t prefix_len = (size_t)(wildcard - pattern);
    return strncmp(version, pattern, prefix_len) == 0;
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
static bool espresso_run_client_initialization_sequence(ui_init_mode_t init_mode)
{
    if (lvgl_port_lock(-1)) {
        ui_screen_begin_initialization(init_mode);
        lvgl_port_unlock();
    }

    ui_set_init_status_and_yield("Testing internal ESP32 components...");
    esp_err_t internal_ret = espresso_run_internal_checks();
    if (internal_ret != ESP_OK) {
        ESP_LOGW(TAG, "Internal ESP32 checks reported failure");
        return espresso_handle_init_failure("Internal ESP32 components",
                                            "Internal hardware checks reported failure.");
    }

    ui_set_init_status_and_yield("Initializing RTC...");
    esp_err_t rtc_ret = peripherals_manager_init_rtc_now();
    if (rtc_ret == ESP_OK) {
        ESP_LOGI(TAG, "RTC initialized successfully");
    } else {
        ESP_LOGW(TAG, "RTC initialization failed: %s", esp_err_to_name(rtc_ret));
        return espresso_handle_init_failure("RTC", esp_err_to_name(rtc_ret));
    }

    ui_set_init_status_and_yield("Initializing TF card...");
    esp_err_t tf_ret = peripherals_manager_init_tf_card();
    if (tf_ret == ESP_OK) {
        ESP_LOGI(TAG, "TF card initialized successfully");
    } else {
        ESP_LOGW(TAG, "TF card initialization failed: %s", esp_err_to_name(tf_ret));
        return espresso_handle_init_failure("TF card", esp_err_to_name(tf_ret));
    }

    if (init_mode == UI_INIT_MODE_OFFLINE) {
        ui_set_init_status_and_yield("Offline mode selected: simulating Wi-Fi...");
        peripherals_manager_set_connection_simulation(false, PERIPHERALS_CONTROLLER_STATUS_READY);
        ESP_LOGI(TAG, "Offline mode: Wi-Fi initialization simulated");
    } else {
        ui_set_init_status_and_yield("Initializing Wi-Fi...");
        esp_err_t wifi_ret = peripherals_manager_init_wifi();
        if (wifi_ret == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi initialized successfully");
        } else {
            ESP_LOGW(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(wifi_ret));
            return espresso_handle_init_failure("Wi-Fi", esp_err_to_name(wifi_ret));
        }
    }

    ui_set_init_status_and_yield("Loading system constants...");
    esp_err_t constants_ret = system_constants_load();
    if (constants_ret == ESP_OK) {
        ESP_LOGI(TAG, "System constants loaded successfully");
    } else {
        ESP_LOGW(TAG, "System constants load failed: %s", esp_err_to_name(constants_ret));
        return espresso_handle_init_failure("SystemConstants DB", esp_err_to_name(constants_ret));
    }

    ui_set_init_status_and_yield("Preparing communication state machine...");
    esp_err_t comm_ret = communication_functions_init();
    if (comm_ret == ESP_OK) {
        ESP_LOGI(TAG, "Communication state machine initialized successfully");
    } else {
        ESP_LOGW(TAG, "Communication state machine init failed: %s", esp_err_to_name(comm_ret));
        return espresso_handle_init_failure("Communication state machine", esp_err_to_name(comm_ret));
    }

    peripherals_controller_status_t controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    if (init_mode == UI_INIT_MODE_OFFLINE) {
        ui_set_init_status_and_yield("Simulating Gaggia BIT and status check...");
        controller_status = PERIPHERALS_CONTROLLER_STATUS_READY;
        peripherals_manager_set_connection_simulation(false, controller_status);
        ESP_LOGI(TAG, "Offline mode: controller initialization simulated as READY");
    } else {
        ui_set_init_status_and_yield("Requesting Gaggia Controller status...");
        esp_err_t controller_ret = peripherals_manager_request_controller_init(&controller_status);
        if (controller_ret != ESP_OK ||
            controller_status == PERIPHERALS_CONTROLLER_STATUS_OFFLINE ||
            controller_status == PERIPHERALS_CONTROLLER_STATUS_UNKNOWN ||
            controller_status == PERIPHERALS_CONTROLLER_STATUS_ERROR) {
            const char *failure_text = (controller_ret == ESP_OK)
                                           ? peripherals_manager_controller_status_to_string(controller_status)
                                           : esp_err_to_name(controller_ret);
            ESP_LOGW(TAG, "Controller initialization failed: %s", failure_text);
            return espresso_handle_init_failure("Gaggia BIT and status check", failure_text);
        }
        ESP_LOGI(TAG,
                 "Controller initialization status: %s",
                 peripherals_manager_controller_status_to_string(controller_status));
    }

    ui_set_init_status_and_yield("Checking version compatibility...");
    const system_constants_data_t *constants = system_constants_get();
    if (init_mode == UI_INIT_MODE_OFFLINE) {
        if (!espresso_version_matches_pattern(constants->client_version, constants->compatible_client_version)) {
            return espresso_handle_init_failure("Version compatibility check",
                                                "Offline compatibility simulation failed.");
        }
        ESP_LOGI(TAG, "Offline mode: version compatibility simulated as PASS");
    } else {
        char controller_version[64];
        esp_err_t version_ret = peripherals_manager_request_controller_version(controller_version,
                                                                               sizeof(controller_version));
        if (version_ret != ESP_OK) {
            return espresso_handle_init_failure("Version compatibility check", esp_err_to_name(version_ret));
        }
        if (!espresso_version_matches_pattern(controller_version, constants->compatible_client_version)) {
            return espresso_handle_init_failure("Version compatibility check",
                                                "Controller version is not compatible with the client.");
        }
        ESP_LOGI(TAG,
                 "Controller version %s is compatible with %s",
                 controller_version,
                 constants->compatible_client_version);
    }

    if (lvgl_port_lock(-1)) {
        ui_screen_show_main();
        lvgl_port_unlock();
    }

    return true;
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
    (void)esp_log_buffer_init();
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

            ui_init_mode_t init_mode = UI_INIT_MODE_NONE;
            while (init_mode == UI_INIT_MODE_NONE) {
                init_mode = ui_screen_take_init_mode_selection();
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            espresso_run_client_initialization_sequence(init_mode);
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
            ui_init_mode_t init_mode = UI_INIT_MODE_NONE;
            while (init_mode == UI_INIT_MODE_NONE) {
                init_mode = ui_screen_take_init_mode_selection();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            espresso_run_client_initialization_sequence(init_mode);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
