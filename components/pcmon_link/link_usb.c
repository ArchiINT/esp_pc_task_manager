/*
 * PC Monitor - USB transport.
 *
 * Uses the ESP32-S3 built-in USB-Serial-JTAG peripheral, so the same cable that
 * powers and flashes the board carries the telemetry.  On the PC the board shows
 * up as /dev/ttyACM* (Linux) or a COM port (Windows) - no driver needed.
 *
 * Note: the IDF console keeps using UART0 (see sdkconfig.defaults) so that log
 * output never mixes into the telemetry stream.
 */
#include "sdkconfig.h"

#if CONFIG_PCMON_LINK_USB

#include "driver/usb_serial_jtag.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pcmon_proto.h"
#include "pcmon_transport.h"

static const char *TAG = "pcmon_usb";

/* One frame is ~70 bytes; a 512 byte RX buffer absorbs several updates worth of
 * bursts even if the UI task is busy repainting. */
#define USB_RX_BUFFER_SIZE 512
#define USB_TX_BUFFER_SIZE 256
#define USB_READ_CHUNK     128

typedef struct {
    pcmon_transport_rx_cb_t rx_cb;
    void                   *ctx;
} usb_link_t;

static usb_link_t s_usb;

/** Blocking reader: hands every received chunk to the decoder. */
static void usb_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[USB_READ_CHUNK];

    ESP_LOGI(TAG, "rx task running");

    for (;;) {
        const int len = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        if (len > 0 && s_usb.rx_cb != NULL) {
            s_usb.rx_cb(s_usb.ctx, buf, (size_t)len);
        }
    }
}

static esp_err_t usb_start(pcmon_transport_rx_cb_t rx_cb, void *ctx)
{
    s_usb.rx_cb = rx_cb;
    s_usb.ctx   = ctx;

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size                  = USB_RX_BUFFER_SIZE;
    cfg.tx_buffer_size                  = USB_TX_BUFFER_SIZE;
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&cfg), TAG, "driver install failed");

    const BaseType_t ok = xTaskCreate(usb_rx_task, "pcmon_usb_rx",
                                      CONFIG_PCMON_LINK_TASK_STACK, NULL,
                                      CONFIG_PCMON_LINK_TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "rx task create failed");

    return ESP_OK;
}

static esp_err_t usb_send(const uint8_t *data, size_t len)
{
    const int written = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(20));
    return (written == (int)len) ? ESP_OK : ESP_FAIL;
}

static bool usb_is_connected(void)
{
    return usb_serial_jtag_is_connected();
}

static const pcmon_transport_t s_usb_transport = {
    .name         = "USB",
    .start        = usb_start,
    .send         = usb_send,
    .is_connected = usb_is_connected,
};

const pcmon_transport_t *pcmon_transport_get(void)
{
    return &s_usb_transport;
}

#endif /* CONFIG_PCMON_LINK_USB */
