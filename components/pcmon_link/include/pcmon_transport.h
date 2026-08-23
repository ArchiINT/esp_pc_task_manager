/*
 * PC Monitor - transport abstraction.
 *
 * A transport is a bidirectional byte pipe to the host.  Exactly one
 * implementation is compiled in, selected by Kconfig:
 *
 *   link_usb.c - native USB-Serial-JTAG (CDC), default
 *   link_ble.c - NimBLE GATT server exposing the Nordic UART Service
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Called by the transport whenever bytes arrive.  Runs in the transport task. */
typedef void (*pcmon_transport_rx_cb_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    const char *name;
    esp_err_t (*start)(pcmon_transport_rx_cb_t rx_cb, void *ctx);
    esp_err_t (*send)(const uint8_t *data, size_t len);
    bool (*is_connected)(void); /**< physical/link level presence of a host */
} pcmon_transport_t;

/** @brief Returns the transport selected at build time. */
const pcmon_transport_t *pcmon_transport_get(void);

#ifdef __cplusplus
}
#endif
