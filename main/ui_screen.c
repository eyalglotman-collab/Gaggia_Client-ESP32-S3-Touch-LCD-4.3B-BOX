/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "lvgl_port.h"

static const char *TAG = "ui_screen";

/**
 * @brief Toggle button event callback
 */
static void toggle_button_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            ESP_LOGI(TAG, "brew ON");
        } else {
            ESP_LOGI(TAG, "brew OFF");
        }
    }
}

/**
 * @brief State label update callback
 */
static void state_label_update_cb(lv_event_t *e)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    
    if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        lv_label_set_text(label, "ON");
    } else {
        lv_label_set_text(label, "OFF");
    }
}

/**
 * @brief Create UI screen with toggle button
 */
void ui_screen_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);

    // Create a label for the button title
    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "Brew Control");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x000000), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);

    // Create a toggle button (switch)
    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_align(sw, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(sw, 100, 60);

    // Add a label for the toggle state
    lv_obj_t *state_label = lv_label_create(scr);
    lv_label_set_text(state_label, "OFF");
    lv_obj_set_style_text_font(state_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(state_label, lv_color_hex(0x000000), 0);
    lv_obj_align(state_label, LV_ALIGN_CENTER, 0, 80);

    // Add event callbacks
    lv_obj_add_event_cb(sw, toggle_button_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sw, state_label_update_cb, LV_EVENT_VALUE_CHANGED, state_label);

    ESP_LOGI(TAG, "UI screen created successfully");
}

