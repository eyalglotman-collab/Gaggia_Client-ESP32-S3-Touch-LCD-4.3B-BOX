/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The 10-line bounce buffer matches the stable Waveshare display path for this board. */
#define HARDWARE_LCD_RGB_BOUNCE_BUFFER_HEIGHT (10)

/**
 * @brief Initialize LCD and touch hardware for ESP32-S3-Touch-LCD-4.3B.
 *
 * @details Initializes board display stack in sequence:
 * 1) I2C bus used by touch and CH422G IO expander
 * 2) CH422G output defaults and touch reset sequence
 * 3) RGB LCD panel (800x480)
 * 4) GT911 touch controller
 * Returns ready-to-use panel and touch handles to caller.
 *
 * @param[out] lcd_handle Pointer to LCD panel handle.
 * @param[out] tp_handle Pointer to touch panel handle.
 *
 * @return
 *      - ESP_OK: LCD initialized successfully (touch may be unavailable; `*tp_handle == NULL`)
 *      - ESP_ERR_*: Initialization failed
 */
esp_err_t hardware_init(esp_lcd_panel_handle_t *lcd_handle,
                        esp_lcd_touch_handle_t *tp_handle);

/**
 * @brief Probe an I2C address on the shared board bus.
 *
 * @details Uses ESP-IDF I2C probe transaction to detect presence of a device
 * address on the initialized shared I2C bus.
 *
 * @param[in] addr 7-bit I2C device address.
 *
 * @return
 *      - ESP_OK: Device acknowledged
 *      - ESP_ERR_INVALID_STATE: I2C bus not initialized
 *      - ESP_ERR_TIMEOUT / ESP_FAIL: Device did not acknowledge
 */
esp_err_t hardware_i2c_probe(uint8_t addr);

/**
 * @brief Write raw bytes to an I2C device on the shared board bus.
 *
 * @details Performs a plain transmit without implicit register addressing.
 * For register writes, include register byte(s) at the start of `data`.
 *
 * @param[in] addr 7-bit I2C device address.
 * @param[in] data Pointer to transmit buffer.
 * @param[in] len Number of bytes to transmit.
 *
 * @return
 *      - ESP_OK: Write succeeded
 *      - ESP_ERR_INVALID_STATE: I2C bus not initialized
 *      - ESP_ERR_INVALID_ARG: Invalid input arguments
 *      - ESP_ERR_*: Underlying I2C transaction failure
 */
esp_err_t hardware_i2c_write_raw(uint8_t addr, const uint8_t *data, size_t len);

/**
 * @brief Read raw bytes from an I2C device on the shared board bus.
 *
 * @details Performs a plain receive without register-address phase.
 *
 * @param[in] addr 7-bit I2C device address.
 * @param[out] data Pointer to receive buffer.
 * @param[in] len Number of bytes to receive.
 *
 * @return
 *      - ESP_OK: Read succeeded
 *      - ESP_ERR_INVALID_STATE: I2C bus not initialized
 *      - ESP_ERR_INVALID_ARG: Invalid input arguments
 *      - ESP_ERR_*: Underlying I2C transaction failure
 */
esp_err_t hardware_i2c_read_raw(uint8_t addr, uint8_t *data, size_t len);

/**
 * @brief Perform write-then-read transaction on shared board I2C bus.
 *
 * @details Typical usage is register access where `wdata` contains register
 * address bytes and `rdata` receives the register payload.
 *
 * @param[in] addr 7-bit I2C device address.
 * @param[in] wdata Pointer to write buffer.
 * @param[in] wlen Number of bytes to write.
 * @param[out] rdata Pointer to read buffer.
 * @param[in] rlen Number of bytes to read.
 *
 * @return
 *      - ESP_OK: Transaction succeeded
 *      - ESP_ERR_INVALID_STATE: I2C bus not initialized
 *      - ESP_ERR_INVALID_ARG: Invalid input arguments
 *      - ESP_ERR_*: Underlying I2C transaction failure
 */
esp_err_t hardware_i2c_write_read(uint8_t addr,
                                  const uint8_t *wdata,
                                  size_t wlen,
                                  uint8_t *rdata,
                                  size_t rlen);

#ifdef __cplusplus
}
#endif
