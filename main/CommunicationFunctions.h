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
 * @brief TopLayer communication state.
 *
 * @details Tracks the high-level communication workflow that wraps the
 * BottomLayer Wi-Fi/TCP reliability engine. The TopLayer state machine uses
 * `reset -> initialize -> connect -> keepalive_server_receive ->
 * keepalive_client_send -> error`.
 */
typedef enum {
    COMMUNICATION_STATE_TOP_LAYER_RESET = 0,
    COMMUNICATION_STATE_TOP_LAYER_INITIALIZE,
    COMMUNICATION_STATE_TOP_LAYER_CONNECT,
    COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE,
    COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND,
    COMMUNICATION_STATE_TOP_LAYER_ERROR,
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
 * @brief Data-channel control events sent from client UI to simulator runtime.
 *
 * @details These events are encoded as low-level DATA text payload commands
 * that the simulator parses to start or stop synthetic data generation.
 */
typedef enum {
    COMMUNICATION_DATA_EVENT_SIMULATION_ON = 0,
    COMMUNICATION_DATA_EVENT_SIMULATION_OFF,
    COMMUNICATION_DATA_EVENT_START_BREW,
} communication_data_event_t;

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
    uint32_t ka_wait_window_ms;
    uint32_t ka_empty_window_limit;
    uint32_t bottom_layer_retry_limit;
    uint32_t top_layer_failure_limit;
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
    bool auto_reconnect_enabled;
    bool connection_fault;
    bool reset_requested;
    bool scan_requested;
    uint32_t server_live_integer;
    uint32_t client_live_integer;
    uint32_t consecutive_keepalive_failures;
    uint32_t bottom_layer_retry_count;
    uint32_t top_layer_failure_count;
    uint32_t top_layer_connect_streak;
    uint32_t timeout_event_count;
    uint32_t reset_to_debug_elapsed_ms;
    uint32_t bottom_layer_checksum_error_count;
    uint32_t bottom_layer_sequence_error_count;
    uint16_t sequence;
    uint32_t scan_duration_ms;
    uint16_t scan_device_count;
    int32_t wifi_rssi;
    /* RF link quality */
    int8_t wifi_noise_floor_dbm;
    int16_t wifi_snr_estimate_db;
    uint8_t wifi_channel;
    uint8_t wifi_authmode;
    char wifi_bssid_str[18];
    /* Keepalive response timing */
    int32_t ka_response_time_last_ms;
    int32_t ka_response_time_max_ms;
    int32_t ka_response_time_min_ms;
    int32_t ka_jitter_ms;
    /* Traffic counters */
    uint32_t keepalive_rx_count;
    uint32_t keepalive_tx_count;
    uint32_t data_frames_rx_count;
    /* Session timing */
    uint32_t session_uptime_ms;
    char local_ip[16];
    char last_error[96];
    uint32_t last_received_text_event_count;
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
 * @brief Enable or disable automatic reconnect after retry-limit exhaustion.
 *
 * @details When enabled, the communication task resets retry counters and
 * re-enters `connect` automatically after three BottomLayer retries. When
 * disabled, the existing escalation path remains unchanged.
 *
 * @param[in] enabled `true` to enable Auto Reconnect, `false` to disable it.
 */
void communication_functions_set_auto_reconnect_enabled(bool enabled);

/**
 * @brief Queue one data-channel control event for the simulator runtime.
 *
 * @details Stores one pending DATA text command that is transmitted by the
 * background communication task once the keepalive session is active.
 *
 * @param[in] event_id Data control event to send.
 *
 * @return
 *      - ESP_OK: Event queued successfully
 *      - ESP_ERR_INVALID_ARG: `event_id` is not recognized
 *      - ESP_ERR_INVALID_STATE: Communication module is not initialized
 */
esp_err_t communication_functions_request_data_event(communication_data_event_t event_id);

/**
 * @brief Queue one raw DATA text command for simulator runtime handling.
 *
 * @details Stores one pending DATA payload string that is transmitted by the
 * communication task once keepalive is active. This is used for commands that
 * require runtime parameters (for example profile selection metadata).
 *
 * @param[in] payload_text DATA payload text to queue.
 *
 * @return
 *      - ESP_OK: Command queued successfully
 *      - ESP_ERR_INVALID_ARG: `payload_text` is NULL or empty
 *      - ESP_ERR_INVALID_STATE: Communication module is not initialized
 */
esp_err_t communication_functions_queue_data_text_command(const char *payload_text);

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
