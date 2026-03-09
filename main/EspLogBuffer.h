/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LOG_BUFFER_MAX_ENTRIES (1000)
#define ESP_LOG_BUFFER_LINE_MAX    (192)

/**
 * @brief One captured ESP log entry.
 *
 * @details Stores one formatted log line together with the parsed ESP log level
 * character so UI filters can operate without reparsing the text.
 */
typedef struct {
    char level;
    char text[ESP_LOG_BUFFER_LINE_MAX];
} esp_log_buffer_entry_t;

/**
 * @brief Install the global ESP log capture hook.
 *
 * @details Wraps the current ESP-IDF `vprintf` sink so formatted log lines are
 * mirrored into a fixed-size ring buffer while still printing to the console.
 *
 * @return
 *      - ESP_OK: Hook installed or already active
 */
esp_err_t esp_log_buffer_init(void);

/**
 * @brief Copy the latest captured ESP log entries in chronological order.
 *
 * @details Copies up to `max_entries` records from the internal ring buffer into
 * caller storage so the UI can freeze a snapshot and filter it locally.
 *
 * @param[out] out_entries Destination array for copied entries.
 * @param[in] max_entries Capacity of `out_entries`.
 * @param[out] out_count Number of entries copied.
 *
 * @return
 *      - ESP_OK: Snapshot copied successfully
 *      - ESP_ERR_INVALID_ARG: Output pointers are invalid
 */
esp_err_t esp_log_buffer_copy_entries(esp_log_buffer_entry_t *out_entries,
                                      size_t max_entries,
                                      size_t *out_count);

#ifdef __cplusplus
}
#endif
