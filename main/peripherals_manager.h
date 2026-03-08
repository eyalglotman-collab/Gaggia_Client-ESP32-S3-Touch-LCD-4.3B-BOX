/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief High-level result of controller initialization handshake.
 *
 * @details Captures whether the remote machine controller answered the client
 * initialization request and what the current coarse state is.
 */
typedef enum {
    PERIPHERALS_CONTROLLER_STATUS_UNKNOWN = 0,
    PERIPHERALS_CONTROLLER_STATUS_READY,
    PERIPHERALS_CONTROLLER_STATUS_BUSY,
    PERIPHERALS_CONTROLLER_STATUS_ERROR,
    PERIPHERALS_CONTROLLER_STATUS_OFFLINE,
} peripherals_controller_status_t;

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
 * @brief Initialize the RTC and only set it when retained time is invalid.
 *
 * @details Configures the PCF85063A control register, checks whether the RTC
 * still holds a valid retained time, and only falls back to the C compiler
 * `__DATE__` and `__TIME__` macros when the clock contents are invalid. This
 * preserves RTC-backed time across power cycles when backup power is present.
 *
 * @return
 *      - ESP_OK: RTC initialized and timestamp written successfully
 *      - ESP_ERR_*: RTC communication or timestamp parsing failed
 */
esp_err_t peripherals_manager_init_rtc_now(void);

/**
 * @brief Read the current RTC time into a `struct tm`.
 *
 * @details Fetches the current PCF85063A calendar registers, converts them
 * from BCD to standard calendar fields, and returns the decoded time.
 *
 * @param[out] out_tm Destination time structure.
 *
 * @return
 *      - ESP_OK: RTC time read successfully
 *      - ESP_ERR_INVALID_STATE: RTC has not been initialized yet
 *      - ESP_ERR_INVALID_ARG: `out_tm` is NULL
 *      - ESP_ERR_*: Underlying RTC read failed
 */
esp_err_t peripherals_manager_get_rtc_time(struct tm *out_tm);

/**
 * @brief Write a new calendar time into the RTC.
 *
 * @details Encodes a caller-provided `struct tm` into the PCF85063A register
 * format, writes the updated calendar values, and marks the RTC as ready for
 * subsequent reads.
 *
 * @param[in] new_tm New calendar time to store in the RTC.
 *
 * @return
 *      - ESP_OK: RTC time written successfully
 *      - ESP_ERR_INVALID_ARG: `new_tm` is NULL or contains out-of-range fields
 *      - ESP_ERR_*: Underlying RTC write failed
 */
esp_err_t peripherals_manager_set_rtc_time(const struct tm *new_tm);

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

/**
 * @brief Initialize Wi-Fi in station mode and run a scan test.
 *
 * @details Brings up NVS, TCP/IP stack, default event loop, and the ESP-IDF
 * Wi-Fi station interface. After startup it performs a blocking scan and logs
 * the number of visible access points as a radio-path verification step.
 *
 * @return
 *      - ESP_OK: Wi-Fi initialized and scan test completed successfully
 *      - ESP_ERR_*: Wi-Fi stack initialization or scan failed
 */
esp_err_t peripherals_manager_init_wifi(void);

/**
 * @brief Request initialization from the remote machine controller.
 *
 * @details Sends a short initialization request over the RS485 path used for
 * controller communications, waits for a textual response, and maps that
 * response to a coarse controller status. If no response arrives before the
 * timeout, the controller is reported as offline.
 *
 * @param[out] out_status Destination status value.
 *
 * @return
 *      - ESP_OK: Request sent and a controller status determined
 *      - ESP_ERR_INVALID_ARG: `out_status` is NULL
 *      - ESP_ERR_*: RS485 path could not be initialized or used
 */
esp_err_t peripherals_manager_request_controller_init(peripherals_controller_status_t *out_status);

/**
 * @brief Convert controller status enum to printable text.
 *
 * @details Returns a short constant string suitable for UI and log messages.
 *
 * @param[in] status Controller status enum value.
 *
 * @return Constant status text.
 */
const char *peripherals_manager_controller_status_to_string(peripherals_controller_status_t status);

#ifdef __cplusplus
}
#endif
