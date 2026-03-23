/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_CONSTANTS_MAX_PROFILES (8)

/**
 * @brief One brew-profile entry loaded from the constants XML.
 *
 * @details Stores the default profile targets parsed from `SystemConstants.xml`
 * so the UI can present names and apply profile-specific defaults.
 */
typedef struct {
    char name[32];
    int target_temperature_c;
    int preinfusion_seconds;
    int target_pressure_tenths;
    int target_flow_tenths;
} system_constants_profile_t;

/**
 * @brief Parsed system constants database contents.
 *
 * @details Aggregates the currently loaded constants, including profile data,
 * process limits, connection details, and compatibility/version metadata.
 */
typedef struct {
    char client_version[16];
    char compatible_client_version[16];
    char connection_host[64];
    char connection_port[32];
    int connection_baud_rate;
    int profile_count;
    int temperature_min_c;
    int temperature_max_c;
    int pressure_min_tenths;
    int pressure_max_tenths;
    int flow_min_tenths;
    int flow_max_tenths;
    system_constants_profile_t profiles[SYSTEM_CONSTANTS_MAX_PROFILES];
} system_constants_data_t;

/**
 * @brief Load and parse the embedded `SystemConstants.xml` database.
 *
 * @details Reads the default XML database embedded in the firmware image and
 * updates the in-memory constants snapshot used by application startup and UI
 * code.
 *
 * @return
 *      - ESP_OK: Constants parsed successfully
 *      - ESP_ERR_*: XML database could not be loaded or parsed
 */
esp_err_t system_constants_load(void);

/**
 * @brief Access the currently loaded constants snapshot.
 *
 * @details Returns a pointer to the last successfully loaded constants data.
 * The returned pointer remains owned by the module.
 *
 * @return Pointer to the active constants snapshot.
 */
const system_constants_data_t *system_constants_get(void);

/**
 * @brief Access the embedded raw XML text for `SystemConstants.xml`.
 *
 * @details Returns a pointer to the embedded default XML database so UI code
 * can present the actual XML contents to the user when requested.
 *
 * @return Pointer to the embedded XML text.
 */
const char *system_constants_get_xml_text(void);

/**
 * @brief Get the embedded XML text length.
 *
 * @details Returns the byte length of the embedded `SystemConstants.xml`
 * content without relying on a NUL terminator assumption.
 *
 * @return Embedded XML text length in bytes.
 */
size_t system_constants_get_xml_length(void);

#ifdef __cplusplus
}
#endif
