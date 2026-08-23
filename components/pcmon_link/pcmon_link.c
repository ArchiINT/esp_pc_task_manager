/*
 * PC Monitor - host link (transport independent part).
 */
#include "pcmon_link.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "pcmon_transport.h"

static const char *TAG = "pcmon_link";

/** Period of the link supervision timer. */
#define LINK_WATCHDOG_PERIOD_US (250 * 1000)

typedef struct {
    const pcmon_transport_t *transport;
    pcmon_framer_t           framer;
    QueueHandle_t            mailbox;   /**< single slot, always overwritten */
    esp_timer_handle_t       watchdog;
    pcmon_link_snapshot_t    snapshot;  /**< owned by the transport task + timer */
    int64_t                  last_rx_us;
} pcmon_link_t;

static pcmon_link_t s_link;

/* -------------------------------------------------------------------------- */
/* Internals                                                                   */
/* -------------------------------------------------------------------------- */

/** Push the current snapshot to whoever is waiting (never blocks). */
static void link_publish(void)
{
    s_link.snapshot.rx_frames = s_link.framer.rx_frames;
    s_link.snapshot.rx_errors = s_link.framer.err_crc + s_link.framer.err_proto;
    xQueueOverwrite(s_link.mailbox, &s_link.snapshot);
}

/** Copy a NUL padded protocol string into a NUL terminated C string. */
static void copy_fixed_string(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    const size_t n = (src_size < dst_size - 1) ? src_size : dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/** Called by the framer for every valid frame. */
static void link_on_frame(void *ctx, uint8_t type, const uint8_t *payload, uint8_t len)
{
    (void)ctx;

    switch (type) {
    case PCMON_MSG_METRICS:
        if (len != sizeof(pcmon_metrics_t)) {
            ESP_LOGW(TAG, "metrics frame size mismatch: %u != %u", len,
                     (unsigned)sizeof(pcmon_metrics_t));
            return;
        }
        memcpy(&s_link.snapshot.metrics, payload, sizeof(pcmon_metrics_t));
        if (s_link.snapshot.metrics.core_count > PCMON_MAX_CORES) {
            s_link.snapshot.metrics.core_count = PCMON_MAX_CORES;
        }
        s_link.last_rx_us    = esp_timer_get_time();
        s_link.snapshot.state = PCMON_LINK_UP;
        link_publish();
        break;

    case PCMON_MSG_HELLO: {
        if (len != sizeof(pcmon_hello_t)) {
            ESP_LOGW(TAG, "hello frame size mismatch: %u", len);
            return;
        }
        const pcmon_hello_t *hello = (const pcmon_hello_t *)payload;
        copy_fixed_string(s_link.snapshot.hello.hostname, sizeof(s_link.snapshot.hello.hostname),
                          hello->hostname, sizeof(hello->hostname));
        copy_fixed_string(s_link.snapshot.hello.cpu_name, sizeof(s_link.snapshot.hello.cpu_name),
                          hello->cpu_name, sizeof(hello->cpu_name));
        copy_fixed_string(s_link.snapshot.hello.gpu_name, sizeof(s_link.snapshot.hello.gpu_name),
                          hello->gpu_name, sizeof(hello->gpu_name));
        s_link.snapshot.hello_valid = true;
        s_link.last_rx_us           = esp_timer_get_time();
        ESP_LOGI(TAG, "host: %s | %s | %s", s_link.snapshot.hello.hostname,
                 s_link.snapshot.hello.cpu_name, s_link.snapshot.hello.gpu_name);
        link_publish();
        break;
    }

    case PCMON_MSG_PING: {
        uint8_t frame[PCMON_FRAME_OVERHEAD];
        const size_t n = pcmon_frame_encode(PCMON_MSG_PONG, NULL, 0, frame, sizeof(frame));
        if (n > 0 && s_link.transport->send != NULL) {
            s_link.transport->send(frame, n);
        }
        s_link.last_rx_us = esp_timer_get_time();
        break;
    }

    default:
        ESP_LOGD(TAG, "ignoring frame type 0x%02x", type);
        break;
    }
}

/** Transport callback: raw bytes in, frames out. */
static void link_on_rx(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    pcmon_framer_feed(&s_link.framer, data, len);
}

/** Periodic supervision: declare the link down when the host goes quiet. */
static void link_watchdog_cb(void *arg)
{
    (void)arg;

    if (s_link.snapshot.state != PCMON_LINK_UP) {
        return;
    }

    const int64_t idle_us = esp_timer_get_time() - s_link.last_rx_us;
    const bool    carrier = (s_link.transport->is_connected == NULL) ||
                            s_link.transport->is_connected();

    if (idle_us > (int64_t)CONFIG_PCMON_LINK_TIMEOUT_MS * 1000 || !carrier) {
        ESP_LOGW(TAG, "link down (idle %lld ms, carrier %d)", idle_us / 1000, (int)carrier);
        s_link.snapshot.state = PCMON_LINK_DOWN;
        link_publish();
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t pcmon_link_start(void)
{
    if (s_link.mailbox != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_link.transport = pcmon_transport_get();
    if (s_link.transport == NULL) {
        ESP_LOGE(TAG, "no transport compiled in");
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_link.mailbox = xQueueCreate(1, sizeof(pcmon_link_snapshot_t));
    ESP_RETURN_ON_FALSE(s_link.mailbox != NULL, ESP_ERR_NO_MEM, TAG, "mailbox alloc failed");

    memset(&s_link.snapshot, 0, sizeof(s_link.snapshot));
    s_link.snapshot.state = PCMON_LINK_DOWN;
    pcmon_framer_init(&s_link.framer, link_on_frame, NULL);

    const esp_timer_create_args_t timer_args = {
        .callback = link_watchdog_cb,
        .name     = "pcmon_link_wd",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_link.watchdog));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_link.watchdog, LINK_WATCHDOG_PERIOD_US));

    ESP_LOGI(TAG, "starting %s transport", s_link.transport->name);
    return s_link.transport->start(link_on_rx, NULL);
}

bool pcmon_link_wait(pcmon_link_snapshot_t *out, TickType_t ticks_to_wait)
{
    if (s_link.mailbox == NULL || out == NULL) {
        return false;
    }
    return xQueueReceive(s_link.mailbox, out, ticks_to_wait) == pdTRUE;
}

const char *pcmon_link_transport_name(void)
{
    const pcmon_transport_t *t = pcmon_transport_get();
    return (t != NULL) ? t->name : "none";
}
