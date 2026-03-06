/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start all non-display board peripherals.
 *
 * @details Applies settings extracted from the Waveshare example set for:
 * I2C diagnostics, CH422G IO expander, RTC (PCF85063A), SD card over SPI,
 * RS485 UART, and TWAI bus. Non-critical peripheral failures are logged and
 * do not stop the application from running.
 *
 * @return
 *      - ESP_OK: Manager startup task created
 *      - ESP_ERR_*: Failed to create manager task
 */
esp_err_t peripherals_manager_start(void);

#ifdef __cplusplus
}
#endif
