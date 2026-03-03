/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LCD and Touch hardware for ESP32-S3-Touch-LCD-4.3b-Box
 *
 * Initializes:
 * - RGB LCD Panel (480x480, 16-bit color)
 * - I2C bus for touch controller
 * - GT911 Capacitive Touch Controller
 * - Backlight control
 *
 * @param[out] lcd_handle: Pointer to LCD panel handle
 * @param[out] tp_handle: Pointer to touch panel handle
 *
 * @return
 *      - ESP_OK: Initialization successful
 *      - ESP_ERR_*: Initialization failed
 */
esp_err_t hardware_init(esp_lcd_panel_handle_t *lcd_handle, 
                        esp_lcd_touch_handle_t *tp_handle);

#ifdef __cplusplus
}
#endif

