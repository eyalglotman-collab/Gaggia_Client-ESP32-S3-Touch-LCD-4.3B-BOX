/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CommunicationFunctions.h"

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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "peripherals_manager.h"

#define COMMUNICATION_TASK_NAME              "comm_link"
#define COMMUNICATION_TASK_STACK_BYTES       (16384)
#define COMMUNICATION_TASK_PRIORITY          (5)
#define COMMUNICATION_STEP_PERIOD_MS         (100)
#define COMMUNICATION_DEFAULT_WIFI_SSID      "EyalSimulatorAP"
#define COMMUNICATION_DEFAULT_WIFI_PASSWORD  "espresso1234"
#define COMMUNICATION_DEFAULT_SERVER_IP      "192.168.4.1"
#define COMMUNICATION_DEFAULT_SERVER_PORT    (3333)
#define COMMUNICATION_DEFAULT_WIFI_TIMEOUT   (5000)
#define COMMUNICATION_DEFAULT_TCP_TIMEOUT    (3000)
#define COMMUNICATION_DEFAULT_KEEPALIVE_MS   (100)
#define COMMUNICATION_WIFI_RETRY_PERIOD_MS   (2500)
#define COMMUNICATION_KEEPALIVE_FAILURE_LIMIT (5)
#define COMMUNICATION_SCAN_WINDOW_MS         (10000)
#define COMMUNICATION_SCAN_MAX_APS           (10)
#define COMMUNICATION_CONNECT_PRECHECK_APS   (16)
#define COMMUNICATION_WATCHDOG_GRACE_MS      (350)
#define COMMUNICATION_FRAME_SOF0             (0xA5U)
#define COMMUNICATION_FRAME_SOF1             (0x5AU)
#define COMMUNICATION_FRAME_HEADER_BYTES     (15U)
#define COMMUNICATION_FRAME_CRC_BYTES        (2U)
#define COMMUNICATION_FRAME_MAX_PAYLOAD      (160U)
#define COMMUNICATION_RX_BUFFER_BYTES        (2048U)

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

typedef struct {
    SemaphoreHandle_t mutex;
    TaskHandle_t task_handle;
    communication_snapshot_t snapshot;
    bool initialized;
    bool reset_requested;
    bool disconnect_requested;
    bool wifi_connect_started;
    bool tcp_connect_started;
    bool keepalive_ack_pending;
    int64_t connect_requested_us;
    int64_t last_valid_rx_us;
    int64_t last_wifi_connect_attempt_us;
    int64_t scan_started_us;
    int socket_fd;
    int64_t state_started_us;
    int64_t last_keep_alive_us;
    size_t rx_buffer_len;
    uint8_t rx_buffer[COMMUNICATION_RX_BUFFER_BYTES];
} communication_context_t;

static communication_context_t s_comm = {
    .mutex = NULL,
    .task_handle = NULL,
    .snapshot = {0},
    .initialized = false,
    .reset_requested = false,
    .disconnect_requested = false,
    .wifi_connect_started = false,
    .tcp_connect_started = false,
    .keepalive_ack_pending = false,
    .connect_requested_us = 0,
    .last_valid_rx_us = 0,
    .last_wifi_connect_attempt_us = 0,
    .scan_started_us = 0,
    .socket_fd = -1,
    .state_started_us = 0,
    .last_keep_alive_us = 0,
    .rx_buffer_len = 0,
    .rx_buffer = {0},
};

static void communication_close_socket_locked(void);
static void communication_enter_state_locked(communication_state_t next_state);
static void communication_schedule_reconnect_locked(const char *reason_text, bool keepalive_failure);
static size_t communication_append_text(char *buffer, size_t buffer_len, size_t offset, const char *text);
static size_t communication_append_u32(char *buffer, size_t buffer_len, size_t offset, uint32_t value);

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
 * @brief Reset framed-link bookkeeping while keeping configured defaults.
 *
 * @details Clears counters, RX buffering, and staged progression flags so the
 * next initialize sequence begins from a clean low-level transport baseline.
 * This guarantees that the first live counter value after initialization comes
 * from the server-side `ServerLiveInteger`.
 */
static void communication_clear_transport_flow_locked(void)
{
    s_comm.snapshot.initialize_passed = false;
    s_comm.snapshot.connect_passed = false;
    s_comm.snapshot.send_data_enabled = false;
    s_comm.snapshot.server_live_integer = 0;
    s_comm.snapshot.client_live_integer = 0;
    s_comm.snapshot.sequence = 0;
    s_comm.keepalive_ack_pending = false;
    s_comm.connect_requested_us = 0;
    s_comm.last_valid_rx_us = 0;
    s_comm.last_keep_alive_us = 0;
    s_comm.rx_buffer_len = 0;
    snprintf(s_comm.snapshot.last_received_text,
             sizeof(s_comm.snapshot.last_received_text),
             "No peer text received yet");
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
    (void)communication_append_u32(buffer, buffer_len, offset, s_comm.snapshot.config.keep_alive_period_ms);
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
static esp_err_t communication_send_frame_locked(communication_message_type_t message_type, const char *payload_text)
{
    uint8_t frame[COMMUNICATION_FRAME_HEADER_BYTES + COMMUNICATION_FRAME_MAX_PAYLOAD + COMMUNICATION_FRAME_CRC_BYTES] = {0};
    size_t payload_len = 0U;
    size_t frame_len = 0U;
    const char *safe_payload = (payload_text != NULL) ? payload_text : "";

    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        communication_set_generic_failure_locked("Frame send", "Socket is not connected");
        return ESP_FAIL;
    }

    payload_len = strnlen(safe_payload, COMMUNICATION_FRAME_MAX_PAYLOAD);
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
    if (payload_len > 0U) {
        memcpy(&frame[COMMUNICATION_FRAME_HEADER_BYTES], safe_payload, payload_len);
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

/**
 * @brief Consume one validated framed packet from the TCP stream.
 *
 * @details Parses the shared framed protocol from the non-blocking socket and
 * updates client-side state progression, counters, and last received text.
 */
static void communication_poll_received_frames_locked(void)
{
    uint8_t temp[128];

    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        return;
    }

    while (true) {
        int bytes_read = recv(s_comm.socket_fd, temp, sizeof(temp), MSG_DONTWAIT);
        if (bytes_read > 0) {
            size_t copy_len = (size_t)bytes_read;
            if ((s_comm.rx_buffer_len + copy_len) > sizeof(s_comm.rx_buffer)) {
                ESP_LOGW(TAG,
                         "RX buffer overflow (%u + %u > %u), clearing buffered frames",
                         (unsigned)s_comm.rx_buffer_len,
                         (unsigned)copy_len,
                         (unsigned)sizeof(s_comm.rx_buffer));
                s_comm.rx_buffer_len = 0U;
            }
            memcpy(&s_comm.rx_buffer[s_comm.rx_buffer_len], temp, copy_len);
            s_comm.rx_buffer_len += copy_len;
        } else {
            if (bytes_read == 0) {
                communication_schedule_reconnect_locked("TCP peer closed socket", false);
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
            communication_schedule_reconnect_locked("RX payload too large", false);
            return;
        }
        if (s_comm.rx_buffer_len < frame_len) {
            return;
        }

        uint16_t expected_crc = (uint16_t)s_comm.rx_buffer[frame_len - 2U] |
                                ((uint16_t)s_comm.rx_buffer[frame_len - 1U] << 8);
        uint16_t actual_crc = communication_crc16_ccitt(s_comm.rx_buffer, frame_len - COMMUNICATION_FRAME_CRC_BYTES);
        if (expected_crc != actual_crc) {
            communication_schedule_reconnect_locked("CRC mismatch", false);
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
        s_comm.last_valid_rx_us = esp_timer_get_time();

        char payload_text[COMMUNICATION_FRAME_MAX_PAYLOAD + 1U] = {0};
        if (payload_len > 0U) {
            memcpy(payload_text, &s_comm.rx_buffer[COMMUNICATION_FRAME_HEADER_BYTES], payload_len);
            payload_text[payload_len] = '\0';
        }

        if (message_type == COMMUNICATION_MESSAGE_ERROR) {
            communication_schedule_reconnect_locked(
                (payload_text[0] != '\0') ? payload_text : "Peer reported error",
                false);
            return;
        }

        if (message_type == COMMUNICATION_MESSAGE_DATA) {
            snprintf(s_comm.snapshot.last_received_text,
                     sizeof(s_comm.snapshot.last_received_text),
                     "%s",
                     (payload_text[0] != '\0') ? payload_text : "Empty peer payload");
            s_comm.snapshot.send_data_enabled = true;
        }

        if (s_comm.snapshot.state == COMMUNICATION_STATE_CONNECT &&
            (strcmp(payload_text, "client_connected") == 0 ||
             strcmp(payload_text, "connect_success") == 0 ||
             strcmp(payload_text, "tcp_connected") == 0)) {
            s_comm.snapshot.connect_passed = true;
            s_comm.snapshot.send_data_enabled = true;
            s_comm.keepalive_ack_pending = false;
            communication_enter_state_locked(COMMUNICATION_STATE_KEEPALIVE);
        }

        if (message_type == COMMUNICATION_MESSAGE_KEEPALIVE) {
            uint32_t expected_server_ack_client_live_integer = s_comm.snapshot.client_live_integer;
            uint32_t expected_client_live_integer = received_server_live_integer + 1U;

            if (received_client_live_integer != expected_server_ack_client_live_integer) {
                communication_schedule_reconnect_locked("keepalive_counter_mismatch", true);
                return;
            }

            s_comm.snapshot.server_live_integer = received_server_live_integer;
            s_comm.snapshot.client_live_integer = expected_client_live_integer;
            communication_clear_connection_fault_locked();
            if (communication_send_frame_locked(COMMUNICATION_MESSAGE_KEEPALIVE, "keepalive") != ESP_OK) {
                communication_schedule_reconnect_locked(s_comm.snapshot.last_error, true);
                return;
            }
            s_comm.keepalive_ack_pending = false;
            communication_enter_state_locked(COMMUNICATION_STATE_KEEPALIVE);
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
        return;
    }

    s_comm.snapshot.wifi_rssi = -127;
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
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT && ret != ESP_ERR_WIFI_CONN) {
        communication_set_generic_failure_locked("Wi-Fi disconnect retry", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Communication initialize retry step: esp_wifi_connect");
    ret = esp_wifi_connect();
    ESP_LOGI(TAG, "Communication initialize retry result: esp_wifi_connect -> %s", esp_err_to_name(ret));
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi connect retry", esp_err_to_name(ret));
        return ret;
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

    if (next_state == COMMUNICATION_STATE_RESET) {
        s_comm.last_keep_alive_us = 0;
    }
}

/**
 * @brief Schedule a reconnect attempt and optionally latch a connection fault.
 *
 * @details Stores the latest retry reason, increments the sequential
 * keepalive-failure counter when applicable, latches the connection fault after
 * repeated keepalive failures, and either routes the workflow back toward
 * `connect` or stops in `wait_for_com_reset` until the operator explicitly
 * presses Reset Connection.
 *
 * @param[in] reason_text NUL-terminated retry description.
 * @param[in] keepalive_failure Whether this retry counts toward the sequential
 *            keepalive-failure latch.
 */
static void communication_schedule_reconnect_locked(const char *reason_text, bool keepalive_failure)
{
    const char *resolved_error = reason_text;
    char error_copy[sizeof(s_comm.snapshot.last_error)] = {0};

    if (resolved_error == NULL) {
        resolved_error = "Unknown communication retry reason";
    }

    snprintf(error_copy, sizeof(error_copy), "%s", resolved_error);
    memcpy(s_comm.snapshot.last_error, error_copy, sizeof(s_comm.snapshot.last_error));
    ESP_LOGW(TAG, "Communication retry scheduled: %s", s_comm.snapshot.last_error);

    if (keepalive_failure) {
        s_comm.snapshot.consecutive_keepalive_failures++;
        ESP_LOGW(TAG,
                 "Keepalive failure count: %" PRIu32 "/%u",
                 s_comm.snapshot.consecutive_keepalive_failures,
                 COMMUNICATION_KEEPALIVE_FAILURE_LIMIT);
        if (s_comm.snapshot.consecutive_keepalive_failures >= COMMUNICATION_KEEPALIVE_FAILURE_LIMIT) {
            s_comm.snapshot.connection_fault = true;
            ESP_LOGW(TAG, "ConnectionFault latched: repeated keepalive failures, waiting for Reset Connection");
        }
    }

    communication_close_socket_locked();
    s_comm.snapshot.connect_passed = false;
    s_comm.snapshot.send_data_enabled = false;
    s_comm.keepalive_ack_pending = false;
    s_comm.connect_requested_us = 0;
    s_comm.last_valid_rx_us = 0;
    if (s_comm.snapshot.connection_fault) {
        communication_enter_state_locked(COMMUNICATION_STATE_WAIT_FOR_COM_RESET);
    } else {
        communication_enter_state_locked(COMMUNICATION_STATE_CONNECT);
    }
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
    s_comm.snapshot.state = COMMUNICATION_STATE_RESET;
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
    snprintf(s_comm.snapshot.last_received_text,
             sizeof(s_comm.snapshot.last_received_text),
             "No peer text received yet");
    communication_reset_scan_locked();
    communication_clear_transport_flow_locked();
}

/**
 * @brief Begin a clean reset cycle for the low-level link.
 *
 * @details Clears the active TCP socket, drops the current station session, and
 * moves the task back to `RESET` so the following tick can reinitialize Wi-Fi.
 */
static void communication_start_reset_locked(void)
{
    communication_close_socket_locked();
    (void)esp_wifi_disconnect();
    s_comm.snapshot.reset_requested = false;
    snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "No error");
    s_comm.wifi_connect_started = false;
    s_comm.tcp_connect_started = false;
    s_comm.last_wifi_connect_attempt_us = 0;
    s_comm.snapshot.connection_fault = false;
    s_comm.snapshot.consecutive_keepalive_failures = 0;
    communication_refresh_local_ip_locked();
    communication_refresh_rssi_locked();
    communication_reset_scan_locked();
    communication_clear_transport_flow_locked();
    communication_enter_state_locked(COMMUNICATION_STATE_RESET);
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
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        if (ret != ESP_ERR_INVALID_STATE && ret != ESP_ERR_NO_MEM) {
            communication_set_generic_failure_locked("Initialize precheck", esp_err_to_name(ret));
        }
        return ret;
    }
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG,
                 "Configured Wi-Fi AP '%s' not visible yet; continuing initialize retry window",
                 s_comm.snapshot.config.wifi_ssid);
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
    if (ret != ESP_OK) {
        communication_set_generic_failure_locked("Wi-Fi connect", esp_err_to_name(ret));
        return ret;
    }

    s_comm.wifi_connect_started = true;
    s_comm.last_wifi_connect_attempt_us = esp_timer_get_time();
    communication_enter_state_locked(COMMUNICATION_STATE_INITIALIZE);
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
    ESP_LOGI(TAG, "Communication TCP step result: initialize counter baseline -> server=0 client=0");

    char initialize_payload[COMMUNICATION_FRAME_MAX_PAYLOAD + 1U] = {0};
    communication_build_initialize_payload_locked(initialize_payload, sizeof(initialize_payload));
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
    communication_enter_state_locked(COMMUNICATION_STATE_CONNECT);
    ESP_LOGI(TAG, "Communication TCP connect end -> %s", esp_err_to_name(ESP_OK));
    return ESP_OK;
}

/**
 * @brief Supervise server-driven keep-alive while the TCP socket is active.
 *
 * @details The ESP32-C3 server owns keepalive initiation. The client therefore
 * does not emit periodic heartbeat traffic on its own; it only responds to the
 * latest `ServerLiveInteger` sent by the server and validates that keepalive
 * traffic continues to arrive within the configured watchdog window.
 */
static void communication_service_keep_alive_locked(void)
{
    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        return;
    }
}

/**
 * @brief Start the dedicated Wi-Fi device discovery scan.
 *
 * @details Clears the previous text immediately, requests a non-blocking scan
 * from the ESP-IDF Wi-Fi driver, and marks the scan workflow as active.
 */
static void communication_begin_scan_locked(void)
{
    esp_err_t ret = communication_ensure_wifi_stack_ready_locked();
    if (ret != ESP_OK) {
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

    ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) {
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan failed to start: %s",
                 esp_err_to_name(ret));
        return;
    }

    s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_IN_PROGRESS;
    s_comm.scan_started_us = esp_timer_get_time();
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

    (void)esp_wifi_scan_stop();

    ret = esp_wifi_scan_get_ap_num(&total_ap_count);
    if (ret != ESP_OK) {
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan result read failed: %s",
                 esp_err_to_name(ret));
        return;
    }

    wifi_ap_record_t *ap_records = communication_alloc_ap_records(ap_count);
    if (ap_count > 0U && ap_records == NULL) {
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_ERROR;
        snprintf(s_comm.snapshot.scan_results,
                 sizeof(s_comm.snapshot.scan_results),
                 "Scan device allocation failed");
        return;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (ret != ESP_OK) {
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

    if (total_ap_count == 0 || ap_count == 0) {
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
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_REQUESTED;
        communication_begin_scan_locked();
    }

    if (s_comm.snapshot.scan_state == COMMUNICATION_SCAN_STATE_IN_PROGRESS) {
        int64_t elapsed_ms = (esp_timer_get_time() - s_comm.scan_started_us) / 1000LL;
        if (elapsed_ms >= COMMUNICATION_SCAN_WINDOW_MS) {
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
 * `wait_for_com_reset` until the operator requests a reset.
 */
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
    case COMMUNICATION_STATE_RESET: {
        esp_err_t ret = communication_begin_initialize_locked();
        if (ret != ESP_OK) {
            if (s_comm.snapshot.last_error[0] == '\0' ||
                strcmp(s_comm.snapshot.last_error, "No error") == 0) {
                communication_set_generic_failure_locked("Initialize start", esp_err_to_name(ret));
            }
            communication_enter_state_locked(COMMUNICATION_STATE_INITIALIZE);
        }
        break;
    }

    case COMMUNICATION_STATE_INITIALIZE: {
        int64_t elapsed_ms = (esp_timer_get_time() - s_comm.state_started_us) / 1000LL;
        if (s_comm.snapshot.wifi_has_ip) {
            communication_enter_state_locked(COMMUNICATION_STATE_CONNECT);
        } else if ((uint32_t)elapsed_ms >= s_comm.snapshot.config.wifi_connect_timeout_ms) {
            snprintf(s_comm.snapshot.last_error,
                     sizeof(s_comm.snapshot.last_error),
                     "Wi-Fi AP '%s' not available after %" PRIu32 " ms",
                     s_comm.snapshot.config.wifi_ssid,
                     s_comm.snapshot.config.wifi_connect_timeout_ms);
            communication_enter_state_locked(COMMUNICATION_STATE_RESET);
        } else if (s_comm.wifi_connect_started) {
            esp_err_t ret = communication_retry_wifi_connect_locked();
            if (ret != ESP_OK) {
                communication_enter_state_locked(COMMUNICATION_STATE_RESET);
            }
        }
        break;
    }

    case COMMUNICATION_STATE_CONNECT:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_close_socket_locked();
            s_comm.snapshot.connect_passed = false;
            s_comm.snapshot.send_data_enabled = false;
            s_comm.connect_requested_us = 0;
            communication_enter_state_locked(COMMUNICATION_STATE_INITIALIZE);
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            esp_err_t ret = communication_open_tcp_socket_locked();
            if (ret != ESP_OK &&
                (s_comm.snapshot.last_error[0] == '\0' ||
                 strcmp(s_comm.snapshot.last_error, "No error") == 0)) {
                communication_set_generic_failure_locked("TCP connect", strerror(errno));
            }
            break;
        }
        if (s_comm.connect_requested_us != 0 &&
            ((uint32_t)((esp_timer_get_time() - s_comm.connect_requested_us) / 1000LL) >=
             s_comm.snapshot.config.tcp_connect_timeout_ms)) {
            communication_schedule_reconnect_locked("Connect response timeout", false);
            break;
        }
        break;

    case COMMUNICATION_STATE_KEEPALIVE:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_schedule_reconnect_locked("Wi-Fi link lost", true);
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            communication_schedule_reconnect_locked("TCP socket lost", true);
            break;
        }
        communication_service_keep_alive_locked();
        if (s_comm.last_valid_rx_us != 0 &&
            ((uint32_t)((esp_timer_get_time() - s_comm.last_valid_rx_us) / 1000LL) >= COMMUNICATION_WATCHDOG_GRACE_MS)) {
            communication_schedule_reconnect_locked("Server keepalive timeout", true);
        }
        break;

    case COMMUNICATION_STATE_WAIT_FOR_COM_RESET:
        break;

    default:
        break;
    }

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

    while (true) {
        communication_task_step();
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
    if (offline) {
        ESP_LOGI(TAG, "Communication init running in offline mode; caller downgrades failures to warnings");
    }

    if (s_comm.initialized) {
        ESP_LOGI(TAG, "Communication module init end -> already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Communication module init step: xSemaphoreCreateMutex");
    s_comm.mutex = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreCreateMutex -> %s",
             (s_comm.mutex != NULL) ? "OK" : "NULL");
    if (s_comm.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Communication module init step: xSemaphoreTake(mutex)");
    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGW(TAG, "Communication module init step result: xSemaphoreTake(mutex) -> FAILED");
        vSemaphoreDelete(s_comm.mutex);
        s_comm.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Communication module init step result: xSemaphoreTake(mutex) -> OK");

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
    BaseType_t task_ret = xTaskCreate(communication_task,
                                      COMMUNICATION_TASK_NAME,
                                      COMMUNICATION_TASK_STACK_BYTES,
                                      NULL,
                                      COMMUNICATION_TASK_PRIORITY,
                                      &s_comm.task_handle);
    ESP_LOGI(TAG, "Communication module init step result: xTaskCreate -> %s",
             (task_ret == pdPASS) ? "pdPASS" : "FAILED");
    if (task_ret != pdPASS) {
        vSemaphoreDelete(s_comm.mutex);
        s_comm.mutex = NULL;
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

    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) == pdTRUE) {
        s_comm.reset_requested = true;
        s_comm.snapshot.reset_requested = true;
        xSemaphoreGive(s_comm.mutex);
    }
}

void communication_functions_request_disconnect(void)
{
    if (!s_comm.initialized || s_comm.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) == pdTRUE) {
        s_comm.disconnect_requested = true;
        xSemaphoreGive(s_comm.mutex);
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
        return;
    }

    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) == pdTRUE) {
        s_comm.snapshot.scan_requested = true;
        s_comm.snapshot.scan_state = COMMUNICATION_SCAN_STATE_REQUESTED;
        s_comm.snapshot.scan_duration_ms = 0;
        s_comm.snapshot.scan_device_count = 0;
        s_comm.snapshot.scan_results[0] = '\0';
        xSemaphoreGive(s_comm.mutex);
    }
}

esp_err_t communication_functions_get_snapshot(communication_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_comm.initialized || s_comm.mutex == NULL) {
        memset(out_snapshot, 0, sizeof(*out_snapshot));
        out_snapshot->state = COMMUNICATION_STATE_RESET;
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

    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    *out_snapshot = s_comm.snapshot;
    xSemaphoreGive(s_comm.mutex);
    return ESP_OK;
}

const char *communication_functions_state_to_string(communication_state_t state)
{
    switch (state) {
    case COMMUNICATION_STATE_RESET:
        return "Reset";
    case COMMUNICATION_STATE_INITIALIZE:
        return "Initialize";
    case COMMUNICATION_STATE_CONNECT:
        return "Connect";
    case COMMUNICATION_STATE_KEEPALIVE:
        return "Keepalive";
    case COMMUNICATION_STATE_WAIT_FOR_COM_RESET:
        return "Wait For Com Reset";
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
