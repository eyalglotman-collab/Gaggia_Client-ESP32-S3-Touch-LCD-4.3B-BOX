/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "system_constants.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "sys_constants";
static const char *SYSTEM_CONSTANTS_OVERRIDES_PATH = "/sdcard/SystemConstantsOverrides.ini";
static const char *SYSTEM_CONSTANTS_LCD_DATASET_PATH = "/sdcard/LCDProfileDataset.bin";

#define SYSTEM_CONSTANTS_LCD_DATASET_MAGIC (0x4C434450UL)
#define SYSTEM_CONSTANTS_LCD_DATASET_STORAGE_VERSION (1U)

#define LIVE_SHOT_PRESSURE_MIN_BAR (1.0f)
#define LIVE_SHOT_PRESSURE_MAX_BAR (20.0f)
#define LIVE_SHOT_WEIGHT_MIN_G (10.0f)
#define LIVE_SHOT_WEIGHT_MAX_G (100.0f)
#define LIVE_SHOT_FLOW_MIN_ML_S (1.0f)
#define LIVE_SHOT_FLOW_MAX_ML_S (30.0f)
#define LIVE_SHOT_TEMPERATURE_MIN_C (30.0f)
#define LIVE_SHOT_TEMPERATURE_MAX_C (105.0f)

extern const char SystemConstants_xml_start[] asm("_binary_SystemConstants_xml_start");
extern const char SystemConstants_xml_end[] asm("_binary_SystemConstants_xml_end");
extern const char Profiles_xml_start[] asm("_binary_Profiles_xml_start");
extern const char Profiles_xml_end[] asm("_binary_Profiles_xml_end");

static system_constants_data_t s_constants = {0};

typedef struct {
    uint32_t magic;
    uint16_t storage_version;
    uint16_t dataset_size;
    lcd_controller_dataset_t dataset;
} system_constants_lcd_dataset_storage_t;

static float clampf_range(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static bool parse_bool_text(const char *text, bool fallback_value)
{
    if (text == NULL) {
        return fallback_value;
    }
    if ((strcmp(text, "1") == 0) ||
        (strcasecmp(text, "true") == 0) ||
        (strcasecmp(text, "yes") == 0) ||
        (strcasecmp(text, "on") == 0)) {
        return true;
    }
    if ((strcmp(text, "0") == 0) ||
        (strcasecmp(text, "false") == 0) ||
        (strcasecmp(text, "no") == 0) ||
        (strcasecmp(text, "off") == 0)) {
        return false;
    }
    return fallback_value;
}

/**
 * @brief Convert decimal text into tenths.
 *
 * @details Parses decimal strings such as `9.0` or `2.5` and stores the value
 * as tenths to avoid floating-point state in the public constants snapshot.
 *
 * @param[in] text Decimal input string.
 *
 * @return Parsed value in tenths.
 */
static int parse_tenths(const char *text)
{
    float value = strtof(text, NULL);
    if (value >= 0.0f) {
        return (int)(value * 10.0f + 0.5f);
    }
    return (int)(value * 10.0f - 0.5f);
}

static int system_constants_profile_count_limit(void)
{
    if (SYSTEM_CONSTANTS_MAX_PROFILES <= 0) {
        return 1;
    }
    return SYSTEM_CONSTANTS_MAX_PROFILES;
}

static int setpoint_to_celsius_int(uint16_t setpoint_value)
{
    if (setpoint_value > 200U) {
        return (int)((setpoint_value + 5U) / 10U);
    }
    return (int)setpoint_value;
}

static void system_constants_sync_profile_summary_from_lcd_dataset(void)
{
    int max_profiles = system_constants_profile_count_limit();

    for (int index = 0; index < max_profiles; index++) {
        const lcd_controller_profile_t *source = &s_constants.lcd_profile_dataset.profiles[index];
        system_constants_profile_t *target = &s_constants.profiles[index];
        float pressure_bar = source->tp_profiling_finish;
        float flow_ml_s = source->mf_profile_end;
        int temp_c = setpoint_to_celsius_int(source->setpoint);

        if (source->name[0] == '\0') {
            snprintf(target->name, sizeof(target->name), "Profile %d", index + 1);
        } else {
            snprintf(target->name, sizeof(target->name), "%s", source->name);
        }
        target->target_temperature_c = temp_c;
        target->preinfusion_seconds = (int)source->preinfusion_sec;
        target->target_pressure_tenths = (int)lroundf(pressure_bar * 10.0f);
        target->target_flow_tenths = (int)lroundf(flow_ml_s * 10.0f);
    }

    s_constants.profile_count = max_profiles;
}

static void system_constants_seed_lcd_dataset_from_summary(void)
{
    int max_profiles = system_constants_profile_count_limit();

    memset(&s_constants.lcd_profile_dataset, 0, sizeof(s_constants.lcd_profile_dataset));
    s_constants.lcd_profile_dataset.schema_version = LCD_CONTROLLER_DATA_SCHEMA_VERSION;
    s_constants.lcd_profile_dataset.settings.steam_setpoint = 1450U;
    s_constants.lcd_profile_dataset.settings.offset_temp = 0U;
    s_constants.lcd_profile_dataset.settings.hpwr = 1000U;
    s_constants.lcd_profile_dataset.settings.main_divider = 100U;
    s_constants.lcd_profile_dataset.settings.brew_divider = 100U;
    s_constants.lcd_profile_dataset.settings.active_profile = 1U;
    s_constants.lcd_profile_dataset.settings.power_line_frequency = 50U;
    s_constants.lcd_profile_dataset.settings.lcd_sleep = 30U;
    s_constants.lcd_profile_dataset.settings.warmup_state = true;
    s_constants.lcd_profile_dataset.settings.home_on_shot_finish = false;
    s_constants.lcd_profile_dataset.settings.brew_delta_state = false;
    s_constants.lcd_profile_dataset.settings.basket_prefill = false;
    s_constants.lcd_profile_dataset.settings.scales_f1 = 1;
    s_constants.lcd_profile_dataset.settings.scales_f2 = 1;
    s_constants.lcd_profile_dataset.settings.pump_flow_at_zero = 0.0f;
    s_constants.lcd_profile_dataset.settings.led_state = true;
    s_constants.lcd_profile_dataset.settings.led_disco = false;
    s_constants.lcd_profile_dataset.settings.led_r = 255U;
    s_constants.lcd_profile_dataset.settings.led_g = 190U;
    s_constants.lcd_profile_dataset.settings.led_b = 120U;

    for (int index = 0; index < max_profiles; index++) {
        lcd_controller_profile_t *target = &s_constants.lcd_profile_dataset.profiles[index];
        const system_constants_profile_t *source = &s_constants.profiles[index];
        float target_pressure_bar = (float)source->target_pressure_tenths / 10.0f;
        float target_flow_ml_s = (float)source->target_flow_tenths / 10.0f;
        float shot_target_g = 36.0f + (float)(index * 2);

        snprintf(target->name, sizeof(target->name), "%s", source->name);
        target->preinfusion_state = (source->preinfusion_seconds > 0);
        target->preinfusion_flow_state = false;
        target->preinfusion_sec = (uint16_t)((source->preinfusion_seconds > 0) ? source->preinfusion_seconds : 0);
        target->preinfusion_bar = target_pressure_bar * 0.3f;
        target->preinfusion_flow_vol = target_flow_ml_s * 0.4f;
        target->preinfusion_flow_time = target->preinfusion_sec;
        target->preinfusion_flow_pressure_target = target_pressure_bar;
        target->preinfusion_pressure_flow_target = target_flow_ml_s;
        target->soak_state = true;
        target->soak_time_pressure = target->preinfusion_sec / 2U;
        target->soak_time_flow = target->preinfusion_sec / 2U;
        target->soak_keep_pressure = target_pressure_bar * 0.45f;
        target->soak_keep_flow = target_flow_ml_s * 0.45f;
        target->preinfusion_ramp = target->preinfusion_sec;
        target->preinfusion_ramp_slope = target->preinfusion_sec * 100U;
        target->tp_state = true;
        target->tp_type = true;
        target->tp_profiling_start = target_pressure_bar * 0.3f;
        target->tp_profiling_finish = target_pressure_bar;
        target->tp_profiling_hold = 12U;
        target->tp_profiling_hold_limit = target_pressure_bar;
        target->tp_profiling_slope = 1200U;
        target->tp_profiling_slope_shape = LCD_TRANSITION_CURVE_EASE_IN_OUT;
        target->tp_profiling_flow_restriction = target_flow_ml_s * 1.2f;
        target->tf_profile_start = target_flow_ml_s * 0.4f;
        target->tf_profile_end = target_flow_ml_s;
        target->tf_profile_hold = 12U;
        target->tf_profile_hold_limit = target_flow_ml_s;
        target->tf_profile_slope = 1200U;
        target->tf_profile_slope_shape = LCD_TRANSITION_CURVE_LINEAR;
        target->tf_profiling_pressure_restriction = target_pressure_bar * 1.2f;
        target->profiling_state = true;
        target->mf_profile_state = true;
        target->mp_profiling_start = target_pressure_bar * 0.5f;
        target->mp_profiling_finish = target_pressure_bar;
        target->mp_profiling_slope = 900U;
        target->mp_profiling_slope_shape = LCD_TRANSITION_CURVE_LINEAR;
        target->mp_profiling_flow_restriction = target_flow_ml_s * 1.1f;
        target->mf_profile_start = target_flow_ml_s * 0.5f;
        target->mf_profile_end = target_flow_ml_s;
        target->mf_profile_slope = 900U;
        target->mf_profile_slope_shape = LCD_TRANSITION_CURVE_LINEAR;
        target->mf_profiling_pressure_restriction = target_pressure_bar * 1.1f;
        target->setpoint = (uint16_t)(source->target_temperature_c * 10);
        target->stop_on_weight_state = true;
        target->shot_dose = 18.0f;
        target->shot_stop_on_custom_weight = shot_target_g;
        target->shot_preset = (uint16_t)index;
    }
}

static esp_err_t system_constants_save_lcd_dataset_to_sd(void)
{
    system_constants_lcd_dataset_storage_t storage = {0};
    /* Remove prior dataset first so stale profile bytes can never remain. */
    (void)remove(SYSTEM_CONSTANTS_LCD_DATASET_PATH);
    FILE *f = fopen(SYSTEM_CONSTANTS_LCD_DATASET_PATH, "wb");

    if (f == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    storage.magic = SYSTEM_CONSTANTS_LCD_DATASET_MAGIC;
    storage.storage_version = SYSTEM_CONSTANTS_LCD_DATASET_STORAGE_VERSION;
    storage.dataset_size = (uint16_t)sizeof(storage.dataset);
    storage.dataset = s_constants.lcd_profile_dataset;

    size_t written = fwrite(&storage, 1U, sizeof(storage), f);
    int flush_ret = fflush(f);
    int close_ret = fclose(f);
    if (written != sizeof(storage) || flush_ret != 0 || close_ret != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t system_constants_try_load_lcd_dataset_from_sd(void)
{
    system_constants_lcd_dataset_storage_t storage = {0};
    FILE *f = fopen(SYSTEM_CONSTANTS_LCD_DATASET_PATH, "rb");

    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t read_len = fread(&storage, 1U, sizeof(storage), f);
    fclose(f);
    if (read_len != sizeof(storage)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (storage.magic != SYSTEM_CONSTANTS_LCD_DATASET_MAGIC ||
        storage.storage_version != SYSTEM_CONSTANTS_LCD_DATASET_STORAGE_VERSION ||
        storage.dataset_size != sizeof(storage.dataset)) {
        return ESP_ERR_INVALID_VERSION;
    }

    s_constants.lcd_profile_dataset = storage.dataset;
    system_constants_sync_profile_summary_from_lcd_dataset();
    return ESP_OK;
}

/**
 * @brief Remove leading and trailing whitespace from a mutable string.
 *
 * @details Normalizes extracted XML text content before integer or string
 * conversion so formatting whitespace does not affect parsing.
 *
 * @param[in,out] text Mutable string buffer to trim.
 */
static void trim_in_place(char *text)
{
    char *start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    if (start != text) {
        memmove(text, start, (size_t)(end - start) + 1);
    }
}

/**
 * @brief Extract the first matching XML tag value from a text range.
 *
 * @details Uses a simple tag search for the fixed structure of the embedded
 * constants database. It is intentionally minimal and not a general XML parser.
 *
 * @param[in] xml_start Start of search range.
 * @param[in] xml_end End of search range.
 * @param[in] tag XML tag name without angle brackets.
 * @param[out] out_text Destination buffer.
 * @param[in] out_len Size of destination buffer.
 *
 * @return `true` when the tag content was extracted successfully.
 */
static bool xml_extract_text_range(const char *xml_start,
                                   const char *xml_end,
                                   const char *tag,
                                   char *out_text,
                                   size_t out_len)
{
    char open_tag[64];
    char close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *value_start = strstr(xml_start, open_tag);
    if (value_start == NULL || value_start >= xml_end) {
        return false;
    }
    value_start += strlen(open_tag);

    const char *value_end = strstr(value_start, close_tag);
    if (value_end == NULL || value_end > xml_end) {
        return false;
    }

    size_t copy_len = (size_t)(value_end - value_start);
    if (copy_len >= out_len) {
        copy_len = out_len - 1;
    }

    memcpy(out_text, value_start, copy_len);
    out_text[copy_len] = '\0';
    trim_in_place(out_text);
    return true;
}

/**
 * @brief Populate the in-memory constants snapshot with safe defaults.
 *
 * @details Establishes a known-good baseline that matches the default XML file
 * so partial XML parsing failures still leave the application usable.
 */
static void system_constants_set_defaults(void)
{
    memset(&s_constants, 0, sizeof(s_constants));

    snprintf(s_constants.client_version, sizeof(s_constants.client_version), "0.2.4");
    snprintf(s_constants.compatible_client_version, sizeof(s_constants.compatible_client_version), "0.2.x");
    snprintf(s_constants.connection_host, sizeof(s_constants.connection_host), "GaggiaController");
    snprintf(s_constants.connection_port, sizeof(s_constants.connection_port), "UART2");
    s_constants.connection_baud_rate = 115200;
    s_constants.profile_count = SYSTEM_CONSTANTS_MAX_PROFILES;
    s_constants.temperature_min_c = 86;
    s_constants.temperature_max_c = 98;
    s_constants.pressure_min_tenths = 0;
    s_constants.pressure_max_tenths = 120;
    s_constants.flow_min_tenths = 0;
    s_constants.flow_max_tenths = 50;
    s_constants.live_shot_pressure_max_bar = 12.0f;
    s_constants.live_shot_weight_max_g = 60.0f;
    s_constants.live_shot_flow_max_ml_s = 5.0f;
    s_constants.live_shot_temperature_max_c = 105.0f;
    s_constants.live_shot_autoscale_pressure = true;
    s_constants.live_shot_autoscale_weight = true;
    s_constants.live_shot_autoscale_flow = true;
    s_constants.live_shot_autoscale_temperature = true;

    snprintf(s_constants.profiles[0].name, sizeof(s_constants.profiles[0].name), "Classic 9 Bar");
    s_constants.profiles[0].target_temperature_c = 93;
    s_constants.profiles[0].preinfusion_seconds = 4;
    s_constants.profiles[0].target_pressure_tenths = 90;
    s_constants.profiles[0].target_flow_tenths = 20;

    snprintf(s_constants.profiles[1].name, sizeof(s_constants.profiles[1].name), "Turbo Shot");
    s_constants.profiles[1].target_temperature_c = 91;
    s_constants.profiles[1].preinfusion_seconds = 2;
    s_constants.profiles[1].target_pressure_tenths = 75;
    s_constants.profiles[1].target_flow_tenths = 30;

    snprintf(s_constants.profiles[2].name, sizeof(s_constants.profiles[2].name), "Light Roast");
    s_constants.profiles[2].target_temperature_c = 96;
    s_constants.profiles[2].preinfusion_seconds = 6;
    s_constants.profiles[2].target_pressure_tenths = 95;
    s_constants.profiles[2].target_flow_tenths = 18;

    snprintf(s_constants.profiles[3].name, sizeof(s_constants.profiles[3].name), "Blooming Filter");
    s_constants.profiles[3].target_temperature_c = 94;
    s_constants.profiles[3].preinfusion_seconds = 5;
    s_constants.profiles[3].target_pressure_tenths = 70;
    s_constants.profiles[3].target_flow_tenths = 21;

    snprintf(s_constants.profiles[4].name, sizeof(s_constants.profiles[4].name), "Ristretto Dense");
    s_constants.profiles[4].target_temperature_c = 92;
    s_constants.profiles[4].preinfusion_seconds = 3;
    s_constants.profiles[4].target_pressure_tenths = 98;
    s_constants.profiles[4].target_flow_tenths = 14;

    system_constants_seed_lcd_dataset_from_summary();
}

/**
 * @brief Parse one `<Profile>` block from the constants XML.
 *
 * @details Extracts the profile name and target values from the provided XML
 * subsection and stores them into the destination profile record.
 *
 * @param[in] profile_start Start of the profile XML block.
 * @param[in] profile_end End of the profile XML block.
 * @param[out] out_profile Destination profile structure.
 */
static void parse_profile_block(const char *profile_start,
                                const char *profile_end,
                                system_constants_profile_t *out_profile)
{
    char value[64];

    if (xml_extract_text_range(profile_start, profile_end, "Name", out_profile->name, sizeof(out_profile->name))) {
        /* Parsed successfully. */
    }
    if (xml_extract_text_range(profile_start, profile_end, "TargetTemperature", value, sizeof(value))) {
        out_profile->target_temperature_c = atoi(value);
    }
    if (xml_extract_text_range(profile_start, profile_end, "PreinfusionSeconds", value, sizeof(value))) {
        out_profile->preinfusion_seconds = atoi(value);
    }
    if (xml_extract_text_range(profile_start, profile_end, "TargetPressure", value, sizeof(value))) {
        out_profile->target_pressure_tenths = parse_tenths(value);
    }
    if (xml_extract_text_range(profile_start, profile_end, "TargetFlow", value, sizeof(value))) {
        out_profile->target_flow_tenths = parse_tenths(value);
    }
}

/**
 * @brief Parse the global non-profile values from the XML database.
 *
 * @details Updates version, limit, and connection metadata using the top-level
 * tags from the embedded constants database.
 *
 * @param[in] xml_start Start of the XML text.
 * @param[in] xml_end End of the XML text.
 */
static void parse_global_values(const char *xml_start, const char *xml_end)
{
    char value[64];

    xml_extract_text_range(xml_start, xml_end, "ClientVersion", s_constants.client_version, sizeof(s_constants.client_version));
    xml_extract_text_range(xml_start,
                           xml_end,
                           "CompatibleClientVersion",
                           s_constants.compatible_client_version,
                           sizeof(s_constants.compatible_client_version));
    xml_extract_text_range(xml_start, xml_end, "Host", s_constants.connection_host, sizeof(s_constants.connection_host));
    xml_extract_text_range(xml_start, xml_end, "Port", s_constants.connection_port, sizeof(s_constants.connection_port));

    if (xml_extract_text_range(xml_start, xml_end, "BaudRate", value, sizeof(value))) {
        s_constants.connection_baud_rate = atoi(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "TemperatureMin", value, sizeof(value))) {
        s_constants.temperature_min_c = atoi(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "TemperatureMax", value, sizeof(value))) {
        s_constants.temperature_max_c = atoi(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "PressureMin", value, sizeof(value))) {
        s_constants.pressure_min_tenths = parse_tenths(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "PressureMax", value, sizeof(value))) {
        s_constants.pressure_max_tenths = parse_tenths(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "FlowMin", value, sizeof(value))) {
        s_constants.flow_min_tenths = parse_tenths(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "FlowMax", value, sizeof(value))) {
        s_constants.flow_max_tenths = parse_tenths(value);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotPressureMax", value, sizeof(value))) {
        s_constants.live_shot_pressure_max_bar = strtof(value, NULL);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotWeightMax", value, sizeof(value))) {
        s_constants.live_shot_weight_max_g = strtof(value, NULL);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotFlowMax", value, sizeof(value))) {
        s_constants.live_shot_flow_max_ml_s = strtof(value, NULL);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotTemperatureMax", value, sizeof(value))) {
        s_constants.live_shot_temperature_max_c = strtof(value, NULL);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotAutoscalePressure", value, sizeof(value))) {
        s_constants.live_shot_autoscale_pressure = parse_bool_text(value, s_constants.live_shot_autoscale_pressure);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotAutoscaleWeight", value, sizeof(value))) {
        s_constants.live_shot_autoscale_weight = parse_bool_text(value, s_constants.live_shot_autoscale_weight);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotAutoscaleFlow", value, sizeof(value))) {
        s_constants.live_shot_autoscale_flow = parse_bool_text(value, s_constants.live_shot_autoscale_flow);
    }
    if (xml_extract_text_range(xml_start, xml_end, "LiveShotAutoscaleTemperature", value, sizeof(value))) {
        s_constants.live_shot_autoscale_temperature = parse_bool_text(value, s_constants.live_shot_autoscale_temperature);
    }

    s_constants.live_shot_pressure_max_bar = clampf_range(
        s_constants.live_shot_pressure_max_bar,
        LIVE_SHOT_PRESSURE_MIN_BAR,
        LIVE_SHOT_PRESSURE_MAX_BAR);
    s_constants.live_shot_weight_max_g = clampf_range(
        s_constants.live_shot_weight_max_g,
        LIVE_SHOT_WEIGHT_MIN_G,
        LIVE_SHOT_WEIGHT_MAX_G);
    s_constants.live_shot_flow_max_ml_s = clampf_range(
        s_constants.live_shot_flow_max_ml_s,
        LIVE_SHOT_FLOW_MIN_ML_S,
        LIVE_SHOT_FLOW_MAX_ML_S);
    s_constants.live_shot_temperature_max_c = clampf_range(
        s_constants.live_shot_temperature_max_c,
        LIVE_SHOT_TEMPERATURE_MIN_C,
        LIVE_SHOT_TEMPERATURE_MAX_C);
}

static void system_constants_try_load_live_shot_overrides_from_sd(void)
{
    FILE *f = fopen(SYSTEM_CONSTANTS_OVERRIDES_PATH, "r");
    if (f == NULL) {
        return;
    }

    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        char key[64] = {0};
        char value_text[64] = {0};
        if (sscanf(line, " %63[^=]=%63s", key, value_text) != 2) {
            continue;
        }
        if (strcmp(key, "live_shot_pressure_max_bar") == 0) {
            s_constants.live_shot_pressure_max_bar = strtof(value_text, NULL);
        } else if (strcmp(key, "live_shot_weight_max_g") == 0) {
            s_constants.live_shot_weight_max_g = strtof(value_text, NULL);
        } else if (strcmp(key, "live_shot_flow_max_ml_s") == 0) {
            s_constants.live_shot_flow_max_ml_s = strtof(value_text, NULL);
        } else if (strcmp(key, "live_shot_temperature_max_c") == 0) {
            s_constants.live_shot_temperature_max_c = strtof(value_text, NULL);
        } else if (strcmp(key, "live_shot_autoscale_pressure") == 0) {
            s_constants.live_shot_autoscale_pressure =
                parse_bool_text(value_text, s_constants.live_shot_autoscale_pressure);
        } else if (strcmp(key, "live_shot_autoscale_weight") == 0) {
            s_constants.live_shot_autoscale_weight =
                parse_bool_text(value_text, s_constants.live_shot_autoscale_weight);
        } else if (strcmp(key, "live_shot_autoscale_flow") == 0) {
            s_constants.live_shot_autoscale_flow =
                parse_bool_text(value_text, s_constants.live_shot_autoscale_flow);
        } else if (strcmp(key, "live_shot_autoscale_temperature") == 0) {
            s_constants.live_shot_autoscale_temperature =
                parse_bool_text(value_text, s_constants.live_shot_autoscale_temperature);
        }
    }
    fclose(f);

    s_constants.live_shot_pressure_max_bar = clampf_range(
        s_constants.live_shot_pressure_max_bar,
        LIVE_SHOT_PRESSURE_MIN_BAR,
        LIVE_SHOT_PRESSURE_MAX_BAR);
    s_constants.live_shot_weight_max_g = clampf_range(
        s_constants.live_shot_weight_max_g,
        LIVE_SHOT_WEIGHT_MIN_G,
        LIVE_SHOT_WEIGHT_MAX_G);
    s_constants.live_shot_flow_max_ml_s = clampf_range(
        s_constants.live_shot_flow_max_ml_s,
        LIVE_SHOT_FLOW_MIN_ML_S,
        LIVE_SHOT_FLOW_MAX_ML_S);
    s_constants.live_shot_temperature_max_c = clampf_range(
        s_constants.live_shot_temperature_max_c,
        LIVE_SHOT_TEMPERATURE_MIN_C,
        LIVE_SHOT_TEMPERATURE_MAX_C);
}

/**
 * @brief Parse the profile list from the embedded XML database.
 *
 * @details Walks through each `<Profile>` section and fills the corresponding
 * in-memory profile slot until the configured maximum is reached.
 *
 * @param[in] xml_start Start of the XML text.
 * @param[in] xml_end End of the XML text.
 */
static void parse_profiles(const char *xml_start, const char *xml_end)
{
    const char *cursor = xml_start;
    char value[32] = {0};
    int declared_count = SYSTEM_CONSTANTS_MAX_PROFILES;
    int parsed_count = 0;

    if (xml_extract_text_range(xml_start, xml_end, "ProfileCount", value, sizeof(value))) {
        int count = atoi(value);
        if (count > 0 && count <= SYSTEM_CONSTANTS_MAX_PROFILES) {
            declared_count = count;
        }
    }

    while (parsed_count < declared_count && parsed_count < SYSTEM_CONSTANTS_MAX_PROFILES) {
        const char *profile_start = strstr(cursor, "<Profile>");
        if (profile_start == NULL || profile_start >= xml_end) {
            break;
        }

        const char *profile_end = strstr(profile_start, "</Profile>");
        if (profile_end == NULL || profile_end > xml_end) {
            break;
        }

        parse_profile_block(profile_start, profile_end, &s_constants.profiles[parsed_count]);
        parsed_count++;
        cursor = profile_end + strlen("</Profile>");
    }

    if (parsed_count > 0) {
        s_constants.profile_count = parsed_count;
    }
}

/**
 * @brief Load and parse the embedded default constants database.
 *
 * @details Reads embedded `SystemConstants.xml` (global/system values) and
 * `Profiles.xml` (brew profile defaults), applies defaults, parses both files
 * into the in-memory snapshot, and logs the resulting metadata.
 *
 * @return
 *      - ESP_OK: Constants loaded successfully
 *      - ESP_ERR_INVALID_STATE: Embedded XML was unavailable
 */
esp_err_t system_constants_load(void)
{
    ESP_LOGI(TAG, "System constants load begin");
    ESP_LOGI(TAG, "System constants step: applying built-in defaults");
    system_constants_set_defaults();
    ESP_LOGI(TAG, "System constants step result: built-in defaults applied");

    const char *system_xml_start = SystemConstants_xml_start;
    const char *system_xml_end = SystemConstants_xml_end;
    const char *profiles_xml_start = Profiles_xml_start;
    const char *profiles_xml_end = Profiles_xml_end;
    ESP_LOGI(TAG, "System constants step: validating embedded XML pointers");
    ESP_RETURN_ON_FALSE(system_xml_start != NULL && system_xml_end != NULL && system_xml_end > system_xml_start,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Embedded SystemConstants.xml is unavailable");
    ESP_RETURN_ON_FALSE(profiles_xml_start != NULL && profiles_xml_end != NULL && profiles_xml_end > profiles_xml_start,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Embedded Profiles.xml is unavailable");
    ESP_LOGI(TAG, "System constants step result: embedded XML pointers valid");

    ESP_LOGI(TAG, "System constants step: parse global values");
    parse_global_values(system_xml_start, system_xml_end);
    ESP_LOGI(TAG, "System constants step result: parse global values complete");
    ESP_LOGI(TAG, "System constants step: parse profiles (Profiles.xml)");
    parse_profiles(profiles_xml_start, profiles_xml_end);
    ESP_LOGI(TAG, "System constants step result: parse profiles complete");
    ESP_LOGI(TAG, "System constants step: seed LCD dataset from current profile summary");
    system_constants_seed_lcd_dataset_from_summary();
    ESP_LOGI(TAG, "System constants step result: seed LCD dataset complete");
    ESP_LOGI(TAG, "System constants step: load LCD dataset override from TF");
    esp_err_t lcd_dataset_ret = system_constants_try_load_lcd_dataset_from_sd();
    if (lcd_dataset_ret == ESP_OK) {
        ESP_LOGI(TAG, "System constants step result: loaded LCD dataset from TF.");
    } else {
        ESP_LOGI(TAG, "System constants step result: no TF dataset override (%s).", esp_err_to_name(lcd_dataset_ret));
    }
    ESP_LOGI(TAG, "System constants step: load SD range overrides");
    system_constants_try_load_live_shot_overrides_from_sd();
    ESP_LOGI(TAG, "System constants step result: load SD range overrides complete");

    ESP_LOGI(TAG,
             "Loaded XML defaults: system=SystemConstants.xml profiles=Profiles.xml profiles=%d client=%s compatible=%s",
             s_constants.profile_count,
             s_constants.client_version,
             s_constants.compatible_client_version);
    ESP_LOGI(TAG, "System constants load end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Return the currently active constants snapshot.
 *
 * @details Exposes the last successfully loaded constants database to the rest
 * of the application.
 *
 * @return Pointer to the active constants snapshot.
 */
const system_constants_data_t *system_constants_get(void)
{
    return &s_constants;
}

/**
 * @brief Return the embedded raw XML text for the constants database.
 *
 * @details Exposes the default `SystemConstants.xml` contents so the UI can
 * present the exact XML data to the operator.
 *
 * @return Pointer to the embedded XML text buffer.
 */
const char *system_constants_get_xml_text(void)
{
    return SystemConstants_xml_start;
}

/**
 * @brief Return the embedded constants XML length.
 *
 * @details Calculates the byte span between the embedded start and end symbols
 * for the default `SystemConstants.xml` file.
 *
 * @return Embedded XML size in bytes.
 */
size_t system_constants_get_xml_length(void)
{
    return (size_t)(SystemConstants_xml_end - SystemConstants_xml_start);
}

esp_err_t system_constants_apply_lcd_dataset(const lcd_controller_dataset_t *dataset, bool persist_to_tf)
{
    if (dataset == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dataset->schema_version == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    s_constants.lcd_profile_dataset = *dataset;
    system_constants_sync_profile_summary_from_lcd_dataset();

    if (!persist_to_tf) {
        return ESP_OK;
    }

    return system_constants_save_lcd_dataset_to_sd();
}

const lcd_controller_dataset_t *system_constants_get_lcd_dataset(void)
{
    return &s_constants.lcd_profile_dataset;
}

esp_err_t system_constants_set_live_shot_ranges(float pressure_max_bar,
                                                float weight_max_g,
                                                float flow_max_ml_s,
                                                float temperature_max_c)
{
    if (!isfinite(pressure_max_bar) ||
        !isfinite(weight_max_g) ||
        !isfinite(flow_max_ml_s) ||
        !isfinite(temperature_max_c)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_constants.live_shot_pressure_max_bar = clampf_range(
        pressure_max_bar,
        LIVE_SHOT_PRESSURE_MIN_BAR,
        LIVE_SHOT_PRESSURE_MAX_BAR);
    s_constants.live_shot_weight_max_g = clampf_range(
        weight_max_g,
        LIVE_SHOT_WEIGHT_MIN_G,
        LIVE_SHOT_WEIGHT_MAX_G);
    s_constants.live_shot_flow_max_ml_s = clampf_range(
        flow_max_ml_s,
        LIVE_SHOT_FLOW_MIN_ML_S,
        LIVE_SHOT_FLOW_MAX_ML_S);
    s_constants.live_shot_temperature_max_c = clampf_range(
        temperature_max_c,
        LIVE_SHOT_TEMPERATURE_MIN_C,
        LIVE_SHOT_TEMPERATURE_MAX_C);
    return ESP_OK;
}

esp_err_t system_constants_set_live_shot_autoscale(bool autoscale_pressure,
                                                   bool autoscale_weight,
                                                   bool autoscale_flow,
                                                   bool autoscale_temperature)
{
    s_constants.live_shot_autoscale_pressure = autoscale_pressure;
    s_constants.live_shot_autoscale_weight = autoscale_weight;
    s_constants.live_shot_autoscale_flow = autoscale_flow;
    s_constants.live_shot_autoscale_temperature = autoscale_temperature;
    return ESP_OK;
}

esp_err_t system_constants_save_live_shot_ranges(void)
{
    FILE *f = fopen(SYSTEM_CONSTANTS_OVERRIDES_PATH, "w");
    if (f == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = fprintf(
        f,
        "live_shot_pressure_max_bar=%.1f\n"
        "live_shot_weight_max_g=%.1f\n"
        "live_shot_flow_max_ml_s=%.1f\n"
        "live_shot_temperature_max_c=%.1f\n"
        "live_shot_autoscale_pressure=%u\n"
        "live_shot_autoscale_weight=%u\n"
        "live_shot_autoscale_flow=%u\n"
        "live_shot_autoscale_temperature=%u\n",
        (double)s_constants.live_shot_pressure_max_bar,
        (double)s_constants.live_shot_weight_max_g,
        (double)s_constants.live_shot_flow_max_ml_s,
        (double)s_constants.live_shot_temperature_max_c,
        s_constants.live_shot_autoscale_pressure ? 1U : 0U,
        s_constants.live_shot_autoscale_weight ? 1U : 0U,
        s_constants.live_shot_autoscale_flow ? 1U : 0U,
        s_constants.live_shot_autoscale_temperature ? 1U : 0U);
    int flush_ret = fflush(f);
    int close_ret = fclose(f);
    if (written <= 0 || flush_ret != 0 || close_ret != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}
