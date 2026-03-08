/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
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
    lv_obj_t *init_status_label;
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
    bool brewing;
    bool steaming;
    int active_profile;
    int target_temp_c;
    int preinf_s;
    int shot_s;
    bool reinit_requested;
} ui_state_t;

static ui_state_t s_ui = {
    .root = NULL,
    .init_status_label = NULL,
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
    .brewing = false,
    .steaming = false,
    .active_profile = 1,
    .target_temp_c = 93,
    .preinf_s = 4,
    .shot_s = 0,
    .reinit_requested = false,
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

static char s_system_constants_pretty_text[UI_SYSTEM_CONSTANTS_TEXT_MAX];

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
 * @brief Format the embedded XML into an indented text view.
 *
 * @details Converts the raw embedded `SystemConstants.xml` into a tree-style
 * text block so nested tags are easier to read on the display. Small inline
 * value tags remain on one line, while container tags are broken into indented
 * lines.
 *
 * @return Pointer to a static formatted XML buffer.
 */
static const char *ui_get_system_constants_pretty_text(void)
{
    const char *xml = system_constants_get_xml_text();
    size_t xml_len = system_constants_get_xml_length();
    size_t used = 0;
    int indent_level = 0;
    size_t cursor = 0;

    if (xml == NULL || xml_len == 0U) {
        snprintf(s_system_constants_pretty_text,
                 sizeof(s_system_constants_pretty_text),
                 "SystemConstants.xml is unavailable.");
        return s_system_constants_pretty_text;
    }

    s_system_constants_pretty_text[0] = '\0';

    while (cursor < xml_len && used + 2 < sizeof(s_system_constants_pretty_text)) {
        if (xml[cursor] != '<') {
            cursor++;
            continue;
        }

        size_t tag_end = cursor;
        while (tag_end < xml_len && xml[tag_end] != '>') {
            tag_end++;
        }
        if (tag_end >= xml_len) {
            break;
        }

        bool is_closing_tag = (cursor + 1U < xml_len && xml[cursor + 1U] == '/');
        bool is_self_closing = (tag_end > cursor && xml[tag_end - 1U] == '/');
        size_t next_tag = tag_end + 1U;
        while (next_tag < xml_len && xml[next_tag] != '<') {
            next_tag++;
        }

        size_t text_start = tag_end + 1U;
        while (text_start < next_tag && (xml[text_start] == ' ' || xml[text_start] == '\t' ||
                                         xml[text_start] == '\r' || xml[text_start] == '\n')) {
            text_start++;
        }

        size_t text_end = next_tag;
        while (text_end > text_start && (xml[text_end - 1U] == ' ' || xml[text_end - 1U] == '\t' ||
                                         xml[text_end - 1U] == '\r' || xml[text_end - 1U] == '\n')) {
            text_end--;
        }

        bool has_inline_text = (text_end > text_start);

        if (is_closing_tag && indent_level > 0) {
            indent_level--;
        }

        if (used > 0U && used + 1U < sizeof(s_system_constants_pretty_text)) {
            s_system_constants_pretty_text[used++] = '\n';
        }

        for (int indent = 0; indent < indent_level && used + 4U < sizeof(s_system_constants_pretty_text); indent++) {
            s_system_constants_pretty_text[used++] = ' ';
            s_system_constants_pretty_text[used++] = ' ';
            s_system_constants_pretty_text[used++] = ' ';
            s_system_constants_pretty_text[used++] = ' ';
        }

        size_t tag_len = tag_end - cursor + 1U;
        if (used + tag_len >= sizeof(s_system_constants_pretty_text)) {
            tag_len = sizeof(s_system_constants_pretty_text) - used - 1U;
        }
        memcpy(&s_system_constants_pretty_text[used], &xml[cursor], tag_len);
        used += tag_len;

        if (has_inline_text && used + 1U < sizeof(s_system_constants_pretty_text)) {
            s_system_constants_pretty_text[used++] = ' ';
            size_t text_len = text_end - text_start;
            if (used + text_len >= sizeof(s_system_constants_pretty_text)) {
                text_len = sizeof(s_system_constants_pretty_text) - used - 1U;
            }
            memcpy(&s_system_constants_pretty_text[used], &xml[text_start], text_len);
            used += text_len;
            cursor = next_tag;
        } else {
            cursor = tag_end + 1U;
        }

        if (!is_closing_tag && !is_self_closing && !has_inline_text) {
            indent_level++;
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

    peripherals_connection_info_t info = {0};
    esp_err_t ret = peripherals_manager_get_connection_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Connection info read failed: %s", esp_err_to_name(ret));
        snprintf(info.ip_address, sizeof(info.ip_address), "Unavailable");
        snprintf(info.port_text, sizeof(info.port_text), "Unavailable");
        info.wifi_ready = false;
        info.rtc_ready = false;
        info.tf_ready = false;
        info.wifi_ap_count = 0;
        info.controller_status = PERIPHERALS_CONTROLLER_STATUS_UNKNOWN;
    }

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

    lv_obj_t *info_body = lv_obj_create(panel);
    lv_obj_set_size(info_body, 720, 282);
    lv_obj_align(info_body, LV_ALIGN_TOP_MID, 0, 70);
    ui_style_card(info_body, UI_COLOR_CARD);
    lv_obj_set_scrollbar_mode(info_body, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_all(info_body, 18, 0);
    lv_obj_set_style_pad_row(info_body, 14, 0);
    lv_obj_set_layout(info_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(info_body, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *ip_label = lv_label_create(info_body);
    lv_label_set_text_fmt(ip_label, "IP: %s", info.ip_address);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t *port_label = lv_label_create(info_body);
    lv_label_set_text_fmt(port_label, "Port: %s", info.port_text);
    lv_obj_set_style_text_font(port_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(port_label, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t *telemetry_title = lv_label_create(info_body);
    lv_label_set_text(telemetry_title, "Telemetry");
    lv_obj_set_style_text_font(telemetry_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(telemetry_title, lv_color_hex(UI_COLOR_TEXT), 0);

    lv_obj_t *telemetry_card = lv_obj_create(info_body);
    lv_obj_set_width(telemetry_card, lv_pct(100));
    ui_style_card(telemetry_card, UI_COLOR_CARD_ALT);
    lv_obj_clear_flag(telemetry_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(telemetry_card, 18, 0);

    lv_obj_t *telemetry_label = lv_label_create(telemetry_card);
    lv_label_set_text_fmt(telemetry_label,
                          "Wi-Fi Ready: %s\n"
                          "Visible APs: %u\n"
                          "RTC Ready: %s\n"
                          "TF Card Ready: %s\n"
                          "Controller Status: %s",
                          info.wifi_ready ? "Yes" : "No",
                          info.wifi_ap_count,
                          info.rtc_ready ? "Yes" : "No",
                          info.tf_ready ? "Yes" : "No",
                          peripherals_manager_controller_status_to_string(info.controller_status));
    lv_obj_set_style_text_font(telemetry_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(telemetry_label, lv_color_hex(UI_COLOR_TEXT), 0);

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
 * @brief Handle settings backlight toggle state changes.
 *
 * @details Writes the board backlight enable bit through the hardware layer.
 * A later touch anywhere on the panel will wake the backlight again.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_settings_backlight_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(obj, LV_STATE_CHECKED);
    esp_err_t ret = hardware_set_backlight_enabled(enabled);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "settings backlight %s", enabled ? "ON" : "OFF");
    } else {
        ESP_LOGW(TAG, "settings backlight change failed: %s", esp_err_to_name(ret));
        if (enabled) {
            lv_obj_add_state(obj, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(obj, LV_STATE_CHECKED);
        }
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
    ui_screen_create();
    ESP_LOGI(TAG, "client UI returned to initialization state");
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

    s_ui.settings_backlight_toggle = ui_create_toggle_button(s_ui.content,
                                                             "Backlight",
                                                             hardware_get_backlight_enabled(),
                                                             action_left_x,
                                                             action_row1_y,
                                                             ui_settings_backlight_toggle_event_cb);
    lv_obj_set_size(s_ui.settings_backlight_toggle, action_btn_width, action_btn_height);

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
    s_ui.init_status_label = NULL;
    s_ui.tabview = NULL;
    s_ui.clock_set_overlay = NULL;
    s_ui.connection_info_overlay = NULL;
    s_ui.system_constants_overlay = NULL;
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

    lv_obj_t *splash = lv_obj_create(scr);
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, 800, 480);
    lv_obj_center(splash);
    lv_obj_set_style_bg_color(splash, lv_color_hex(UI_COLOR_BG), 0);

    lv_obj_t *label = lv_label_create(splash);
    lv_label_set_text(label, "Initializing System...");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

    s_ui.init_status_label = lv_label_create(splash);
    lv_label_set_text(s_ui.init_status_label, "Preparing startup checks...");
    lv_obj_set_style_text_font(s_ui.init_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.init_status_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(s_ui.init_status_label, LV_ALIGN_CENTER, 0, 24);

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
 * @brief Replace the splash screen with the main workflow UI.
 *
 * @details Builds the normal tab-based UI after initialization checks and
 * controller startup handshake complete.
 */
void ui_screen_show_main(void)
{
    s_ui.init_status_label = NULL;
    ui_build_main_screen();
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
