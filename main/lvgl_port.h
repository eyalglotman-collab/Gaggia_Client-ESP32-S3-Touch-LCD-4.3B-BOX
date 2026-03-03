/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * LVGL related parameters for ESP32-S3-Touch-LCD-4.3b-Box
 * Resolution: 480x480 IPS Touch Display
 */
#define LVGL_PORT_H_RES                 (480)
#define LVGL_PORT_V_RES                 (480)
#define LVGL_PORT_TICK_PERIOD_MS        (2)

/**
 * LVGL buffer configuration
 */
#define LVGL_PORT_BUFFER_HEIGHT         (100)
#define LVGL_PORT_TASK_MAX_DELAY_MS     (500)
#define LVGL_PORT_TASK_MIN_DELAY_MS     (1)
#define LVGL_PORT_TASK_STACK_SIZE       (4096)
#define LVGL_PORT_TASK_PRIORITY         (3)
#define LVGL_PORT_TASK_CORE             (-1)

/**
 * Avoid tearing configuration
 */
#define LVGL_PORT_AVOID_TEAR_ENABLE     (1)
#define LVGL_PORT_AVOID_TEAR_MODE       (3)  // RGB double-buffer & LVGL direct-mode
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE  (0)  // 0 degree
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#define LVGL_PORT_FULL_REFRESH          (0)

/**
 * @brief Initialize LVGL port
 *
 * @param[in] lcd_handle: LCD panel handle
 * @param[in] tp_handle: Touch panel handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - Others: Fail
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);

/**
 * @brief Take LVGL mutex
 *
 * @param[in] timeout_ms: Timeout in [ms]. 0 will block indefinitely.
 *
 * @return
 *      - true:  Mutex was taken
 *      - false: Mutex was NOT taken
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief Give LVGL mutex
 */
void lvgl_port_unlock(void);

#ifdef __cplusplus
}
#endif

