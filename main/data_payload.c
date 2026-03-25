/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file data_payload.c
 *
 * @brief Downlink (server → client) receive FIFO and uplink (client → server)
 *        transmit FIFO for the binary data payload channel.
 *
 * @details Both FIFOs are classic ring buffers protected by individual
 * FreeRTOS mutexes so the communication task and application tasks can access
 * them concurrently without contention. Large packet arrays are allocated at
 * runtime with SPIRAM preference to avoid starving internal RAM.
 */

#include "data_payload.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "data_payload";

/* ---------------------------------------------------------------------------
 * Internal FIFO structures
 * ---------------------------------------------------------------------------*/

typedef struct {
    data_downlink_packet_t *buf;
    uint32_t               capacity;
    uint32_t               head;    /* next write index */
    uint32_t               tail;    /* next read index  */
    uint32_t               count;   /* items currently stored */
    SemaphoreHandle_t      mutex;
} downlink_fifo_t;

typedef struct {
    data_uplink_packet_t *buf;
    uint32_t             capacity;
    uint32_t             head;
    uint32_t             tail;
    uint32_t             count;
    SemaphoreHandle_t    mutex;
} uplink_fifo_t;

static downlink_fifo_t s_dl;
static uplink_fifo_t   s_ul;
static bool            s_initialized = false;

#define DATA_DOWNLINK_FIFO_MIN_DEPTH  (4U)
#define DATA_UPLINK_FIFO_MIN_DEPTH    (2U)

/**
 * @brief Allocate packet FIFO storage with PSRAM preference.
 *
 * @details Large packet buffers can consume significant internal DRAM once the
 * payload shape is expanded (e.g., 100-float packets). Prefer SPIRAM to keep
 * enough internal heap for FreeRTOS objects and tasks.
 */
static void *data_payload_alloc_buffer(size_t size_bytes)
{
    void *ptr = heap_caps_malloc(size_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = heap_caps_malloc(size_bytes, MALLOC_CAP_8BIT);
    }
    return ptr;
}

/**
 * @brief Allocate packet storage with graceful depth fallback.
 *
 * @details Starts from the configured FIFO depth and progressively halves the
 * request until allocation succeeds or the minimum depth is reached.
 */
static void *data_payload_alloc_with_fallback(size_t packet_size,
                                              uint32_t requested_depth,
                                              uint32_t min_depth,
                                              uint32_t *resolved_depth,
                                              const char *label)
{
    uint32_t depth = requested_depth;
    void *buffer = NULL;

    while (depth >= min_depth) {
        size_t bytes = packet_size * (size_t)depth;
        buffer = data_payload_alloc_buffer(bytes);
        if (buffer != NULL) {
            if (resolved_depth != NULL) {
                *resolved_depth = depth;
            }
            ESP_LOGI(TAG,
                     "%s buffer allocated: depth=%u bytes=%u",
                     (label != NULL) ? label : "fifo",
                     (unsigned)depth,
                     (unsigned)bytes);
            return buffer;
        }

        if (depth == min_depth) {
            break;
        }

        depth /= 2U;
        if (depth < min_depth) {
            depth = min_depth;
        }
    }

    if (resolved_depth != NULL) {
        *resolved_depth = 0U;
    }

    ESP_LOGE(TAG,
             "%s buffer allocation failed (requested_depth=%u min_depth=%u packet_size=%u)",
             (label != NULL) ? label : "fifo",
             (unsigned)requested_depth,
             (unsigned)min_depth,
             (unsigned)packet_size);
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

esp_err_t data_payload_init(void)
{
    size_t dl_requested_bytes = sizeof(data_downlink_packet_t) * DATA_DOWNLINK_FIFO_DEPTH;
    size_t ul_requested_bytes = sizeof(data_uplink_packet_t) * DATA_UPLINK_FIFO_DEPTH;

    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_dl, 0, sizeof(s_dl));
    memset(&s_ul, 0, sizeof(s_ul));

    ESP_LOGI(TAG,
             "Init requested: dl_bytes=%u ul_bytes=%u internal_free=%u psram_free=%u",
             (unsigned)dl_requested_bytes,
             (unsigned)ul_requested_bytes,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    s_dl.buf = (data_downlink_packet_t *)data_payload_alloc_with_fallback(
        sizeof(data_downlink_packet_t),
        DATA_DOWNLINK_FIFO_DEPTH,
        DATA_DOWNLINK_FIFO_MIN_DEPTH,
        &s_dl.capacity,
        "Downlink");
    if (s_dl.buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memset(s_dl.buf, 0, sizeof(data_downlink_packet_t) * (size_t)s_dl.capacity);

    s_ul.buf = (data_uplink_packet_t *)data_payload_alloc_with_fallback(
        sizeof(data_uplink_packet_t),
        DATA_UPLINK_FIFO_DEPTH,
        DATA_UPLINK_FIFO_MIN_DEPTH,
        &s_ul.capacity,
        "Uplink");
    if (s_ul.buf == NULL) {
        heap_caps_free(s_dl.buf);
        s_dl.buf = NULL;
        s_dl.capacity = 0U;
        return ESP_ERR_NO_MEM;
    }
    memset(s_ul.buf, 0, sizeof(data_uplink_packet_t) * (size_t)s_ul.capacity);

    s_dl.mutex = xSemaphoreCreateMutex();
    if (s_dl.mutex == NULL) {
        ESP_LOGE(TAG, "Downlink mutex allocation failed");
        heap_caps_free(s_ul.buf);
        s_ul.buf = NULL;
        s_ul.capacity = 0U;
        heap_caps_free(s_dl.buf);
        s_dl.buf = NULL;
        s_dl.capacity = 0U;
        return ESP_ERR_NO_MEM;
    }

    s_ul.mutex = xSemaphoreCreateMutex();
    if (s_ul.mutex == NULL) {
        ESP_LOGE(TAG, "Uplink mutex allocation failed");
        vSemaphoreDelete(s_dl.mutex);
        s_dl.mutex = NULL;
        heap_caps_free(s_ul.buf);
        s_ul.buf = NULL;
        s_ul.capacity = 0U;
        heap_caps_free(s_dl.buf);
        s_dl.buf = NULL;
        s_dl.capacity = 0U;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "Init complete: dl_depth=%u ul_depth=%u internal_free=%u psram_free=%u",
             (unsigned)s_dl.capacity,
             (unsigned)s_ul.capacity,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return ESP_OK;
}

/* --- Downlink FIFO ------------------------------------------------------- */

data_fifo_result_t data_downlink_push(const data_downlink_packet_t *pkt)
{
    bool dropped_oldest = false;

    if (pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }
    if (!s_initialized || s_dl.mutex == NULL || s_dl.buf == NULL || s_dl.capacity == 0U) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_dl.mutex, portMAX_DELAY);

    if (s_dl.count >= s_dl.capacity) {
        /* Keep comm task timing deterministic under burst load by dropping
         * the oldest packet and preserving the newest downlink sample.
         */
        s_dl.tail = (s_dl.tail + 1U) % s_dl.capacity;
        s_dl.count--;
        dropped_oldest = true;
    }

    memcpy(&s_dl.buf[s_dl.head], pkt, sizeof(*pkt));
    s_dl.head = (s_dl.head + 1U) % s_dl.capacity;
    s_dl.count++;

    xSemaphoreGive(s_dl.mutex);
    return dropped_oldest ? DATA_FIFO_FULL : DATA_FIFO_OK;
}

data_fifo_result_t data_downlink_pop(data_downlink_packet_t *out_pkt)
{
    if (out_pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }
    if (!s_initialized || s_dl.mutex == NULL || s_dl.buf == NULL || s_dl.capacity == 0U) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_dl.mutex, portMAX_DELAY);

    if (s_dl.count == 0U) {
        xSemaphoreGive(s_dl.mutex);
        return DATA_FIFO_EMPTY;
    }

    memcpy(out_pkt, &s_dl.buf[s_dl.tail], sizeof(*out_pkt));
    s_dl.tail = (s_dl.tail + 1U) % s_dl.capacity;
    s_dl.count--;

    xSemaphoreGive(s_dl.mutex);
    return DATA_FIFO_OK;
}

uint32_t data_downlink_available(void)
{
    if (!s_initialized || s_dl.mutex == NULL) {
        return 0U;
    }
    xSemaphoreTake(s_dl.mutex, portMAX_DELAY);
    uint32_t count = s_dl.count;
    xSemaphoreGive(s_dl.mutex);
    return count;
}

/* --- Uplink FIFO --------------------------------------------------------- */

data_fifo_result_t data_uplink_push(const data_uplink_packet_t *pkt)
{
    if (pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }
    if (!s_initialized || s_ul.mutex == NULL || s_ul.buf == NULL || s_ul.capacity == 0U) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);

    if (s_ul.count >= s_ul.capacity) {
        xSemaphoreGive(s_ul.mutex);
        return DATA_FIFO_FULL;
    }

    memcpy(&s_ul.buf[s_ul.head], pkt, sizeof(*pkt));
    s_ul.head = (s_ul.head + 1U) % s_ul.capacity;
    s_ul.count++;

    xSemaphoreGive(s_ul.mutex);
    return DATA_FIFO_OK;
}

data_fifo_result_t data_uplink_pop(data_uplink_packet_t *out_pkt)
{
    if (out_pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }
    if (!s_initialized || s_ul.mutex == NULL || s_ul.buf == NULL || s_ul.capacity == 0U) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);

    if (s_ul.count == 0U) {
        xSemaphoreGive(s_ul.mutex);
        return DATA_FIFO_EMPTY;
    }

    memcpy(out_pkt, &s_ul.buf[s_ul.tail], sizeof(*out_pkt));
    s_ul.tail = (s_ul.tail + 1U) % s_ul.capacity;
    s_ul.count--;

    xSemaphoreGive(s_ul.mutex);
    return DATA_FIFO_OK;
}

uint32_t data_uplink_pending(void)
{
    if (!s_initialized || s_ul.mutex == NULL) {
        return 0U;
    }
    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);
    uint32_t count = s_ul.count;
    xSemaphoreGive(s_ul.mutex);
    return count;
}
