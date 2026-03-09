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
#define COMMUNICATION_DEFAULT_WIFI_TIMEOUT   (10000)
#define COMMUNICATION_DEFAULT_TCP_TIMEOUT    (3000)
#define COMMUNICATION_DEFAULT_KEEPALIVE_MS   (100)
#define COMMUNICATION_SCAN_WINDOW_MS         (10000)
#define COMMUNICATION_SCAN_MAX_APS           (10)
#define COMMUNICATION_CONNECT_PRECHECK_APS   (16)

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
    int64_t scan_started_us;
    int socket_fd;
    int64_t state_started_us;
    int64_t last_keep_alive_us;
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
    .scan_started_us = 0,
    .socket_fd = -1,
    .state_started_us = 0,
    .last_keep_alive_us = 0,
};

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
    esp_err_t ret = peripherals_manager_init_wifi();
    if (ret == ESP_OK) {
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
 * avoid repeated association attempts against an AP that is not currently
 * visible. This keeps the module in a deterministic error state with a clear
 * message instead of relying on repeated driver warnings alone.
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

    snprintf(s_comm.snapshot.last_error,
             sizeof(s_comm.snapshot.last_error),
             "Configured SSID '%s' is not visible",
             s_comm.snapshot.config.wifi_ssid);
    return ESP_ERR_NOT_FOUND;
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

    if (next_state != COMMUNICATION_STATE_CONNECT) {
        s_comm.snapshot.live_integer = 0;
        s_comm.last_keep_alive_us = 0;
    }
}

/**
 * @brief Store a human-readable low-level transport error.
 *
 * @details Copies the latest failure text into the snapshot so UI code can show
 * the reason the state machine moved into `ERROR`.
 *
 * @param[in] error_text NUL-terminated error description.
 */
static void communication_set_error_locked(const char *error_text)
{
    snprintf(s_comm.snapshot.last_error,
             sizeof(s_comm.snapshot.last_error),
             "%s",
             (error_text != NULL) ? error_text : "Unknown communication error");
    communication_enter_state_locked(COMMUNICATION_STATE_ERROR);
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
    s_comm.snapshot.state = COMMUNICATION_STATE_DISCONNECT;
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
    communication_reset_scan_locked();
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
    communication_refresh_local_ip_locked();
    communication_refresh_rssi_locked();
    communication_reset_scan_locked();
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
    esp_err_t ret = communication_ensure_wifi_stack_ready_locked();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = communication_validate_target_ap_visible_locked();
    if (ret != ESP_OK) {
        return ret;
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

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        return ret;
    }

    s_comm.wifi_connect_started = true;
    communication_enter_state_locked(COMMUNICATION_STATE_INITIALIZE);
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
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_comm.snapshot.config.server_port),
    };

    if (inet_pton(AF_INET, s_comm.snapshot.config.server_ip, &server_addr.sin_addr) != 1) {
        return ESP_ERR_INVALID_ARG;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd < 0) {
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = (time_t)(s_comm.snapshot.config.tcp_connect_timeout_ms / 1000U),
        .tv_usec = (suseconds_t)((s_comm.snapshot.config.tcp_connect_timeout_ms % 1000U) * 1000U),
    };
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        int socket_errno = errno;
        close(socket_fd);
        errno = socket_errno;
        return ESP_FAIL;
    }

    s_comm.socket_fd = socket_fd;
    s_comm.snapshot.tcp_connected = true;
    s_comm.tcp_connect_started = true;
    s_comm.last_keep_alive_us = 0;
    communication_enter_state_locked(COMMUNICATION_STATE_CONNECT);
    return ESP_OK;
}

/**
 * @brief Advance the periodic keep-alive while the TCP socket is active.
 *
 * @details Sends a compact line-oriented keep-alive message at the configured
 * interval so the low-level link visibly progresses once connected.
 */
static void communication_service_keep_alive_locked(void)
{
    if (s_comm.socket_fd < 0 || !s_comm.snapshot.tcp_connected) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t period_us = (int64_t)s_comm.snapshot.config.keep_alive_period_ms * 1000LL;
    if (s_comm.last_keep_alive_us != 0 && (now_us - s_comm.last_keep_alive_us) < period_us) {
        return;
    }

    char payload[48];
    int payload_len = snprintf(payload, sizeof(payload), "LIVE:%" PRIu32 "\n", s_comm.snapshot.live_integer + 1U);
    int bytes_sent = send(s_comm.socket_fd, payload, (size_t)payload_len, 0);
    if (bytes_sent != payload_len) {
        char error_text[96];
        snprintf(error_text,
                 sizeof(error_text),
                 "Keep-alive send failed (%d)",
                 errno);
        communication_set_error_locked(error_text);
        communication_close_socket_locked();
        return;
    }

    s_comm.snapshot.live_integer++;
    s_comm.last_keep_alive_us = now_us;
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
 * @details Polls Wi-Fi/IP readiness, reacts to operator reset/disconnect
 * requests, opens the TCP transport when Wi-Fi is ready, and latches failures
 * into the `ERROR` state for explicit operator recovery.
 */
static void communication_task_step(void)
{
    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    communication_refresh_local_ip_locked();
    communication_refresh_rssi_locked();
    communication_service_scan_locked();

    if (s_comm.disconnect_requested) {
        s_comm.disconnect_requested = false;
        communication_close_socket_locked();
        (void)esp_wifi_disconnect();
        communication_enter_state_locked(COMMUNICATION_STATE_DISCONNECT);
        snprintf(s_comm.snapshot.last_error, sizeof(s_comm.snapshot.last_error), "Disconnected by operator");
        xSemaphoreGive(s_comm.mutex);
        return;
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
            char error_text[96];
            snprintf(error_text,
                     sizeof(error_text),
                     "Initialize start failed (%s)",
                     esp_err_to_name(ret));
            communication_set_error_locked(error_text);
        }
        break;
    }

    case COMMUNICATION_STATE_INITIALIZE: {
        int64_t elapsed_ms = (esp_timer_get_time() - s_comm.state_started_us) / 1000LL;
        if (s_comm.snapshot.wifi_has_ip) {
            esp_err_t ret = communication_open_tcp_socket_locked();
            if (ret != ESP_OK) {
                char error_text[96];
                snprintf(error_text,
                         sizeof(error_text),
                         "TCP connect failed (%d)",
                         errno);
                communication_set_error_locked(error_text);
            }
        } else if ((uint32_t)elapsed_ms >= s_comm.snapshot.config.wifi_connect_timeout_ms) {
            communication_set_error_locked("Wi-Fi connect timeout");
        }
        break;
    }

    case COMMUNICATION_STATE_CONNECT:
        if (!s_comm.snapshot.wifi_has_ip) {
            communication_set_error_locked("Wi-Fi link lost");
            communication_close_socket_locked();
            break;
        }
        if (!s_comm.snapshot.tcp_connected || s_comm.socket_fd < 0) {
            communication_set_error_locked("TCP socket lost");
            break;
        }
        communication_service_keep_alive_locked();
        break;

    case COMMUNICATION_STATE_DISCONNECT:
    case COMMUNICATION_STATE_ERROR:
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

esp_err_t communication_functions_init(void)
{
    if (s_comm.initialized) {
        return ESP_OK;
    }

    s_comm.mutex = xSemaphoreCreateMutex();
    if (s_comm.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xSemaphoreTake(s_comm.mutex, portMAX_DELAY) != pdTRUE) {
        vSemaphoreDelete(s_comm.mutex);
        s_comm.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    communication_load_defaults_locked();
    s_comm.socket_fd = -1;
    s_comm.state_started_us = esp_timer_get_time();
    s_comm.last_keep_alive_us = 0;
    xSemaphoreGive(s_comm.mutex);

    BaseType_t task_ret = xTaskCreate(communication_task,
                                      COMMUNICATION_TASK_NAME,
                                      COMMUNICATION_TASK_STACK_BYTES,
                                      NULL,
                                      COMMUNICATION_TASK_PRIORITY,
                                      &s_comm.task_handle);
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
        out_snapshot->state = COMMUNICATION_STATE_DISCONNECT;
        out_snapshot->scan_state = COMMUNICATION_SCAN_STATE_IDLE;
        snprintf(out_snapshot->last_error, sizeof(out_snapshot->last_error), "Communication module not initialized");
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
    case COMMUNICATION_STATE_DISCONNECT:
        return "Disconnect";
    case COMMUNICATION_STATE_ERROR:
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
