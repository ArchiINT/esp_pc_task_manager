/*
 * PC Monitor - host link.
 *
 * Owns the selected transport (USB-Serial-JTAG or BLE), decodes the incoming
 * byte stream into protocol frames and publishes the latest state as a
 * snapshot that the UI task can pick up.
 *
 * Threading model: the transport feeds a decoder from its own task; the decoded
 * state is handed to consumers through a single-slot mailbox (overwrite queue),
 * so a slow consumer never blocks the link and always sees the freshest data.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "pcmon_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Connection state as seen by the application. */
typedef enum {
    PCMON_LINK_DOWN = 0, /**< no host agent seen for CONFIG_PCMON_LINK_TIMEOUT_MS */
    PCMON_LINK_UP,       /**< frames arriving */
} pcmon_link_state_t;

/** Everything the UI needs to render one refresh. */
typedef struct {
    pcmon_link_state_t state;
    bool               hello_valid; /**< true once the host identified itself */
    pcmon_hello_t      hello;
    pcmon_metrics_t    metrics;     /**< last received sample (kept while DOWN) */
    uint32_t           rx_frames;   /**< diagnostics: good frames since boot */
    uint32_t           rx_errors;   /**< diagnostics: CRC + protocol errors */
} pcmon_link_snapshot_t;

/**
 * @brief Bring up the transport and start decoding host frames.
 *
 * Safe to call once, from app_main.
 */
esp_err_t pcmon_link_start(void);

/**
 * @brief Wait for a new snapshot.
 *
 * @param out          receives the snapshot (untouched if nothing arrived)
 * @param ticks_to_wait how long to block
 * @return true if a new snapshot was published within the timeout
 */
bool pcmon_link_wait(pcmon_link_snapshot_t *out, TickType_t ticks_to_wait);

/** @brief Human readable name of the compiled-in transport ("USB" / "BLE"). */
const char *pcmon_link_transport_name(void);

#ifdef __cplusplus
}
#endif
