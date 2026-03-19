/*
 * SPDX-FileCopyrightText: 2026 Eyal Espresso
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Payload size constants — change these to resize both client and server.
 * ---------------------------------------------------------------------------*/

/** @brief Number of float fields in every data packet. */
#define DATA_SIZE_FLOATS   (100U)

/** @brief Number of int32 fields in every data packet. */
#define DATA_SIZE_INT      (20U)

/** @brief Fixed string field length (bytes, null-padding fills unused space). */
#define DATA_SIZE_STRING   (50U)

/** @brief FIFO depth for the downlink (server → client) receive buffer. */
#define DATA_DOWNLINK_FIFO_DEPTH  (10U)

/** @brief FIFO depth for the uplink (client → server) transmit buffer. */
#define DATA_UPLINK_FIFO_DEPTH    (10U)

/* ---------------------------------------------------------------------------
 * Magic discriminator bytes — embedded as the first byte of every DATA frame
 * payload so the receive path can distinguish binary data packets from the
 * existing ASCII control messages ("wifi_enable", "espresso_payload", etc.).
 * ---------------------------------------------------------------------------*/

/** @brief First payload byte identifying a downlink binary data packet. */
#define DATA_PAYLOAD_MAGIC_DOWNLINK  (0xD0U)

/** @brief First payload byte identifying an uplink binary data packet. */
#define DATA_PAYLOAD_MAGIC_UPLINK    (0xD1U)

/* ---------------------------------------------------------------------------
 * Packet structures (packed, little-endian on both sides).
 * ---------------------------------------------------------------------------*/

/**
 * @brief Downlink packet: server → client.
 *
 * @details Sequential; the receiver must process packets in arrival order.
 * The sequence counter starts at zero on every server reset and wraps at
 * UINT32_MAX.  A gap in `seq` values indicates a dropped packet.
 *
 * Total wire size: 1 + 4 + (DATA_SIZE_FLOATS×4) + (DATA_SIZE_INT×4) +
 *                  DATA_SIZE_STRING bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic;                  /**< Always DATA_PAYLOAD_MAGIC_DOWNLINK */
    uint32_t seq;                    /**< Server-side packet sequence counter */
    float    f[DATA_SIZE_FLOATS];    /**< Float payload array */
    int32_t  i[DATA_SIZE_INT];       /**< Integer payload array */
    char     s[DATA_SIZE_STRING];    /**< Fixed-length string (null-padded) */
} data_downlink_packet_t;

/**
 * @brief Uplink packet: client → server.
 *
 * @details Non-synchronized best-effort channel.  `seq` and `timestamp_ms`
 * allow the server to detect drops and measure latency but ordering is not
 * enforced.  Sent whenever the application pushes a packet and the keepalive
 * session is active.
 *
 * Total wire size: 1 + 4 + 4 + (DATA_SIZE_FLOATS×4) + (DATA_SIZE_INT×4) +
 *                  DATA_SIZE_STRING bytes.
 */
typedef struct __attribute__((packed)) {
    uint8_t  magic;                  /**< Always DATA_PAYLOAD_MAGIC_UPLINK */
    uint32_t seq;                    /**< Client-side packet sequence counter */
    uint32_t timestamp_ms;           /**< esp_timer_get_time() / 1000 at send time */
    float    f[DATA_SIZE_FLOATS];    /**< Float payload array */
    int32_t  i[DATA_SIZE_INT];       /**< Integer payload array */
    char     s[DATA_SIZE_STRING];    /**< Fixed-length string (null-padded) */
} data_uplink_packet_t;

/* ---------------------------------------------------------------------------
 * FIFO result codes
 * ---------------------------------------------------------------------------*/

typedef enum {
    DATA_FIFO_OK    = 0,  /**< Operation succeeded */
    DATA_FIFO_FULL  = 1,  /**< Buffer full; oldest entry was NOT overwritten */
    DATA_FIFO_EMPTY = 2,  /**< Buffer empty; nothing to pop */
} data_fifo_result_t;

/* ---------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------------*/

/**
 * @brief Initialize the data payload FIFO module.
 *
 * @details Creates the FreeRTOS mutexes protecting the downlink RX and
 * uplink TX FIFOs.  Must be called once before any push/pop operations.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NO_MEM if a mutex could not be created
 */
esp_err_t data_payload_init(void);

/* --- Downlink (server → client) receive FIFO ----------------------------- */

/**
 * @brief Push one downlink packet into the receive FIFO.
 *
 * @details Called by the communication task when it receives a DATA frame
 * whose first byte is DATA_PAYLOAD_MAGIC_DOWNLINK.  Thread-safe.
 *
 * @param[in] pkt Pointer to the packet to copy into the FIFO.
 * @return DATA_FIFO_OK or DATA_FIFO_FULL.
 */
data_fifo_result_t data_downlink_push(const data_downlink_packet_t *pkt);

/**
 * @brief Pop the oldest downlink packet from the receive FIFO.
 *
 * @details Called by the application to consume received server data.
 * Thread-safe.
 *
 * @param[out] out_pkt Destination for the popped packet.
 * @return DATA_FIFO_OK or DATA_FIFO_EMPTY.
 */
data_fifo_result_t data_downlink_pop(data_downlink_packet_t *out_pkt);

/**
 * @brief Return the number of downlink packets currently available.
 *
 * @return Packet count (0 … DATA_DOWNLINK_FIFO_DEPTH).
 */
uint32_t data_downlink_available(void);

/* --- Uplink (client → server) transmit FIFO ------------------------------ */

/**
 * @brief Push one uplink packet into the transmit FIFO.
 *
 * @details Called by the application to schedule a packet for transmission.
 * The communication task drains the FIFO whenever the keepalive session is
 * active.  Thread-safe.
 *
 * @param[in] pkt Pointer to the packet to copy into the FIFO.
 * @return DATA_FIFO_OK or DATA_FIFO_FULL.
 */
data_fifo_result_t data_uplink_push(const data_uplink_packet_t *pkt);

/**
 * @brief Pop the oldest uplink packet from the transmit FIFO.
 *
 * @details Called only by the communication task.  Thread-safe.
 *
 * @param[out] out_pkt Destination for the popped packet.
 * @return DATA_FIFO_OK or DATA_FIFO_EMPTY.
 */
data_fifo_result_t data_uplink_pop(data_uplink_packet_t *out_pkt);

/**
 * @brief Return the number of uplink packets pending transmission.
 *
 * @return Packet count (0 … DATA_UPLINK_FIFO_DEPTH).
 */
uint32_t data_uplink_pending(void);

#ifdef __cplusplus
}
#endif
