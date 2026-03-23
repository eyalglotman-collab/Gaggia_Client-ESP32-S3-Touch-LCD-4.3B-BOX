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
 * @details Both FIFOs are classic power-of-two ring buffers protected by
 * individual FreeRTOS mutexes so the communication task and application tasks
 * can access them concurrently without contention.  The module holds all
 * storage as static globals to avoid heap fragmentation.
 */

#include "data_payload.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/* ---------------------------------------------------------------------------
 * Internal FIFO structures
 * ---------------------------------------------------------------------------*/

typedef struct {
    data_downlink_packet_t buf[DATA_DOWNLINK_FIFO_DEPTH];
    uint32_t               head;    /* next write index */
    uint32_t               tail;    /* next read index  */
    uint32_t               count;   /* items currently stored */
    SemaphoreHandle_t      mutex;
} downlink_fifo_t;

typedef struct {
    data_uplink_packet_t buf[DATA_UPLINK_FIFO_DEPTH];
    uint32_t             head;
    uint32_t             tail;
    uint32_t             count;
    SemaphoreHandle_t    mutex;
} uplink_fifo_t;

static downlink_fifo_t s_dl;
static uplink_fifo_t   s_ul;
static bool            s_initialized = false;

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

esp_err_t data_payload_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    memset(&s_dl, 0, sizeof(s_dl));
    memset(&s_ul, 0, sizeof(s_ul));

    s_dl.mutex = xSemaphoreCreateMutex();
    if (s_dl.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_ul.mutex = xSemaphoreCreateMutex();
    if (s_ul.mutex == NULL) {
        vSemaphoreDelete(s_dl.mutex);
        s_dl.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    return ESP_OK;
}

/* --- Downlink FIFO ------------------------------------------------------- */

data_fifo_result_t data_downlink_push(const data_downlink_packet_t *pkt)
{
    if (pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }

    /* Delay instead of dropping: wait until one slot is available. */
    while (true) {
        xSemaphoreTake(s_dl.mutex, portMAX_DELAY);
        if (s_dl.count < DATA_DOWNLINK_FIFO_DEPTH) {
            break;
        }
        xSemaphoreGive(s_dl.mutex);
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    memcpy(&s_dl.buf[s_dl.head], pkt, sizeof(*pkt));
    s_dl.head = (s_dl.head + 1U) % DATA_DOWNLINK_FIFO_DEPTH;
    s_dl.count++;

    xSemaphoreGive(s_dl.mutex);
    return DATA_FIFO_OK;
}

data_fifo_result_t data_downlink_pop(data_downlink_packet_t *out_pkt)
{
    if (out_pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_dl.mutex, portMAX_DELAY);

    if (s_dl.count == 0U) {
        xSemaphoreGive(s_dl.mutex);
        return DATA_FIFO_EMPTY;
    }

    memcpy(out_pkt, &s_dl.buf[s_dl.tail], sizeof(*out_pkt));
    s_dl.tail = (s_dl.tail + 1U) % DATA_DOWNLINK_FIFO_DEPTH;
    s_dl.count--;

    xSemaphoreGive(s_dl.mutex);
    return DATA_FIFO_OK;
}

uint32_t data_downlink_available(void)
{
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

    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);

    if (s_ul.count >= DATA_UPLINK_FIFO_DEPTH) {
        xSemaphoreGive(s_ul.mutex);
        return DATA_FIFO_FULL;
    }

    memcpy(&s_ul.buf[s_ul.head], pkt, sizeof(*pkt));
    s_ul.head = (s_ul.head + 1U) % DATA_UPLINK_FIFO_DEPTH;
    s_ul.count++;

    xSemaphoreGive(s_ul.mutex);
    return DATA_FIFO_OK;
}

data_fifo_result_t data_uplink_pop(data_uplink_packet_t *out_pkt)
{
    if (out_pkt == NULL) {
        return DATA_FIFO_EMPTY;
    }

    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);

    if (s_ul.count == 0U) {
        xSemaphoreGive(s_ul.mutex);
        return DATA_FIFO_EMPTY;
    }

    memcpy(out_pkt, &s_ul.buf[s_ul.tail], sizeof(*out_pkt));
    s_ul.tail = (s_ul.tail + 1U) % DATA_UPLINK_FIFO_DEPTH;
    s_ul.count--;

    xSemaphoreGive(s_ul.mutex);
    return DATA_FIFO_OK;
}

uint32_t data_uplink_pending(void)
{
    xSemaphoreTake(s_ul.mutex, portMAX_DELAY);
    uint32_t count = s_ul.count;
    xSemaphoreGive(s_ul.mutex);
    return count;
}
