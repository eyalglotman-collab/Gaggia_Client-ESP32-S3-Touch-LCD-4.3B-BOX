/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ProtocolLCD_Controller.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../system_constants.h"

#define LCD_CONTROLLER_PROFILE_ITEMS_MAX_TEXT (320U)
#define LCD_CONTROLLER_DATASET_SETTINGS_MAX_TEXT (256U)
#define LCD_CONTROLLER_DATASET_PROFILES_MAX_TEXT (2048U)
#define LCD_CONTROLLER_DATASET_PROFILE_TOKEN_MAX_TEXT (640U)

static const char *TAG = "LCDProtocol";

typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    bool hooks_requested;
    uint32_t last_event_count;
    lcd_controller_protocol_link_state_t link_state;
    lcd_controller_profile_catalog_t profile_catalog;
    lcd_controller_dataset_t profile_dataset;
    lcd_controller_brew_home_state_t brew_state;
    lcd_controller_send_command_fn_t send_command_cb;
    void *send_command_user_ctx;
    lcd_controller_profile_catalog_hook_t profile_hook_cb;
    void *profile_hook_user_ctx;
    lcd_controller_brew_state_hook_t brew_hook_cb;
    void *brew_hook_user_ctx;
} lcd_controller_protocol_context_t;

static lcd_controller_protocol_context_t s_protocol = {
    .mutex = NULL,
    .initialized = false,
    .hooks_requested = false,
    .last_event_count = 0U,
    .link_state = LCD_CONTROLLER_PROTOCOL_STATE_UNINITIALIZED,
    .profile_catalog = {0},
    .profile_dataset = {0},
    .brew_state = {0},
    .send_command_cb = NULL,
    .send_command_user_ctx = NULL,
    .profile_hook_cb = NULL,
    .profile_hook_user_ctx = NULL,
    .brew_hook_cb = NULL,
    .brew_hook_user_ctx = NULL,
};

/**
 * @brief Compare a prefix in a case-insensitive way.
 */
static bool lcd_controller_starts_with_ignore_case(const char *text, const char *prefix)
{
    size_t index = 0U;

    if (text == NULL || prefix == NULL) {
        return false;
    }

    while (prefix[index] != '\0') {
        if (text[index] == '\0') {
            return false;
        }

        if (tolower((unsigned char)text[index]) != tolower((unsigned char)prefix[index])) {
            return false;
        }
        index++;
    }

    return true;
}

/**
 * @brief Extract one semicolon-delimited `key=value` token value.
 */
static bool lcd_controller_extract_payload_value(const char *payload_text,
                                                 const char *key_text,
                                                 char *out_value,
                                                 size_t out_value_size)
{
    const size_t key_len = (key_text != NULL) ? strlen(key_text) : 0U;
    const char *cursor = payload_text;

    if (payload_text == NULL || key_text == NULL || out_value == NULL || out_value_size == 0U || key_len == 0U) {
        return false;
    }

    out_value[0] = '\0';

    while (cursor != NULL && *cursor != '\0') {
        const bool token_start = (cursor == payload_text) || (*(cursor - 1) == ';');

        if (token_start &&
            strncmp(cursor, key_text, key_len) == 0 &&
            cursor[key_len] == '=') {
            const char *value_start = cursor + key_len + 1U;
            const char *value_end = strchr(value_start, ';');
            size_t value_len = 0U;

            if (value_end == NULL) {
                value_end = value_start + strlen(value_start);
            }

            value_len = (size_t)(value_end - value_start);
            if (value_len >= out_value_size) {
                value_len = out_value_size - 1U;
            }

            if (value_len > 0U) {
                memcpy(out_value, value_start, value_len);
            }
            out_value[value_len] = '\0';
            return true;
        }

        cursor = strchr(cursor, ';');
        if (cursor != NULL) {
            cursor++;
        }
    }

    return false;
}

/**
 * @brief Parse one `id:name` profile token and write it into catalog entry.
 */
static bool lcd_controller_parse_profile_item(const char *item_text,
                                              lcd_controller_profile_summary_t *out_entry)
{
    char local_copy[96] = {0};
    char *colon = NULL;
    char *end_ptr = NULL;
    unsigned long profile_id = 0UL;

    if (item_text == NULL || out_entry == NULL || item_text[0] == '\0') {
        return false;
    }

    snprintf(local_copy, sizeof(local_copy), "%s", item_text);
    colon = strchr(local_copy, ':');
    if (colon == NULL) {
        return false;
    }

    *colon = '\0';
    profile_id = strtoul(local_copy, &end_ptr, 10);
    if (end_ptr == local_copy || *end_ptr != '\0' || profile_id > UINT8_MAX) {
        return false;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    out_entry->profile_id = (uint8_t)profile_id;
    snprintf(out_entry->profile_name, sizeof(out_entry->profile_name), "%s", colon + 1);
    return true;
}

/**
 * @brief Parse `LCDProtoProfileCatalog` payload into a catalog structure.
 */
static bool lcd_controller_parse_profile_catalog_payload(const char *payload_text,
                                                         lcd_controller_profile_catalog_t *out_catalog)
{
    char count_text[16] = {0};
    char items_text[LCD_CONTROLLER_PROFILE_ITEMS_MAX_TEXT] = {0};
    unsigned long expected_count = 0UL;
    char *end_ptr = NULL;
    char *token = NULL;
    char *save_ptr = NULL;
    uint8_t parsed_count = 0U;

    if (payload_text == NULL || out_catalog == NULL) {
        return false;
    }

    memset(out_catalog, 0, sizeof(*out_catalog));

    if (!lcd_controller_extract_payload_value(payload_text, "count", count_text, sizeof(count_text))) {
        return false;
    }

    expected_count = strtoul(count_text, &end_ptr, 10);
    if (end_ptr == count_text || *end_ptr != '\0') {
        return false;
    }

    if (!lcd_controller_extract_payload_value(payload_text, "items", items_text, sizeof(items_text))) {
        return (expected_count == 0UL);
    }

    token = strtok_r(items_text, "|", &save_ptr);
    while (token != NULL && parsed_count < LCD_CONTROLLER_MAX_PROFILES) {
        if (lcd_controller_parse_profile_item(token, &out_catalog->entries[parsed_count])) {
            parsed_count++;
        }
        token = strtok_r(NULL, "|", &save_ptr);
    }

    out_catalog->count = parsed_count;
    return (parsed_count > 0U) || (expected_count == 0UL);
}

static bool lcd_controller_parse_bool_token(const char *token_text, bool *out_value)
{
    unsigned long numeric_value = 0UL;
    char *end_ptr = NULL;

    if (token_text == NULL || out_value == NULL || token_text[0] == '\0') {
        return false;
    }

    if (strcasecmp(token_text, "true") == 0 || strcasecmp(token_text, "on") == 0) {
        *out_value = true;
        return true;
    }
    if (strcasecmp(token_text, "false") == 0 || strcasecmp(token_text, "off") == 0) {
        *out_value = false;
        return true;
    }

    numeric_value = strtoul(token_text, &end_ptr, 10);
    if (end_ptr == token_text || *end_ptr != '\0') {
        return false;
    }
    *out_value = (numeric_value != 0UL);
    return true;
}

static bool lcd_controller_parse_u16_token(const char *token_text, uint16_t *out_value)
{
    unsigned long numeric_value = 0UL;
    char *end_ptr = NULL;

    if (token_text == NULL || out_value == NULL || token_text[0] == '\0') {
        return false;
    }

    numeric_value = strtoul(token_text, &end_ptr, 10);
    if (end_ptr == token_text || *end_ptr != '\0' || numeric_value > UINT16_MAX) {
        return false;
    }
    *out_value = (uint16_t)numeric_value;
    return true;
}

static bool lcd_controller_parse_u8_token(const char *token_text, uint8_t *out_value)
{
    unsigned long numeric_value = 0UL;
    char *end_ptr = NULL;

    if (token_text == NULL || out_value == NULL || token_text[0] == '\0') {
        return false;
    }

    numeric_value = strtoul(token_text, &end_ptr, 10);
    if (end_ptr == token_text || *end_ptr != '\0' || numeric_value > UINT8_MAX) {
        return false;
    }
    *out_value = (uint8_t)numeric_value;
    return true;
}

static bool lcd_controller_parse_i32_token(const char *token_text, int32_t *out_value)
{
    long numeric_value = 0L;
    char *end_ptr = NULL;

    if (token_text == NULL || out_value == NULL || token_text[0] == '\0') {
        return false;
    }

    numeric_value = strtol(token_text, &end_ptr, 10);
    if (end_ptr == token_text || *end_ptr != '\0' || numeric_value < INT32_MIN || numeric_value > INT32_MAX) {
        return false;
    }
    *out_value = (int32_t)numeric_value;
    return true;
}

static bool lcd_controller_parse_float_token(const char *token_text, float *out_value)
{
    float numeric_value = 0.0f;
    char *end_ptr = NULL;

    if (token_text == NULL || out_value == NULL || token_text[0] == '\0') {
        return false;
    }

    numeric_value = strtof(token_text, &end_ptr);
    if (end_ptr == token_text || *end_ptr != '\0') {
        return false;
    }
    *out_value = numeric_value;
    return true;
}

static bool lcd_controller_parse_dataset_settings_csv(char *settings_csv,
                                                      lcd_controller_settings_t *out_settings)
{
    char *token = NULL;
    char *save_ptr = NULL;
    int token_index = 0;

    if (settings_csv == NULL || out_settings == NULL) {
        return false;
    }

    token = strtok_r(settings_csv, ",", &save_ptr);
    while (token != NULL) {
        switch (token_index) {
            case 0: if (!lcd_controller_parse_u16_token(token, &out_settings->steam_setpoint)) return false; break;
            case 1: if (!lcd_controller_parse_u16_token(token, &out_settings->offset_temp)) return false; break;
            case 2: if (!lcd_controller_parse_u16_token(token, &out_settings->hpwr)) return false; break;
            case 3: if (!lcd_controller_parse_u16_token(token, &out_settings->main_divider)) return false; break;
            case 4: if (!lcd_controller_parse_u16_token(token, &out_settings->brew_divider)) return false; break;
            case 5: if (!lcd_controller_parse_u8_token(token, &out_settings->active_profile)) return false; break;
            case 6: if (!lcd_controller_parse_u16_token(token, &out_settings->power_line_frequency)) return false; break;
            case 7: if (!lcd_controller_parse_u16_token(token, &out_settings->lcd_sleep)) return false; break;
            case 8: if (!lcd_controller_parse_bool_token(token, &out_settings->warmup_state)) return false; break;
            case 9: if (!lcd_controller_parse_bool_token(token, &out_settings->home_on_shot_finish)) return false; break;
            case 10: if (!lcd_controller_parse_bool_token(token, &out_settings->brew_delta_state)) return false; break;
            case 11: if (!lcd_controller_parse_bool_token(token, &out_settings->basket_prefill)) return false; break;
            case 12: if (!lcd_controller_parse_i32_token(token, &out_settings->scales_f1)) return false; break;
            case 13: if (!lcd_controller_parse_i32_token(token, &out_settings->scales_f2)) return false; break;
            case 14: if (!lcd_controller_parse_float_token(token, &out_settings->pump_flow_at_zero)) return false; break;
            case 15: if (!lcd_controller_parse_bool_token(token, &out_settings->led_state)) return false; break;
            case 16: if (!lcd_controller_parse_bool_token(token, &out_settings->led_disco)) return false; break;
            case 17: if (!lcd_controller_parse_u8_token(token, &out_settings->led_r)) return false; break;
            case 18: if (!lcd_controller_parse_u8_token(token, &out_settings->led_g)) return false; break;
            case 19: if (!lcd_controller_parse_u8_token(token, &out_settings->led_b)) return false; break;
            default: break;
        }
        token_index++;
        token = strtok_r(NULL, ",", &save_ptr);
    }

    return (token_index >= 20);
}

static bool lcd_controller_parse_dataset_profile_csv(char *profile_csv,
                                                     lcd_controller_profile_t *out_profile,
                                                     uint8_t *out_profile_id)
{
    char *token = NULL;
    char *save_ptr = NULL;
    int token_index = 0;
    uint8_t parsed_id = 0U;

    if (profile_csv == NULL || out_profile == NULL || out_profile_id == NULL) {
        return false;
    }

    memset(out_profile, 0, sizeof(*out_profile));

    token = strtok_r(profile_csv, ",", &save_ptr);
    while (token != NULL) {
        switch (token_index) {
            case 0: if (!lcd_controller_parse_u8_token(token, &parsed_id)) return false; break;
            case 1: snprintf(out_profile->name, sizeof(out_profile->name), "%s", token); break;
            case 2: if (!lcd_controller_parse_bool_token(token, &out_profile->preinfusion_state)) return false; break;
            case 3: if (!lcd_controller_parse_bool_token(token, &out_profile->preinfusion_flow_state)) return false; break;
            case 4: if (!lcd_controller_parse_u16_token(token, &out_profile->preinfusion_sec)) return false; break;
            case 5: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_bar)) return false; break;
            case 6: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_flow_vol)) return false; break;
            case 7: if (!lcd_controller_parse_u16_token(token, &out_profile->preinfusion_flow_time)) return false; break;
            case 8: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_flow_pressure_target)) return false; break;
            case 9: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_pressure_flow_target)) return false; break;
            case 10: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_filled)) return false; break;
            case 11: if (!lcd_controller_parse_bool_token(token, &out_profile->preinfusion_pressure_above)) return false; break;
            case 12: if (!lcd_controller_parse_float_token(token, &out_profile->preinfusion_weight_above)) return false; break;
            case 13: if (!lcd_controller_parse_bool_token(token, &out_profile->soak_state)) return false; break;
            case 14: if (!lcd_controller_parse_u16_token(token, &out_profile->soak_time_pressure)) return false; break;
            case 15: if (!lcd_controller_parse_u16_token(token, &out_profile->soak_time_flow)) return false; break;
            case 16: if (!lcd_controller_parse_float_token(token, &out_profile->soak_keep_pressure)) return false; break;
            case 17: if (!lcd_controller_parse_float_token(token, &out_profile->soak_keep_flow)) return false; break;
            case 18: if (!lcd_controller_parse_float_token(token, &out_profile->soak_below_pressure)) return false; break;
            case 19: if (!lcd_controller_parse_float_token(token, &out_profile->soak_above_pressure)) return false; break;
            case 20: if (!lcd_controller_parse_float_token(token, &out_profile->soak_above_weight)) return false; break;
            case 21: if (!lcd_controller_parse_u16_token(token, &out_profile->preinfusion_ramp)) return false; break;
            case 22: if (!lcd_controller_parse_u16_token(token, &out_profile->preinfusion_ramp_slope)) return false; break;
            case 23: if (!lcd_controller_parse_bool_token(token, &out_profile->tp_state)) return false; break;
            case 24: if (!lcd_controller_parse_bool_token(token, &out_profile->tp_type)) return false; break;
            case 25: if (!lcd_controller_parse_float_token(token, &out_profile->tp_profiling_start)) return false; break;
            case 26: if (!lcd_controller_parse_float_token(token, &out_profile->tp_profiling_finish)) return false; break;
            case 27: if (!lcd_controller_parse_u16_token(token, &out_profile->tp_profiling_hold)) return false; break;
            case 28: if (!lcd_controller_parse_float_token(token, &out_profile->tp_profiling_hold_limit)) return false; break;
            case 29: if (!lcd_controller_parse_u16_token(token, &out_profile->tp_profiling_slope)) return false; break;
            case 30: if (!lcd_controller_parse_u16_token(token, &out_profile->tp_profiling_slope_shape)) return false; break;
            case 31: if (!lcd_controller_parse_float_token(token, &out_profile->tp_profiling_flow_restriction)) return false; break;
            case 32: if (!lcd_controller_parse_float_token(token, &out_profile->tf_profile_start)) return false; break;
            case 33: if (!lcd_controller_parse_float_token(token, &out_profile->tf_profile_end)) return false; break;
            case 34: if (!lcd_controller_parse_u16_token(token, &out_profile->tf_profile_hold)) return false; break;
            case 35: if (!lcd_controller_parse_float_token(token, &out_profile->tf_profile_hold_limit)) return false; break;
            case 36: if (!lcd_controller_parse_u16_token(token, &out_profile->tf_profile_slope)) return false; break;
            case 37: if (!lcd_controller_parse_u16_token(token, &out_profile->tf_profile_slope_shape)) return false; break;
            case 38: if (!lcd_controller_parse_float_token(token, &out_profile->tf_profiling_pressure_restriction)) return false; break;
            case 39: if (!lcd_controller_parse_bool_token(token, &out_profile->profiling_state)) return false; break;
            case 40: if (!lcd_controller_parse_bool_token(token, &out_profile->mf_profile_state)) return false; break;
            case 41: if (!lcd_controller_parse_float_token(token, &out_profile->mp_profiling_start)) return false; break;
            case 42: if (!lcd_controller_parse_float_token(token, &out_profile->mp_profiling_finish)) return false; break;
            case 43: if (!lcd_controller_parse_u16_token(token, &out_profile->mp_profiling_slope)) return false; break;
            case 44: if (!lcd_controller_parse_u16_token(token, &out_profile->mp_profiling_slope_shape)) return false; break;
            case 45: if (!lcd_controller_parse_float_token(token, &out_profile->mp_profiling_flow_restriction)) return false; break;
            case 46: if (!lcd_controller_parse_float_token(token, &out_profile->mf_profile_start)) return false; break;
            case 47: if (!lcd_controller_parse_float_token(token, &out_profile->mf_profile_end)) return false; break;
            case 48: if (!lcd_controller_parse_u16_token(token, &out_profile->mf_profile_slope)) return false; break;
            case 49: if (!lcd_controller_parse_u16_token(token, &out_profile->mf_profile_slope_shape)) return false; break;
            case 50: if (!lcd_controller_parse_float_token(token, &out_profile->mf_profiling_pressure_restriction)) return false; break;
            case 51: if (!lcd_controller_parse_u16_token(token, &out_profile->setpoint)) return false; break;
            case 52: if (!lcd_controller_parse_bool_token(token, &out_profile->stop_on_weight_state)) return false; break;
            case 53: if (!lcd_controller_parse_float_token(token, &out_profile->shot_dose)) return false; break;
            case 54: if (!lcd_controller_parse_float_token(token, &out_profile->shot_stop_on_custom_weight)) return false; break;
            case 55: if (!lcd_controller_parse_u16_token(token, &out_profile->shot_preset)) return false; break;
            default: break;
        }

        token_index++;
        token = strtok_r(NULL, ",", &save_ptr);
    }

    if (token_index < 56 || parsed_id == 0U) {
        return false;
    }
    *out_profile_id = parsed_id;
    return true;
}

static void lcd_controller_build_catalog_from_dataset(const lcd_controller_dataset_t *dataset,
                                                      lcd_controller_profile_catalog_t *out_catalog)
{
    uint8_t index = 0U;

    if (dataset == NULL || out_catalog == NULL) {
        return;
    }

    memset(out_catalog, 0, sizeof(*out_catalog));
    for (index = 0U; index < LCD_CONTROLLER_MAX_PROFILES; index++) {
        const lcd_controller_profile_t *profile = &dataset->profiles[index];
        const float setpoint_c = (profile->setpoint > 200U)
                                     ? ((float)profile->setpoint / 10.0f)
                                     : (float)profile->setpoint;
        out_catalog->entries[index].profile_id = (uint8_t)(index + 1U);
        snprintf(out_catalog->entries[index].profile_name,
                 sizeof(out_catalog->entries[index].profile_name),
                 "%s",
                 profile->name);
        out_catalog->entries[index].target_temperature_c = setpoint_c;
        out_catalog->entries[index].target_pressure_bar = profile->tp_profiling_finish;
        out_catalog->entries[index].target_flow_ml_s = profile->mf_profile_end;
        out_catalog->entries[index].shot_target_g = profile->shot_stop_on_custom_weight;
    }
    out_catalog->count = LCD_CONTROLLER_MAX_PROFILES;
}

static bool lcd_controller_parse_profile_dataset_payload(const char *payload_text,
                                                         lcd_controller_dataset_t *out_dataset)
{
    char schema_text[16] = {0};
    char count_text[16] = {0};
    char settings_text[LCD_CONTROLLER_DATASET_SETTINGS_MAX_TEXT] = {0};
    char profiles_text[LCD_CONTROLLER_DATASET_PROFILES_MAX_TEXT] = {0};
    unsigned long expected_count = 0UL;
    char *end_ptr = NULL;
    char *profile_token = NULL;
    char *profile_save_ptr = NULL;
    bool profile_slot_used[LCD_CONTROLLER_MAX_PROFILES] = {false};
    uint8_t parsed_profiles = 0U;
    uint16_t parsed_schema = 0U;

    if (payload_text == NULL || out_dataset == NULL) {
        return false;
    }

    memset(out_dataset, 0, sizeof(*out_dataset));

    if (!lcd_controller_extract_payload_value(payload_text, "schema", schema_text, sizeof(schema_text))) {
        return false;
    }
    parsed_schema = (uint16_t)strtoul(schema_text, &end_ptr, 10);
    if (end_ptr == schema_text || *end_ptr != '\0') {
        return false;
    }

    if (!lcd_controller_extract_payload_value(payload_text, "count", count_text, sizeof(count_text))) {
        return false;
    }
    expected_count = strtoul(count_text, &end_ptr, 10);
    if (end_ptr == count_text || *end_ptr != '\0') {
        return false;
    }
    /* Initialization requires a full profile set in one dataset payload. */
    if (expected_count != LCD_CONTROLLER_MAX_PROFILES) {
        return false;
    }

    if (!lcd_controller_extract_payload_value(payload_text, "settings", settings_text, sizeof(settings_text))) {
        return false;
    }
    if (!lcd_controller_parse_dataset_settings_csv(settings_text, &out_dataset->settings)) {
        return false;
    }

    if (!lcd_controller_extract_payload_value(payload_text, "profiles", profiles_text, sizeof(profiles_text))) {
        return false;
    }

    profile_token = strtok_r(profiles_text, "|", &profile_save_ptr);
    while (profile_token != NULL && parsed_profiles < LCD_CONTROLLER_MAX_PROFILES) {
        char profile_token_copy[LCD_CONTROLLER_DATASET_PROFILE_TOKEN_MAX_TEXT] = {0};
        lcd_controller_profile_t parsed_profile = {0};
        uint8_t parsed_profile_id = 0U;
        uint8_t target_slot = 0U;

        snprintf(profile_token_copy, sizeof(profile_token_copy), "%s", profile_token);
        if (!lcd_controller_parse_dataset_profile_csv(profile_token_copy, &parsed_profile, &parsed_profile_id)) {
            return false;
        }

        if (parsed_profile_id < 1U || parsed_profile_id > LCD_CONTROLLER_MAX_PROFILES) {
            return false;
        }
        target_slot = (uint8_t)(parsed_profile_id - 1U);
        if (profile_slot_used[target_slot]) {
            return false;
        }

        out_dataset->profiles[target_slot] = parsed_profile;
        profile_slot_used[target_slot] = true;
        parsed_profiles++;
        profile_token = strtok_r(NULL, "|", &profile_save_ptr);
    }
    if (profile_token != NULL) {
        /* Payload contains more profiles than the fixed schema supports. */
        return false;
    }

    out_dataset->schema_version = parsed_schema;
    if (out_dataset->settings.active_profile < 1U || out_dataset->settings.active_profile > LCD_CONTROLLER_MAX_PROFILES) {
        out_dataset->settings.active_profile = 1U;
    }

    if (parsed_profiles != LCD_CONTROLLER_MAX_PROFILES) {
        return false;
    }
    for (uint8_t index = 0U; index < LCD_CONTROLLER_MAX_PROFILES; index++) {
        if (!profile_slot_used[index]) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Safely read and return the current link-state snapshot.
 */
lcd_controller_protocol_link_state_t lcd_controller_protocol_get_link_state(void)
{
    lcd_controller_protocol_link_state_t link_state = LCD_CONTROLLER_PROTOCOL_STATE_UNINITIALIZED;

    if (s_protocol.mutex == NULL) {
        return link_state;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    link_state = s_protocol.link_state;
    xSemaphoreGive(s_protocol.mutex);
    return link_state;
}

/**
 * @brief Initialize protocol context and register transport send callback.
 */
esp_err_t lcd_controller_protocol_init(lcd_controller_send_command_fn_t send_command_cb,
                                       void *send_command_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        s_protocol.mutex = xSemaphoreCreateMutex();
        if (s_protocol.mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);

    s_protocol.initialized = true;
    s_protocol.hooks_requested = false;
    s_protocol.last_event_count = 0U;
    s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_READY;
    memset(&s_protocol.profile_catalog, 0, sizeof(s_protocol.profile_catalog));
    memset(&s_protocol.profile_dataset, 0, sizeof(s_protocol.profile_dataset));
    memset(&s_protocol.brew_state, 0, sizeof(s_protocol.brew_state));
    s_protocol.send_command_cb = send_command_cb;
    s_protocol.send_command_user_ctx = send_command_user_ctx;

    xSemaphoreGive(s_protocol.mutex);

    ESP_LOGI(TAG, "Initialized Brew/Home protocol layer (schema=%u)", (unsigned)LCD_CONTROLLER_BREW_SCHEMA_VERSION);
    return ESP_OK;
}

/**
 * @brief Send protocol bootstrap commands (`init` + dataset + catalog get`).
 */
esp_err_t lcd_controller_protocol_initialize_hooks(void)
{
    lcd_controller_send_command_fn_t send_cb = NULL;
    void *send_ctx = NULL;

    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized || s_protocol.send_command_cb == NULL) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    send_cb = s_protocol.send_command_cb;
    send_ctx = s_protocol.send_command_user_ctx;
    s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_WAIT_SCHEMA_ACK;
    xSemaphoreGive(s_protocol.mutex);

    esp_err_t ret = send_cb(LCD_CONTROLLER_PROTOCOL_CMD_INIT, send_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = send_cb(LCD_CONTROLLER_PROTOCOL_CMD_PROFILE_DATASET_GET, send_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = send_cb(LCD_CONTROLLER_PROTOCOL_CMD_PROFILE_CATALOG_GET, send_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.hooks_requested = true;
    xSemaphoreGive(s_protocol.mutex);

    ESP_LOGI(TAG, "Bootstrap hooks requested: init + dataset + profile catalog polls queued.");
    return ESP_OK;
}

/**
 * @brief Decode one peer text-event and update local protocol caches.
 */
esp_err_t lcd_controller_protocol_process_peer_text_event(uint32_t event_count,
                                                          const char *payload_text)
{
    lcd_controller_profile_catalog_t parsed_catalog = {0};
    lcd_controller_dataset_t parsed_dataset = {0};
    bool publish_catalog = false;
    lcd_controller_profile_catalog_hook_t catalog_hook = NULL;
    void *catalog_hook_ctx = NULL;

    if (payload_text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (event_count == s_protocol.last_event_count) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_OK;
    }
    s_protocol.last_event_count = event_count;
    xSemaphoreGive(s_protocol.mutex);

    if (lcd_controller_starts_with_ignore_case(payload_text, LCD_CONTROLLER_PROTOCOL_EVENT_PREFIX_ACK)) {
        xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
        s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_SCHEMA_ACKED;
        xSemaphoreGive(s_protocol.mutex);
        return ESP_OK;
    }

    if (lcd_controller_starts_with_ignore_case(payload_text, LCD_CONTROLLER_PROTOCOL_EVENT_PREFIX_PROFILE_DATASET)) {
        esp_err_t constants_ret = ESP_OK;

        if (!lcd_controller_parse_profile_dataset_payload(payload_text, &parsed_dataset)) {
            ESP_LOGW(TAG, "Rejected profile dataset payload: full %u-profile dataset is required.",
                     (unsigned)LCD_CONTROLLER_MAX_PROFILES);
            return ESP_ERR_INVALID_ARG;
        }

        xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
        s_protocol.profile_dataset = parsed_dataset;
        lcd_controller_build_catalog_from_dataset(&s_protocol.profile_dataset, &s_protocol.profile_catalog);
        s_protocol.link_state = LCD_CONTROLLER_PROTOCOL_STATE_DATASET_SYNCED;
        catalog_hook = s_protocol.profile_hook_cb;
        catalog_hook_ctx = s_protocol.profile_hook_user_ctx;
        publish_catalog = (catalog_hook != NULL);
        xSemaphoreGive(s_protocol.mutex);

        constants_ret = system_constants_apply_lcd_dataset(&parsed_dataset, true);
        if (constants_ret != ESP_OK) {
            ESP_LOGW(TAG, "Profile dataset apply/persist failed: %s", esp_err_to_name(constants_ret));
        } else {
            ESP_LOGI(TAG, "Profile dataset applied and stored to TF (schema=%u).",
                     (unsigned)parsed_dataset.schema_version);
        }

        if (publish_catalog) {
            lcd_controller_profile_catalog_t catalog_from_dataset = {0};
            lcd_controller_build_catalog_from_dataset(&parsed_dataset, &catalog_from_dataset);
            catalog_hook(&catalog_from_dataset, catalog_hook_ctx);
        }
        return ESP_OK;
    }

    if (!lcd_controller_starts_with_ignore_case(payload_text, LCD_CONTROLLER_PROTOCOL_EVENT_PREFIX_PROFILE_CATALOG)) {
        return ESP_OK;
    }

    if (!lcd_controller_parse_profile_catalog_payload(payload_text, &parsed_catalog)) {
        ESP_LOGW(TAG, "Failed to parse profile catalog payload: %s", payload_text);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.profile_catalog = parsed_catalog;
    catalog_hook = s_protocol.profile_hook_cb;
    catalog_hook_ctx = s_protocol.profile_hook_user_ctx;
    publish_catalog = (catalog_hook != NULL);
    xSemaphoreGive(s_protocol.mutex);

    if (publish_catalog) {
        catalog_hook(&parsed_catalog, catalog_hook_ctx);
    }
    return ESP_OK;
}

/**
 * @brief Publish latest Brew/Home state into the protocol cache.
 */
esp_err_t lcd_controller_protocol_publish_brew_home_state(const lcd_controller_brew_home_state_t *state)
{
    lcd_controller_brew_state_hook_t state_hook = NULL;
    void *state_hook_ctx = NULL;

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_protocol.brew_state = *state;
    state_hook = s_protocol.brew_hook_cb;
    state_hook_ctx = s_protocol.brew_hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);

    if (state_hook != NULL) {
        state_hook(state, state_hook_ctx);
    }
    return ESP_OK;
}

/**
 * @brief Return the latest Brew/Home state snapshot.
 */
esp_err_t lcd_controller_protocol_get_brew_home_state(lcd_controller_brew_home_state_t *out_state)
{
    if (out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *out_state = s_protocol.brew_state;
    xSemaphoreGive(s_protocol.mutex);
    return ESP_OK;
}

/**
 * @brief Return the latest profile catalog snapshot.
 */
esp_err_t lcd_controller_protocol_get_profile_catalog(lcd_controller_profile_catalog_t *out_catalog)
{
    if (out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_protocol.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    if (!s_protocol.initialized) {
        xSemaphoreGive(s_protocol.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *out_catalog = s_protocol.profile_catalog;
    xSemaphoreGive(s_protocol.mutex);
    return ESP_OK;
}

/**
 * @brief Register optional profile-catalog callback hook.
 */
void lcd_controller_protocol_set_profile_catalog_hook(lcd_controller_profile_catalog_hook_t hook_cb,
                                                      void *hook_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.profile_hook_cb = hook_cb;
    s_protocol.profile_hook_user_ctx = hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);
}

/**
 * @brief Register optional Brew/Home-state callback hook.
 */
void lcd_controller_protocol_set_brew_state_hook(lcd_controller_brew_state_hook_t hook_cb,
                                                 void *hook_user_ctx)
{
    if (s_protocol.mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_protocol.mutex, portMAX_DELAY);
    s_protocol.brew_hook_cb = hook_cb;
    s_protocol.brew_hook_user_ctx = hook_user_ctx;
    xSemaphoreGive(s_protocol.mutex);
}
