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
    lv_obj_t *header_status;
    lv_obj_t *header_runtime;
    lv_obj_t *content;
    lv_obj_t *nav_btns[UI_PAGE_COUNT];
    lv_obj_t *brew_toggle;
    lv_obj_t *steam_toggle;
    lv_obj_t *home_active_profile;
    lv_obj_t *home_target_label;
    lv_obj_t *settings_target_slider;
    lv_obj_t *settings_target_value;
    lv_obj_t *settings_preinf_slider;
    lv_obj_t *settings_preinf_value;
    lv_timer_t *heartbeat_timer;
    ui_page_t active_page;
    bool brewing;
    bool steaming;
    int active_profile;
    int target_temp_c;
    int preinf_s;
    int shot_s;
} ui_state_t;

static ui_state_t s_ui = {
    .root = NULL,
    .header_status = NULL,
    .header_runtime = NULL,
    .content = NULL,
    .nav_btns = {NULL},
    .brew_toggle = NULL,
    .steam_toggle = NULL,
    .home_active_profile = NULL,
    .home_target_label = NULL,
    .settings_target_slider = NULL,
    .settings_target_value = NULL,
    .settings_preinf_slider = NULL,
    .settings_preinf_value = NULL,
    .heartbeat_timer = NULL,
    .active_page = UI_PAGE_HOME,
    .brewing = false,
    .steaming = false,
    .active_profile = 1,
    .target_temp_c = 93,
    .preinf_s = 4,
    .shot_s = 0,
};

/**
 * @brief Update high-level status line in the header.
 *
 * @details Builds a single concise status text showing active mode and profile
 * to mimic dashboard-style espresso workflows.
 */
static void ui_update_header_status(void)
{
    if (!s_ui.header_status) {
        return;
    }

    char line[96];
    const char *mode = s_ui.brewing ? "BREWING" : (s_ui.steaming ? "STEAM" : "IDLE");
    snprintf(line, sizeof(line), "%s  |  Profile %d  |  Target %d C",
             mode, s_ui.active_profile, s_ui.target_temp_c);
    lv_label_set_text(s_ui.header_status, line);
}

/**
 * @brief Update live runtime text.
 *
 * @details Renders shot timer and a simple synthetic pressure/flow readout to
 * keep the home screen feeling active.
 */
static void ui_update_header_runtime(void)
{
    if (!s_ui.header_runtime) {
        return;
    }

    int pressure_tenths = s_ui.brewing ? (85 + (s_ui.shot_s % 20)) : 2;
    char line[96];
    snprintf(line, sizeof(line), "Shot %02ds  |  Pressure %d.%d bar",
             s_ui.shot_s, pressure_tenths / 10, pressure_tenths % 10);
    lv_label_set_text(s_ui.header_runtime, line);
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
 * @brief Update bottom navigation visual selection state.
 *
 * @details Clears checked state from all nav buttons and marks the active page
 * button as selected.
 */
static void ui_update_nav_style(void)
{
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        if (!s_ui.nav_btns[i]) {
            continue;
        }
        lv_obj_clear_state(s_ui.nav_btns[i], LV_STATE_CHECKED);
        if (s_ui.active_page == (ui_page_t)i) {
            lv_obj_add_state(s_ui.nav_btns[i], LV_STATE_CHECKED);
        }
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
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x0D1B2A), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, 4);
    return lbl;
}

/**
 * @brief Handle page navigation button click.
 *
 * @details Reads target page enum from event user data and triggers content
 * rebuild for the selected section.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_nav_btn_event_cb(lv_event_t *e);

/**
 * @brief Handle brew switch state transitions.
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
        if (s_ui.steam_toggle) {
            lv_obj_clear_state(s_ui.steam_toggle, LV_STATE_CHECKED);
        }
        ESP_LOGI(TAG, "brew ON");
    } else {
        ESP_LOGI(TAG, "brew OFF");
    }
    ui_update_header_status();
}

/**
 * @brief Handle steam switch state transitions.
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
        if (s_ui.brew_toggle) {
            lv_obj_clear_state(s_ui.brew_toggle, LV_STATE_CHECKED);
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

    lv_obj_t *card = lv_obj_create(s_ui.content);
    lv_obj_set_size(card, 760, 260);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, 8, 56);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xEAF3FF), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xB5D0F1), 0);

    s_ui.home_active_profile = lv_label_create(card);
    lv_obj_align(s_ui.home_active_profile, LV_ALIGN_TOP_LEFT, 20, 24);
    lv_obj_set_style_text_font(s_ui.home_active_profile, &lv_font_montserrat_20, 0);

    s_ui.home_target_label = lv_label_create(card);
    lv_obj_align(s_ui.home_target_label, LV_ALIGN_TOP_LEFT, 20, 68);
    lv_obj_set_style_text_font(s_ui.home_target_label, &lv_font_montserrat_20, 0);

    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Use Brew page to start shot workflow\nUse Profiles page to switch recipe");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
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

    lv_obj_t *row1 = lv_label_create(s_ui.content);
    lv_label_set_text(row1, "Pump / Brew");
    lv_obj_set_style_text_font(row1, &lv_font_montserrat_20, 0);
    lv_obj_align(row1, LV_ALIGN_TOP_LEFT, 20, 90);

    s_ui.brew_toggle = lv_switch_create(s_ui.content);
    lv_obj_align(s_ui.brew_toggle, LV_ALIGN_TOP_RIGHT, -60, 80);
    if (s_ui.brewing) {
        lv_obj_add_state(s_ui.brew_toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_ui.brew_toggle, ui_brew_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *row2 = lv_label_create(s_ui.content);
    lv_label_set_text(row2, "Steam Mode");
    lv_obj_set_style_text_font(row2, &lv_font_montserrat_20, 0);
    lv_obj_align(row2, LV_ALIGN_TOP_LEFT, 20, 170);

    s_ui.steam_toggle = lv_switch_create(s_ui.content);
    lv_obj_align(s_ui.steam_toggle, LV_ALIGN_TOP_RIGHT, -60, 160);
    if (s_ui.steaming) {
        lv_obj_add_state(s_ui.steam_toggle, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_ui.steam_toggle, ui_steam_toggle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *note = lv_label_create(s_ui.content);
    lv_label_set_text(note, "Workflow: preinfusion -> extraction -> finish");
    lv_obj_set_style_text_font(note, &lv_font_montserrat_16, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 20, -26);
}

/**
 * @brief Construct profile selection page.
 *
 * @details Displays quick profile presets with one-click activation.
 */
static void ui_build_page_profiles(void)
{
    ui_build_page_title(s_ui.content, "Profiles");

    const char *names[3] = {"Classic 9 Bar", "Turbo Shot", "Light Roast"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(s_ui.content);
        lv_obj_set_size(btn, 740, 72);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 20, 90 + (i * 86));
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn,
                                  (s_ui.active_profile == (i + 1)) ? lv_color_hex(0x1D4ED8) : lv_color_hex(0xDCEAFF),
                                  0);
        lv_obj_add_event_cb(btn, ui_profile_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, names[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl,
                                    (s_ui.active_profile == (i + 1)) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x0F172A),
                                    0);
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

    lv_obj_t *temp_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(temp_lbl, "Target Temperature");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_TOP_LEFT, 20, 90);

    s_ui.settings_target_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_target_slider, 540, 8);
    lv_obj_align(s_ui.settings_target_slider, LV_ALIGN_TOP_LEFT, 20, 132);
    lv_slider_set_range(s_ui.settings_target_slider, 86, 98);
    lv_slider_set_value(s_ui.settings_target_slider, s_ui.target_temp_c, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_target_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)0);

    s_ui.settings_target_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_target_value, &lv_font_montserrat_20, 0);
    lv_obj_align(s_ui.settings_target_value, LV_ALIGN_TOP_RIGHT, -40, 118);

    lv_obj_t *pre_lbl = lv_label_create(s_ui.content);
    lv_label_set_text(pre_lbl, "Preinfusion");
    lv_obj_set_style_text_font(pre_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(pre_lbl, LV_ALIGN_TOP_LEFT, 20, 200);

    s_ui.settings_preinf_slider = lv_slider_create(s_ui.content);
    lv_obj_set_size(s_ui.settings_preinf_slider, 540, 8);
    lv_obj_align(s_ui.settings_preinf_slider, LV_ALIGN_TOP_LEFT, 20, 242);
    lv_slider_set_range(s_ui.settings_preinf_slider, 0, 12);
    lv_slider_set_value(s_ui.settings_preinf_slider, s_ui.preinf_s, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_ui.settings_preinf_slider, ui_settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

    s_ui.settings_preinf_value = lv_label_create(s_ui.content);
    lv_obj_set_style_text_font(s_ui.settings_preinf_value, &lv_font_montserrat_20, 0);
    lv_obj_align(s_ui.settings_preinf_value, LV_ALIGN_TOP_RIGHT, -40, 228);

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
}

/**
 * @brief Rebuild content area for currently active page.
 *
 * @details Clears dynamic content widgets then draws selected workflow page.
 */
static void ui_render_active_page(void)
{
    s_ui.brew_toggle = NULL;
    s_ui.steam_toggle = NULL;
    s_ui.home_active_profile = NULL;
    s_ui.home_target_label = NULL;
    s_ui.settings_target_slider = NULL;
    s_ui.settings_target_value = NULL;
    s_ui.settings_preinf_slider = NULL;
    s_ui.settings_preinf_value = NULL;

    lv_obj_clean(s_ui.content);

    switch (s_ui.active_page) {
    case UI_PAGE_HOME:
        ui_build_page_home();
        break;
    case UI_PAGE_BREW:
        ui_build_page_brew();
        break;
    case UI_PAGE_PROFILES:
        ui_build_page_profiles();
        break;
    case UI_PAGE_SETTINGS:
        ui_build_page_settings();
        break;
    default:
        ui_build_page_home();
        break;
    }

    ui_update_nav_style();
    ui_update_header_status();
    ui_update_header_runtime();
}

/**
 * @brief Handle page navigation button click.
 *
 * @details Switches active page and triggers full content re-render.
 *
 * @param[in] e LVGL event payload.
 */
static void ui_nav_btn_event_cb(lv_event_t *e)
{
    intptr_t page = (intptr_t)lv_event_get_user_data(e);
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return;
    }
    s_ui.active_page = (ui_page_t)page;
    ui_render_active_page();
}

/**
 * @brief Periodic UI heartbeat updater.
 *
 * @details Advances the shot timer during brew mode and refreshes header stats.
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
}

/**
 * @brief Build root layout and initialize workflow UI.
 *
 * @details Creates header, content, and bottom navigation sections and renders
 * the default dashboard page.
 */
void ui_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF7FAFC), 0);

    s_ui.root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_ui.root);
    lv_obj_set_size(s_ui.root, 800, 480);
    lv_obj_center(s_ui.root);

    lv_obj_t *header = lv_obj_create(s_ui.root);
    lv_obj_set_size(header, 800, 86);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0B2545), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Gaggia Workflow");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 16, 8);

    s_ui.header_status = lv_label_create(header);
    lv_obj_set_style_text_font(s_ui.header_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.header_status, lv_color_hex(0xCFE8FF), 0);
    lv_obj_align(s_ui.header_status, LV_ALIGN_TOP_LEFT, 16, 40);

    s_ui.header_runtime = lv_label_create(header);
    lv_obj_set_style_text_font(s_ui.header_runtime, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.header_runtime, lv_color_hex(0xEAF3FF), 0);
    lv_obj_align(s_ui.header_runtime, LV_ALIGN_TOP_RIGHT, -16, 40);

    s_ui.content = lv_obj_create(s_ui.root);
    lv_obj_set_size(s_ui.content, 800, 326);
    lv_obj_align(s_ui.content, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(s_ui.content, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(s_ui.content, 0, 0);
    lv_obj_set_style_border_width(s_ui.content, 0, 0);
    lv_obj_set_style_pad_all(s_ui.content, 10, 0);
    lv_obj_set_scrollbar_mode(s_ui.content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *nav = lv_obj_create(s_ui.root);
    lv_obj_set_size(nav, 800, 68);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_border_width(nav, 0, 0);

    static const char *nav_labels[UI_PAGE_COUNT] = {"Home", "Brew", "Profiles", "Settings"};
    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_t *btn = lv_button_create(nav);
        lv_obj_set_size(btn, 180, 46);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, 10 + (i * 196), 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(btn, ui_nav_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_ui.nav_btns[i] = btn;

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    }

    s_ui.active_page = UI_PAGE_HOME;
    ui_render_active_page();

    if (!s_ui.heartbeat_timer) {
        s_ui.heartbeat_timer = lv_timer_create(ui_heartbeat_timer_cb, 1000, NULL);
    }

    ESP_LOGI(TAG, "UI screen created successfully");
}
