/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "hardware_init.h"
#include "peripherals_manager.h"

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
#define UI_TABVIEW_HEIGHT      (432)
#define UI_CLOCK_BAR_HEIGHT    (48)

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
        char txt[32];
        snprintf(txt, sizeof(txt), "%02d:%02d:%02d",
                 rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
        lv_label_set_text(s_ui.clock_label, txt);
    } else {
        lv_label_set_text(s_ui.clock_label, "--:--:--");
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
        char txt[48];
        snprintf(txt, sizeof(txt), "Active Profile: %d", s_ui.active_profile);
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
    if (profile < 1 || profile > 3) {
        return;
    }
    s_ui.active_profile = (int)profile;
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

    const char *names[3] = {"Classic 9 Bar", "Turbo Shot", "Light Roast"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(s_ui.content);
        lv_obj_set_size(btn, 740, 72);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20, 102 + (i * 86));
        ui_style_action_button(btn);
        if (s_ui.active_profile == (i + 1)) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(btn, ui_profile_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, names[i]);
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
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_LEFT, 20, 102);

    s_ui.settings_target_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_target_slider, 540, 8);
    lv_obj_align(s_ui.settings_target_slider, LV_ALIGN_TOP_LEFT, 20, 144);
    ui_style_slider(s_ui.settings_target_slider);
    lv_slider_set_range(s_ui.settings_target_slider, 86, 98);
    lv_slider_set_value(s_ui.settings_target_slider, s_ui.target_temp_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_target_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)0);

    s_ui.settings_target_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_target_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_target_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_target_value, LV_ALIGN_TOP_RIGHT, -40, 130);

    lv_obj_t *pre_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(pre_lbl, "Preinfusion");
    lv_obj_set_style_text_font(pre_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pre_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(pre_lbl, LV_ALIGN_TOP_LEFT, 20, 212);

    s_ui.settings_preinf_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_preinf_slider, 540, 8);
    lv_obj_align(s_ui.settings_preinf_slider, LV_ALIGN_TOP_LEFT, 20, 254);
    ui_style_slider(s_ui.settings_preinf_slider);
    lv_slider_set_range(s_ui.settings_preinf_slider, 0, 12);
    lv_slider_set_value(s_ui.settings_preinf_slider, s_ui.preinf_s, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_preinf_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

    s_ui.settings_preinf_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_preinf_value, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.settings_preinf_value, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.settings_preinf_value, LV_ALIGN_TOP_RIGHT, -40, 240);

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

    lv_obj_t *bl_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(bl_lbl, "Display Backlight");
    lv_obj_set_style_text_font(bl_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bl_lbl, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(bl_lbl, LV_ALIGN_TOP_LEFT, 20, 314);

    s_ui.settings_backlight_toggle = ui_create_toggle_button(s_ui.content,
                                                             "Backlight",
                                                             hardware_get_backlight_enabled(),
                                                             500,
                                                             302,
                                                             ui_settings_backlight_toggle_event_cb);

    lv_obj_t *reboot_btn = lv_button_create(s_ui.content);
    lv_obj_set_size(reboot_btn, 320, 58);
    lv_obj_align(reboot_btn, LV_ALIGN_TOP_LEFT, 20, 372);
    ui_style_action_button(reboot_btn);
    lv_obj_add_event_cb(reboot_btn, ui_settings_reboot_client_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reboot_lbl = lv_label_create(reboot_btn);
    lv_label_set_text(reboot_lbl, "Reboot Client");
    ui_style_button_label(reboot_lbl);
    lv_obj_center(reboot_lbl);
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
    lv_obj_set_style_text_font(s_ui.clock_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_ui.clock_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(s_ui.clock_label, LV_ALIGN_CENTER, 0, 0);

    ui_update_tab_style();

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
