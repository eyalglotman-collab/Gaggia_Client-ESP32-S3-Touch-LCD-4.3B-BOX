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
#include "peripherals_manager.h"
#include "system_constants.h"

static const char *TAG = "espresso";
#define UI_INIT_STATUS_SHOW_MS (300)
#define UI_INIT_ERROR_TEXT_MAX (320)
#define UI_INIT_WARNING_TEXT_MAX (256)

typedef struct {
    ui_init_mode_t selected_mode;
    bool offline_mode_requested;
    bool server_communication_enabled;
} espresso_startup_options_t;

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
 * @brief Apply the shared startup failure policy for one init step.
 *
 * @details This helper is the C equivalent of a strict try/catch policy for
 * startup work: every init-like step returns an `esp_err_t` or explicit status,
 * and this function guarantees the result is handled in exactly one place.
 * Online startup escalates failures to the blocking error screen. Offline
 * startup keeps the same sequence but downgrades failures to warnings with
 * step-specific detail in the log.
 *
 * @param[in] offline Startup offline-mode flag.
 * @param[in] step_name Human-readable step label.
 * @param[in] detail_text Detailed failure explanation.
 *
 * @return `true` when startup should continue, otherwise `false`.
 */
static bool espresso_handle_init_step_issue(bool offline,
                                            const char *step_name,
                                            const char *detail_text)
{
    const char *safe_step = (step_name != NULL) ? step_name : "Unknown";
    const char *safe_detail = (detail_text != NULL) ? detail_text : "Unknown";

    if (offline) {
        char warning_text[UI_INIT_WARNING_TEXT_MAX];
        snprintf(warning_text,
                 sizeof(warning_text),
                 "Offline init warning: step '%s' failed. Detail: %s",
                 safe_step,
                 safe_detail);
        ESP_LOGW(TAG, "%s", warning_text);
        return true;
    }

    ESP_LOGE(TAG, "Online init failure at step '%s': %s", safe_step, safe_detail);
    return espresso_handle_init_failure(safe_step, safe_detail);
}

/**
 * @brief Handle an `esp_err_t` result from a startup step.
 *
 * @details Centralizes the result check so every startup init call is handled
 * and none are left to fail silently or bypass the offline warning policy.
 *
 * @param[in] offline Startup offline-mode flag.
 * @param[in] step_name Human-readable step label.
 * @param[in] ret Result code returned by the startup step.
 *
 * @return `true` when startup should continue, otherwise `false`.
 */
static bool espresso_handle_init_step_result(bool offline,
                                             const char *step_name,
                                             esp_err_t ret)
{
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s initialized successfully", step_name);
        return true;
    }

    return espresso_handle_init_step_issue(offline, step_name, esp_err_to_name(ret));
}

/**
 * @brief Run a lightweight internal ESP32 health check.
 *
 * @details Verifies that core runtime services such as flash size discovery,
 * PSRAM reporting, and free heap availability are in a sane state before the
 * application begins peripheral initialization.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: Internal checks passed
 *      - ESP_FAIL: One or more internal checks failed
 */
static esp_err_t espresso_run_internal_checks(bool offline)
{
    if (offline) {
        ESP_LOGI(TAG, "Internal checks running in offline mode; caller downgrades failures to warnings");
    }

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
 * @brief Build normalized startup options from the selected UI mode.
 *
 * @details Keeps the current behavior explicit: both Online and Offline choices
 * continue to use server communication for now, while the Offline choice is
 * preserved as a dedicated startup flag for future scoping.
 *
 * @param[in] init_mode Selected startup mode from the splash UI.
 *
 * @return Startup options structure used by the staged initialization flow.
 */
static espresso_startup_options_t espresso_make_startup_options(ui_init_mode_t init_mode)
{
    espresso_startup_options_t options = {
        .selected_mode = init_mode,
        .offline_mode_requested = (init_mode == UI_INIT_MODE_OFFLINE),
        .server_communication_enabled = true,
    };

    return options;
}

/**
 * @brief Execute the staged client initialization flow.
 *
 * @details Shows the splash progress steps, runs internal and peripheral checks,
 * requests controller initialization status, and then returns the UI to the
 * main screen.
 */
static bool espresso_run_client_initialization_sequence(const espresso_startup_options_t *startup_options)
{
    if (startup_options == NULL) {
        return false;
    }

    if (lvgl_port_lock(-1)) {
        ui_screen_begin_initialization(startup_options->selected_mode);
        lvgl_port_unlock();
    }

    ui_set_init_status_and_yield("Testing internal ESP32 components...");
    esp_err_t internal_ret = espresso_run_internal_checks(startup_options->offline_mode_requested);
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested,
                                          "Internal ESP32 components",
                                          internal_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Initializing RTC...");
    esp_err_t rtc_ret = peripherals_manager_init_rtc_now(startup_options->offline_mode_requested);
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested, "RTC", rtc_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Initializing TF card...");
    esp_err_t tf_ret = peripherals_manager_init_tf_card(startup_options->offline_mode_requested);
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested, "TF card", tf_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Initializing Wi-Fi...");
    esp_err_t wifi_ret = peripherals_manager_init_wifi(startup_options->offline_mode_requested);
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested, "Wi-Fi", wifi_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Loading system constants...");
    esp_err_t constants_ret = system_constants_load();
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested,
                                          "SystemConstants DB",
                                          constants_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Preparing communication state machine...");
    esp_err_t comm_ret = communication_functions_init(startup_options->offline_mode_requested);
    if (!espresso_handle_init_step_result(startup_options->offline_mode_requested,
                                          "Communication state machine",
                                          comm_ret)) {
        return false;
    }

    ui_set_init_status_and_yield("Skipping Gaggia RS485 controller checks...");
    ESP_LOGI(TAG,
             "RS485 controller status and version checks are intentionally disabled during startup. "
             "UART2/RS485 remains initialized only for future use.");
    /* TODO(offline-transport): Re-enable Gaggia RS485 startup queries when the
     * controller-side transport is formally in scope again. */

    if (!startup_options->server_communication_enabled) {
        return espresso_handle_init_step_issue(startup_options->offline_mode_requested,
                                               "Startup configuration",
                                               "Server communication is disabled, which is not supported yet.");
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

            espresso_startup_options_t startup_options = espresso_make_startup_options(init_mode);
            espresso_run_client_initialization_sequence(&startup_options);
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
            espresso_startup_options_t startup_options = espresso_make_startup_options(init_mode);
            espresso_run_client_initialization_sequence(&startup_options);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
