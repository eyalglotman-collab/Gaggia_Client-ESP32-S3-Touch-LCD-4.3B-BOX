/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "communication/DataStructuresLCD_Controller.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_CONSTANTS_MAX_PROFILES (LCD_CONTROLLER_MAX_PROFILES)

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
    float live_shot_pressure_max_bar;
    float live_shot_weight_max_g;
    float live_shot_flow_max_ml_s;
    float live_shot_temperature_max_c;
    bool live_shot_autoscale_pressure;
    bool live_shot_autoscale_weight;
    bool live_shot_autoscale_flow;
    bool live_shot_autoscale_temperature;
    system_constants_profile_t profiles[SYSTEM_CONSTANTS_MAX_PROFILES];
    lcd_controller_dataset_t lcd_profile_dataset;
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

/**
 * @brief Apply a full LCD profile/settings dataset and optionally persist it.
 *
 * @details Updates in-memory constants from protocol bootstrap data and writes
 * the dataset to TF (SD) when requested.
 *
 * @param[in] dataset Parsed full LCD dataset from protocol.
 * @param[in] persist_to_tf Save dataset to TF card when true.
 *
 * @return ESP_OK on success or an ESP_ERR_* code on validation/storage errors.
 */
esp_err_t system_constants_apply_lcd_dataset(const lcd_controller_dataset_t *dataset, bool persist_to_tf);

/**
 * @brief Return the last applied LCD dataset snapshot.
 *
 * @return Pointer to in-memory dataset owned by this module.
 */
const lcd_controller_dataset_t *system_constants_get_lcd_dataset(void);

/**
 * @brief Update in-memory Live Shot manual range limits.
 *
 * @details Applies range-clamped values to the active constants snapshot.
 * Values can later be persisted with `system_constants_save_live_shot_ranges`.
 *
 * @param[in] pressure_max_bar Pressure axis maximum in bar.
 * @param[in] weight_max_g Weight axis maximum in grams.
 * @param[in] flow_max_ml_s Flow axis maximum in ml/sec.
 * @param[in] temperature_max_c Temperature axis maximum in degC.
 *
 * @return ESP_OK on success or ESP_ERR_INVALID_ARG when any input is non-finite.
 */
esp_err_t system_constants_set_live_shot_ranges(float pressure_max_bar,
                                                float weight_max_g,
                                                float flow_max_ml_s,
                                                float temperature_max_c);

/**
 * @brief Update in-memory Live Shot autoscale enable flags.
 *
 * @param[in] autoscale_pressure Enable pressure autoscale when true.
 * @param[in] autoscale_weight Enable weight autoscale when true.
 * @param[in] autoscale_flow Enable flow autoscale when true.
 * @param[in] autoscale_temperature Enable temperature autoscale when true.
 *
 * @return ESP_OK on success.
 */
esp_err_t system_constants_set_live_shot_autoscale(bool autoscale_pressure,
                                                   bool autoscale_weight,
                                                   bool autoscale_flow,
                                                   bool autoscale_temperature);

/**
 * @brief Persist current Live Shot plot settings to SD card.
 *
 * @details Writes `/sdcard/SystemConstantsOverrides.ini` so range settings and
 * autoscale flags survive reboot and are reloaded by `system_constants_load`.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE when SD is unavailable
 *      - ESP_ERR_INVALID_RESPONSE when file write fails
 */
esp_err_t system_constants_save_live_shot_ranges(void);

#ifdef __cplusplus
}
#endif
