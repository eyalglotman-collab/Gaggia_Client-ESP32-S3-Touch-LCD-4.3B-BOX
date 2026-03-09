/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "EspLogBuffer.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_previous_vprintf = NULL;
static esp_log_buffer_entry_t *s_log_entries = NULL;
static size_t s_log_head = 0;
static size_t s_log_count = 0;
static bool s_log_hook_installed = false;

/**
 * @brief Parse the leading ESP-IDF log level character from a formatted line.
 *
 * @details The ESP-IDF default log format starts with `I`, `W`, `E`, `D`, or
 * `V`. Unknown prefixes are tagged with `?`.
 *
 * @param[in] line Formatted log line.
 *
 * @return Parsed level character.
 */
static char esp_log_buffer_detect_level(const char *line)
{
    if (line == NULL || line[0] == '\0') {
        return '?';
    }

    switch (line[0]) {
    case 'I':
    case 'W':
    case 'E':
    case 'D':
    case 'V':
        return line[0];
    default:
        return '?';
    }
}

/**
 * @brief Capture formatted log lines into the fixed ring buffer.
 *
 * @details Mirrors every formatted log line into a local ring buffer before
 * forwarding the original log call to the previously installed sink.
 *
 * @param[in] format Printf-style log format string.
 * @param[in] args Variadic argument list.
 *
 * @return Number of characters printed by the previous sink, or the local line
 * length when no previous sink exists.
 */
static int esp_log_buffer_vprintf(const char *format, va_list args)
{
    char line[ESP_LOG_BUFFER_LINE_MAX];
    va_list args_copy;
    va_copy(args_copy, args);
    int line_len = vsnprintf(line, sizeof(line), format, args_copy);
    va_end(args_copy);

    if (line_len > 0) {
        portENTER_CRITICAL(&s_log_lock);
        esp_log_buffer_entry_t *entry = &s_log_entries[s_log_head];
        entry->level = esp_log_buffer_detect_level(line);
        snprintf(entry->text, sizeof(entry->text), "%s", line);
        s_log_head = (s_log_head + 1U) % ESP_LOG_BUFFER_MAX_ENTRIES;
        if (s_log_count < ESP_LOG_BUFFER_MAX_ENTRIES) {
            s_log_count++;
        }
        portEXIT_CRITICAL(&s_log_lock);
    }

    if (s_previous_vprintf != NULL) {
        return s_previous_vprintf(format, args);
    }

    return line_len;
}

esp_err_t esp_log_buffer_init(void)
{
    if (s_log_hook_installed) {
        return ESP_OK;
    }

    s_log_entries = heap_caps_calloc(ESP_LOG_BUFFER_MAX_ENTRIES,
                                     sizeof(*s_log_entries),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_log_entries == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_log_head = 0;
    s_log_count = 0;
    s_previous_vprintf = esp_log_set_vprintf(esp_log_buffer_vprintf);
    s_log_hook_installed = true;
    return ESP_OK;
}

esp_err_t esp_log_buffer_copy_entries(esp_log_buffer_entry_t *out_entries,
                                      size_t max_entries,
                                      size_t *out_count)
{
    if (out_entries == NULL || out_count == NULL || max_entries == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_log_entries == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    portENTER_CRITICAL(&s_log_lock);
    size_t copy_count = (s_log_count < max_entries) ? s_log_count : max_entries;
    size_t start_index = (s_log_count < max_entries)
                             ? ((s_log_head + ESP_LOG_BUFFER_MAX_ENTRIES - s_log_count) % ESP_LOG_BUFFER_MAX_ENTRIES)
                             : s_log_head;

    for (size_t index = 0; index < copy_count; index++) {
        size_t src_index = (start_index + index) % ESP_LOG_BUFFER_MAX_ENTRIES;
        out_entries[index] = s_log_entries[src_index];
    }
    portEXIT_CRITICAL(&s_log_lock);

    *out_count = copy_count;
    return ESP_OK;
}
