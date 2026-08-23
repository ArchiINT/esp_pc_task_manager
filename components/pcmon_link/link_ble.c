/*
 * PC Monitor - BLE transport.
 *
 * Implements a GATT server exposing the de-facto standard Nordic UART Service
 * (NUS), which every desktop BLE stack can talk to without a custom driver:
 *
 *   Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
 *     RX  6E400002-...  write / write-no-response   host -> device (telemetry)
 *     TX  6E400003-...  notify                      device -> host (PONG, status)
 *
 * The decoder upstream works on a raw byte stream, so ATT fragmentation and MTU
 * negotiation need no special handling here.
 */
#include "sdkconfig.h"

#if CONFIG_PCMON_LINK_BLE

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "pcmon_transport.h"

static const char *TAG = "pcmon_ble";

/* Connection interval requested after connect, in 1.25 ms units.
 * 12..24 -> 15..30 ms, i.e. comfortably faster than the 10 Hz telemetry rate. */
#define BLE_CONN_ITVL_MIN 12
#define BLE_CONN_ITVL_MAX 24
#define BLE_CONN_LATENCY  0
#define BLE_SUPERV_TMO    400 /* 4 s, in 10 ms units */

/* Nordic UART Service UUIDs (128-bit, little endian byte order). */
static const ble_uuid128_t s_uuid_nus_svc =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t s_uuid_nus_rx =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t s_uuid_nus_tx =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

typedef struct {
    pcmon_transport_rx_cb_t rx_cb;
    void                   *ctx;
    uint16_t                conn_handle;
    uint16_t                tx_val_handle;
    bool                    notify_enabled;
    uint8_t                 own_addr_type;
} ble_link_t;

static ble_link_t s_ble = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
};

static void ble_advertise(void);

/* -------------------------------------------------------------------------- */
/* GATT                                                                        */
/* -------------------------------------------------------------------------- */

/** Write access on the RX characteristic: forward the payload to the decoder. */
static int gatt_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* A write may be delivered as a chain of mbufs; flatten it before decoding. */
    uint8_t  buf[256];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (len > 0 && s_ble.rx_cb != NULL) {
        s_ble.rx_cb(s_ble.ctx, buf, len);
    }
    return 0;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &s_uuid_nus_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &s_uuid_nus_rx.u,
                .access_cb  = gatt_rx_access,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid       = &s_uuid_nus_tx.u,
                .access_cb  = gatt_rx_access, /* never read, notify only */
                .val_handle = &s_ble.tx_val_handle,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, /* no more characteristics */
        },
    },
    { 0 }, /* no more services */
};

/* -------------------------------------------------------------------------- */
/* GAP                                                                         */
/* -------------------------------------------------------------------------- */

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_ble.conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "host connected (handle %u)", s_ble.conn_handle);

            /* Ask for a short connection interval to keep the display in sync. */
            const struct ble_gap_upd_params params = {
                .itvl_min            = BLE_CONN_ITVL_MIN,
                .itvl_max            = BLE_CONN_ITVL_MAX,
                .latency             = BLE_CONN_LATENCY,
                .supervision_timeout = BLE_SUPERV_TMO,
            };
            ble_gap_update_params(s_ble.conn_handle, &params);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d), advertising again", event->connect.status);
            ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "host disconnected (reason %d)", event->disconnect.reason);
        s_ble.conn_handle    = BLE_HS_CONN_HANDLE_NONE;
        s_ble.notify_enabled = false;
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ble_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_ble.tx_val_handle) {
            s_ble.notify_enabled = event->subscribe.cur_notify;
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU negotiated: %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    const char              *name   = ble_svc_gap_device_name();

    fields.flags             = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl        = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;
    fields.name              = (uint8_t *)name;
    fields.name_len          = strlen(name);
    fields.name_is_complete  = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    /* The 128-bit service UUID does not fit next to the name in the 31 byte
     * advertisement, so it goes into the scan response instead. */
    struct ble_hs_adv_fields rsp_fields = { 0 };
    rsp_fields.uuids128                 = (ble_uuid128_t *)&s_uuid_nus_svc;
    rsp_fields.num_uuids128             = 1;
    rsp_fields.uuids128_is_complete     = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    const struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    rc = ble_gap_adv_start(s_ble.own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as \"%s\"", name);
    }
}

static void ble_on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "failed to ensure a BLE identity address");
        return;
    }
    if (ble_hs_id_infer_auto(0, &s_ble.own_addr_type) != 0) {
        ESP_LOGE(TAG, "no usable BLE address");
        return;
    }
    ble_advertise();
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "controller reset, reason %d", reason);
    s_ble.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run(); /* returns only after nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* -------------------------------------------------------------------------- */
/* Transport interface                                                         */
/* -------------------------------------------------------------------------- */

static esp_err_t ble_start(pcmon_transport_rx_cb_t rx_cb, void *ctx)
{
    s_ble.rx_cb = rx_cb;
    s_ble.ctx   = ctx;

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb  = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_gatt_svcs);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT registration failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(CONFIG_PCMON_BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGW(TAG, "device name set failed: %d", rc);
    }
    ble_att_set_preferred_mtu(247);

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

static esp_err_t ble_send(const uint8_t *data, size_t len)
{
    if (s_ble.conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_ble.notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return (ble_gatts_notify_custom(s_ble.conn_handle, s_ble.tx_val_handle, om) == 0)
               ? ESP_OK
               : ESP_FAIL;
}

static bool ble_is_connected(void)
{
    return s_ble.conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

static const pcmon_transport_t s_ble_transport = {
    .name         = "BLE",
    .start        = ble_start,
    .send         = ble_send,
    .is_connected = ble_is_connected,
};

const pcmon_transport_t *pcmon_transport_get(void)
{
    return &s_ble_transport;
}

#endif /* CONFIG_PCMON_LINK_BLE */
