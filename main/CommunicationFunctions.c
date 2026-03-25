/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CommunicationFunctions.h"
#include "data_payload.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "peripherals_manager.h"

#define COMMUNICATION_TASK_NAME              "comm_link"
#define COMMUNICATION_TASK_STACK_WORDS       (16384U)
#define COMMUNICATION_TASK_PRIORITY          (5)
#define COMMUNICATION_STEP_PERIOD_MS         (20)
#define COMMUNICATION_DEFAULT_WIFI_SSID      "EyalSimulatorAP"
#define COMMUNICATION_DEFAULT_WIFI_PASSWORD  "espresso1234"
#define COMMUNICATION_DEFAULT_SERVER_IP      "192.168.4.1"
#define COMMUNICATION_DEFAULT_SERVER_PORT    (3333)
#define COMMUNICATION_DEFAULT_WIFI_TIMEOUT   (1000)
#define COMMUNICATION_DEFAULT_TCP_TIMEOUT    (1000)
#define COMMUNICATION_DEFAULT_KEEPALIVE_MS   (300)
#define COMMUNICATION_WIFI_RETRY_PERIOD_MS   (2500)
#define COMMUNICATION_BOTTOM_LAYER_RETRY_LIMIT (3)
#define COMMUNICATION_TOP_LAYER_FAILURE_LIMIT  (3)
#define COMMUNICATION_SCAN_WINDOW_MS         (10000)
#define COMMUNICATION_SCAN_MAX_APS           (10)
#define COMMUNICATION_CONNECT_PRECHECK_APS   (16)
#define COMMUNICATION_KEEPALIVE_WAIT_WINDOW_MS (450)
#define COMMUNICATION_KEEPALIVE_EMPTY_WINDOW_LIMIT (3)
#define COMMUNICATION_KEEPALIVE_RESPONSE_ATTEMPT_LIMIT (1U)
#define COMMUNICATION_DATA_INTERFACE_VERSION (1U)
#define COMMUNICATION_API_LOCK_TIMEOUT_MS   (5U)
#define COMMUNICATION_DATA_EVENT_SIMULATION_ON_TEXT  "DataSimulationOn"
#define COMMUNICATION_DATA_EVENT_SIMULATION_OFF_TEXT "DataSimulationOFF"
#define COMMUNICATION_FRAME_SOF0             (0xA5U)
#define COMMUNICATION_FRAME_SOF1             (0x5AU)
#define COMMUNICATION_FRAME_HEADER_BYTES     (15U)
#define COMMUNICATION_FRAME_CRC_BYTES        (2U)
/* Must be >= sizeof(data_downlink_packet_t) and sizeof(data_uplink_packet_t).
 * Downlink: 1+4+(DATA_SIZE_FLOATS*4)+(DATA_SIZE_INT*4)+DATA_SIZE_STRING = 535 B
 * Uplink:   1+4+4+(DATA_SIZE_FLOATS*4)+(DATA_SIZE_INT*4)+DATA_SIZE_STRING = 539 B */
#define COMMUNICATION_FRAME_MAX_PAYLOAD      (2300U)
#define COMMUNICATION_RX_BUFFER_BYTES        (16384U)
#define COMMUNICATION_RX_BUFFER_MIN_BYTES    ((COMMUNICATION_FRAME_HEADER_BYTES + COMMUNICATION_FRAME_MAX_PAYLOAD + COMMUNICATION_FRAME_CRC_BYTES) * 2U)

typedef enum {
    COMMUNICATION_MESSAGE_RESET = 1,
    COMMUNICATION_MESSAGE_INITIALIZE = 2,
    COMMUNICATION_MESSAGE_CONNECT = 3,
    COMMUNICATION_MESSAGE_DISCONNECT = 4,
    COMMUNICATION_MESSAGE_KEEPALIVE = 5,
    COMMUNICATION_MESSAGE_ERROR = 6,
    COMMUNICATION_MESSAGE_ACK = 7,
    COMMUNICATION_MESSAGE_DATA = 8,
} communication_message_type_t;

static const char *TAG = "CommunicationFunctions";

/**
 * @brief Emit heap availability at critical initialization checkpoints.
 *
 * @details Helps pinpoint `ESP_ERR_NO_MEM` causes by showing free and largest
 * allocatable blocks in both internal RAM and SPIRAM.
 */
static void communication_log_heap_checkpoint(const char *step)
{
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,
             "Heap checkpoint (%s): internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u",
             (step != NULL) ? step : "n/a",
             (unsigned)internal_free,
             (unsigned)internal_largest,
             (unsigned)psram_free,
             (unsigned)psram_largest);
}

typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t snapshot_mutex;
    TaskHandle_t task_handle;
    communication_snapshot_t snapshot;
    communication_snapshot_t published_snapshot;
    bool initialized;
    bool reset_requested;
    bool disconnect_requested;
    bool wifi_connect_started;
    bool tcp_connect_started;
    bool keepalive_ack_pending;
    bool pending_data_command_valid;
    bool running_integer_seeded;
    bool fast_reset_skip_wifi;
    bool keepalive_window_has_message;
    bool session_id_valid;
    bool last_keepalive_request_valid;
    bool last_responded_keepalive_request_valid;
    bool duplicate_keepalive_replay_valid;
    int64_t connect_requested_us;
    int64_t last_valid_rx_us;
    int64_t last_wifi_connect_attempt_us;
    int64_t keepalive_window_started_us;
    int64_t reset_cycle_started_us;
    int64_t scan_started_us;
    uint32_t keepalive_empty_window_count;
    uint32_t bottom_layer_retry_count;
    uint32_t top_layer_failure_count;
    uint32_t active_session_id;
    uint32_t last_keepalive_request_id;
    uint32_t last_responded_keepalive_request_id;
    uint32_t duplicate_keepalive_replay_request_id;
    uint16_t last_rx_sequence;
    bool last_rx_sequence_valid;
    int socket_fd;
    int64_t state_started_us;
    int64_t last_keep_alive_us;
    int64_t keepalive_rx_us;
    int64_t session_established_us;
    bool ka_timing_valid;
    bool session_established;
    uint32_t keepalive_rx_count;
    uint32_t keepalive_tx_count;
    uint32_t data_frames_rx_count;
    int32_t ka_response_time_max_ms;
    int32_t ka_response_time_min_ms;
    size_t rx_buffer_len;
    size_t rx_buffer_capacity;
    uint8_t *rx_buffer;
    /* Shared scratch buffers keep large frame/payload arrays off task stack. */
    uint8_t frame_encode_buffer[COMMUNICATION_FRAME_HEADER_BYTES + COMMUNICATION_FRAME_MAX_PAYLOAD + COMMUNICATION_FRAME_CRC_BYTES];
    char scratch_payload[COMMUNICATION_FRAME_MAX_PAYLOAD + 1U];
    char pending_data_command[COMMUNICATION_FRAME_MAX_PAYLOAD + 1U];
} communication_context_t;

static communication_context_t s_comm = {
    .mutex = NULL,
    .snapshot_mutex = NULL,
    .task_handle = NULL,
    .snapshot = {0},
    .published_snapshot = {0},
    .initialized = false,
    .reset_requested = false,
    .disconnect_requested = false,
    .wifi_connect_started = false,
    .tcp_connect_started = false,
    .keepalive_ack_pending = false,
    .pending_data_command_valid = false,
    .running_integer_seeded = false,
    .fast_reset_skip_wifi = false,
    .keepalive_window_has_message = false,
    .session_id_valid = false,
    .last_keepalive_request_valid = false,
    .last_responded_keepalive_request_valid = false,
    .duplicate_keepalive_replay_valid = false,
    .connect_requested_us = 0,
    .last_valid_rx_us = 0,
    .last_wifi_connect_attempt_us = 0,
    .keepalive_window_started_us = 0,
    .reset_cycle_started_us = 0,
    .scan_started_us = 0,
    .keepalive_empty_window_count = 0,
    .bottom_layer_retry_count = 0,
    .top_layer_failure_count = 0,
    .active_session_id = 0,
    .last_keepalive_request_id = 0,
    .last_responded_keepalive_request_id = 0,
    .duplicate_keepalive_replay_request_id = 0,
    .last_rx_sequence = 0,
    .last_rx_sequence_valid = false,
    .socket_fd = -1,
    .state_started_us = 0,
    .last_keep_alive_us = 0,
    .keepalive_rx_us = 0,
    .session_established_us = 0,
    .ka_timing_valid = false,
    .session_established = false,
    .keepalive_rx_count = 0,
    .keepalive_tx_count = 0,
    .data_frames_rx_count = 0,
    .ka_response_time_max_ms = 0,
    .ka_response_time_min_ms = 0,
    .rx_buffer_len = 0,
    .rx_buffer_capacity = 0,
    .rx_buffer = NULL,
    .frame_encode_buffer = {0},
    .scratch_payload = {0},
    .pending_data_command = {0},
};

static void communication_close_socket_locked(void);
static void communication_enter_state_locked(communication_state_t next_state);
static void communication_schedule_bottom_layer_retry_locked(const char *reason_text, bool checksum_failure);
static void communication_record_top_layer_failure_locked(const char *reason_text);
static void communication_publish_snapshot_locked(void);
static bool communication_is_low_layer_ok_locked(void);
static void communication_start_keepalive_window_locked(bool clear_transport_buffer);
static void communication_mark_keepalive_window_message_locked(void);
static bool communication_reason_is_timeout(const char *reason_text);
static esp_err_t communication_send_pending_keepalive_response_locked(void);
static void communication_drain_uplink_fifo_locked(void);
static esp_err_t communication_queue_data_command_locked(const char *payload_text);
static esp_err_t communication_send_pending_data_command_locked(void);
static size_t communication_append_text(char *buffer, size_t buffer_len, size_t offset, const char *text);
static size_t communication_append_u32(char *buffer, size_t buffer_len, size_t offset, uint32_t value);
static bool communication_try_parse_u32_payload_value(const char *payload_text,
                                                      const char *key_text,
                                                      uint32_t *out_value);
static bool communication_payload_is_connect_success(const char *payload_text);
static void communication_build_keepalive_response_payload_locked(char *buffer,
                                                                  size_t buffer_len,
                                                                  bool include_metadata,
                                                                  uint32_t request_id);
static void communication_free_rx_buffer_locked(void);
static esp_err_t communication_alloc_rx_buffer_locked(void);

/**
 * @brief Format a precise TCP server availability error.
 *
 * @details Distinguishes server/listener problems from generic transport
 * failures using the configured IP/port and the last socket errno value.
 *
 * @param[in] socket_errno Last socket errno value.
 */
static void communication_set_tcp_server_not_found_error_locked(int socket_errno)
{
    snprintf(s_comm.snapshot.last_error,
             sizeof(s_comm.snapshot.last_error),
             "TCP server %s:%u not found or not listening (errno=%d)",
             s_comm.snapshot.config.server_ip,
             (unsigned)s_comm.snapshot.config.server_port,
             socket_errno);
}

/**
 * @brief Format a generic low-level transport failure.
 *
 * @details Used when the client can identify the failing stage but cannot
 * honestly prove a more specific root cause such as remote COM-port state.
 *
 * @param[in] stage_text Short stage label.
 * @param[in] detail_text Failure detail or error-name string.
 */
static void communication_set_generic_failure_locked(const char *stage_text, const char *detail_text)
{
    snprintf(s_comm.snapshot.last_error,
             sizeof(s_comm.snapshot.last_error),
             "%s failure: %s",
             (stage_text != NULL) ? stage_text : "Transport",
             (detail_text != NULL) ? detail_text : "Unknown");
}

/**
 * @brief Compute CRC16-CCITT over framed transport bytes.
 *
 * @details Uses the same integrity algorithm as the server simulator so both
 * sides can validate one canonical low-level frame format.
 *
 * @param[in] data Source bytes.
 * @param[in] length Number of source bytes.
 *
 * @return CRC16-CCITT value.
 */
static uint16_t communication_crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;

    for (size_t index = 0; index < length; index++) {
        crc ^= (uint16_t)data[index] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)(((uint16_t)(crc << 1)) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief Release the dynamic RX frame assembly buffer.
 */
static void communication_free_rx_buffer_locked(void)
{
    if (s_comm.rx_buffer != NULL) {
        heap_caps_free(s_comm.rx_buffer);
        s_comm.rx_buffer = NULL;
    }
    s_comm.rx_buffer_capacity = 0U;
    s_comm.rx_buffer_len = 0U;
}

/**
 * @brief Allocate RX frame assembly storage with SPIRAM preference.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NO_MEM on allocation failure
 */
static esp_err_t communication_alloc_rx_buffer_locked(void)
{
    size_t target_capacity = COMMUNICATION_RX_BUFFER_BYTES;

    if (s_comm.rx_buffer != NULL) {
        return ESP_OK;
    }

    while (target_capacity >= COMMUNICATION_RX_BUFFER_MIN_BYTES) {
        s_comm.rx_buffer = (uint8_t *)heap_caps_malloc(target_capacity,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_comm.rx_buffer == NULL) {
            s_comm.rx_buffer = (uint8_t *)heap_caps_malloc(target_capacity,
                                                            MALLOC_CAP_8BIT);
        }
        if (s_comm.rx_buffer != NULL) {
            break;
        }

        if (target_capacity == COMMUNICATION_RX_BUFFER_MIN_BYTES) {
            break;
        }
        target_capacity /= 2U;
        if (target_capacity < COMMUNICATION_RX_BUFFER_MIN_BYTES) {
            target_capacity = COMMUNICATION_RX_BUFFER_MIN_BYTES;
        }
    }

    if (s_comm.rx_buffer == NULL) {
        s_comm.rx_buffer_capacity = 0U;
        s_comm.rx_buffer_len = 0U;
        ESP_LOGE(TAG,
                 "RX buffer allocation failed (requested=%u min=%u)",
                 (unsigned)COMMUNICATION_RX_BUFFER_BYTES,
                 (unsigned)COMMUNICATION_RX_BUFFER_MIN_BYTES);
        return ESP_ERR_NO_MEM;
    }

    s_comm.rx_buffer_capacity = target_capacity;
    s_comm.rx_buffer_len = 0U;
    memset(s_comm.rx_buffer, 0, s_comm.rx_buffer_capacity);
    ESP_LOGI(TAG,
             "RX buffer allocated: capacity=%u bytes",
             (unsigned)s_comm.rx_buffer_capacity);
    return ESP_OK;
}

/**
 * @brief Reset framed-link bookkeeping while keeping configured defaults.
 *
 * @details Clears transient RX buffering and staged progression flags so the
 * next initialize sequence begins from a clean transport baseline. The live
 * keepalive integers remain server-authoritative and are not reset by the
 * client during reconnect flows.
 */
static void communication_clear_transport_flow_locked(void)
{
    s_comm.snapshot.initialize_passed = false;
    s_comm.snapshot.connect_passed = false;
    s_comm.snapshot.send_data_enabled = false;
    s_comm.snapshot.sequence = 0;
    s_comm.snapshot.top_layer_connect_streak = 0;
    s_comm.keepalive_ack_pending = false;
    s_comm.running_integer_seeded = false;
    s_comm.keepalive_window_has_message = false;
    s_comm.keepalive_window_started_us = 0;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.session_id_valid = false;
    s_comm.last_keepalive_request_valid = false;
    s_comm.last_responded_keepalive_request_valid = false;
    s_comm.duplicate_keepalive_replay_valid = false;
    s_comm.active_session_id = 0;
    s_comm.last_keepalive_request_id = 0;
    s_comm.last_responded_keepalive_request_id = 0;
    s_comm.duplicate_keepalive_replay_request_id = 0;
    s_comm.connect_requested_us = 0;
    s_comm.last_valid_rx_us = 0;
    s_comm.last_keep_alive_us = 0;
    if (s_comm.rx_buffer != NULL) {
        s_comm.rx_buffer_len = 0;
    }
    s_comm.last_rx_sequence = 0;
    s_comm.last_rx_sequence_valid = false;
    s_comm.snapshot.reset_to_debug_elapsed_ms = 0;
    s_comm.snapshot.last_received_text_event_count = 0;
    snprintf(s_comm.snapshot.last_received_text,
             sizeof(s_comm.snapshot.last_received_text),
             "No peer text received yet");
}

/**
 * @brief Start or restart the keepalive supervision deadline window.
 *
 * @details The client uses this timestamp as the start of one keepalive
 * deadline window (`COMMUNICATION_KEEPALIVE_WAIT_WINDOW_MS` milliseconds).
 * A valid keepalive request restarts the deadline.
 *
 * @param[in] clear_transport_buffer Legacy option to clear buffered RX bytes.
 */
static void communication_start_keepalive_window_locked(bool clear_transport_buffer)
{
    if (clear_transport_buffer && s_comm.rx_buffer != NULL) {
        s_comm.rx_buffer_len = 0;
    }

    s_comm.keepalive_window_started_us = esp_timer_get_time();
}

/**
 * @brief Record one valid keepalive reception.
 *
 * @details Resets timeout counters and restarts the configured keepalive
 * supervision deadline from the current moment.
 */
static void communication_mark_keepalive_window_message_locked(void)
{
    s_comm.keepalive_window_has_message = true;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.keepalive_window_started_us = esp_timer_get_time();
}

/**
 * @brief Clear the latched connection fault after a verified keepalive success.
 *
 * @details The connection-fault indicator remains active across reconnect
 * attempts until the client receives a valid keepalive from the server again.
 */
static void communication_clear_connection_fault_locked(void)
{
    if (s_comm.snapshot.connection_fault) {
        ESP_LOGI(TAG, "Connection fault cleared after successful keepalive");
    }

    s_comm.snapshot.connection_fault = false;
    s_comm.snapshot.consecutive_keepalive_failures = 0;
    s_comm.top_layer_failure_count = 0;
    s_comm.snapshot.top_layer_failure_count = 0;
}

/**
 * @brief Publish the latest internal snapshot for lock-independent UI reads.
 *
 * @details Copies the communication state into a dedicated published snapshot
 * buffer protected by `snapshot_mutex`, allowing UI polling to remain
 * responsive even when the communication worker holds `mutex` during blocking
 * transport operations.
 */
static void communication_publish_snapshot_locked(void)
{
    if (s_comm.snapshot_mutex == NULL) {
        return;
    }

    if (s_comm.session_established && s_comm.session_established_us > 0) {
        s_comm.snapshot.session_uptime_ms =
            (uint32_t)((esp_timer_get_time() - s_comm.session_established_us) / 1000LL);
    }

    if (xSemaphoreTake(s_comm.snapshot_mutex, portMAX_DELAY) == pdTRUE) {
        s_comm.published_snapshot = s_comm.snapshot;
        xSemaphoreGive(s_comm.snapshot_mutex);
    }
}

/**
 * @brief Append plain text into a bounded payload buffer.
 *
 * @details Writes as much text as fits, always preserving NUL termination when
 * the destination buffer length is non-zero. The returned offset reflects the
 * virtual untruncated length so additional truncation checks can be inferred by
 * callers without risking format warnings.
 *
 * @param[out] buffer Destination buffer.
 * @param[in] buffer_len Destination buffer size in bytes.
 * @param[in] offset Current write offset.
 * @param[in] text NUL-terminated text to append.
 *
 * @return Virtual offset after the append attempt.
 */
static size_t communication_append_text(char *buffer, size_t buffer_len, size_t offset, const char *text)
{
    size_t text_len = 0;
    size_t copy_len = 0;

    if (text == NULL) {
        text = "";
    }

    text_len = strlen(text);
    if (buffer_len > 0U && offset < (buffer_len - 1U)) {
        copy_len = buffer_len - 1U - offset;
        if (copy_len > text_len) {
            copy_len = text_len;
        }
        memcpy(&buffer[offset], text, copy_len);
        buffer[offset + copy_len] = '\0';
    } else if (buffer_len > 0U) {
        buffer[buffer_len - 1U] = '\0';
    }

    return offset + text_len;
}

/**
 * @brief Append one unsigned integer into a bounded payload buffer.
 *
 * @details Formats the integer into a small temporary buffer first so the
 * shared initialize payload can be assembled without a monolithic `snprintf`
 * that triggers truncation warnings under `-Werror`.
 *
 * @param[out] buffer Destination buffer.
 * @param[in] buffer_len Destination buffer size in bytes.
 * @param[in] offset Current write offset.
 * @param[in] value Integer value to append.
 *
 * @return Virtual offset after the append attempt.
 */
static size_t communication_append_u32(char *buffer, size_t buffer_len, size_t offset, uint32_t value)
{
    char value_text[16] = {0};

    snprintf(value_text, sizeof(value_text), "%" PRIu32, value);
    return communication_append_text(buffer, buffer_len, offset, value_text);
}

/**
 * @brief Parse one unsigned payload field formatted as `key=value`.
 *
 * @details The parser accepts semicolon-delimited key/value tokens and returns
 * `false` for missing or malformed fields.
 *
 * @param[in] payload_text NUL-terminated payload text.
 * @param[in] key_text Field key without the equals sign.
 * @param[out] out_value Parsed unsigned integer value.
 *
 * @return `true` when the field exists and parses cleanly; otherwise `false`.
 */
static bool communication_try_parse_u32_payload_value(const char *payload_text,
                                                      const char *key_text,
                                                      uint32_t *out_value)
{
    size_t key_len = 0U;
    const char *token = NULL;
    const char *value_start = NULL;
    char *end_ptr = NULL;
    unsigned long parsed_value = 0UL;

    if (payload_text == NULL || key_text == NULL || out_value == NULL) {
        return false;
    }

    key_len = strlen(key_text);
    if (key_len == 0U) {
        return false;
    }

    token = payload_text;
    while ((token = strstr(token, key_text)) != NULL) {
        bool token_start_ok = (token == payload_text) || (*(token - 1) == ';');
        if (!token_start_ok || token[key_len] != '=') {
            token += key_len;
            continue;
        }

        value_start = token + key_len + 1U;
        parsed_value = strtoul(value_start, &end_ptr, 10);
        if (end_ptr == value_start || parsed_value > UINT32_MAX) {
            return false;
        }
        if (*end_ptr != '\0' && *end_ptr != ';') {
            return false;
        }

        *out_value = (uint32_t)parsed_value;
        return true;
    }

    return false;
}

/**
 * @brief Detect a connect-success payload from the bridge.
 *
 * @details Connect acknowledgements may include additional metadata (for
 * example `sid=...`), so this matcher uses prefix checks instead of strict
 * full-string equality. Low-level transport readiness notices (for example
 * `tcp_connected`) are intentionally excluded because they occur before the
 * TopLayer handshake is complete.
 *
 * @param[in] payload_text NUL-terminated payload text.
 *
 * @return `true` when the payload indicates connect success; otherwise `false`.
 */
static bool communication_payload_is_connect_success(const char *payload_text)
{
    if (payload_text == NULL || payload_text[0] == '\0') {
        return false;
    }

    return (strncmp(payload_text, "client_connected", strlen("client_connected")) == 0) ||
           (strncmp(payload_text, "connect_success", strlen("connect_success")) == 0);
}

/**
 * @brief Build one keepalive response payload with correlation metadata.
 *
 * @details When a session and request id are known, the response echoes both so
 * the bridge can reject stale frames without resetting the integer sequence.
 *
 * @param[out] buffer Destination payload buffer.
 * @param[in] buffer_len Destination buffer length in bytes.
 * @param[in] include_metadata Whether to encode `sid`/`req`.
 * @param[in] request_id Keepalive request id to echo when metadata is enabled.
 */
static void communication_build_keepalive_response_payload_locked(char *buffer,
                                                                  size_t buffer_len,
                                                                  bool include_metadata,
                                                                  uint32_t request_id)
{
    if (buffer == NULL || buffer_len == 0U) {
        return;
    }

    if (include_metadata && s_comm.session_id_valid) {
        snprintf(buffer,
                 buffer_len,
                 "ka_resp;sid=%" PRIu32 ";req=%" PRIu32 ";rver=%" PRIu32,
                 s_comm.active_session_id,
                 request_id,
                 (uint32_t)COMMUNICATION_DATA_INTERFACE_VERSION);
        return;
    }

    snprintf(buffer, buffer_len, "keepalive;rver=%" PRIu32, (uint32_t)COMMUNICATION_DATA_INTERFACE_VERSION);
}

/**
 * @brief Build the framed initialize payload using the active defaults.
 *
 * @details Mirrors the server simulator key-value transport contract so the
 * ESP32-S3 client emits the same config payload shape during initialize.
 *
 * @param[out] buffer Destination text buffer.
 * @param[in] buffer_len Destination buffer length.
 */
static void communication_build_initialize_payload_locked(char *buffer, size_t buffer_len)
{
    size_t offset = 0;

    if (buffer_len == 0U) {
        return;
    }

    buffer[0] = '\0';
    offset = communication_append_text(buffer, buffer_len, offset, "ssid=");
    offset = communication_append_text(buffer, buffer_len, offset, s_comm.snapshot.config.wifi_ssid);
    offset = communication_append_text(buffer, buffer_len, offset, ";password=");
    offset = communication_append_text(buffer, buffer_len, offset, s_comm.snapshot.config.wifi_password);
    offset = communication_append_text(buffer, buffer_len, offset, ";server_ip=");
    offset = communication_append_text(buffer, buffer_len, offset, s_comm.snapshot.config.server_ip);
    offset = communication_append_text(buffer, buffer_len, offset, ";server_port=");
    offset = communication_append_u32(buffer, buffer_len, offset, s_comm.snapshot.config.server_port);
    offset = communication_append_text(buffer, buffer_len, offset, ";wifi_timeout_ms=");
    offset = communication_append_u32(buffer, buffer_len, offset, s_comm.snapshot.config.wifi_connect_timeout_ms);
    offset = communication_append_text(buffer, buffer_len, offset, ";tcp_timeout_ms=");
    offset = communication_append_u32(buffer, buffer_len, offset, s_comm.snapshot.config.tcp_connect_timeout_ms);
    offset = communication_append_text(buffer, buffer_len, offset, ";keepalive_ms=");
    offset = communication_append_u32(buffer, buffer_len, offset, s_comm.snapshot.config.keep_alive_period_ms);
    offset = communication_append_text(buffer, buffer_len, offset, ";data_ver=");
    (void)communication_append_u32(buffer, buffer_len, offset, COMMUNICATION_DATA_INTERFACE_VERSION);
}

/**
 * @brief Send one framed low-level message over the active TCP socket.
 *
 * @details Serializes the shared framed transport contract: SOF, message type,
 * payload length, host/device counters, sequence, payload, and CRC16.
 *
 * @param[in] message_type Low-level transport message type.
 * @param[in] payload_text Optional UTF-8 payload text.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL when the socket write fails
 */
/**
 * @brief Low-level binary-capable frame encoder and sender.
 *
 * @details Accepts raw bytes for the payload so both text control frames and
 * binary data payload frames can share the same encoding path.
 *
 * @param[in] message_type Frame message type.
 * @param[in] payload      Payload bytes (may contain null bytes for binary).
 * @param[in] payload_len  Payload length in bytes.
 *
 * @return ESP_OK on success, ESP_FAIL on send error.
 */
static esp_err_t communication_send_raw_frame_locked(communication_message_type_t message_type,
                                                      const uint8_t *payload,
                                                      size_t payload_len)
{
    uint8_t *frame = s_comm.frame_encode_buffer;
    size_t frame_len = 0U;

    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        communication_set_generic_failure_locked("Frame send", "Socket is not connected");
        return ESP_FAIL;
    }

    if (payload_len > COMMUNICATION_FRAME_MAX_PAYLOAD) {
        payload_len = COMMUNICATION_FRAME_MAX_PAYLOAD;
    }

    frame[0] = COMMUNICATION_FRAME_SOF0;
    frame[1] = COMMUNICATION_FRAME_SOF1;
    frame[2] = (uint8_t)message_type;
    frame[3] = (uint8_t)(payload_len & 0xFFU);
    frame[4] = (uint8_t)((payload_len >> 8) & 0xFFU);
    frame[5] = (uint8_t)(s_comm.snapshot.server_live_integer & 0xFFU);
    frame[6] = (uint8_t)((s_comm.snapshot.server_live_integer >> 8) & 0xFFU);
    frame[7] = (uint8_t)((s_comm.snapshot.server_live_integer >> 16) & 0xFFU);
    frame[8] = (uint8_t)((s_comm.snapshot.server_live_integer >> 24) & 0xFFU);
    frame[9] = (uint8_t)(s_comm.snapshot.client_live_integer & 0xFFU);
    frame[10] = (uint8_t)((s_comm.snapshot.client_live_integer >> 8) & 0xFFU);
    frame[11] = (uint8_t)((s_comm.snapshot.client_live_integer >> 16) & 0xFFU);
    frame[12] = (uint8_t)((s_comm.snapshot.client_live_integer >> 24) & 0xFFU);
    s_comm.snapshot.sequence = (uint16_t)((s_comm.snapshot.sequence + 1U) & 0xFFFFU);
    frame[13] = (uint8_t)(s_comm.snapshot.sequence & 0xFFU);
    frame[14] = (uint8_t)((s_comm.snapshot.sequence >> 8) & 0xFFU);

    if (payload != NULL && payload_len > 0U) {
        memcpy(&frame[COMMUNICATION_FRAME_HEADER_BYTES], payload, payload_len);
    }

    frame_len = COMMUNICATION_FRAME_HEADER_BYTES + payload_len + COMMUNICATION_FRAME_CRC_BYTES;
    uint16_t crc = communication_crc16_ccitt(frame, frame_len - COMMUNICATION_FRAME_CRC_BYTES);
    frame[frame_len - 2U] = (uint8_t)(crc & 0xFFU);
    frame[frame_len - 1U] = (uint8_t)((crc >> 8) & 0xFFU);

    int bytes_sent = send(s_comm.socket_fd, frame, frame_len, 0);
    if (bytes_sent != (int)frame_len) {
        communication_set_generic_failure_locked("Frame send", strerror(errno));
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t communication_send_frame_locked(communication_message_type_t message_type, const char *payload_text)
{
    const char *safe = (payload_text != NULL) ? payload_text : "";
    size_t len = strnlen(safe, COMMUNICATION_FRAME_MAX_PAYLOAD);
    return communication_send_raw_frame_locked(message_type, (const uint8_t *)safe, len);
}

/**
 * @brief Consume one validated framed packet from the TCP stream.
 *
 * @details Parses the shared framed protocol from the non-blocking socket and
 * updates client-side state progression, counters, and last received text.
 */
static void communication_poll_received_frames_locked(void)
{
    uint8_t temp[1024];
    char *payload_text = s_comm.scratch_payload;

    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        return;
    }
    if (s_comm.rx_buffer == NULL || s_comm.rx_buffer_capacity == 0U) {
        communication_set_generic_failure_locked("RX parser", "RX buffer unavailable");
        communication_schedule_bottom_layer_retry_locked("RX buffer unavailable", false);
        return;
    }

    while (true) {
        int bytes_read = recv(s_comm.socket_fd, temp, sizeof(temp), MSG_DONTWAIT);
        if (bytes_read > 0) {
            size_t copy_len = (size_t)bytes_read;
            if ((s_comm.rx_buffer_len + copy_len) > s_comm.rx_buffer_capacity) {
                ESP_LOGW(TAG,
                         "RX buffer overflow (%u + %u > %u), clearing buffered frames",
                         (unsigned)s_comm.rx_buffer_len,
                         (unsigned)copy_len,
                         (unsigned)s_comm.rx_buffer_capacity);
                s_comm.rx_buffer_len = 0U;
            }
            memcpy(&s_comm.rx_buffer[s_comm.rx_buffer_len], temp, copy_len);
            s_comm.rx_buffer_len += copy_len;
        } else {
            if (bytes_read == 0) {
                communication_schedule_bottom_layer_retry_locked("TCP peer closed socket", false);
            }
            break;
        }
    }

    while (s_comm.rx_buffer_len >= (COMMUNICATION_FRAME_HEADER_BYTES + COMMUNICATION_FRAME_CRC_BYTES)) {
        size_t sof_index = 0U;
        while (sof_index + 1U < s_comm.rx_buffer_len) {
            if (s_comm.rx_buffer[sof_index] == COMMUNICATION_FRAME_SOF0 &&
                s_comm.rx_buffer[sof_index + 1U] == COMMUNICATION_FRAME_SOF1) {
                break;
            }
            sof_index++;
        }

        if (sof_index > 0U) {
            memmove(s_comm.rx_buffer, &s_comm.rx_buffer[sof_index], s_comm.rx_buffer_len - sof_index);
            s_comm.rx_buffer_len -= sof_index;
        }

        if (s_comm.rx_buffer_len < (COMMUNICATION_FRAME_HEADER_BYTES + COMMUNICATION_FRAME_CRC_BYTES)) {
            return;
        }

        size_t payload_len = (size_t)s_comm.rx_buffer[3] | ((size_t)s_comm.rx_buffer[4] << 8);
        size_t frame_len = COMMUNICATION_FRAME_HEADER_BYTES + payload_len + COMMUNICATION_FRAME_CRC_BYTES;
        if (payload_len > COMMUNICATION_FRAME_MAX_PAYLOAD) {
            communication_schedule_bottom_layer_retry_locked("RX payload too large", false);
            return;
        }
        if (s_comm.rx_buffer_len < frame_len) {
            return;
        }

        uint16_t expected_crc = (uint16_t)s_comm.rx_buffer[frame_len - 2U] |
                                ((uint16_t)s_comm.rx_buffer[frame_len - 1U] << 8);
        uint16_t actual_crc = communication_crc16_ccitt(s_comm.rx_buffer, frame_len - COMMUNICATION_FRAME_CRC_BYTES);
        if (expected_crc != actual_crc) {
            communication_schedule_bottom_layer_retry_locked("CRC mismatch", true);
            return;
        }

        communication_message_type_t message_type = (communication_message_type_t)s_comm.rx_buffer[2];
        uint32_t received_server_live_integer =
            (uint32_t)s_comm.rx_buffer[5] |
            ((uint32_t)s_comm.rx_buffer[6] << 8) |
            ((uint32_t)s_comm.rx_buffer[7] << 16) |
            ((uint32_t)s_comm.rx_buffer[8] << 24);
        uint32_t received_client_live_integer =
            (uint32_t)s_comm.rx_buffer[9] |
            ((uint32_t)s_comm.rx_buffer[10] << 8) |
            ((uint32_t)s_comm.rx_buffer[11] << 16) |
            ((uint32_t)s_comm.rx_buffer[12] << 24);
        uint16_t received_sequence =
            (uint16_t)s_comm.rx_buffer[13] |
            ((uint16_t)s_comm.rx_buffer[14] << 8);
        s_comm.last_valid_rx_us = esp_timer_get_time();

        if (s_comm.last_rx_sequence_valid &&
            (message_type == COMMUNICATION_MESSAGE_KEEPALIVE ||
             message_type == COMMUNICATION_MESSAGE_DATA) &&
            (s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE ||
             s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND)) {
            uint16_t expected_sequence = (uint16_t)(s_comm.last_rx_sequence + 1U);
            if (received_sequence != expected_sequence) {
                s_comm.snapshot.bottom_layer_sequence_error_count++;
                communication_schedule_bottom_layer_retry_locked("BottomLayer frame sequence mismatch", false);
                return;
            }
        }
        s_comm.last_rx_sequence = received_sequence;
        s_comm.last_rx_sequence_valid = true;

        payload_text[0] = '\0';
        if (payload_len > 0U) {
            memcpy(payload_text, &s_comm.rx_buffer[COMMUNICATION_FRAME_HEADER_BYTES], payload_len);
            payload_text[payload_len] = '\0';
        }

        if (message_type == COMMUNICATION_MESSAGE_ERROR) {
            communication_schedule_bottom_layer_retry_locked(
                (payload_text[0] != '\0') ? payload_text : "Peer reported error",
                false);
            return;
        }

        if (message_type == COMMUNICATION_MESSAGE_DATA) {
            bool in_keepalive_phase =
                (s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE) ||
                (s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND);
            bool connect_success_payload =
                (s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_CONNECT) &&
                communication_payload_is_connect_success(payload_text);
            bool binary_downlink_payload =
                (payload_len == sizeof(data_downlink_packet_t)) &&
                ((uint8_t)s_comm.rx_buffer[COMMUNICATION_FRAME_HEADER_BYTES] == DATA_PAYLOAD_MAGIC_DOWNLINK);

            if (!in_keepalive_phase && !connect_success_payload) {
                /* Ignore asynchronous pre-keepalive DATA frames instead of
                 * tearing down the session; this keeps connect/keepalive timing
                 * stable when bridge traffic overlaps state transitions.
                 */
                if (!binary_downlink_payload) {
                    s_comm.snapshot.last_received_text_event_count++;
                    snprintf(s_comm.snapshot.last_received_text,
                             sizeof(s_comm.snapshot.last_received_text),
                             "%s",
                             (payload_text[0] != '\0') ? payload_text : "Empty peer payload");
                }
                ESP_LOGW(TAG,
                         "TopLayer DATA received before KeepAlive state ignored: state=%s",
                         communication_functions_state_to_string(s_comm.snapshot.state));
            } else if (in_keepalive_phase) {
                s_comm.snapshot.send_data_enabled = true;
                s_comm.data_frames_rx_count++;
                s_comm.snapshot.data_frames_rx_count = s_comm.data_frames_rx_count;

                /* Binary downlink data packet — route to FIFO for app consumption. */
                if (binary_downlink_payload) {
                    data_downlink_push((const data_downlink_packet_t *)(const void *)
                                       &s_comm.rx_buffer[COMMUNICATION_FRAME_HEADER_BYTES]);
                } else {
                    /* Text control frame: update last_received_text as before. */
                    s_comm.snapshot.last_received_text_event_count++;
                    snprintf(s_comm.snapshot.last_received_text,
                             sizeof(s_comm.snapshot.last_received_text),
                             "%s",
                             (payload_text[0] != '\0') ? payload_text : "Empty peer payload");
                }
            }
        }

        if (s_comm.snapshot.state == COMMUNICATION_STATE_TOP_LAYER_CONNECT &&
            communication_payload_is_connect_success(payload_text)) {
            uint32_t connect_session_id = 0U;
            s_comm.snapshot.connect_passed = true;
            s_comm.snapshot.send_data_enabled = false;
            s_comm.keepalive_ack_pending = false;
            s_comm.running_integer_seeded = false;
            if (communication_try_parse_u32_payload_value(payload_text, "sid", &connect_session_id)) {
                s_comm.active_session_id = connect_session_id;
                s_comm.session_id_valid = true;
                s_comm.last_keepalive_request_valid = false;
                s_comm.last_responded_keepalive_request_valid = false;
                s_comm.duplicate_keepalive_replay_valid = false;
                ESP_LOGI(TAG, "TopLayer connect acknowledged for session=%" PRIu32, connect_session_id);
            }
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE);
        }

        if (message_type == COMMUNICATION_MESSAGE_KEEPALIVE) {
            uint32_t payload_session_id = 0U;
            uint32_t payload_request_id = 0U;
            bool has_session_id = communication_try_parse_u32_payload_value(payload_text, "sid", &payload_session_id);
            bool has_request_id = communication_try_parse_u32_payload_value(payload_text, "req", &payload_request_id);
            bool duplicate_request = false;
            bool stale_request = false;
            bool valid_keepalive_request = false;
            uint32_t expected_server_live_integer = s_comm.snapshot.client_live_integer + 1U;
            uint32_t expected_client_live_integer = s_comm.snapshot.client_live_integer;

            if (has_session_id &&
                (!s_comm.session_id_valid || s_comm.active_session_id != payload_session_id)) {
                if (s_comm.session_id_valid &&
                    payload_session_id < s_comm.active_session_id &&
                    s_comm.snapshot.state != COMMUNICATION_STATE_TOP_LAYER_CONNECT) {
                    stale_request = true;
                    ESP_LOGW(TAG,
                             "TopLayer stale session keepalive ignored: sid=%" PRIu32
                             " < active=%" PRIu32,
                             payload_session_id,
                             s_comm.active_session_id);
                } else {
                    ESP_LOGI(TAG,
                             "TopLayer session synchronized to server session=%" PRIu32,
                             payload_session_id);
                    s_comm.active_session_id = payload_session_id;
                    s_comm.session_id_valid = true;
                    s_comm.last_keepalive_request_valid = false;
                    s_comm.last_responded_keepalive_request_valid = false;
                    s_comm.duplicate_keepalive_replay_valid = false;
                    s_comm.running_integer_seeded = false;
                }
            }

            if (has_request_id && s_comm.last_keepalive_request_valid) {
                if (payload_request_id < s_comm.last_keepalive_request_id) {
                    stale_request = true;
                } else if (payload_request_id == s_comm.last_keepalive_request_id) {
                    duplicate_request = true;
                }
            }

            if (stale_request) {
                ESP_LOGW(TAG,
                         "TopLayer stale keepalive ignored: req=%" PRIu32
                         " < last=%" PRIu32,
                         payload_request_id,
                         s_comm.last_keepalive_request_id);
            } else if (duplicate_request) {
                communication_mark_keepalive_window_message_locked();
                ESP_LOGW(TAG,
                         "TopLayer duplicate keepalive request detected: req=%" PRIu32
                         " (single-shot response mode: no resend)",
                         payload_request_id);
            } else {
                if ((received_server_live_integer & 1U) != 0U ||
                    (received_client_live_integer & 1U) == 0U) {
                    ESP_LOGW(TAG,
                             "TopLayer keepalive ignored due to parity mismatch: server=%" PRIu32
                             " client=%" PRIu32,
                             received_server_live_integer,
                             received_client_live_integer);
                } else if (!s_comm.running_integer_seeded) {
                    if (received_client_live_integer != (received_server_live_integer + 1U)) {
                        ESP_LOGW(TAG,
                                 "TopLayer keepalive ignored during seed: expected client=server+1, got "
                                 "server=%" PRIu32 " client=%" PRIu32,
                                 received_server_live_integer,
                                 received_client_live_integer);
                    } else {
                        valid_keepalive_request = true;
                        s_comm.running_integer_seeded = true;
                        ESP_LOGI(TAG,
                                 "TopLayer running integer seeded by server: server=%" PRIu32
                                 " client=%" PRIu32,
                                 received_server_live_integer,
                                 received_client_live_integer);
                    }
                } else if (received_server_live_integer != expected_server_live_integer ||
                           received_client_live_integer != expected_client_live_integer) {
                    ESP_LOGW(TAG,
                             "TopLayer running integer mismatch ignored while waiting for timeout: "
                             "expected server=%" PRIu32 " client=%" PRIu32
                             ", got server=%" PRIu32 " client=%" PRIu32 " (seq=%" PRIu16 ")",
                             expected_server_live_integer,
                             expected_client_live_integer,
                             received_server_live_integer,
                             received_client_live_integer,
                             received_sequence);
                } else {
                    valid_keepalive_request = true;
                }

                if (valid_keepalive_request) {
                    communication_mark_keepalive_window_message_locked();
                    s_comm.keepalive_rx_us = esp_timer_get_time();
                    s_comm.keepalive_rx_count++;
                    s_comm.snapshot.keepalive_rx_count = s_comm.keepalive_rx_count;
                    s_comm.snapshot.server_live_integer = received_server_live_integer;
                    s_comm.snapshot.client_live_integer = received_server_live_integer + 1U;
                    if ((s_comm.snapshot.client_live_integer & 1U) == 0U) {
                        s_comm.snapshot.client_live_integer++;
                    }
                    s_comm.snapshot.connect_passed = true;

                    if (has_request_id) {
                        s_comm.last_keepalive_request_id = payload_request_id;
                        s_comm.last_keepalive_request_valid = true;
                        s_comm.duplicate_keepalive_replay_valid = false;
                    }

                    s_comm.keepalive_ack_pending = true;
                    s_comm.bottom_layer_retry_count = 0;
                    s_comm.snapshot.bottom_layer_retry_count = 0;
                    s_comm.keepalive_empty_window_count = 0;

                    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND);
                }
            }
        } else if (message_type == COMMUNICATION_MESSAGE_ACK) {
            s_comm.keepalive_ack_pending = false;
        }

        memmove(s_comm.rx_buffer, &s_comm.rx_buffer[frame_len], s_comm.rx_buffer_len - frame_len);
        s_comm.rx_buffer_len -= frame_len;
    }
}

/**
 * @brief Ensure the underlying Wi-Fi hardware stack is available.
 *
 * @details Lazily brings up the ESP-IDF station stack through the shared
 * peripheral manager so the communication state machine can be entered even
 * when the application originally booted in offline mode.
 *
 * @return
 *      - ESP_OK: Wi-Fi hardware and driver stack are ready
 *      - ESP_ERR_*: Underlying Wi-Fi bring-up failed
 */
static esp_err_t communication_ensure_wifi_stack_ready_locked(void)
{
    ESP_LOGI(TAG, "Communication init helper begin: ensure Wi-Fi stack ready");
    esp_err_t ret = peripherals_manager_init_wifi(false);
    ESP_LOGI(TAG, "Communication init helper result: peripherals_manager_init_wifi(false) -> %s",
             esp_err_to_name(ret));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Communication init helper end: Wi-Fi stack ready");
        return ESP_OK;
    }

    snprintf(s_comm.snapshot.last_error,
             sizeof(s_comm.snapshot.last_error),
             "Wi-Fi HW init failed (%s)",
             esp_err_to_name(ret));
    return ret;
}

/**
 * @brief Format the last local IP address into the shared snapshot.
 *
 * @details Polls the default station netif and updates the low-level runtime
 * fields that indicate whether Wi-Fi currently has a valid IPv4 address.
 */
static void communication_refresh_local_ip_locked(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    snprintf(s_comm.snapshot.local_ip, sizeof(s_comm.snapshot.local_ip), "Not assigned");
    s_comm.snapshot.wifi_has_ip = false;

    if (netif == NULL) {
        return;
    }

    esp_netif_ip_info_t ip_info = {0};
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        snprintf(s_comm.snapshot.local_ip,
                 sizeof(s_comm.snapshot.local_ip),
                 IPSTR,
                 IP2STR(&ip_info.ip));
        s_comm.snapshot.wifi_has_ip = true;
    }
}

/**
 * @brief Refresh the cached station RSSI if the STA link is associated.
 *
 * @details Queries the active AP record and stores the current RSSI for UI
 * visibility. A disconnected station is represented by a sentinel value.
 */
static void communication_refresh_rssi_locked(void)
{
    wifi_ap_record_t ap_record = {0};
    if (esp_wifi_sta_get_ap_info(&ap_record) == ESP_OK) {
        s_comm.snapshot.wifi_rssi = ap_record.rssi;
        s_comm.snapshot.wifi_noise_floor_dbm = -95;
        s_comm.snapshot.wifi_snr_estimate_db = (int16_t)((int32_t)ap_record.rssi - (-95));
        s_comm.snapshot.wifi_channel = ap_record.primary;
        s_comm.snapshot.wifi_authmode = (uint8_t)ap_record.authmode;
        snprintf(s_comm.snapshot.wifi_bssid_str,
                 sizeof(s_comm.snapshot.wifi_bssid_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
                 ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5]);
        return;
    }

    s_comm.snapshot.wifi_rssi = -127;
    s_comm.snapshot.wifi_noise_floor_dbm = -95;
    s_comm.snapshot.wifi_snr_estimate_db = 0;
    s_comm.snapshot.wifi_channel = 0;
    s_comm.snapshot.wifi_authmode = 0;
    snprintf(s_comm.snapshot.wifi_bssid_str,
             sizeof(s_comm.snapshot.wifi_bssid_str),
             "--:--:--:--:--:--");
}

/**
 * @brief Check whether the low-layer Wi-Fi link is already healthy.
 *
 * @details A healthy low layer means the station still has an IPv4 address and
 * remains associated to an AP. Reset can then skip Wi-Fi teardown and reconnect
 * directly to the TCP phase.
 *
 * @return `true` when Wi-Fi is still healthy; otherwise `false`.
 */
static bool communication_is_low_layer_ok_locked(void)
{
    wifi_ap_record_t ap_record = {0};

    communication_refresh_local_ip_locked();
    if (!s_comm.snapshot.wifi_has_ip) {
        return false;
    }

    return esp_wifi_sta_get_ap_info(&ap_record) == ESP_OK;
}

/**
 * @brief Close the active TCP socket if one exists.
 *
 * @details Shuts down the current socket and clears the cached TCP-ready state
 * so the next connect attempt starts from a clean transport baseline.
 */
static void communication_close_socket_locked(void)
{
    if (s_comm.socket_fd >= 0) {
        shutdown(s_comm.socket_fd, 0);
        close(s_comm.socket_fd);
        s_comm.socket_fd = -1;
    }

    s_comm.snapshot.tcp_connected = false;
    s_comm.tcp_connect_started = false;
}

/**
 * @brief Reset the operator-triggered Wi-Fi scan workflow.
 *
 * @details Clears the visible scan status and previously discovered device list
 * so each new scan begins from a deterministic baseline.
 */
static void communication_reset_scan_locked(void)
{
    s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_IDLE;
    s_comm.snapshot.scan_requested = false;
    s_comm.snapshot.scan_duration_ms = 0;
    s_comm.snapshot.scan_device_count = 0;
    s_comm.scan_started_us = 0;
    snprintf(s_comm.snapshot.scan_results,
             sizeof(s_comm.snapshot.scan_results),
             "Press 'Scan for Devices' to discover available Wi-Fi devices.");
}

/**
 * @brief Allocate AP-record storage for scan processing.
 *
 * @details Moves scan result storage off the communication-task stack so
 * button-triggered Wi-Fi scans and SSID prechecks cannot overflow the
 * `comm_link` task stack during deep ESP-IDF Wi-Fi call chains.
 *
 * @param[in] requested_count Number of records requested.
 *
 * @return Heap-allocated AP record buffer or `NULL` on allocation failure.
 */
static wifi_ap_record_t *communication_alloc_ap_records(size_t requested_count)
{
    if (requested_count == 0U) {
        return NULL;
    }

    return calloc(requested_count, sizeof(wifi_ap_record_t));
}

/**
 * @brief Check whether the configured target SSID is visible before association.
 *
 * @details Runs a short blocking scan on the background communication task to
 * check whether the configured SSID is currently visible before association.
 * The result is advisory only for normal operation, because the client still
 * enters the bounded initialize retry window even when the AP is temporarily
 * absent.
 *
 * @return
 *      - ESP_OK: Configured SSID was found in the current scan results
 *      - ESP_ERR_INVALID_STATE: No SSID is configured
 *      - ESP_ERR_NOT_FOUND: Configured SSID is not currently visible
 *      - ESP_ERR_*: Wi-Fi scan path failed
 */
static esp_err_t communication_validate_target_ap_visible_locked(void)
{
    if (s_comm.snapshot.config.wifi_ssid[0] == '\0') {
        snprintf(s_comm.snapshot.last_error,
                 sizeof(s_comm.snapshot.last_error),
                 "Configured SSID is empty");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        snprintf(s_comm.snapshot.last_error,
                 sizeof(s_comm.snapshot.last_error),
                 "AP visibility scan failed (%s)",
                 esp_err_to_name(ret));
        return ret;
    }

    uint16_t total_ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&total_ap_count);
    if (ret != ESP_OK) {
        snprintf(s_comm.snapshot.last_error,
                 sizeof(s_comm.snapshot.last_error),
                 "AP count read failed (%s)",
                 esp_err_to_name(ret));
        return ret;
    }

    uint16_t ap_count = total_ap_count;
    if (ap_count > COMMUNICATION_CONNECT_PRECHECK_APS) {
        ap_count = COMMUNICATION_CONNECT_PRECHECK_APS;
    }

    wifi_ap_record_t *ap_records = communication_alloc_ap_records(ap_count);
    if (ap_count > 0U && ap_records == NULL) {
        snprintf(s_comm.snapshot.last_error,
                 sizeof(s_comm.snapshot.last_error),
                 "AP record allocation failed");
        return ESP_ERR_NO_MEM;
    }

    if (ap_count > 0U) {
        ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        if (ret != ESP_OK) {
            snprintf(s_comm.snapshot.last_error,
                     sizeof(s_comm.snapshot.last_error),
                     "AP record read failed (%s)",
                     esp_err_to_name(ret));
            free(ap_records);
            return ret;
        }
    }

    for (uint16_t index = 0; index < ap_count; index++) {
        if (strncmp((const char *)ap_records[index].ssid,
                    s_comm.snapshot.config.wifi_ssid,
                    sizeof(ap_records[index].ssid)) == 0) {
            free(ap_records);
            return ESP_OK;
        }
    }

    free(ap_records);

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Re-issue Wi-Fi association while the initialize timeout window is open.
 *
 * @details Some simulator reset cycles bring the SoftAP up slightly after the
 * client has already issued its first `esp_wifi_connect()` call. This helper
 * performs a bounded retry by rechecking SSID visibility, disconnecting any
 * stale station attempt, and reissuing `esp_wifi_connect()` no more than once
 * per retry interval while the caller remains in `INITIALIZE`.
 *
 * @return
 *      - ESP_OK: Retry was accepted or the AP is still not visible yet
 *      - ESP_ERR_*: A concrete Wi-Fi operation failed and should abort init
 */
static esp_err_t communication_retry_wifi_connect_locked(void)
{
    int64_t now_us = esp_timer_get_time();
    if (s_comm.last_wifi_connect_attempt_us != 0 &&
        ((uint32_t)((now_us - s_comm.last_wifi_connect_attempt_us) / 1000LL) < COMMUNICATION_WIFI_RETRY_PERIOD_MS)) {
        return ESP_OK;
    }

    s_comm.last_wifi_connect_attempt_us = now_us;

    ESP_LOGI(TAG, "Communication initialize retry step: validate target AP visibility");
    esp_err_t ret = communication_validate_target_ap_visible_locked();
    ESP_LOGI(TAG, "Communication initialize retry result: validate target AP visibility -> %s",
             esp_err_to_name(ret));
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG,
                 "Configured Wi-Fi AP '%s' still not visible; retrying until timeout window expires",
                 s_comm.snapshot.config.wifi_ssid);
        return ESP_OK;
    }
    if (ret == ESP_ERR_WIFI_STATE) {
        ESP_LOGI(TAG, "Communication initialize retry skipped because station association is already in progress");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi visibility retry", esp_err_to_name(ret));
        return ret;
    }

    communication_refresh_local_ip_locked();
    if (s_comm.snapshot.wifi_has_ip) {
        ESP_LOGI(TAG, "Communication initialize retry skipped because the station already has an IP address");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Communication initialize retry step: esp_wifi_disconnect");
    ret = esp_wifi_disconnect();
    ESP_LOGI(TAG, "Communication initialize retry result: esp_wifi_disconnect -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK &&
        ret != ESP_ERR_WIFI_NOT_CONNECT &&
        ret != ESP_ERR_WIFI_CONN &&
        ret != ESP_ERR_WIFI_STATE) {
        communication_set_generic_failure_locked("Wi-Fi disconnect retry", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Communication initialize retry step: esp_wifi_connect");
    ret = esp_wifi_connect();
    ESP_LOGI(TAG, "Communication initialize retry result: esp_wifi_connect -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
        communication_set_generic_failure_locked("Wi-Fi connect retry", esp_err_to_name(ret));
        return ret;
    }
    if (ret == ESP_ERR_WIFI_STATE) {
        ESP_LOGI(TAG, "esp_wifi_connect retry skipped because station association is already in progress");
    }

    s_comm.wifi_connect_started = true;
    return ESP_OK;
}

/**
 * @brief Transition the task to a new low-level state.
 *
 * @details Updates the state tracking timestamps and logs the state movement for
 * debugging from the Connection Info overlay and serial monitor.
 *
 * @param[in] next_state New state to enter.
 */
static void communication_enter_state_locked(communication_state_t next_state)
{
    if (s_comm.snapshot.state != next_state) {
        ESP_LOGI(TAG,
                 "State %s -> %s",
                 communication_functions_state_to_string(s_comm.snapshot.state),
                 communication_functions_state_to_string(next_state));
    }

    s_comm.snapshot.state = next_state;
    s_comm.state_started_us = esp_timer_get_time();

    if (next_state == COMMUNICATION_STATE_TOP_LAYER_RESET) {
        s_comm.last_keep_alive_us = 0;
        s_comm.reset_cycle_started_us = s_comm.state_started_us;
        s_comm.keepalive_window_started_us = 0;
        s_comm.keepalive_window_has_message = false;
        s_comm.keepalive_empty_window_count = 0;
        s_comm.session_established = false;
        s_comm.session_established_us = 0;
        s_comm.ka_timing_valid = false;
        s_comm.ka_response_time_max_ms = 0;
        s_comm.ka_response_time_min_ms = 0;
        s_comm.keepalive_rx_us = 0;
        s_comm.snapshot.ka_response_time_last_ms = 0;
        s_comm.snapshot.ka_response_time_max_ms = 0;
        s_comm.snapshot.ka_response_time_min_ms = 0;
        s_comm.snapshot.ka_jitter_ms = 0;
        s_comm.snapshot.session_uptime_ms = 0;
    }

    if (next_state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE) {
        /* Link is established; allow uplink payload draining immediately. */
        s_comm.snapshot.send_data_enabled = true;
        communication_clear_connection_fault_locked();
        s_comm.keepalive_empty_window_count = 0;
        communication_start_keepalive_window_locked(false);
        if (!s_comm.session_established) {
            s_comm.session_established = true;
            s_comm.session_established_us = s_comm.state_started_us;
        }
    }

    if (next_state == COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND) {
        /* Keepalive response phase is still link-active; keep TX enabled. */
        s_comm.snapshot.send_data_enabled = true;
    }
}

/**
 * @brief Detect whether a failure reason represents a timeout event.
 *
 * @details Timeout tracking is explicit in debug telemetry and should include
 * both lowercase and title-case reason text variants.
 *
 * @param[in] reason_text Retry/failure reason text.
 *
 * @return `true` when the reason contains `timeout`; otherwise `false`.
 */
static bool communication_reason_is_timeout(const char *reason_text)
{
    if (reason_text == NULL) {
        return false;
    }

    return (strstr(reason_text, "timeout") != NULL) ||
           (strstr(reason_text, "Timeout") != NULL);
}

/**
 * @brief Send one pending keepalive response in client-send phase.
 *
 * @details `KeepAliveClientSend` transmits exactly one response for the latest
 * accepted keepalive request, then returns to `KeepAliveServerReceive`.
 *
 * @return
 *      - ESP_OK when the response is sent and state advances
 *      - ESP_ERR_INVALID_STATE when no request metadata is available
 *      - ESP_FAIL when TX fails
 */
static esp_err_t communication_send_pending_keepalive_response_locked(void)
{
    if (!s_comm.last_keepalive_request_valid) {
        snprintf(s_comm.snapshot.last_error,
                 sizeof(s_comm.snapshot.last_error),
                 "No pending keepalive request metadata");
        return ESP_ERR_INVALID_STATE;
    }

    if ((s_comm.snapshot.server_live_integer & 1U) != 0U) {
        ESP_LOGW(TAG,
                 "Adjusting server live integer parity before keepalive response: server=%" PRIu32,
                 s_comm.snapshot.server_live_integer);
        s_comm.snapshot.server_live_integer++;
    }

    s_comm.snapshot.client_live_integer = s_comm.snapshot.server_live_integer + 1U;
    if ((s_comm.snapshot.client_live_integer & 1U) == 0U) {
        s_comm.snapshot.client_live_integer++;
    }

    char *keepalive_payload = s_comm.scratch_payload;
    keepalive_payload[0] = '\0';
    communication_build_keepalive_response_payload_locked(
        keepalive_payload,
        COMMUNICATION_FRAME_MAX_PAYLOAD + 1U,
        true,
        s_comm.last_keepalive_request_id);

    if (communication_send_frame_locked(COMMUNICATION_MESSAGE_KEEPALIVE, keepalive_payload) != ESP_OK) {
        return ESP_FAIL;
    }

    /* Measure keepalive response time and update timing telemetry */
    if (s_comm.keepalive_rx_us > 0) {
        int32_t response_ms = (int32_t)((esp_timer_get_time() - s_comm.keepalive_rx_us) / 1000LL);
        s_comm.snapshot.ka_response_time_last_ms = response_ms;
        if (response_ms > s_comm.ka_response_time_max_ms) {
            s_comm.ka_response_time_max_ms = response_ms;
            s_comm.snapshot.ka_response_time_max_ms = response_ms;
        }
        if (!s_comm.ka_timing_valid || response_ms < s_comm.ka_response_time_min_ms) {
            s_comm.ka_response_time_min_ms = response_ms;
            s_comm.snapshot.ka_response_time_min_ms = response_ms;
        }
        s_comm.ka_timing_valid = true;
        s_comm.snapshot.ka_jitter_ms = s_comm.ka_response_time_max_ms - s_comm.ka_response_time_min_ms;
    }
    s_comm.keepalive_tx_count++;
    s_comm.snapshot.keepalive_tx_count = s_comm.keepalive_tx_count;

    s_comm.last_responded_keepalive_request_id = s_comm.last_keepalive_request_id;
    s_comm.last_responded_keepalive_request_valid = true;
    s_comm.keepalive_ack_pending = false;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.bottom_layer_retry_count = 0;
    s_comm.snapshot.bottom_layer_retry_count = 0;
    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE);
    return ESP_OK;
}

/**
 * @brief Schedule a reconnect attempt and optionally latch a connection fault.
 *
 * @details Stores the latest retry reason, increments the sequential
 * keepalive-failure counter when applicable, latches the connection fault after
 * repeated keepalive failures, and either routes the workflow back toward
 * `connect` or stops in `error` until the operator explicitly
 * presses Reset Connection.
 *
 * @param[in] reason_text NUL-terminated retry description.
 * @param[in] keepalive_failure Whether this retry counts toward the sequential
 *            keepalive-failure latch.
 */
static void communication_record_top_layer_failure_locked(const char *reason_text)
{
    const char *resolved_error = (reason_text != NULL) ? reason_text : "Unknown TopLayer failure";

    snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "%s", resolved_error);
    s_comm.top_layer_failure_count++;
    s_comm.snapshot.top_layer_failure_count = s_comm.top_layer_failure_count;
    s_comm.snapshot.connection_fault = true;
    s_comm.snapshot.consecutive_keepalive_failures++;

    ESP_LOGW(TAG,
             "TopLayer failure recorded (%" PRIu32 "/%u): %s",
             s_comm.top_layer_failure_count,
             COMMUNICATION_TOP_LAYER_FAILURE_LIMIT,
             resolved_error);

    communication_close_socket_locked();
    s_comm.snapshot.connect_passed = false;
    s_comm.snapshot.send_data_enabled = false;
    s_comm.keepalive_ack_pending = false;
    s_comm.connect_requested_us = 0;
    s_comm.last_valid_rx_us = 0;
    s_comm.keepalive_window_has_message = false;
    s_comm.keepalive_window_started_us = 0;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.session_id_valid = false;
    s_comm.last_keepalive_request_valid = false;
    s_comm.last_responded_keepalive_request_valid = false;
    s_comm.duplicate_keepalive_replay_valid = false;
    s_comm.active_session_id = 0;
    s_comm.last_keepalive_request_id = 0;
    s_comm.last_responded_keepalive_request_id = 0;
    s_comm.duplicate_keepalive_replay_request_id = 0;
    s_comm.snapshot.top_layer_connect_streak = 0;

    if (s_comm.top_layer_failure_count >= COMMUNICATION_TOP_LAYER_FAILURE_LIMIT) {
        communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_ERROR);
        return;
    }

    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_RESET);
}

/**
 * @brief Schedule a BottomLayer retry or escalate to TopLayer error.
 *
 * @details BottomLayer failures (loss-of-link and checksum failures) retry up
 * to three times. Once the retry limit is reached, behavior depends on the UI
 * Auto Reconnect toggle: enabled resets counters and retries `connect`
 * automatically, disabled keeps the existing TopLayer-failure escalation path.
 *
 * @param[in] reason_text Retry reason text.
 * @param[in] checksum_failure Whether this failure originated from checksum validation.
 */
static void communication_schedule_bottom_layer_retry_locked(const char *reason_text, bool checksum_failure)
{
    const char *resolved_error = (reason_text != NULL) ? reason_text : "Unknown BottomLayer failure";
    snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "%s", resolved_error);

    s_comm.bottom_layer_retry_count++;
    s_comm.snapshot.bottom_layer_retry_count = s_comm.bottom_layer_retry_count;
    if (communication_reason_is_timeout(resolved_error)) {
        s_comm.snapshot.timeout_event_count++;
    }
    if (checksum_failure) {
        s_comm.snapshot.bottom_layer_checksum_error_count++;
    }

    ESP_LOGW(TAG,
             "BottomLayer retry (%" PRIu32 "/%u): %s",
             s_comm.bottom_layer_retry_count,
             COMMUNICATION_BOTTOM_LAYER_RETRY_LIMIT,
             resolved_error);

    communication_close_socket_locked();
    s_comm.snapshot.connect_passed = false;
    s_comm.snapshot.send_data_enabled = false;
    s_comm.keepalive_ack_pending = false;
    s_comm.connect_requested_us = 0;
    s_comm.last_valid_rx_us = 0;
    s_comm.keepalive_window_has_message = false;
    s_comm.keepalive_window_started_us = 0;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.session_id_valid = false;
    s_comm.last_keepalive_request_valid = false;
    s_comm.last_responded_keepalive_request_valid = false;
    s_comm.active_session_id = 0;
    s_comm.last_keepalive_request_id = 0;
    s_comm.last_responded_keepalive_request_id = 0;
    s_comm.snapshot.top_layer_connect_streak = 0;

    if (s_comm.bottom_layer_retry_count >= COMMUNICATION_BOTTOM_LAYER_RETRY_LIMIT) {
        if (s_comm.snapshot.auto_reconnect_enabled) {
            s_comm.snapshot.connection_fault = true;
            s_comm.snapshot.consecutive_keepalive_failures++;
            ESP_LOGW(TAG,
                     "BottomLayer retries exhausted with Auto Reconnect enabled; resetting counters and retrying connect");
            s_comm.bottom_layer_retry_count = 0;
            s_comm.top_layer_failure_count = 0;
            s_comm.snapshot.bottom_layer_retry_count = 0;
            s_comm.snapshot.top_layer_failure_count = 0;
            s_comm.snapshot.top_layer_connect_streak = 0;
            s_comm.snapshot.timeout_event_count = 0;
            s_comm.snapshot.bottom_layer_checksum_error_count = 0;
            s_comm.snapshot.bottom_layer_sequence_error_count = 0;
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_CONNECT);
            return;
        }
        communication_record_top_layer_failure_locked("BottomLayer retries exhausted");
        return;
    }

    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_CONNECT);
}

/**
 * @brief Load the default transport configuration into the snapshot.
 *
 * @details Establishes a deterministic Wi-Fi/TCP configuration that can be
 * exercised from the UI before dynamic configuration editing is added.
 */
static void communication_load_defaults_locked(void)
{
    memset(&s_comm.snapshot, 0, sizeof(s_comm.snapshot));
    s_comm.reset_cycle_started_us = esp_timer_get_time();
    s_comm.snapshot.state = COMMUNICATION_STATE_TOP_LAYER_RESET;
    s_comm.snapshot.wifi_rssi = -127;
    snprintf(s_comm.snapshot.local_ip, sizeof(s_comm.snapshot.local_ip), "Not assigned");
    snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "No error");
    snprintf(s_comm.snapshot.config.wifi_ssid,
             sizeof(s_comm.snapshot.config.wifi_ssid),
             COMMUNICATION_DEFAULT_WIFI_SSID);
    snprintf(s_comm.snapshot.config.wifi_password,
             sizeof(s_comm.snapshot.config.wifi_password),
             COMMUNICATION_DEFAULT_WIFI_PASSWORD);
    snprintf(s_comm.snapshot.config.server_ip,
             sizeof(s_comm.snapshot.config.server_ip),
             COMMUNICATION_DEFAULT_SERVER_IP);
    s_comm.snapshot.config.server_port = COMMUNICATION_DEFAULT_SERVER_PORT;
    s_comm.snapshot.config.wifi_connect_timeout_ms = COMMUNICATION_DEFAULT_WIFI_TIMEOUT;
    s_comm.snapshot.config.tcp_connect_timeout_ms = COMMUNICATION_DEFAULT_TCP_TIMEOUT;
    s_comm.snapshot.config.keep_alive_period_ms = COMMUNICATION_DEFAULT_KEEPALIVE_MS;
    s_comm.snapshot.config.ka_wait_window_ms = COMMUNICATION_KEEPALIVE_WAIT_WINDOW_MS;
    s_comm.snapshot.config.ka_empty_window_limit = COMMUNICATION_KEEPALIVE_EMPTY_WINDOW_LIMIT;
    s_comm.snapshot.config.bottom_layer_retry_limit = COMMUNICATION_BOTTOM_LAYER_RETRY_LIMIT;
    s_comm.snapshot.config.top_layer_failure_limit = COMMUNICATION_TOP_LAYER_FAILURE_LIMIT;
    s_comm.snapshot.auto_reconnect_enabled = true;
    snprintf(s_comm.snapshot.last_received_text,
             sizeof(s_comm.snapshot.last_received_text),
             "No peer text received yet");
    communication_reset_scan_locked();
    communication_clear_transport_flow_locked();
    communication_publish_snapshot_locked();
}

/**
 * @brief Begin a clean reset cycle for the low-level link.
 *
 * @details Clears the active TCP socket and moves the task back to `RESET`.
 * When the low-layer Wi-Fi link is still healthy, reset skips Wi-Fi teardown
 * and reconnects TCP directly to avoid long association delays.
 */
static void communication_start_reset_locked(void)
{
    bool low_layer_ok = communication_is_low_layer_ok_locked();

    communication_close_socket_locked();
    if (low_layer_ok) {
        ESP_LOGI(TAG, "Reset fast-path: LowLayer Wi-Fi healthy, skipping esp_wifi_disconnect");
        s_comm.fast_reset_skip_wifi = true;
    } else {
        (void)esp_wifi_disconnect();
        s_comm.fast_reset_skip_wifi = false;
    }
    s_comm.snapshot.reset_requested = false;
    snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "No error");
    s_comm.wifi_connect_started = low_layer_ok;
    s_comm.tcp_connect_started = false;
    s_comm.last_wifi_connect_attempt_us = 0;
    s_comm.bottom_layer_retry_count = 0;
    s_comm.top_layer_failure_count = 0;
    s_comm.snapshot.bottom_layer_retry_count = 0;
    s_comm.snapshot.top_layer_failure_count = 0;
    s_comm.snapshot.top_layer_connect_streak = 0;
    s_comm.snapshot.timeout_event_count = 0;
    s_comm.snapshot.reset_to_debug_elapsed_ms = 0;
    s_comm.keepalive_empty_window_count = 0;
    s_comm.keepalive_window_started_us = 0;
    s_comm.keepalive_window_has_message = false;
    communication_refresh_local_ip_locked();
    communication_refresh_rssi_locked();
    communication_reset_scan_locked();
    communication_clear_transport_flow_locked();
    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_RESET);
}

/**
 * @brief Kick the Wi-Fi station toward the configured AP.
 *
 * @details Pushes the configured SSID/password into the already-started Wi-Fi
 * stack and begins association for the current reset cycle.
 */
static esp_err_t communication_begin_initialize_locked(void)
{
    ESP_LOGI(TAG, "Communication initialize begin");
    esp_err_t ret = communication_ensure_wifi_stack_ready_locked();
    ESP_LOGI(TAG, "Communication initialize step result: ensure Wi-Fi stack -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi hardware init", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Communication initialize step: validate target AP visibility");
    ret = communication_validate_target_ap_visible_locked();
    ESP_LOGI(TAG, "Communication initialize step result: validate target AP visibility -> %s",
             esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND && ret != ESP_ERR_WIFI_STATE) {
        if (ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NO_MEM) {
            communication_set_generic_failure_locked("Initialize precheck", esp_err_to_name(ret));
        }
        return ret;
    }
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG,
                 "Configured Wi-Fi AP '%s' not visible yet; continuing initialize retry window",
                 s_comm.snapshot.config.wifi_ssid);
    } else if (ret == ESP_ERR_WIFI_STATE) {
        ESP_LOGI(TAG,
                 "Wi-Fi visibility precheck skipped because station is busy (connecting/scanning)");
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid,
            s_comm.snapshot.config.wifi_ssid,
            sizeof(wifi_cfg.sta.ssid) - 1U);
    strncpy((char *)wifi_cfg.sta.password,
            s_comm.snapshot.config.wifi_password,
            sizeof(wifi_cfg.sta.password) - 1U);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable = true;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Communication initialize step: esp_wifi_set_mode(STA)");
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Communication initialize step result: esp_wifi_set_mode -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi mode set", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Communication initialize step: esp_wifi_set_config(STA)");
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    ESP_LOGI(TAG, "Communication initialize step result: esp_wifi_set_config -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi config", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Communication initialize step: esp_wifi_connect");
    ret = esp_wifi_connect();
    ESP_LOGI(TAG, "Communication initialize step result: esp_wifi_connect -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
        communication_set_generic_failure_locked("Wi-Fi connect", esp_err_to_name(ret));
        return ret;
    }
    if (ret == ESP_ERR_WIFI_STATE) {
        ESP_LOGI(TAG, "esp_wifi_connect skipped because station association is already in progress");
    }

    s_comm.wifi_connect_started = true;
    s_comm.last_wifi_connect_attempt_us = esp_timer_get_time();
    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_INITIALIZE);
    ESP_LOGI(TAG, "Communication initialize end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Open the configured TCP socket toward the Wi-Fi server.
 *
 * @details Creates a client socket, applies short I/O timeouts, and performs a
 * blocking connect to the configured remote endpoint from the worker task.
 *
 * @return
 *      - ESP_OK: Socket connected successfully
 *      - ESP_ERR_*: Socket create or connect failed
 */
static esp_err_t communication_open_tcp_socket_locked(void)
{
    ESP_LOGI(TAG, "Communication TCP connect begin");
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_comm.snapshot.config.server_port),
    };

    if (inet_pton(AF_INET, s_comm.snapshot.config.server_ip, &server_addr.sin_addr) != 1) {
        communication_set_generic_failure_locked("TCP address parse", "Invalid server IP");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Communication TCP step: socket()");
    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    ESP_LOGI(TAG, "Communication TCP step result: socket -> fd=%d", socket_fd);
    if (socket_fd < 0) {
        communication_set_generic_failure_locked("TCP socket create", strerror(errno));
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = (time_t)(s_comm.snapshot.config.tcp_connect_timeout_ms / 1000U),
        .tv_usec = (suseconds_t)((s_comm.snapshot.config.tcp_connect_timeout_ms % 1000U) * 1000U),
    };
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "Communication TCP step: connect(%s:%u)",
             s_comm.snapshot.config.server_ip,
             (unsigned)s_comm.snapshot.config.server_port);
    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        int socket_errno = errno;
        ESP_LOGI(TAG, "Communication TCP step result: connect -> errno=%d (%s)",
                 socket_errno,
                 strerror(socket_errno));
        close(socket_fd);
        errno = socket_errno;
        if (socket_errno == ECONNREFUSED ||
            socket_errno == ETIMEDOUT ||
            socket_errno == EHOSTUNREACH ||
            socket_errno == ENETUNREACH) {
            communication_set_tcp_server_not_found_error_locked(socket_errno);
        } else {
            communication_set_generic_failure_locked("TCP connect", strerror(socket_errno));
        }
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Communication TCP step result: connect -> OK");

    s_comm.socket_fd = socket_fd;
    s_comm.snapshot.tcp_connected = true;
    s_comm.tcp_connect_started = true;
    communication_clear_transport_flow_locked();
    s_comm.snapshot.tcp_connected = true;
    ESP_LOGI(TAG, "Communication TCP step result: keepalive counters remain server-authoritative");

    char *initialize_payload = s_comm.scratch_payload;
    initialize_payload[0] = '\0';
    communication_build_initialize_payload_locked(initialize_payload,
                                                  COMMUNICATION_FRAME_MAX_PAYLOAD + 1U);
    ESP_LOGI(TAG, "Communication TCP step: send INITIALIZE frame");
    if (communication_send_frame_locked(COMMUNICATION_MESSAGE_INITIALIZE, initialize_payload) != ESP_OK) {
        communication_close_socket_locked();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Communication TCP step result: send INITIALIZE frame -> OK");

    s_comm.snapshot.initialize_passed = true;
    ESP_LOGI(TAG, "Communication TCP step: send CONNECT frame");
    if (communication_send_frame_locked(COMMUNICATION_MESSAGE_CONNECT, "client_connect") != ESP_OK) {
        communication_close_socket_locked();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Communication TCP step result: send CONNECT frame -> OK");

    s_comm.connect_requested_us = esp_timer_get_time();
    communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_CONNECT);
    ESP_LOGI(TAG, "Communication TCP connect end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Supervise server-driven keep-alive while the TCP socket is active.
 *
 * @details The ESP32-C3 server owns keepalive initiation. The client therefore
 * does not emit periodic heartbeat traffic on its own. Instead it tracks one
 * rolling keepalive deadline window and records a timeout only when no valid
 * keepalive request arrives before the deadline expires.
 */
static void communication_service_keep_alive_locked(void)
{
    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        return;
    }

    if (s_comm.keepalive_window_started_us <= 0) {
        communication_start_keepalive_window_locked(false);
        return;
    }

    uint32_t window_elapsed_ms =
        (uint32_t)((esp_timer_get_time() - s_comm.keepalive_window_started_us) / 1000LL);
    if (window_elapsed_ms < COMMUNICATION_KEEPALIVE_WAIT_WINDOW_MS) {
        return;
    }

    s_comm.keepalive_empty_window_count++;
    s_comm.snapshot.timeout_event_count++;
    ESP_LOGW(TAG,
             "TopLayer keepalive response timeout (%" PRIu32 "/%u): no request after %u ms",
             s_comm.keepalive_empty_window_count,
             COMMUNICATION_KEEPALIVE_EMPTY_WINDOW_LIMIT,
             COMMUNICATION_KEEPALIVE_WAIT_WINDOW_MS);

    if (s_comm.keepalive_empty_window_count >= COMMUNICATION_KEEPALIVE_EMPTY_WINDOW_LIMIT) {
        s_comm.snapshot.connection_fault = true;
        s_comm.snapshot.consecutive_keepalive_failures++;
        communication_schedule_bottom_layer_retry_locked("TopLayer keepalive response timeout", false);
        return;
    }

    communication_start_keepalive_window_locked(false);
}

/**
 * @brief Start the dedicated Wi-Fi device discovery scan.
 *
 * @details Clears the previous text immediately, requests a non-blocking scan
 * from the ESP-IDF Wi-Fi driver, and marks the scan workflow as active.
 */
static void communication_begin_scan_locked(void)
{
    ESP_LOGI(TAG,
             "Scan begin requested (state=%s, wifi_has_ip=%d, tcp_connected=%d)",
             communication_functions_state_to_string(s_comm.snapshot.state),
             (int)s_comm.snapshot.wifi_has_ip,
             (int)s_comm.snapshot.tcp_connected);

    esp_err_t ret = communication_ensure_wifi_stack_ready_locked();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan unavailable: communication_ensure_wifi_stack_ready_locked -> %s", esp_err_to_name(ret));
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan unavailable: %s",
                 esp_err_to_name(ret));
        return;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    s_comm.snapshot.scan_requested = false;
    s_comm.snapshot.scan_duration_ms = 0;
    s_comm.snapshot.scan_device_count = 0;
    snprintf(s_comm.snapshot.scan_results,
             sizeof(s_comm.snapshot.scan_results),
             "Scanning for devices...\nPlease wait 10 seconds.");

    ESP_LOGI(TAG,
             "Scan start call: esp_wifi_scan_start(blocking=false, show_hidden=%d)",
             (int)scan_cfg.show_hidden);
    ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan failed to start: esp_wifi_scan_start -> %s", esp_err_to_name(ret));
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan failed to start: %s",
                 esp_err_to_name(ret));
        return;
    }

    s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_IN_PROGRESS;
    s_comm.scan_started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Scan in progress (window=%u ms)", (unsigned)COMMUNICATION_SCAN_WINDOW_MS);
}

/**
 * @brief Complete the 10-second Wi-Fi scan and format the discovered list.
 *
 * @details Stops the active scan window, reads back the strongest discovered
 * APs, and stores a compact text report for the Connection Info overlay.
 */
static void communication_complete_scan_locked(void)
{
    uint16_t ap_count = COMMUNICATION_SCAN_MAX_APS;
    uint16_t total_ap_count = 0;
    esp_err_t ret = ESP_OK;

    esp_err_t stop_ret = esp_wifi_scan_stop();
    ESP_LOGI(TAG, "Scan complete step: esp_wifi_scan_stop -> %s", esp_err_to_name(stop_ret));

    ret = esp_wifi_scan_get_ap_num(&total_ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan result read failed: esp_wifi_scan_get_ap_num -> %s", esp_err_to_name(ret));
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan result read failed: %s",
                 esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Scan result count: total_ap_count=%u", (unsigned)total_ap_count);

    wifi_ap_record_t *ap_records = communication_alloc_ap_records(ap_count);
    if (ap_count > 0U && ap_records == NULL) {
        ESP_LOGE(TAG, "Scan device allocation failed (requested=%u)", (unsigned)ap_count);
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan device allocation failed");
        return;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan device list failed: esp_wifi_scan_get_ap_records -> %s", esp_err_to_name(ret));
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan device list failed: %s",
                 esp_err_to_name(ret));
        free(ap_records);
        return;
    }

    s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_COMPLETE;
    s_comm.snapshot.scan_duration_ms = COMMUNICATION_SCAN_WINDOW_MS;
    s_comm.snapshot.scan_device_count = total_ap_count;
    ESP_LOGI(TAG,
             "Scan complete: total_ap_count=%u returned_records=%u duration_ms=%u",
             (unsigned)total_ap_count,
             (unsigned)ap_count,
             (unsigned)s_comm.snapshot.scan_duration_ms);

    if (total_ap_count == 0 || ap_count == 0) {
        ESP_LOGW(TAG, "Scan complete with no discoverable AP records");
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan complete in %u ms.\nNo devices were discovered.",
                 (unsigned)s_comm.snapshot.scan_duration_ms);
        return;
    }

    int offset = snprintf(s_comm.snapshot.scan_results,
                          sizeof(s_comm.snapshot.scan_results),
                          "Scan complete in %u ms.\nDiscovered devices: %u\n",
                          (unsigned)s_comm.snapshot.scan_duration_ms,
                          (unsigned)total_ap_count);
    for (uint16_t index = 0; index < ap_count && offset > 0 && offset < (int)sizeof(s_comm.snapshot.scan_results); index++) {
        const char *ssid_text = ((const char *)ap_records[index].ssid)[0] != '\0'
                                    ? (const char *)ap_records[index].ssid
                                    : "<hidden>";
        ESP_LOGI(TAG,
                 "Scan AP[%u]: SSID='%s' RSSI=%d CH=%u",
                 (unsigned)index,
                 ssid_text,
                 ap_records[index].rssi,
                 (unsigned)ap_records[index].primary);
        int written = snprintf(&s_comm.snapshot.scan_results[offset],
                               sizeof(s_comm.snapshot.scan_results) - (size_t)offset,
                               "%u. %s | RSSI %d dBm | CH %u\n",
                               (unsigned)(index + 1U),
                               ssid_text,
                               ap_records[index].rssi,
                               (unsigned)ap_records[index].primary);
        if (written < 0) {
            break;
        }
        offset += written;
    }

    free(ap_records);
}

/**
 * @brief Advance the dedicated device-scan state machine.
 *
 * @details Handles `requested -> in progress -> complete/error` transitions
 * without blocking the UI thread. The visible results are replaced only after
 * the fixed 10-second scan window expires.
 */
static void communication_service_scan_locked(void)
{
    if (s_comm.snapshot.scan_requested) {
        ESP_LOGI(TAG,
                 "Scan request consumed (scan_state=%s, comm_state=%s)",
                 communication_functions_scan_state_to_string(s_comm.snapshot.scan_state),
                 communication_functions_state_to_string(s_comm.snapshot.state));
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_REQUESTED;
        communication_begin_scan_locked();
    }

    if (s_comm.snapshot.scan_state == COMMUNICATION_SCAN_STATE_IN_PROGRESS) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_comm.scan_started_us) / 1000LL;
        if (elapsed_ms >= COMMUNICATION_SCAN_WINDOW_MS) {
            ESP_LOGI(TAG, "Scan window elapsed (%u ms), collecting results", (unsigned)elapsed_ms);
            communication_complete_scan_locked();
        }
    }
}

/**
 * @brief Execute one low-level state-machine step.
 *
 * @details Polls Wi-Fi/IP readiness, reacts to operator reset requests, opens
 * the TCP transport when Wi-Fi is ready, services framed low-level RX/TX, and
 * retries failed keepalive sessions through `connect` while a separate fault
 * latch tracks repeated keepalive loss and eventually stops in
 * `error` until the operator requests a reset.
 */

/**
 * @brief Send one pending uplink data packet from the transmit FIFO.
 *
 * @details Called from the task step whenever the keepalive session is active.
 * Sends at most one packet per task tick (20 ms) to avoid starving keepalive
 * traffic.  The application layer is responsible for the push rate.
 */
static void communication_drain_uplink_fifo_locked(void)
{
    if (!s_comm.snapshot.send_data_enabled) {
        return;
    }

    data_uplink_packet_t pkt;
    if (data_uplink_pop(&pkt) != DATA_FIFO_OK) {
        return;
    }

    pkt.magic        = DATA_PAYLOAD_MAGIC_UPLINK;
    pkt.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000LL);

    if (communication_send_raw_frame_locked(COMMUNICATION_MESSAGE_DATA,
                                            (const uint8_t *)&pkt,
                                            sizeof(pkt)) == ESP_OK) {
        s_comm.keepalive_tx_count++;
        s_comm.snapshot.keepalive_tx_count = s_comm.keepalive_tx_count;
    }
}

/**
 * @brief Queue one text DATA command for deferred keepalive-safe transmission.
 *
 * @details UI threads call into the communication API while the background
 * transport task owns the socket. This helper stores one latest command and the
 * task sends it on the next keepalive-active tick.
 *
 * @param[in] payload_text Command payload text.
 *
 * @return
 *      - ESP_OK when the command is queued
 *      - ESP_ERR_INVALID_ARG when payload is NULL or empty
 */
static esp_err_t communication_queue_data_command_locked(const char *payload_text)
{
    size_t command_len = 0U;

    if (payload_text == NULL || payload_text[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    command_len = strlen(payload_text);
    if (command_len > COMMUNICATION_FRAME_MAX_PAYLOAD) {
        command_len = COMMUNICATION_FRAME_MAX_PAYLOAD;
    }
    memcpy(s_comm.pending_data_command, payload_text, command_len);
    s_comm.pending_data_command[command_len] = '\0';
    s_comm.pending_data_command_valid = true;
    return ESP_OK;
}

/**
 * @brief Send one queued text DATA command when keepalive is active.
 *
 * @details Commands are transmitted only from keepalive states to match the
 * existing send-data gating policy. Failed sends keep the command queued for
 * the next task tick.
 *
 * @return ESP_OK on success or when nothing is pending, otherwise ESP_FAIL.
 */
static esp_err_t communication_send_pending_data_command_locked(void)
{
    if (!s_comm.pending_data_command_valid) {
        return ESP_OK;
    }

    if (s_comm.snapshot.state != COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE &&
        s_comm.snapshot.state != COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND) {
        return ESP_OK;
    }

    if (communication_send_frame_locked(COMMUNICATION_MESSAGE_DATA, s_comm.pending_data_command) != ESP_OK) {
        return ESP_FAIL;
    }

    s_comm.pending_data_command[0] = '\0';
    s_comm.pending_data_command_valid = false;
    return ESP_OK;
}

static void communication_task_step(void)
{
    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    communication_refresh_local_ip_locked();
    communication_refresh_rssi_locked();
    communication_service_scan_locked();
    communication_poll_received_frames_locked();

    if (s_comm.disconnect_requested) {
        s_comm.disconnect_requested = false;
        communication_start_reset_locked();
    }

    if (s_comm.reset_requested) {
        s_comm.reset_requested = false;
        s_comm.snapshot.reset_requested = true;
        communication_start_reset_locked();
    }

    switch (s_comm.snapshot.state) {
    case COMMUNICATION_STATE_TOP_LAYER_RESET: {
        if (s_comm.fast_reset_skip_wifi && s_comm.snapshot.wifi_has_ip) {
            ESP_LOGI(TAG, "Reset fast-path: skipping Wi-Fi reinitialize and moving directly to Connect");
            s_comm.fast_reset_skip_wifi = false;
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_CONNECT);
            break;
        }

        s_comm.fast_reset_skip_wifi = false;
        esp_err_t ret = communication_begin_initialize_locked();
        if (ret != ESP_OK) {
            if (s_comm.snapshot.last_error[0] == '\0' ||
                strcmp(s_comm.snapshot.last_error, "No error") == 0) {
                communication_set_generic_failure_locked("Initialize start", esp_err_to_name(ret));
            }
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_INITIALIZE);
        }
        break;
    }

    case COMMUNICATION_STATE_TOP_LAYER_INITIALIZE: {
        int64_t elapsed_ms = (esp_timer_get_time() - s_comm.state_started_us) / 1000LL;
        if (s_comm.snapshot.wifi_has_ip) {
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_CONNECT);
        } else if ((uint32_t)elapsed_ms >= s_comm.snapshot.config.wifi_connect_timeout_ms) {
            snprintf(s_comm.snapshot.last_error,
                     sizeof(s_comm.snapshot.last_error),
                     "Wi-Fi AP '%s' not available after %" PRIu32 " ms",
                     s_comm.snapshot.config.wifi_ssid,
                     s_comm.snapshot.config.wifi_connect_timeout_ms);
            communication_schedule_bottom_layer_retry_locked("BottomLayer Wi-Fi timeout", false);
        } else if (s_comm.wifi_connect_started) {
            esp_err_t ret = communication_retry_wifi_connect_locked();
            if (ret != ESP_OK) {
                communication_schedule_bottom_layer_retry_locked("BottomLayer Wi-Fi retry failure", false);
            }
        }
        break;
    }

    case COMMUNICATION_STATE_TOP_LAYER_CONNECT:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_close_socket_locked();
            s_comm.snapshot.connect_passed = false;
            s_comm.snapshot.send_data_enabled = false;
            s_comm.connect_requested_us = 0;
            communication_enter_state_locked(COMMUNICATION_STATE_TOP_LAYER_INITIALIZE);
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            esp_err_t ret = communication_open_tcp_socket_locked();
            if (ret != ESP_OK &&
                (s_comm.snapshot.last_error[0] == '\0' ||
                 strcmp(s_comm.snapshot.last_error, "No error") == 0)) {
                communication_set_generic_failure_locked("TCP connect", strerror(errno));
            }
            if (ret != ESP_OK) {
                communication_schedule_bottom_layer_retry_locked("BottomLayer TCP connect failed", false);
            }
            break;
        }
        if (s_comm.connect_requested_us != 0 &&
            !s_comm.running_integer_seeded &&
            ((uint32_t)((esp_timer_get_time() - s_comm.connect_requested_us) / 1000LL) >=
             s_comm.snapshot.config.tcp_connect_timeout_ms)) {
            communication_schedule_bottom_layer_retry_locked("TopLayer connect timeout", false);
            break;
        }
        break;

    case COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_schedule_bottom_layer_retry_locked("BottomLayer Wi-Fi link lost", false);
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            communication_schedule_bottom_layer_retry_locked("BottomLayer TCP socket lost", false);
            break;
        }
        communication_service_keep_alive_locked();
        (void)communication_send_pending_data_command_locked();
        communication_drain_uplink_fifo_locked();
        break;

    case COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_schedule_bottom_layer_retry_locked("BottomLayer Wi-Fi link lost", false);
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            communication_schedule_bottom_layer_retry_locked("BottomLayer TCP socket lost", false);
            break;
        }
        if (communication_send_pending_keepalive_response_locked() != ESP_OK) {
            communication_schedule_bottom_layer_retry_locked("TopLayer keepalive response send failure", false);
        }
        (void)communication_send_pending_data_command_locked();
        communication_drain_uplink_fifo_locked();
        break;

    case COMMUNICATION_STATE_TOP_LAYER_ERROR:
        break;

    default:
        break;
    }

    communication_publish_snapshot_locked();
    xSemaphoreGive(s_comm.mutex);
}

/**
 * @brief Background owner task for the communication module.
 *
 * @details Runs the low-level state machine at a fixed cadence so UI actions can
 * request transport resets without blocking the LVGL or application threads.
 *
 * @param[in] arg Unused task argument.
 */
static void communication_task(void *arg)
{
    (void)arg;
    uint32_t stack_log_ticks = 0U;

    while (true) {
        communication_task_step();
        stack_log_ticks++;
        if ((stack_log_ticks % 250U) == 0U) {
            UBaseType_t free_words = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG,
                     "comm_link stack watermark: free=%u bytes",
                     (unsigned)(free_words * sizeof(StackType_t)));
        }
        vTaskDelay(pdMS_TO_TICKS(COMMUNICATION_STEP_PERIOD_MS));
    }
}

/**
 * @brief Initialize the communication task and load default settings.
 *
 * @details Creates the background task that owns the low-level Wi-Fi/TCP state
 * machine. The `offline` argument does not skip initialization; it documents
 * that startup callers may downgrade failures to warnings while still running
 * the same initialization sequence.
 *
 * @param[in] offline Startup offline-mode flag.
 *
 * @return
 *      - ESP_OK: Module initialized successfully or was already initialized
 *      - ESP_ERR_NO_MEM: Task or synchronization primitives could not be created
 */
esp_err_t communication_functions_init(bool offline)
{
    ESP_LOGI(TAG, "Communication module init begin (offline=%d)", offline);
    communication_log_heap_checkpoint("init_begin");
    if (offline) {
        ESP_LOGI(TAG, "Communication init running in offline mode; caller downgrades failures to warnings");
    }

    if (s_comm.initialized) {
        ESP_LOGI(TAG, "Communication module init end -> already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Communication module init step: xSemaphoreCreateMutex");
    communication_log_heap_checkpoint("before_mutex");
    s_comm.mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreCreateMutex -> %s",
             (s_comm.mutex != NULL) ? "OK" : "NULL");
    if (s_comm.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Communication module init step: xSemaphoreCreateMutex(snapshot)");
    communication_log_heap_checkpoint("before_snapshot_mutex");
    s_comm.snapshot_mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreCreateMutex(snapshot) -> %s",
             (s_comm.snapshot_mutex != NULL) ? "OK" : "NULL");
    if (s_comm.snapshot_mutex == NULL) {
        vSemaphoreDelete(s_comm.mutex);
        s_comm.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Communication module init step: xSemaphoreTake(mutex)");
    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Communication module init step result: xSemaphoreTake(mutex) -> FAILED");
        vSemaphoreDelete(s_comm.mutex);
        vSemaphoreDelete(s_comm.snapshot_mutex);
        s_comm.mutex = NULL;
        s_comm.snapshot_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreTake(mutex) -> OK");

    ESP_LOGI(TAG, "Communication module init step: communication_alloc_rx_buffer_locked");
    communication_log_heap_checkpoint("before_rx_buffer_alloc");
    esp_err_t rx_buffer_ret = communication_alloc_rx_buffer_locked();
    ESP_LOGI(TAG, "Communication module init step result: communication_alloc_rx_buffer_locked -> %s",
             esp_err_to_name(rx_buffer_ret));
    if (rx_buffer_ret != ESP_OK) {
        xSemaphoreGive(s_comm.mutex);
        vSemaphoreDelete(s_comm.mutex);
        vSemaphoreDelete(s_comm.snapshot_mutex);
        s_comm.mutex = NULL;
        s_comm.snapshot_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Communication module init step: data_payload_init");
    communication_log_heap_checkpoint("before_data_payload_init");
    esp_err_t data_payload_ret = data_payload_init();
    if (data_payload_ret != ESP_OK) {
        ESP_LOGE(TAG, "data_payload_init failed: %s", esp_err_to_name(data_payload_ret));
        communication_free_rx_buffer_locked();
        xSemaphoreGive(s_comm.mutex);
        vSemaphoreDelete(s_comm.mutex);
        vSemaphoreDelete(s_comm.snapshot_mutex);
        s_comm.mutex = NULL;
        s_comm.snapshot_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Communication module init step result: data_payload_init -> OK");
    ESP_LOGI(TAG, "Communication module init step: communication_load_defaults_locked");
    communication_load_defaults_locked();
    ESP_LOGI(TAG, "Communication module init step result: communication_load_defaults_locked -> OK");
    s_comm.socket_fd = -1;
    s_comm.state_started_us = esp_timer_get_time();
    s_comm.last_keep_alive_us = 0;
    ESP_LOGI(TAG, "Communication module init step: xSemaphoreGive(mutex)");
    xSemaphoreGive(s_comm.mutex);
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreGive(mutex) -> OK");

    ESP_LOGI(TAG, "Communication module init step: xTaskCreate(communication_task)");
    communication_log_heap_checkpoint("before_task_create");
    BaseType_t task_ret = xTaskCreate(communication_task,
                                      COMMUNICATION_TASK_NAME,
                                      COMMUNICATION_TASK_STACK_WORDS,
                                      NULL,
                                      COMMUNICATION_TASK_PRIORITY,
                                      &s_comm.task_handle);
    ESP_LOGI(TAG, "Communication module init step result: xTaskCreate -> %s",
             (task_ret == pdPASS) ? "pdPASS" : "FAILED");
    if (task_ret != pdPASS) {
        if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) == pdTRUE) {
            communication_free_rx_buffer_locked();
            xSemaphoreGive(s_comm.mutex);
        } else {
            communication_free_rx_buffer_locked();
        }
        vSemaphoreDelete(s_comm.mutex);
        vSemaphoreDelete(s_comm.snapshot_mutex);
        s_comm.mutex = NULL;
        s_comm.snapshot_mutex = NULL;
        s_comm.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_comm.initialized = true;
    ESP_LOGI(TAG,
             "Communication task ready with default endpoint %s:%u on SSID %s",
             s_comm.snapshot.config.server_ip,
             (unsigned)s_comm.snapshot.config.server_port,
             s_comm.snapshot.config.wifi_ssid);
    ESP_LOGI(TAG, "Communication module init end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

void communication_functions_request_reset(void)
{
    if (!s_comm.initialized || s_comm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, pdMS_TO_TICKS(COMMUNICATION_API_LOCK_TIMEOUT_MS)) == pdTRUE) {
        s_comm.reset_requested = true;
        s_comm.snapshot.reset_requested = true;
        communication_publish_snapshot_locked();
        xSemaphoreGive(s_comm.mutex);
    } else {
        /* Do not block UI threads indefinitely while comm task is in Wi-Fi APIs. */
        s_comm.reset_requested = true;
        ESP_LOGW(TAG, "Reset request queued while communication mutex was busy");
    }
}

void communication_functions_request_disconnect(void)
{
    if (!s_comm.initialized || s_comm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, pdMS_TO_TICKS(COMMUNICATION_API_LOCK_TIMEOUT_MS)) == pdTRUE) {
        s_comm.disconnect_requested = true;
        communication_publish_snapshot_locked();
        xSemaphoreGive(s_comm.mutex);
    } else {
        s_comm.disconnect_requested = true;
        ESP_LOGW(TAG, "Disconnect request queued while communication mutex was busy");
    }
}

/**
 * @brief Request a Wi-Fi device discovery scan from the background task.
 *
 * @details Clears the previous visible scan text immediately and queues a new
 * `requested -> in progress -> complete/error` scan sequence.
 */
void communication_functions_request_scan(void)
{
    if (!s_comm.initialized || s_comm.mutex == NULL) {
        ESP_LOGW(TAG, "Scan request ignored because communication module is not initialized");
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, pdMS_TO_TICKS(COMMUNICATION_API_LOCK_TIMEOUT_MS)) == pdTRUE) {
        ESP_LOGI(TAG,
                 "Scan requested from UI (scan_state=%s, comm_state=%s, wifi_has_ip=%d)",
                 communication_functions_scan_state_to_string(s_comm.snapshot.scan_state),
                 communication_functions_state_to_string(s_comm.snapshot.state),
                 (int)s_comm.snapshot.wifi_has_ip);
        s_comm.snapshot.scan_requested = true;
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_REQUESTED;
        s_comm.snapshot.scan_duration_ms = 0;
        s_comm.snapshot.scan_device_count = 0;
        s_comm.snapshot.scan_results[0] = '\0';
        communication_publish_snapshot_locked();
        xSemaphoreGive(s_comm.mutex);
    } else {
        ESP_LOGW(TAG, "Scan request deferred because communication mutex was busy");
    }
}

/**
 * @brief Update the runtime Auto Reconnect policy from the UI toggle.
 *
 * @details Stores the requested policy so retry-limit handling can either
 * escalate to TopLayer fault mode (disabled) or clear counters and retry
 * automatically (enabled).
 *
 * @param[in] enabled `true` to enable auto reconnect behavior.
 */
void communication_functions_set_auto_reconnect_enabled(bool enabled)
{
    if (!s_comm.initialized || s_comm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, pdMS_TO_TICKS(COMMUNICATION_API_LOCK_TIMEOUT_MS)) == pdTRUE) {
        s_comm.snapshot.auto_reconnect_enabled = enabled;
        communication_publish_snapshot_locked();
        xSemaphoreGive(s_comm.mutex);
    } else {
        ESP_LOGW(TAG, "Auto Reconnect update skipped because communication mutex was busy");
    }
}

/**
 * @brief Queue one simulator data-control event for low-level transmission.
 *
 * @details The communication worker sends queued events as DATA text payloads
 * once keepalive is active, preserving thread ownership of the socket.
 *
 * @param[in] event_id Requested simulator data-control event.
 *
 * @return ESP_OK when queued, otherwise an ESP error code.
 */
esp_err_t communication_functions_request_data_event(communication_data_event_t event_id)
{
    const char *event_payload = NULL;

    if (!s_comm.initialized || s_comm.mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    switch (event_id) {
    case COMMUNICATION_DATA_EVENT_SIMULATION_ON:
        event_payload = COMMUNICATION_DATA_EVENT_SIMULATION_ON_TEXT;
        break;
    case COMMUNICATION_DATA_EVENT_SIMULATION_OFF:
        event_payload = COMMUNICATION_DATA_EVENT_SIMULATION_OFF_TEXT;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_comm.mutex, pdMS_TO_TICKS(COMMUNICATION_API_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Data event queue skipped because communication mutex was busy");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = communication_queue_data_command_locked(event_payload);
    communication_publish_snapshot_locked();
    xSemaphoreGive(s_comm.mutex);
    return ret;
}

esp_err_t communication_functions_get_snapshot(communication_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_comm.initialized || s_comm.mutex == NULL || s_comm.snapshot_mutex == NULL) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
        out_snapshot->state = COMMUNICATION_STATE_TOP_LAYER_RESET;
        out_snapshot->scan_state = COMMUNICATION_SCAN_STATE_IDLE;
        snprintf(out_snapshot->last_error, sizeof(out_snapshot->last_error), "Communication module not initialized");
        snprintf(out_snapshot->last_received_text,
                 sizeof(out_snapshot->last_received_text),
                 "Communication module not initialized");
        snprintf(out_snapshot->scan_results,
                 sizeof(out_snapshot->scan_results),
                 "Communication module not initialized");
        return ESP_OK;
    }

    if (xSemaphoreTake(s_comm.snapshot_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_FAIL;
    }

    *out_snapshot = s_comm.published_snapshot;
    xSemaphoreGive(s_comm.snapshot_mutex);
    return ESP_OK;
}

const char *communication_functions_state_to_string(communication_state_t state)
{
    switch (state) {
    case COMMUNICATION_STATE_TOP_LAYER_RESET:
        return "Reset";
    case COMMUNICATION_STATE_TOP_LAYER_INITIALIZE:
        return "Initialize";
    case COMMUNICATION_STATE_TOP_LAYER_CONNECT:
        return "Connect";
    case COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_SERVER_RECEIVE:
        return "KeepAliveServerReceive";
    case COMMUNICATION_STATE_TOP_LAYER_KEEPALIVE_CLIENT_SEND:
        return "KeepAliveClientSend";
    case COMMUNICATION_STATE_TOP_LAYER_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

/**
 * @brief Convert a scan state enum into printable text.
 *
 * @details Returns a short constant string for UI and runtime logs.
 *
 * @param[in] state Scan workflow state.
 *
 * @return Constant scan state text.
 */
const char *communication_functions_scan_state_to_string(communication_scan_state_t state)
{
    switch (state) {
    case COMMUNICATION_SCAN_STATE_IDLE:
        return "Idle";
    case COMMUNICATION_SCAN_STATE_REQUESTED:
        return "Requested";
    case COMMUNICATION_SCAN_STATE_IN_PROGRESS:
        return "In Progress";
    case COMMUNICATION_SCAN_STATE_COMPLETE:
        return "Complete";
    case COMMUNICATION_SCAN_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}
