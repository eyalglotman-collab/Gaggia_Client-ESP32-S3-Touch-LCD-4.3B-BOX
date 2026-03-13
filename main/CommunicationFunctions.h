/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Low-level Wi-Fi transport state.
 *
 * @details Tracks the transport-side workflow used to prepare the ESP32-S3 for
 * framed TCP communication with the remote Wi-Fi server. The low-level link
 * uses the client-side topology `reset -> initialize -> connect ->
 * keepalive -> wait_for_com_reset`. Connection faults are tracked through a
 * separate latched flag, and repeated keepalive failures can force the task to
 * wait for an explicit operator reset.
 */
typedef enum {
    COMMUNICATION_STATE_RESET = 0,
    COMMUNICATION_STATE_INITIALIZE,
    COMMUNICATION_STATE_CONNECT,
    COMMUNICATION_STATE_KEEPALIVE,
    COMMUNICATION_STATE_WAIT_FOR_COM_RESET,
} communication_state_t;

/**
 * @brief Wi-Fi access-point scan workflow state.
 *
 * @details Tracks the dedicated operator-triggered device discovery workflow
 * used by the Connection Info screen. This scan state machine is independent of
 * the main transport connection state machine.
 */
typedef enum {
    COMMUNICATION_SCAN_STATE_IDLE = 0,
    COMMUNICATION_SCAN_STATE_REQUESTED,
    COMMUNICATION_SCAN_STATE_IN_PROGRESS,
    COMMUNICATION_SCAN_STATE_COMPLETE,
    COMMUNICATION_SCAN_STATE_ERROR,
} communication_scan_state_t;

/**
 * @brief Defaultable Wi-Fi server communication settings.
 *
 * @details Stores the station credentials and remote TCP endpoint required for
 * the low-level transport task. The task uses these values when a reset request
 * restarts the communication workflow.
 */
typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char server_ip[16];
    uint16_t server_port;
    uint32_t wifi_connect_timeout_ms;
    uint32_t tcp_connect_timeout_ms;
    uint32_t keep_alive_period_ms;
} communication_config_t;

/**
 * @brief Snapshot of current communication-task runtime state.
 *
 * @details Provides a UI-friendly summary of the current low-level state,
 * active defaults, TCP/Wi-Fi link readiness, frame counters, and the latest
 * keepalive/fault bookkeeping maintained by the communication task.
 */
typedef struct {
    communication_state_t state;
    communication_scan_state_t scan_state;
    communication_config_t config;
    bool wifi_has_ip;
    bool tcp_connected;
    bool initialize_passed;
    bool connect_passed;
    bool send_data_enabled;
    bool connection_fault;
    bool reset_requested;
    bool scan_requested;
    uint32_t server_live_integer;
    uint32_t client_live_integer;
    uint32_t consecutive_keepalive_failures;
    uint16_t sequence;
    uint32_t scan_duration_ms;
    uint16_t scan_device_count;
    int32_t wifi_rssi;
    char local_ip[16];
    char last_error[96];
    char last_received_text[160];
    char scan_results[640];
} communication_snapshot_t;

/**
 * @brief Initialize the communication task and load default settings.
 *
 * @details Creates the background task that owns the low-level Wi-Fi/TCP state
 * machine. The task starts in the `RESET` state and the worker owns the
 * automatic reset-to-keepalive sequencing once reset is requested.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: Module initialized successfully or was already initialized
 *      - ESP_ERR_NO_MEM: Task or synchronization primitives could not be created
 */
esp_err_t communication_functions_init(bool offline);

/**
 * @brief Request a transport reset and reconnect cycle.
 *
 * @details Schedules an asynchronous `reset -> initialize -> connect` sequence.
 * The actual Wi-Fi and TCP work is performed by the background task.
 */
void communication_functions_request_reset(void);

/**
 * @brief Request a transport stop/reset cycle.
 *
 * @details Preserved for compatibility with existing UI call sites. The client
 * transport no longer exposes a dedicated `disconnect` state, so this request
 * now maps to a reset-style teardown.
 */
void communication_functions_request_disconnect(void);

/**
 * @brief Request a Wi-Fi access-point scan from the communication task.
 *
 * @details Starts the dedicated scan state machine used by the Connection Info
 * overlay. The overlay can poll the snapshot API to show progress and the final
 * device list without blocking the UI thread.
 */
void communication_functions_request_scan(void);

/**
 * @brief Read the latest communication snapshot.
 *
 * @details Copies the current low-level transport snapshot for UI and logging
 * use without exposing internal mutable module state.
 *
 * @param[out] out_snapshot Destination snapshot structure.
 *
 * @return
 *      - ESP_OK: Snapshot copied successfully
 *      - ESP_ERR_INVALID_ARG: `out_snapshot` is NULL
 */
esp_err_t communication_functions_get_snapshot(communication_snapshot_t *out_snapshot);

/**
 * @brief Convert a communication state enum into printable text.
 *
 * @details Returns a constant string suitable for status labels and logs.
 *
 * @param[in] state Communication state value.
 *
 * @return Constant state name string.
 */
const char *communication_functions_state_to_string(communication_state_t state);

/**
 * @brief Convert a scan state enum into printable text.
 *
 * @details Returns a constant string suitable for UI status labels and logs.
 *
 * @param[in] state Scan state value.
 *
 * @return Constant scan state name string.
 */
const char *communication_functions_scan_state_to_string(communication_scan_state_t state);

#ifdef __cplusplus
}
#endif
