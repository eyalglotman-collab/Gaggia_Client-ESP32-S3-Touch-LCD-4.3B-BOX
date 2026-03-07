/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
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

/**
 * @brief Initialize and verify the TF card interface.
 *
 * @details Mounts the board TF/microSD card over SDSPI, logs card information,
 * and performs a basic write/read verification using the mounted filesystem.
 * This API is intended for normal application startup when only TF support is
 * required without enabling the other demo peripheral tasks.
 *
 * @return
 *      - ESP_OK: TF card mounted and basic read/write verification passed
 *      - ESP_ERR_*: TF card initialization or verification failed
 */
esp_err_t peripherals_manager_init_tf_card(void);

/**
 * @brief Report whether the TF card is currently mounted and ready.
 *
 * @details Returns the last known TF card readiness state maintained by the
 * peripheral manager module.
 *
 * @return
 *      - true: TF card mounted successfully
 *      - false: TF card not available
 */
bool peripherals_manager_is_tf_card_ready(void);

#ifdef __cplusplus
}
#endif
