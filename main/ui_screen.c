/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "CommunicationFunctions.h"
#include "ui_screen.h"
#include "hardware_init.h"
#include "peripherals_manager.h"
#include "system_constants.h"

static const char *TAG = "ui_screen";

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_BREW,
    UI_PAGE_PROFILES,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT
} ui_page_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *init_title_label;
    lv_obj_t *init_status_label;
    lv_obj_t *init_mode_prompt_label;
    lv_obj_t *init_fail_label;
    lv_obj_t *init_mode_yes_btn;
    lv_obj_t *init_mode_no_btn;
    lv_obj_t *init_confirm_btn;
    lv_obj_t *error_screen;
    lv_obj_t *tabview;
    lv_obj_t *tab_pages[UI_PAGE_COUNT];
    lv_obj_t *page_status;
    lv_obj_t *page_runtime;
    lv_obj_t *clock_label;
    lv_obj_t *clock_set_overlay;
    lv_obj_t *connection_info_overlay;
    lv_obj_t *system_constants_overlay;
    lv_obj_t *clock_set_day_roller;
    lv_obj_t *clock_set_month_roller;
    lv_obj_t *clock_set_year_roller;
    lv_obj_t *clock_set_hour_roller;
    lv_obj_t *clock_set_minute_roller;
    lv_obj_t *connection_info_details_label;
    bool connection_info_show_scan_results;
    lv_obj_t *content;
    lv_obj_t *brew_toggle_btn;
    lv_obj_t *steam_toggle_btn;
    lv_obj_t *home_active_profile;
    lv_obj_t *home_target_label;
    lv_obj_t *settings_target_slider;
    lv_obj_t *settings_target_value;
    lv_obj_t *settings_preinf_slider;
    lv_obj_t *settings_preinf_value;
    lv_obj_t *settings_backlight_toggle;
    lv_timer_t *heartbeat_timer;
    ui_page_t active_page;
    ui_init_mode_t init_mode_selection;
    bool brewing;
    bool steaming;
    int active_profile;
    int target_temp_c;
    int preinf_s;
    int shot_s;
    bool reinit_requested;
    bool init_failure_confirm_requested;
} ui_state_t;

static ui_state_t s_ui = {
    .root = NULL,
    .init_title_label = NULL,
    .init_status_label = NULL,
    .init_mode_prompt_label = NULL,
    .init_fail_label = NULL,
    .init_mode_yes_btn = NULL,
    .init_mode_no_btn = NULL,
    .init_confirm_btn = NULL,
    .error_screen = NULL,
    .tabview = NULL,
    .tab_pages = {NULL},
    .page_status = NULL,
    .page_runtime = NULL,
    .clock_label = NULL,
    .clock_set_overlay = NULL,
    .connection_info_overlay = NULL,
    .system_constants_overlay = NULL,
    .clock_set_day_roller = NULL,
    .clock_set_month_roller = NULL,
    .clock_set_year_roller = NULL,
    .clock_set_hour_roller = NULL,
    .clock_set_minute_roller = NULL,
    .connection_info_details_label = NULL,
    .connection_info_show_scan_results = false,
    .content = NULL,
    .brew_toggle_btn = NULL,
    .steam_toggle_btn = NULL,
    .home_active_profile = NULL,
    .home_target_label = NULL,
    .settings_target_slider = NULL,
    .settings_target_value = NULL,
    .settings_preinf_slider = NULL,
    .settings_preinf_value = NULL,
    .settings_backlight_toggle = NULL,
    .heartbeat_timer = NULL,
    .active_page = UI_PAGE_HOME,
    .init_mode_selection = UI_INIT_MODE_NONE,
    .brewing = false,
    .steaming = false,
    .active_profile = 1,
    .target_temp_c = 93,
    .preinf_s = 4,
    .shot_s = 0,
    .reinit_requested = false,
    .init_failure_confirm_requested = false,
};

#define UI_COLOR_BG            0x070B11
#define UI_COLOR_PANEL         0x111827
#define UI_COLOR_PANEL_ALT     0x0F172A
#define UI_COLOR_CARD          0x162033
#define UI_COLOR_CARD_ALT      0x1B263B
#define UI_COLOR_BORDER        0x243349
#define UI_COLOR_TEXT          0xE5EEF9
#define UI_COLOR_TEXT_MUTED    0x9FB3C8
#define UI_COLOR_ACCENT        0x38BDF8
#define UI_COLOR_ACCENT_ALT    0x0EA5E9
#define UI_COLOR_SUCCESS       0x22C55E
#define UI_CLOCK_SET_YEAR_START (2020)
#define UI_CLOCK_SET_YEAR_END   (2045)
#define UI_TABVIEW_HEIGHT      (432)
#define UI_CLOCK_BAR_HEIGHT    (56)
#define UI_SYSTEM_CONSTANTS_TEXT_MAX (4096)

static const system_constants_data_t *ui_get_constants(void);
static const system_constants_profile_t *ui_get_profile_constants(int profile_index);
static void ui_apply_profile_defaults(int profile_index);
static const char *ui_get_system_constants_pretty_text(void);
static void ui_update_connection_info_overlay_contents(void);

static char s_system_constants_pretty_text[UI_SYSTEM_CONSTANTS_TEXT_MAX];

/**
 * @brief Handle the startup-mode `Yes` / `No` prompt selection.
 *
 * @details Stores the operator's requested initialization mode so the main task
 * can begin either offline or online startup sequencing.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_init_mode_select_event_cb(lv_event_t *e)
{
    intptr_t mode_value = (intptr_t)lv_event_get_user_data(e);
    if (mode_value == (intptr_t)UI_INIT_MODE_OFFLINE) {
        s_ui.init_mode_selection = UI_INIT_MODE_OFFLINE;
    } else {
        s_ui.init_mode_selection = UI_INIT_MODE_ONLINE;
    }
}

/**
 * @brief Handle initialization failure confirmation presses.
 *
 * @details Captures the operator acknowledgement so the application can switch
 * from the failed initialization view to the dedicated error screen.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_init_failure_confirm_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.init_failure_confirm_requested = true;
}

/**
 * @brief Apply shared dark card styling.
 *
 * @details Normalizes panel widgets to the selected dark UI palette so each
 * tab feels visually consistent.
 *
 * @param[in] obj LVGL object to style.
 * @param[in] bg_hex Background color in hex RGB form.
 */
static void ui_style_card(lv_obj_t *obj, uint32_t bg_hex)
{
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
}

/**
 * @brief Apply shared styling to action and toggle buttons.
 *
 * @details Gives both momentary buttons and checkable toggle buttons a
 * consistent button-like surface with pressed-state motion and checked-state
 * color changes.
 *
 * @param[in] btn LVGL button object.
 */
static void ui_style_action_button(lv_obj_t *btn)
{
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x020617), LV_PART_MAIN);
    lv_obj_set_style_translate_y(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN);
}

/**
 * @brief Apply shared styling to button labels.
 *
 * @details Keeps labels readable across normal, pressed, and checked button
 * states.
 *
 * @param[in] label LVGL label object hosted inside a button.
 */
static void ui_style_button_label(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_STATE_PRESSED);
}

/**
 * @brief Apply shared dark slider styling.
 *
 * @details Uses the accent color for the active range while keeping the track
 * subdued against dark panels.
 *
 * @param[in] slider LVGL slider object.
 */
static void ui_style_slider(lv_obj_t *slider)
{
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF8FAFC), LV_PART_KNOB);
}

/**
 * @brief Update high-level status line in the header.
 *
 * @details Builds a single concise status text showing active mode and profile
 * to mimic dashboard-style espresso workflows.
 */
static void ui_update_header_status(void)
{
    if (!s_ui.page_status) {
        return;
    }

    char line[96];
    const char *mode = s_ui.brewing ? "BREWING" : (s_ui.steaming ? "STEAM" : "IDLE");
    snprintf(line, sizeof(line), "%s  |  Profile %d  |  Target %d C",
             mode, s_ui.active_profile, s_ui.target_temp_c);
    lv_label_set_text(s_ui.page_status, line);
}

/**
 * @brief Update live runtime text.
 *
 * @details Renders shot timer and a simple synthetic pressure/flow readout to
 * keep the home screen feeling active.
 */
static void ui_update_header_runtime(void)
{
    if (!s_ui.page_runtime) {
        return;
    }

    int pressure_tenths = s_ui.brewing ? (85 + (s_ui.shot_s % 20)) : 2;
    char line[96];
    snprintf(line, sizeof(line), "Shot %02ds  |  Pressure %d.%d bar",
             s_ui.shot_s, pressure_tenths / 10, pressure_tenths % 10);
    lv_label_set_text(s_ui.page_runtime, line);
}

/**
 * @brief Update the persistent bottom clock bar from the RTC.
 *
 * @details Reads the latest RTC time if available and formats a compact clock
 * string for the always-visible lower status bar.
 */
static void ui_update_clock_bar(void)
{
    if (!s_ui.clock_label) {
        return;
    }

    struct tm rtc_tm = {0};
    if (peripherals_manager_get_rtc_time(&rtc_tm) == ESP_OK) {
        static const char *wday_names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char *month_names[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        char txt[64];
        const char *wday = (rtc_tm.tm_wday >= 0 && rtc_tm.tm_wday < 7) ? wday_names[rtc_tm.tm_wday] : "---";
        const char *month = (rtc_tm.tm_mon >= 0 && rtc_tm.tm_mon < 12) ? month_names[rtc_tm.tm_mon] : "---";
        snprintf(txt,
                 sizeof(txt),
                 "%s, %s-%d, %04d %02d:%02d:%02d",
                 wday,
                 month,
                 rtc_tm.tm_mday,
                 rtc_tm.tm_year + 1900,
                 rtc_tm.tm_hour,
                 rtc_tm.tm_min,
                 rtc_tm.tm_sec);
        lv_label_set_text(s_ui.clock_label, txt);
    } else {
        lv_label_set_text(s_ui.clock_label, "--------, --- --, ---- --:--:--");
    }
}

/**
 * @brief Refresh home page detail labels if they exist.
 *
 * @details Updates active profile and target labels without forcing a full page
 * rebuild.
 */
static void ui_update_home_labels(void)
{
    if (s_ui.home_active_profile) {
        const system_constants_profile_t *profile = ui_get_profile_constants(s_ui.active_profile);
        char txt[80];
        snprintf(txt, sizeof(txt), "Active Profile: %s", profile->name);
        lv_label_set_text(s_ui.home_active_profile, txt);
    }

    if (s_ui.home_target_label) {
        char txt[48];
        snprintf(txt, sizeof(txt), "Target Temp: %d C", s_ui.target_temp_c);
        lv_label_set_text(s_ui.home_target_label, txt);
    }
}

/**
 * @brief Update top tab visual selection state.
 *
 * @details Clears checked state from all nav buttons and marks the active page
 * button as selected.
 */
static void ui_update_tab_style(void)
{
    if (!s_ui.tabview) {
        return;
    }

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *btn = lv_tabview_get_tab_button(s_ui.tabview, i);
        if (!btn) {
            continue;
        }
        lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x020617), LV_PART_MAIN);
        lv_obj_set_style_translate_y(btn, 2, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_hex(UI_COLOR_TEXT_MUTED), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    }
}

/**
 * @brief Return the currently loaded system constants snapshot.
 *
 * @details Provides a short local wrapper so UI code can consume the loaded
 * constants database without repeating the accessor call everywhere.
 *
 * @return Pointer to the active constants snapshot.
 */
static const system_constants_data_t *ui_get_constants(void)
{
    return system_constants_get();
}

/**
 * @brief Resolve a one-based profile selection to loaded constants data.
 *
 * @details Maps the UI's profile numbering onto the loaded constants array and
 * clamps invalid indices to the nearest valid profile entry.
 *
 * @param[in] profile_index One-based profile index.
 *
 * @return Pointer to the resolved profile entry.
 */
static const system_constants_profile_t *ui_get_profile_constants(int profile_index)
{
    const system_constants_data_t *constants = ui_get_constants();

    if (constants->profile_count <= 0) {
        return &constants->profiles[0];
    }

    if (profile_index < 1) {
        profile_index = 1;
    }
    if (profile_index > constants->profile_count) {
        profile_index = constants->profile_count;
    }

    return &constants->profiles[profile_index - 1];
}

/**
 * @brief Apply a loaded profile's default targets to the active UI state.
 *
 * @details Copies the selected profile's configured target temperature and
 * preinfusion time from the constants database into the editable UI fields.
 *
 * @param[in] profile_index One-based profile index to apply.
 */
static void ui_apply_profile_defaults(int profile_index)
{
    const system_constants_profile_t *profile = ui_get_profile_constants(profile_index);
    s_ui.active_profile = profile_index;
    s_ui.target_temp_c = profile->target_temperature_c;
    s_ui.preinf_s = profile->preinfusion_seconds;
}

/**
 * @brief Populate a roller with a numeric range.
 *
 * @details Generates newline-separated numeric labels so LVGL rollers can show
 * simple day/month/year/hour/minute selections.
 *
 * @param[in] roller Destination LVGL roller object.
 * @param[in] start_value Inclusive first value.
 * @param[in] end_value Inclusive last value.
 * @param[in] width_digits Minimum digits to print for each option.
 */
static void ui_set_roller_numeric_options(lv_obj_t *roller, int start_value, int end_value, int width_digits)
{
    char options[1024] = {0};
    size_t used = 0;

    for (int value = start_value; value <= end_value; value++) {
        int written = snprintf(options + used,
                               sizeof(options) - used,
                               (value == start_value) ? "%0*d" : "\n%0*d",
                               width_digits,
                               value);
        if (written <= 0 || (size_t)written >= (sizeof(options) - used)) {
            break;
        }
        used += (size_t)written;
    }

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
}

/**
 * @brief Create a labeled time-selection roller.
 *
 * @details Builds a compact label plus roller pair used by the clock-setting
 * overlay.
 *
 * @param[in] parent Overlay panel parent object.
 * @param[in] title Section label text.
 * @param[in] x Left offset within the panel.
 * @param[in] y Top offset within the panel.
 *
 * @return Created LVGL roller object.
 */
static lv_obj_t *ui_create_clock_roller(lv_obj_t *parent, const char *title, int x, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);

    lv_obj_t *roller = lv_roller_create(parent);
    lv_obj_set_size(roller, 120, 124);
    lv_obj_align(roller, LV_ALIGN_TOP_LEFT, x, y + 28);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_COLOR_CARD_ALT), LV_PART_MAIN);
    lv_obj_set_style_text_color(roller, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller, lv_color_hex(UI_COLOR_ACCENT_ALT), LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);
    lv_obj_set_style_border_color(roller, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(roller, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(roller, 14, LV_PART_MAIN);
    return roller;
}

/**
 * @brief Close the clock-setting overlay.
 *
 * @details Deletes the temporary full-screen time-picker UI and clears the
 * stored widget pointers.
 */
static void ui_close_clock_overlay(void)
{
    if (s_ui.clock_set_overlay) {
        lv_obj_del(s_ui.clock_set_overlay);
    }

    s_ui.clock_set_overlay = NULL;
    s_ui.clock_set_day_roller = NULL;
    s_ui.clock_set_month_roller = NULL;
    s_ui.clock_set_year_roller = NULL;
    s_ui.clock_set_hour_roller = NULL;
    s_ui.clock_set_minute_roller = NULL;
}

/**
 * @brief Close the connection-info overlay.
 *
 * @details Deletes the temporary full-screen connection-information UI and
 * clears the stored overlay pointer.
 */
static void ui_close_connection_info_overlay(void)
{
    if (s_ui.connection_info_overlay) {
        lv_obj_del(s_ui.connection_info_overlay);
    }

    s_ui.connection_info_overlay = NULL;
    s_ui.connection_info_details_label = NULL;
    s_ui.connection_info_show_scan_results = false;
}

/**
 * @brief Refresh the dynamic contents of the connection-info overlay.
 *
 * @details Re-reads both peripheral and low-level communication snapshots so
 * the operator can observe the transport state machine progress after a reset.
 */
static void ui_update_connection_info_overlay_contents(void)
{
    if (!s_ui.connection_info_details_label) {
        return;
    }

    peripherals_connection_info_t info = {0};
    communication_snapshot_t comm_snapshot = {0};
    esp_err_t info_ret = peripherals_manager_get_connection_info(&info);
    esp_err_t comm_ret = communication_functions_get_snapshot(&comm_snapshot);
    if (info_ret != ESP_OK) {
        ESP_LOGW(TAG, "Connection info read failed: %s", esp_err_to_name(info_ret));
        snprintf(info.ip_address, sizeof(info.ip_address), "Unavailable");
        snprintf(info.port_text, sizeof(info.port_text), "Unavailable");
        info.wifi_ready = false;
        info.rtc_ready = false;
        info.tf_ready = false;
        info.wifi_ap_count = 0;
        info.controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    }

    if (comm_ret != ESP_OK) {
        ESP_LOGW(TAG, "Communication snapshot read failed: %s", esp_err_to_name(comm_ret));
        snprintf(comm_snapshot.last_error, sizeof(comm_snapshot.last_error), "Snapshot unavailable");
        comm_snapshot.state = COMMUNICATION_STATE_ERROR;
        comm_snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(comm_snapshot.scan_results, sizeof(comm_snapshot.scan_results), "Scan snapshot unavailable");
    }

    if (s_ui.connection_info_show_scan_results) {
        lv_label_set_text_fmt(s_ui.connection_info_details_label,
                              "Device Scan\n"
                              "Scan State: %s\n"
                              "Detected Devices: %u\n"
                              "Duration: %u ms\n"
                              "\n"
                              "%s",
                              communication_functions_scan_state_to_string(comm_snapshot.scan_state),
                              (unsigned)comm_snapshot.scan_device_count,
                              (unsigned)comm_snapshot.scan_duration_ms,
                              comm_snapshot.scan_results);
        return;
    }

    lv_label_set_text_fmt(s_ui.connection_info_details_label,
                          "IP: %s\n"
                          "Port: %s\n"
                          "\n"
                          "Telemetry\n"
                          "Wi-Fi Ready: %s\n"
                          "Visible APs: %u\n"
                          "RTC Ready: %s\n"
                          "TF Card Ready: %s\n"
                          "Controller Status: %s\n"
                          "\n"
                          "Low-Level Communication\n"
                          "State: %s\n"
                          "Default SSID: %s\n"
                          "Server: %s:%u\n"
                          "Local IP: %s\n"
                          "TCP Connected: %s\n"
                          "Initialize Passed: %s\n"
                          "Connect Passed: %s\n"
                          "Send Data Enabled: %s\n"
                          "RSSI: %ld dBm\n"
                          "HostLiveInteger: %" PRIu32 "\n"
                          "DeviceLiveInteger: %" PRIu32 "\n"
                          "Sequence: %u\n"
                          "Last Received Text: %s\n"
                          "Scan State: %s\n"
                          "Last Error: %s",
                          info.ip_address,
                          info.port_text,
                          info.wifi_ready ? "Yes" : "No",
                          info.wifi_ap_count,
                          info.rtc_ready ? "Yes" : "No",
                          info.tf_ready ? "Yes" : "No",
                          peripherals_manager_controller_status_to_string(info.controller_status),
                          communication_functions_state_to_string(comm_snapshot.state),
                          comm_snapshot.config.wifi_ssid,
                          comm_snapshot.config.server_ip,
                          (unsigned)comm_snapshot.config.server_port,
                          comm_snapshot.local_ip,
                          comm_snapshot.tcp_connected ? "Yes" : "No",
                          comm_snapshot.initialize_passed ? "Yes" : "No",
                          comm_snapshot.connect_passed ? "Yes" : "No",
                          comm_snapshot.send_data_enabled ? "Yes" : "No",
                          (long)comm_snapshot.wifi_rssi,
                          comm_snapshot.host_live_integer,
                          comm_snapshot.device_live_integer,
                          (unsigned)comm_snapshot.sequence,
                          comm_snapshot.last_received_text,
                          communication_functions_scan_state_to_string(comm_snapshot.scan_state),
                          comm_snapshot.last_error);
}

/**
 * @brief Close the system-constants overlay.
 *
 * @details Deletes the temporary full-screen XML viewer UI and clears the
 * stored overlay pointer.
 */
static void ui_close_system_constants_overlay(void)
{
    if (s_ui.system_constants_overlay) {
        lv_obj_del(s_ui.system_constants_overlay);
    }

    s_ui.system_constants_overlay = NULL;
}

/**
 * @brief Format loaded system constants into a tree-style text view.
 *
 * @details Converts the active constants snapshot into a readable outline so
 * the hierarchy is clear on screen without showing raw XML tags.
 *
 * @return Pointer to a static formatted XML buffer.
 */
static const char *ui_get_system_constants_pretty_text(void)
{
    const system_constants_data_t *constants = ui_get_constants();
    size_t used = 0;
    int written = 0;

    s_system_constants_pretty_text[0] = '\0';

    if (constants == NULL) {
        snprintf(s_system_constants_pretty_text,
                 sizeof(s_system_constants_pretty_text),
                 "System Constants\n    Unavailable");
        return s_system_constants_pretty_text;
    }

    written = snprintf(
        s_system_constants_pretty_text,
        sizeof(s_system_constants_pretty_text),
        "System Constants\n"
        "    Versions\n"
        "        Client Version: %s\n"
        "        Compatible Version: %s\n"
        "    Connection\n"
        "        Host: %s\n"
        "        Port: %s\n"
        "        Baud Rate: %d\n"
        "    Limits\n"
        "        Temperature: %d-%d C\n"
        "        Pressure: %d.%d-%d.%d bar\n"
        "        Flow: %d.%d-%d.%d ml/s\n"
        "    Profiles (%d)",
        constants->client_version,
        constants->compatible_client_version,
        constants->connection_host,
        constants->connection_port,
        constants->connection_baud_rate,
        constants->temperature_min_c,
        constants->temperature_max_c,
        constants->pressure_min_tenths / 10,
        abs(constants->pressure_min_tenths % 10),
        constants->pressure_max_tenths / 10,
        abs(constants->pressure_max_tenths % 10),
        constants->flow_min_tenths / 10,
        abs(constants->flow_min_tenths % 10),
        constants->flow_max_tenths / 10,
        abs(constants->flow_max_tenths % 10),
        constants->profile_count);

    if (written < 0) {
        s_system_constants_pretty_text[0] = '\0';
        return s_system_constants_pretty_text;
    }

    used = (size_t)written;
    if (used >= sizeof(s_system_constants_pretty_text)) {
        used = sizeof(s_system_constants_pretty_text) - 1U;
    }

    for (int i = 0; i < constants->profile_count && used + 1U < sizeof(s_system_constants_pretty_text); i++) {
        written = snprintf(
            s_system_constants_pretty_text + used,
            sizeof(s_system_constants_pretty_text) - used,
            "\n"
            "        Profile %d\n"
            "            Name: %s\n"
            "            Target Temperature: %d C\n"
            "            Preinfusion: %d s\n"
            "            Target Pressure: %d.%d bar\n"
            "            Target Flow: %d.%d ml/s",
            i + 1,
            constants->profiles[i].name,
            constants->profiles[i].target_temperature_c,
            constants->profiles[i].preinfusion_seconds,
            constants->profiles[i].target_pressure_tenths / 10,
            abs(constants->profiles[i].target_pressure_tenths % 10),
            constants->profiles[i].target_flow_tenths / 10,
            abs(constants->profiles[i].target_flow_tenths % 10));
        if (written < 0) {
            break;
        }
        used += (size_t)written;
        if (used >= sizeof(s_system_constants_pretty_text)) {
            used = sizeof(s_system_constants_pretty_text) - 1U;
            break;
        }
    }

    s_system_constants_pretty_text[used] = '\0';
    return s_system_constants_pretty_text;
}

/**
 * @brief Apply the user-selected clock value to the RTC.
 *
 * @details Reads the active roller selections, converts them into a calendar
 * time, writes the new value into the RTC, then closes the overlay on success.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_clock_set_done_event_cb(lv_event_t *e)
{
    (void)e;

    if (!s_ui.clock_set_day_roller ||
        !s_ui.clock_set_month_roller ||
        !s_ui.clock_set_year_roller ||
        !s_ui.clock_set_hour_roller ||
        !s_ui.clock_set_minute_roller) {
        return;
    }

    struct tm new_tm = {0};
    new_tm.tm_mday = lv_roller_get_selected(s_ui.clock_set_day_roller) + 1;
    new_tm.tm_mon = lv_roller_get_selected(s_ui.clock_set_month_roller);
    new_tm.tm_year = (UI_CLOCK_SET_YEAR_START + lv_roller_get_selected(s_ui.clock_set_year_roller)) - 1900;
    new_tm.tm_hour = lv_roller_get_selected(s_ui.clock_set_hour_roller);
    new_tm.tm_min = lv_roller_get_selected(s_ui.clock_set_minute_roller);
    new_tm.tm_sec = 0;
    new_tm.tm_isdst = -1;

    esp_err_t ret = peripherals_manager_set_rtc_time(&new_tm);
    if (ret == ESP_OK) {
        ui_update_clock_bar();
        ui_close_clock_overlay();
        ESP_LOGI(TAG, "RTC updated from Settings page");
    } else {
        ESP_LOGW(TAG, "RTC update failed from Settings page: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Open the dedicated clock-setting overlay from the Settings tab.
 *
 * @details Builds a full-screen modal UI with rollers for date and time
 * selection and a bottom `Done` action that writes the chosen value to the RTC.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_set_clock_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_clock_overlay();

    struct tm rtc_tm = {0};
    bool have_rtc_time = (peripherals_manager_get_rtc_time(&rtc_tm) == ESP_OK);
    if (!have_rtc_time) {
        rtc_tm.tm_mday = 1;
        rtc_tm.tm_mon = 0;
        rtc_tm.tm_year = 2026 - 1900;
        rtc_tm.tm_hour = 12;
        rtc_tm.tm_min = 0;
    }

    lv_obj_t *scr = lv_screen_active();
    s_ui.clock_set_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.clock_set_overlay);
    lv_obj_set_size(s_ui.clock_set_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.clock_set_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.clock_set_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.clock_set_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Set Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    s_ui.clock_set_day_roller = ui_create_clock_roller(panel, "Day", 20, 76);
    s_ui.clock_set_month_roller = ui_create_clock_roller(panel, "Month", 164, 76);
    s_ui.clock_set_year_roller = ui_create_clock_roller(panel, "Year", 308, 76);
    s_ui.clock_set_hour_roller = ui_create_clock_roller(panel, "Hour", 492, 76);
    s_ui.clock_set_minute_roller = ui_create_clock_roller(panel, "Minute", 620, 76);

    ui_set_roller_numeric_options(s_ui.clock_set_day_roller, 1, 31, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_month_roller, 1, 12, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_year_roller, UI_CLOCK_SET_YEAR_START, UI_CLOCK_SET_YEAR_END, 4);
    ui_set_roller_numeric_options(s_ui.clock_set_hour_roller, 0, 23, 2);
    ui_set_roller_numeric_options(s_ui.clock_set_minute_roller, 0, 59, 2);

    lv_roller_set_selected(s_ui.clock_set_day_roller, rtc_tm.tm_mday - 1, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_month_roller, rtc_tm.tm_mon, LV_ANIM_OFF);
    int year_index = (rtc_tm.tm_year + 1900) - UI_CLOCK_SET_YEAR_START;
    if (year_index < 0) {
        year_index = 0;
    }
    if (year_index > (UI_CLOCK_SET_YEAR_END - UI_CLOCK_SET_YEAR_START)) {
        year_index = UI_CLOCK_SET_YEAR_END - UI_CLOCK_SET_YEAR_START;
    }
    lv_roller_set_selected(s_ui.clock_set_year_roller, year_index, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_hour_roller, rtc_tm.tm_hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_ui.clock_set_minute_roller, rtc_tm.tm_min, LV_ANIM_OFF);

    lv_obj_t *done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 720, 58);
    lv_obj_align(done_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_clock_set_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);
}

/**
 * @brief Close the connection-info screen and return to Settings.
 *
 * @details Dismisses the modal connection-information overlay created from the
 * Settings page.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_done_event_cb(lv_event_t *e)
{
    (void)e;
    ui_close_connection_info_overlay();
}

/**
 * @brief Start a low-level connection reset from the Connection Info overlay.
 *
 * @details Queues a `reset -> initialize -> connect` cycle in the dedicated
 * communication task, then refreshes the overlay to show the new state.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_reset_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.connection_info_show_scan_results = false;
    communication_functions_request_reset();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Start a Wi-Fi device scan from the Connection Info overlay.
 *
 * @details Clears the current text immediately, switches the overlay into scan
 * view, and lets the background communication task populate the list after the
 * fixed 10-second discovery window.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_connection_info_scan_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.connection_info_show_scan_results = true;
    if (s_ui.connection_info_details_label) {
        lv_label_set_text(s_ui.connection_info_details_label, "");
    }
    communication_functions_request_scan();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Close the system-constants screen and return to Settings.
 *
 * @details Dismisses the modal XML viewer overlay created from the Settings
 * page.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_system_constants_done_event_cb(lv_event_t *e)
{
    (void)e;
    ui_close_system_constants_overlay();
}

/**
 * @brief Open the connection-info overlay from the Settings tab.
 *
 * @details Shows the current connection snapshot, including IP address,
 * controller transport port, and a textual telemetry summary, plus a bottom
 * `Done` action that returns to the main UI.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_connection_info_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_connection_info_overlay();

    lv_obj_t *scr = lv_screen_active();
    s_ui.connection_info_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.connection_info_overlay);
    lv_obj_set_size(s_ui.connection_info_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.connection_info_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.connection_info_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.connection_info_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Connection Info");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *reset_btn = lv_button_create(panel);
    lv_obj_set_size(reset_btn, 320, 52);
    lv_obj_align(reset_btn, LV_ALIGN_TOP_LEFT, 20, 60);
    ui_style_action_button(reset_btn);
    lv_obj_add_event_cb(reset_btn, ui_connection_info_reset_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset Connection");
    ui_style_button_label(reset_lbl);
    lv_obj_center(reset_lbl);

    lv_obj_t *scan_btn = lv_button_create(panel);
    lv_obj_set_size(scan_btn, 320, 52);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -20, 60);
    ui_style_action_button(scan_btn);
    lv_obj_add_event_cb(scan_btn, ui_connection_info_scan_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, "Scan for Devices");
    ui_style_button_label(scan_lbl);
    lv_obj_center(scan_lbl);

    lv_obj_t *info_body = lv_obj_create(panel);
    lv_obj_set_size(info_body, 720, 220);
    lv_obj_align(info_body, LV_ALIGN_TOP_MID, 0, 126);
    ui_style_card(info_body, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(info_body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(info_body, 18, 0);
    lv_obj_set_style_pad_row(info_body, 14, 0);
    lv_obj_set_layout(info_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info_body, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *telemetry_card = lv_obj_create(info_body);
    lv_obj_set_width(telemetry_card, lv_pct(100));
    lv_obj_set_height(telemetry_card, LV_SIZE_CONTENT);
    ui_style_card(telemetry_card, UI_COLOR_CARD_ALT);
    lv_obj_clear_flag(telemetry_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(telemetry_card, 18, 0);

    s_ui.connection_info_details_label = lv_label_create(telemetry_card);
    lv_obj_set_width(s_ui.connection_info_details_label, 650);
    lv_label_set_long_mode(s_ui.connection_info_details_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_ui.connection_info_details_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.connection_info_details_label, lv_color_hex(UI_COLOR_TEXT), 0);
    s_ui.connection_info_show_scan_results = false;
    ui_update_connection_info_overlay_contents();

    lv_obj_t *done_btn = lv_button_create(panel);
    lv_obj_set_size(done_btn, 300, 58);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_connection_info_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);

    lv_obj_update_layout(panel);
    lv_coord_t lowest_bottom = lv_obj_get_y(info_body) + lv_obj_get_height(info_body);
    lv_coord_t telemetry_bottom = lv_obj_get_y(info_body) + lv_obj_get_y(telemetry_card) + lv_obj_get_height(telemetry_card);
    if (telemetry_bottom > lowest_bottom) {
        lowest_bottom = telemetry_bottom;
    }
    lv_obj_set_pos(done_btn,
                   (lv_obj_get_width(panel) - lv_obj_get_width(done_btn)) / 2,
                   lowest_bottom + 10);
}

/**
 * @brief Open the System Constants XML viewer from the Settings tab.
 *
 * @details Builds a scrollable overlay that presents the embedded
 * `SystemConstants.xml` text in an indented hierarchy view, with a bottom
 * `Done` button positioned 10 pixels below the rendered text.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_system_constants_event_cb(lv_event_t *e)
{
    (void)e;

    ui_close_system_constants_overlay();

    lv_obj_t *scr = lv_screen_active();
    s_ui.system_constants_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.system_constants_overlay);
    lv_obj_set_size(s_ui.system_constants_overlay, 800, 480);
    lv_obj_set_style_bg_color(s_ui.system_constants_overlay, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_ui.system_constants_overlay, LV_OPA_COVER, 0);

    lv_obj_t *panel = lv_obj_create(s_ui.system_constants_overlay);
    lv_obj_set_size(panel, 760, 440);
    lv_obj_center(panel);
    ui_style_card(panel, UI_COLOR_PANEL);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "System Constants");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    lv_obj_t *xml_body = lv_obj_create(panel);
    lv_obj_set_size(xml_body, 720, 340);
    lv_obj_align(xml_body, LV_ALIGN_TOP_MID, 0, 70);
    ui_style_card(xml_body, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(xml_body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(xml_body, 16, 0);
    lv_obj_set_scroll_dir(xml_body, LV_DIR_VER);

    lv_obj_t *xml_label = lv_label_create(xml_body);
    lv_label_set_long_mode(xml_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(xml_label, 680);
    lv_obj_set_style_text_font(xml_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(xml_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(xml_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(xml_label, ui_get_system_constants_pretty_text());

    lv_obj_t *done_btn = lv_button_create(xml_body);
    lv_obj_set_size(done_btn, 300, 58);
    ui_style_action_button(done_btn);
    lv_obj_add_event_cb(done_btn, ui_system_constants_done_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    ui_style_button_label(done_lbl);
    lv_obj_center(done_lbl);

    lv_obj_update_layout(xml_body);
    lv_coord_t text_bottom = lv_obj_get_y(xml_label) + lv_obj_get_height(xml_label);
    lv_obj_set_pos(done_btn,
                   (lv_obj_get_width(xml_body) - lv_obj_get_width(done_btn)) / 2,
                   text_bottom + 10);
}

/**
 * @brief Build reusable page title inside content region.
 *
 * @param[in] parent Parent LVGL container.
 * @param[in] title Page title text.
 *
 * @return Created title label object.
 */
static lv_obj_t *ui_build_page_title(lv_obj_t *parent, const char *title)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 4);
    return lbl;
}

/**
 * @brief Build the per-page live status summary block.
 *
 * @details Keeps status text inside the active tab content so the only
 * persistent out-of-tab element is the bottom clock bar.
 *
 * @param[in] parent Parent tab page container.
 */
static void ui_build_page_live_summary(lv_obj_t *parent)
{
    s_ui.page_status = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ui.page_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.page_status, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.page_status, LV_ALIGN_TOP_LEFT, 8, 38);

    s_ui.page_runtime = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ui.page_runtime, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ui.page_runtime, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_ui.page_runtime, LV_ALIGN_TOP_LEFT, 8, 60);
}

/**
 * @brief Create a button-style toggle control.
 *
 * @details Uses a checkable LVGL button so toggle actions share the same
 * pressed animation and color behavior as regular push buttons.
 *
 * @param[in] parent Parent container.
 * @param[in] text Button label text.
 * @param[in] checked Initial checked state.
 * @param[in] x X alignment offset from the top-left.
 * @param[in] y Y alignment offset from the top-left.
 * @param[in] cb Event callback for value changes.
 *
 * @return Created LVGL button object.
 */
static lv_obj_t *ui_create_toggle_button(lv_obj_t *parent,
                                         const char *text,
                                         bool checked,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 220, 58);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    ui_style_action_button(btn);
    if (checked) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
    }

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    ui_style_button_label(lbl);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return btn;
}

/**
 * @brief Handle page navigation button click.
 *
 * @details Reads target page enum from event user data and triggers content
 * rebuild for the selected section.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_tabview_event_cb(lv_event_t *e);
void ui_screen_create(void);

/**
 * @brief Rebuild the currently selected tab page.
 *
 * @details Clears and redraws the active tab content while preserving shared UI
 * state stored in `s_ui`.
 */
static void ui_render_active_page(void);

/**
 * @brief Build the full main application UI after initialization completes.
 *
 * @details Creates the tabbed workflow layout for the upper 90% of the screen
 * plus the persistent bottom clock bar.
 */
static void ui_build_main_screen(void);

/**
 * @brief Handle brew toggle button state transitions.
 *
 * @details Toggles brew mode and resets shot timer when brew starts.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_brew_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    s_ui.brewing = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (s_ui.brewing) {
        s_ui.shot_s = 0;
        s_ui.steaming = false;
        if (s_ui.steam_toggle_btn) {
            lv_obj_clear_state(s_ui.steam_toggle_btn, LV_STATE_CHECKED);
        }
        ESP_LOGI(TAG, "brew ON");
    } else {
        ESP_LOGI(TAG, "brew OFF");
    }
    ui_update_header_status();
}

/**
 * @brief Handle steam toggle button state transitions.
 *
 * @details Toggles steam mode and clears brew mode when steam is activated.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_steam_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    s_ui.steaming = lv_obj_has_state(obj, LV_STATE_CHECKED);
    if (s_ui.steaming) {
        s_ui.brewing = false;
        if (s_ui.brew_toggle_btn) {
            lv_obj_clear_state(s_ui.brew_toggle_btn, LV_STATE_CHECKED);
        }
        ESP_LOGI(TAG, "steam ON");
    } else {
        ESP_LOGI(TAG, "steam OFF");
    }
    ui_update_header_status();
}

/**
 * @brief Handle profile selection button click.
 *
 * @details Sets active profile based on button user data index.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_profile_btn_event_cb(lv_event_t *e)
{
    intptr_t profile = (intptr_t)lv_event_get_user_data(e);
    if (profile < 1 || profile > ui_get_constants()->profile_count) {
        return;
    }
    ui_apply_profile_defaults((int)profile);
    ui_update_home_labels();
    ui_update_header_status();
    if (s_ui.active_page == UI_PAGE_PROFILES) {
        ui_render_active_page();
    }
    ESP_LOGI(TAG, "profile %d selected", s_ui.active_profile);
}

/**
 * @brief Handle settings slider value changes.
 *
 * @details Updates target temperature or preinfusion duration and mirrors the
 * numeric value in adjacent labels.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    intptr_t id = (intptr_t)lv_event_get_user_data(e);

    if (id == 0) {
        s_ui.target_temp_c = (int)lv_slider_get_value(slider);
        if (s_ui.settings_target_value) {
            char txt[24];
            snprintf(txt, sizeof(txt), "%d C", s_ui.target_temp_c);
            lv_label_set_text(s_ui.settings_target_value, txt);
        }
    } else if (id == 1) {
        s_ui.preinf_s = (int)lv_slider_get_value(slider);
        if (s_ui.settings_preinf_value) {
            char txt[24];
            snprintf(txt, sizeof(txt), "%d s", s_ui.preinf_s);
            lv_label_set_text(s_ui.settings_preinf_value, txt);
        }
    }

    ui_update_home_labels();
    ui_update_header_status();
}

/**
 * @brief Construct home dashboard page.
 *
 * @details Renders machine overview cards with active profile and target
 * metrics similar to a workflow dashboard landing page.
 */
static void ui_build_page_home(void)
{
    ui_build_page_title(s_ui.content, "Dashboard");
    ui_build_page_live_summary(s_ui.content);

    lv_obj_t *card = lv_obj_create(s_ui.content);
    lv_obj_set_size(card, 760, 250);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, 8, 92);
    ui_style_card(card, UI_COLOR_CARD);

    s_ui.home_active_profile = lv_label_create(card);
    lv_obj_align(s_ui.home_active_profile, LV_ALIGN_TOP_LEFT, 20, 24);
    lv_obj_set_style_text_font(s_ui.home_active_profile, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.home_active_profile, lv_color_hex(UI_COLOR_TEXT), 0);

    s_ui.home_target_label = lv_label_create(card);
    lv_obj_align(s_ui.home_target_label, LV_ALIGN_TOP_LEFT, 20, 68);
    lv_obj_set_style_text_font(s_ui.home_target_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.home_target_label, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Use Brew page to start shot workflow\nUse Profiles page to switch recipe");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 20, 130);

    ui_update_home_labels();
}

/**
 * @brief Construct brew operation page.
 *
 * @details Provides run-time controls for brew and steam operations.
 */
static void ui_build_page_brew(void)
{
    ui_build_page_title(s_ui.content, "Brew");
    ui_build_page_live_summary(s_ui.content);

    lv_obj_t *row1 = lv_label_create(s_ui.content);
    lv_label_set_text(row1, "Pump / Brew");
    lv_obj_set_style_text_font(row1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(row1, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(row1, LV_ALIGN_TOP_LEFT, 20, 104);

    s_ui.brew_toggle_btn = ui_create_toggle_button(s_ui.content,
                                                   "Brew",
                                                   s_ui.brewing,
                                                   500,
                                                   92,
                                                   ui_brew_toggle_event_cb);

    lv_obj_t *row2 = lv_label_create(s_ui.content);
    lv_label_set_text(row2, "Steam Mode");
    lv_obj_set_style_text_font(row2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(row2, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(row2, LV_ALIGN_TOP_LEFT, 20, 186);

    s_ui.steam_toggle_btn = ui_create_toggle_button(s_ui.content,
                                                    "Steam",
                                                    s_ui.steaming,
                                                    500,
                                                    174,
                                                    ui_steam_toggle_event_cb);

    lv_obj_t *note = lv_label_create(s_ui.content);
    lv_label_set_text(note, "Workflow: preinfusion -> extraction -> finish");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(note, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(note, LV_ALIGN_TOP_LEFT, 20, 278);
}

/**
 * @brief Handle Settings-page backlight button presses.
 *
 * @details Toggles the current backlight state through the hardware layer.
 * A later touch anywhere on the panel will still wake the backlight again.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_backlight_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    bool enabled = !hardware_get_backlight_enabled();
    esp_err_t ret = hardware_set_backlight_enabled(enabled);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "settings backlight %s", enabled ? "ON" : "OFF");
    } else {
        ESP_LOGW(TAG, "settings backlight change failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Return the client UI to the initialization state.
 *
 * @details Rebuilds the splash screen so the client visually returns to the
 * initialization state without issuing a hardware reset.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_reboot_client_event_cb(lv_event_t *e)
{
    (void)e;
    s_ui.brewing = false;
    s_ui.steaming = false;
    s_ui.shot_s = 0;
    s_ui.reinit_requested = true;
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    s_ui.init_failure_confirm_requested = false;
    ui_screen_create();
    ESP_LOGI(TAG, "client UI returned to initialization state");
}

/**
 * @brief Handle reset requests from the persistent error screen.
 *
 * @details Reuses the same client-side reinitialization path as the Settings
 * page reboot action so the application returns to the startup prompt without a
 * hardware reset.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_error_reset_event_cb(lv_event_t *e)
{
    ui_settings_reboot_client_event_cb(e);
}

/**
 * @brief Construct profile selection page.
 *
 * @details Displays quick profile presets with one-click activation.
 */
static void ui_build_page_profiles(void)
{
    ui_build_page_title(s_ui.content, "Profiles");
    ui_build_page_live_summary(s_ui.content);

    const system_constants_data_t *constants = ui_get_constants();
    for (int i = 0; i < constants->profile_count; i++) {
        lv_obj_t *btn = lv_button_create(s_ui.content);
        lv_obj_set_size(btn, 740, 72);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20, 102 + (i * 86));
        ui_style_action_button(btn);
        if (s_ui.active_profile == (i + 1)) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(btn, ui_profile_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, constants->profiles[i].name);
        ui_style_button_label(lbl);
        lv_obj_center(lbl);
    }
}

/**
 * @brief Construct settings page.
 *
 * @details Offers quick numeric tuning of target temperature and preinfusion.
 */
static void ui_build_page_settings(void)
{
    ui_build_page_title(s_ui.content, "Settings");
    ui_build_page_live_summary(s_ui.content);

    lv_obj_t *temp_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(temp_lbl, "Target Temperature");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(temp_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_LEFT, 20, 92);

    s_ui.settings_target_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_target_slider, 540, 8);
    lv_obj_align(s_ui.settings_target_slider, LV_ALIGN_TOP_LEFT, 20, 132);
    ui_style_slider(s_ui.settings_target_slider);
    lv_slider_set_range(s_ui.settings_target_slider,
                        ui_get_constants()->temperature_min_c,
                        ui_get_constants()->temperature_max_c);
    lv_slider_set_value(s_ui.settings_target_slider, s_ui.target_temp_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_target_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)0);

    s_ui.settings_target_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_target_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_target_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_target_value, LV_ALIGN_TOP_RIGHT, -40, 118);

    lv_obj_t *pre_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(pre_lbl, "Preinfusion");
    lv_obj_set_style_text_font(pre_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pre_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(pre_lbl, LV_ALIGN_TOP_LEFT, 20, 170);

    s_ui.settings_preinf_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_preinf_slider, 540, 8);
    lv_obj_align(s_ui.settings_preinf_slider, LV_ALIGN_TOP_LEFT, 20, 210);
    ui_style_slider(s_ui.settings_preinf_slider);
    lv_slider_set_range(s_ui.settings_preinf_slider, 0, 12);
    lv_slider_set_value(s_ui.settings_preinf_slider, s_ui.preinf_s, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_preinf_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

    s_ui.settings_preinf_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_preinf_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_preinf_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_preinf_value, LV_ALIGN_TOP_RIGHT, -40, 196);

    if (s_ui.settings_target_value) {
        char txt[24];
        snprintf(txt, sizeof(txt), "%d C", s_ui.target_temp_c);
        lv_label_set_text(s_ui.settings_target_value, txt);
    }
    if (s_ui.settings_preinf_value) {
        char txt[24];
        snprintf(txt, sizeof(txt), "%d s", s_ui.preinf_s);
        lv_label_set_text(s_ui.settings_preinf_value, txt);
    }

    const lv_coord_t action_btn_width = 360;
    const lv_coord_t action_btn_height = 48;
    const lv_coord_t action_left_x = 20;
    const lv_coord_t action_right_x = 400;
    const lv_coord_t action_row1_y = 238;
    const lv_coord_t action_row2_y = 296;
    const lv_coord_t action_row3_y = 354;

    s_ui.settings_backlight_toggle = lv_button_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_backlight_toggle, action_btn_width, action_btn_height);
    lv_obj_align(s_ui.settings_backlight_toggle, LV_ALIGN_TOP_LEFT, action_left_x, action_row1_y);
    ui_style_action_button(s_ui.settings_backlight_toggle);
    lv_obj_add_event_cb(s_ui.settings_backlight_toggle,
                        ui_settings_backlight_toggle_event_cb,
                        LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *backlight_lbl = lv_label_create(s_ui.settings_backlight_toggle);
    lv_label_set_text(backlight_lbl, "Backlight");
    ui_style_button_label(backlight_lbl);
    lv_obj_center(backlight_lbl);

    lv_obj_t *set_clock_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(set_clock_btn, action_btn_width, action_btn_height);
    lv_obj_align(set_clock_btn, LV_ALIGN_TOP_LEFT, action_right_x, action_row1_y);
    ui_style_action_button(set_clock_btn);
    lv_obj_add_event_cb(set_clock_btn, ui_settings_set_clock_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *set_clock_lbl = lv_label_create(set_clock_btn);
    lv_label_set_text(set_clock_lbl, "Set Clock");
    ui_style_button_label(set_clock_lbl);
    lv_obj_center(set_clock_lbl);

    lv_obj_t *connection_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(connection_btn, action_btn_width, action_btn_height);
    lv_obj_align(connection_btn, LV_ALIGN_TOP_LEFT, action_left_x, action_row2_y);
    ui_style_action_button(connection_btn);
    lv_obj_add_event_cb(connection_btn, ui_settings_connection_info_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connection_lbl = lv_label_create(connection_btn);
    lv_label_set_text(connection_lbl, "Connection Info");
    ui_style_button_label(connection_lbl);
    lv_obj_center(connection_lbl);

    lv_obj_t *reboot_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(reboot_btn, action_btn_width, action_btn_height);
    lv_obj_align(reboot_btn, LV_ALIGN_TOP_LEFT, action_right_x, action_row2_y);
    ui_style_action_button(reboot_btn);
    lv_obj_add_event_cb(reboot_btn, ui_settings_reboot_client_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reboot_lbl = lv_label_create(reboot_btn);
    lv_label_set_text(reboot_lbl, "Reboot Client");
    ui_style_button_label(reboot_lbl);
    lv_obj_center(reboot_lbl);

    lv_obj_t *system_constants_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(system_constants_btn, action_btn_width, action_btn_height);
    lv_obj_align(system_constants_btn, LV_ALIGN_TOP_LEFT, action_left_x, action_row3_y);
    ui_style_action_button(system_constants_btn);
    lv_obj_add_event_cb(system_constants_btn, ui_settings_system_constants_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *system_constants_lbl = lv_label_create(system_constants_btn);
    lv_label_set_text(system_constants_lbl, "System Constants");
    ui_style_button_label(system_constants_lbl);
    lv_obj_center(system_constants_lbl);
}

/**
 * @brief Rebuild content area for currently active page.
 *
 * @details Clears dynamic content widgets then draws selected workflow page.
 */
static void ui_render_active_page(void)
{
    s_ui.page_status = NULL;
    s_ui.page_runtime = NULL;

    switch (s_ui.active_page) {
    case UI_PAGE_HOME:
        s_ui.content = s_ui.tab_pages[UI_PAGE_HOME];
        lv_obj_clean(s_ui.content);
        s_ui.home_active_profile = NULL;
        s_ui.home_target_label = NULL;
        ui_build_page_home();
        break;
    case UI_PAGE_BREW:
        s_ui.content = s_ui.tab_pages[UI_PAGE_BREW];
        lv_obj_clean(s_ui.content);
        s_ui.brew_toggle_btn = NULL;
        s_ui.steam_toggle_btn = NULL;
        ui_build_page_brew();
        break;
    case UI_PAGE_PROFILES:
        s_ui.content = s_ui.tab_pages[UI_PAGE_PROFILES];
        lv_obj_clean(s_ui.content);
        ui_build_page_profiles();
        break;
    case UI_PAGE_SETTINGS:
        s_ui.content = s_ui.tab_pages[UI_PAGE_SETTINGS];
        lv_obj_clean(s_ui.content);
        s_ui.settings_target_slider = NULL;
        s_ui.settings_target_value = NULL;
        s_ui.settings_preinf_slider = NULL;
        s_ui.settings_preinf_value = NULL;
        s_ui.settings_backlight_toggle = NULL;
        ui_build_page_settings();
        break;
    default:
        s_ui.active_page = UI_PAGE_HOME;
        s_ui.content = s_ui.tab_pages[UI_PAGE_HOME];
        lv_obj_clean(s_ui.content);
        s_ui.home_active_profile = NULL;
        s_ui.home_target_label = NULL;
        ui_build_page_home();
        break;
    }

    ui_update_tab_style();
    ui_update_header_status();
    ui_update_header_runtime();
}

/**
 * @brief Handle tab selection changes.
 *
 * @details Switches active page and triggers full content re-render.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_tabview_event_cb(lv_event_t *e)
{
    s_ui.active_page = (ui_page_t)lv_tabview_get_tab_active(lv_event_get_target(e));
    ui_render_active_page();
}

/**
 * @brief Periodic UI heartbeat updater.
 *
 * @details Advances the shot timer during brew mode and refreshes active-page
 * status text plus the persistent bottom clock bar.
 *
 * @param[in] timer LVGL timer handle.
 */
static void ui_heartbeat_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_ui.brewing) {
        s_ui.shot_s++;
    }
    ui_update_header_status();
    ui_update_header_runtime();
    ui_update_clock_bar();
    ui_update_connection_info_overlay_contents();
}

/**
 * @brief Build root layout and initialize workflow UI.
 *
 * @details Creates the top tab region for the main interface and the bottom
 * clock bar, then renders the default dashboard tab.
 */
static void ui_build_main_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);

    if (s_ui.root) {
        lv_obj_del(s_ui.root);
        s_ui.root = NULL;
    }

    s_ui.root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_set_size(s_ui.root, 800, 480);
    lv_obj_center(s_ui.root);

    s_ui.tabview = lv_tabview_create(s_ui.root);
    lv_obj_set_size(s_ui.tabview, 800, UI_TABVIEW_HEIGHT);
    lv_obj_align(s_ui.tabview, LV_ALIGN_TOP_MID, 0, 0);
    lv_tabview_set_tab_bar_position(s_ui.tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_ui.tabview, 56);
    lv_obj_set_style_bg_color(s_ui.tabview, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_ui.tabview, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ui.tabview, ui_tabview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_ui.tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(UI_COLOR_PANEL_ALT), LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(tab_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(tab_bar, 8, LV_PART_MAIN);

    s_ui.tab_pages[UI_PAGE_HOME] = lv_tabview_add_tab(s_ui.tabview, "Home");
    s_ui.tab_pages[UI_PAGE_BREW] = lv_tabview_add_tab(s_ui.tabview, "Brew");
    s_ui.tab_pages[UI_PAGE_PROFILES] = lv_tabview_add_tab(s_ui.tabview, "Profiles");
    s_ui.tab_pages[UI_PAGE_SETTINGS] = lv_tabview_add_tab(s_ui.tabview, "Settings");

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *tab = s_ui.tab_pages[i];
        lv_obj_set_style_bg_color(tab, lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_style_pad_all(tab, 12, 0);
        lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
    }
    /* Keep the tabview content scrollable so horizontal swipe gestures can
     * switch tabs with the built-in LVGL slide animation. */

    lv_obj_t *clock_bar = lv_obj_create(s_ui.root);
    lv_obj_set_size(clock_bar, 800, UI_CLOCK_BAR_HEIGHT);
    lv_obj_align(clock_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(clock_bar, 0, 0);
    lv_obj_set_style_bg_color(clock_bar, lv_color_hex(UI_COLOR_PANEL_ALT), 0);
    lv_obj_set_style_border_width(clock_bar, 0, 0);
    lv_obj_set_style_pad_all(clock_bar, 0, 0);

    s_ui.clock_label = lv_label_create(clock_bar);
    lv_obj_set_style_text_font(s_ui.clock_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.clock_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.clock_label, LV_ALIGN_CENTER, 0, 0);

    ui_update_tab_style();

    if (ui_get_constants()->profile_count > 0) {
        ui_apply_profile_defaults(1);
    }
    s_ui.active_page = UI_PAGE_HOME;
    ui_render_active_page();
    lv_tabview_set_active(s_ui.tabview, UI_PAGE_HOME, LV_ANIM_OFF);
    ui_update_clock_bar();

    if (!s_ui.heartbeat_timer) {
        s_ui.heartbeat_timer = lv_timer_create(ui_heartbeat_timer_cb, 1000, NULL);
    }

    ESP_LOGI(TAG, "UI screen created successfully");
}

/**
 * @brief Create the initialization splash screen before the main UI.
 *
 * @details Shows a centered `Initializing System...` message and a dynamic
 * status line while startup checks are running.
 */
void ui_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_clean(scr);

    s_ui.root = NULL;
    s_ui.init_title_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;
    s_ui.error_screen = NULL;
    s_ui.tabview = NULL;
    s_ui.clock_set_overlay = NULL;
    s_ui.connection_info_overlay = NULL;
    s_ui.system_constants_overlay = NULL;
    s_ui.connection_info_details_label = NULL;
    s_ui.connection_info_show_scan_results = false;
    s_ui.clock_set_day_roller = NULL;
    s_ui.clock_set_month_roller = NULL;
    s_ui.clock_set_year_roller = NULL;
    s_ui.clock_set_hour_roller = NULL;
    s_ui.clock_set_minute_roller = NULL;
    s_ui.content = NULL;
    s_ui.page_status = NULL;
    s_ui.page_runtime = NULL;
    s_ui.clock_label = NULL;
    s_ui.brew_toggle_btn = NULL;
    s_ui.steam_toggle_btn = NULL;
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    s_ui.init_failure_confirm_requested = false;

    lv_obj_t *splash = lv_obj_create(scr);
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, 800, 480);
    lv_obj_center(splash);
    lv_obj_set_style_bg_color(splash, lv_color_hex(UI_COLOR_BG), 0);

    s_ui.init_title_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_title_label, "Run in Offline Mode?");
    lv_obj_set_style_text_font(s_ui.init_title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_ui.init_title_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.init_title_label, LV_ALIGN_CENTER, 0, -78);

    s_ui.init_mode_prompt_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_mode_prompt_label,
                      "Select 'Yes' to simulate the server and skip real Wi-Fi.\n"
                      "Select 'No' to run the full online initialization.");
    lv_obj_set_style_text_font(s_ui.init_mode_prompt_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.init_mode_prompt_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_ui.init_mode_prompt_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.init_mode_prompt_label, LV_ALIGN_CENTER, 0, -20);

    s_ui.init_mode_yes_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_mode_yes_btn, 220, 58);
    lv_obj_align(s_ui.init_mode_yes_btn, LV_ALIGN_CENTER, -130, 62);
    ui_style_action_button(s_ui.init_mode_yes_btn);
    lv_obj_add_event_cb(s_ui.init_mode_yes_btn,
                        ui_init_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_INIT_MODE_OFFLINE);

    lv_obj_t *yes_lbl = lv_label_create(s_ui.init_mode_yes_btn);
    lv_label_set_text(yes_lbl, "Yes");
    ui_style_button_label(yes_lbl);
    lv_obj_center(yes_lbl);

    s_ui.init_mode_no_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_mode_no_btn, 220, 58);
    lv_obj_align(s_ui.init_mode_no_btn, LV_ALIGN_CENTER, 130, 62);
    ui_style_action_button(s_ui.init_mode_no_btn);
    lv_obj_add_event_cb(s_ui.init_mode_no_btn,
                        ui_init_mode_select_event_cb,
                        LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_INIT_MODE_ONLINE);

    lv_obj_t *no_lbl = lv_label_create(s_ui.init_mode_no_btn);
    lv_label_set_text(no_lbl, "No");
    ui_style_button_label(no_lbl);
    lv_obj_center(no_lbl);

    s_ui.init_status_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_status_label, "Preparing startup checks...");
    lv_obj_set_style_text_font(s_ui.init_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_status_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_align(s_ui.init_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ui.init_status_label, LV_ALIGN_CENTER, 0, 42);
    lv_obj_add_flag(s_ui.init_status_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.init_fail_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_fail_label, "FAIL");
    lv_obj_set_style_text_font(s_ui.init_fail_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_fail_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(s_ui.init_fail_label, LV_ALIGN_CENTER, 0, 84);
    lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.init_confirm_btn = lv_button_create(splash);
    lv_obj_set_size(s_ui.init_confirm_btn, 260, 58);
    ui_style_action_button(s_ui.init_confirm_btn);
    lv_obj_align(s_ui.init_confirm_btn, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_add_event_cb(s_ui.init_confirm_btn, ui_init_failure_confirm_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *confirm_lbl = lv_label_create(s_ui.init_confirm_btn);
    lv_label_set_text(confirm_lbl, "Confirm");
    ui_style_button_label(confirm_lbl);
    lv_obj_center(confirm_lbl);

    ESP_LOGI(TAG, "Initialization splash screen created");
}

/**
 * @brief Update the splash screen initialization status line.
 *
 * @details Replaces the dynamic status text shown underneath the initialization
 * title while startup checks are in progress.
 *
 * @param[in] status_text New text to show.
 */
void ui_screen_set_init_status(const char *status_text)
{
    if (!s_ui.init_status_label || status_text == NULL) {
        return;
    }

    lv_label_set_text(s_ui.init_status_label, status_text);
}

/**
 * @brief Mark the current initialization status as pass/fail.
 *
 * @details Highlights failed initialization text in red, shows a fail banner,
 * and restores the normal muted appearance when no failure is active.
 *
 * @param[in] failed `true` to show failure styling, `false` otherwise.
 */
void ui_screen_set_init_failed(bool failed)
{
    if (!s_ui.init_status_label) {
        return;
    }

    lv_obj_set_style_text_color(s_ui.init_status_label,
                                failed ? lv_palette_main(LV_PALETTE_RED)
                                       : lv_color_hex(UI_COLOR_TEXT_MUTED),
                                0);
    if (s_ui.init_fail_label) {
        if (failed) {
            lv_obj_clear_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Transition the startup UI from prompt mode into progress mode.
 *
 * @details Hides the offline-mode selection controls and shows the normal
 * `Initializing System...` splash layout for the staged startup sequence.
 *
 * @param[in] mode Selected initialization mode.
 */
void ui_screen_begin_initialization(ui_init_mode_t mode)
{
    if (s_ui.init_title_label) {
        lv_label_set_text(s_ui.init_title_label, "Initializing System...");
        lv_obj_align(s_ui.init_title_label, LV_ALIGN_CENTER, 0, -74);
    }

    if (s_ui.init_mode_prompt_label) {
        const char *mode_text = (mode == UI_INIT_MODE_OFFLINE)
                                    ? "Offline mode selected. Starting simulated initialization..."
                                    : "Online mode selected. Starting full initialization...";
        lv_label_set_text(s_ui.init_mode_prompt_label, mode_text);
        lv_obj_align(s_ui.init_mode_prompt_label, LV_ALIGN_CENTER, 0, -30);
    }

    if (s_ui.init_mode_yes_btn) {
        lv_obj_add_flag(s_ui.init_mode_yes_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_mode_no_btn) {
        lv_obj_add_flag(s_ui.init_mode_no_btn, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_status_label) {
        lv_obj_clear_flag(s_ui.init_status_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_fail_label) {
        lv_obj_add_flag(s_ui.init_fail_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui.init_confirm_btn) {
        lv_obj_add_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
    s_ui.init_failure_confirm_requested = false;
    ui_screen_set_init_failed(false);
}

/**
 * @brief Consume the operator's startup mode choice.
 *
 * @details Returns the stored prompt selection and clears it so startup begins
 * only once per prompt response.
 *
 * @return Selected initialization mode or `UI_INIT_MODE_NONE`.
 */
ui_init_mode_t ui_screen_take_init_mode_selection(void)
{
    ui_init_mode_t selected = s_ui.init_mode_selection;
    s_ui.init_mode_selection = UI_INIT_MODE_NONE;
    return selected;
}

/**
 * @brief Show the confirm action after initialization failure.
 *
 * @details Makes the bottom confirm button visible so the operator can
 * acknowledge a failed initialization result before moving to the error screen.
 */
void ui_screen_show_init_failure_confirm(void)
{
    if (s_ui.init_confirm_btn) {
        lv_obj_clear_flag(s_ui.init_confirm_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Consume the initialization-failure confirm request.
 *
 * @details Returns the current confirm flag and clears it so the main task can
 * react only once to each operator acknowledgement.
 *
 * @return `true` when the confirm button was pressed.
 */
bool ui_screen_take_init_failure_confirm(void)
{
    bool requested = s_ui.init_failure_confirm_requested;
    s_ui.init_failure_confirm_requested = false;
    return requested;
}

/**
 * @brief Replace the splash screen with the main workflow UI.
 *
 * @details Builds the normal tab-based UI after initialization checks and
 * controller startup handshake complete.
 */
void ui_screen_show_main(void)
{
    lv_obj_clean(lv_scr_act());
    s_ui.init_title_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;
    s_ui.error_screen = NULL;
    ui_build_main_screen();
}

/**
 * @brief Show the dedicated persistent error screen.
 *
 * @details Replaces the splash/main UI with a full-screen error view that
 * displays the provided summary and offers a single `Reset` action back to the
 * initialization prompt.
 *
 * @param[in] error_text Error summary to display.
 */
void ui_screen_show_error(const char *error_text)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_clean(scr);
    s_ui.init_title_label = NULL;
    s_ui.init_mode_prompt_label = NULL;
    s_ui.init_status_label = NULL;
    s_ui.init_fail_label = NULL;
    s_ui.init_mode_yes_btn = NULL;
    s_ui.init_mode_no_btn = NULL;
    s_ui.init_confirm_btn = NULL;

    s_ui.error_screen = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.error_screen);
    lv_obj_set_size(s_ui.error_screen, 800, 480);
    lv_obj_center(s_ui.error_screen);
    lv_obj_set_style_bg_color(s_ui.error_screen, lv_color_hex(UI_COLOR_BG), 0);

    lv_obj_t *title = lv_label_create(s_ui.error_screen);
    lv_label_set_text(title, "Error");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t *error_box = lv_obj_create(s_ui.error_screen);
    lv_obj_set_size(error_box, 720, 260);
    lv_obj_align(error_box, LV_ALIGN_TOP_MID, 0, 86);
    ui_style_card(error_box, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(error_box, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(error_box, 18, 0);

    lv_obj_t *error_label = lv_label_create(error_box);
    lv_obj_set_width(error_label, 680);
    lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(error_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(error_label, (error_text != NULL) ? error_text : "Unknown initialization error.");

    lv_obj_t *reset_btn = lv_button_create(s_ui.error_screen);
    lv_obj_set_size(reset_btn, 260, 58);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_MID, 0, -26);
    ui_style_action_button(reset_btn);
    lv_obj_add_event_cb(reset_btn, ui_error_reset_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset");
    ui_style_button_label(reset_lbl);
    lv_obj_center(reset_lbl);
}

/**
 * @brief Consume any pending client re-initialization request.
 *
 * @details Returns the current request flag and clears it so the application
 * loop can rerun initialization exactly once per button press.
 *
 * @return `true` when a re-initialization request was pending.
 */
bool ui_screen_take_reinit_request(void)
{
    bool requested = s_ui.reinit_requested;
    s_ui.reinit_requested = false;
    return requested;
}

/**
 * @brief Synchronize the Settings backlight control with runtime state.
 *
 * @details The Settings control is a momentary push button, so there is no
 * persistent checked state to synchronize. The hook is kept so runtime
 * backlight logic can call a single UI function without needing to know the
 * current control style.
 *
 * @param[in] enabled `true` when the backlight is on, `false` when off.
 */
void ui_screen_set_backlight_toggle_state(bool enabled)
{
    (void)enabled;
}
