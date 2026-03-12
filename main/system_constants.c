/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "system_constants.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "sys_constants";

extern const char SystemConstants_xml_start[] asm("_binary_SystemConstants_xml_start");
extern const char SystemConstants_xml_end[] asm("_binary_SystemConstants_xml_end");

static system_constants_data_t s_constants = {0};

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
    s_constants.profile_count = 3;
    s_constants.temperature_min_c = 86;
    s_constants.temperature_max_c = 98;
    s_constants.pressure_min_tenths = 0;
    s_constants.pressure_max_tenths = 120;
    s_constants.flow_min_tenths = 0;
    s_constants.flow_max_tenths = 50;

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
    if (xml_extract_text_range(xml_start, xml_end, "ProfileCount", value, sizeof(value))) {
        int count = atoi(value);
        if (count > 0 && count <= SYSTEM_CONSTANTS_MAX_PROFILES) {
            s_constants.profile_count = count;
        }
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
    int parsed_count = 0;

    while (parsed_count < SYSTEM_CONSTANTS_MAX_PROFILES) {
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

    if (parsed_count > 0 && parsed_count < s_constants.profile_count) {
        s_constants.profile_count = parsed_count;
    }
}

/**
 * @brief Load and parse the embedded default constants database.
 *
 * @details Reads the embedded `SystemConstants.xml` text, applies defaults,
 * parses the XML into the in-memory snapshot, and logs the resulting profile
 * and compatibility metadata.
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

    const char *xml_start = SystemConstants_xml_start;
    const char *xml_end = SystemConstants_xml_end;
    ESP_LOGI(TAG, "System constants step: validating embedded XML pointers");
    ESP_RETURN_ON_FALSE(xml_start != NULL && xml_end != NULL && xml_end > xml_start,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "Embedded SystemConstants.xml is unavailable");
    ESP_LOGI(TAG, "System constants step result: embedded XML pointers valid");

    ESP_LOGI(TAG, "System constants step: parse global values");
    parse_global_values(xml_start, xml_end);
    ESP_LOGI(TAG, "System constants step result: parse global values complete");
    ESP_LOGI(TAG, "System constants step: parse profiles");
    parse_profiles(xml_start, xml_end);
    ESP_LOGI(TAG, "System constants step result: parse profiles complete");

    ESP_LOGI(TAG,
             "Loaded SystemConstants.xml: profiles=%d client=%s compatible=%s",
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
