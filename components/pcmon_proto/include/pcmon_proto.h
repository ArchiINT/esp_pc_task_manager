/*
 * PC Monitor - wire protocol
 *
 * The host agent (see host/) pushes telemetry frames to the device over a byte
 * stream (USB-CDC / BLE-NUS).  The framing below is transport agnostic: it only
 * assumes an ordered stream of bytes that may be split into arbitrary chunks and
 * may contain garbage (boot logs, partial frames) which we must resynchronise
 * from.
 *
 * Frame layout (little endian):
 *
 *   +------+------+-----+------+-----+------------------+-------+
 *   | 0xA5 | 0x5A | ver | type | len | payload[len]     | crc16 |
 *   +------+------+-----+------+-----+------------------+-------+
 *      0      1      2     3      4     5 .. 4+len       2 bytes
 *
 *   crc16 : CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) computed over
 *           ver, type, len and the payload (i.e. everything but the sync
 *           bytes and the checksum itself).
 *
 * Keep this file in sync with host/pcmon/protocol.py.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCMON_SYNC0             0xA5U
#define PCMON_SYNC1             0x5AU
#define PCMON_PROTO_VERSION     1U
#define PCMON_MAX_PAYLOAD       128U
#define PCMON_FRAME_OVERHEAD    7U /* sync(2) + ver + type + len + crc(2) */
#define PCMON_MAX_FRAME         (PCMON_MAX_PAYLOAD + PCMON_FRAME_OVERHEAD)

/** Maximum number of per-core load slots carried in a metrics frame. */
#define PCMON_MAX_CORES         32U

/** Message types (host -> device unless noted otherwise). */
typedef enum {
    PCMON_MSG_METRICS = 0x01, /**< pcmon_metrics_t, sent periodically */
    PCMON_MSG_HELLO   = 0x02, /**< pcmon_hello_t, sent once after connect */
    PCMON_MSG_PING    = 0x03, /**< empty payload, host keep-alive */
    PCMON_MSG_PONG    = 0x83, /**< empty payload, device -> host answer */
} pcmon_msg_type_t;

/** Bit flags describing which optional metric fields are valid. */
#define PCMON_FLAG_CPU_TEMP_VALID  (1U << 0)
#define PCMON_FLAG_GPU_PRESENT     (1U << 1)
#define PCMON_FLAG_GPU_TEMP_VALID  (1U << 2)
#define PCMON_FLAG_GPU_FAN_VALID   (1U << 3)
#define PCMON_FLAG_GPU_POWER_VALID (1U << 4)

/**
 * @brief Periodic telemetry sample.
 *
 * Fixed size, packed, little endian.  Fractional values are transported as
 * integers to keep the device side free of floating point parsing:
 *   *_pm  - per mille  (0..1000)  -> percent = value / 10.0
 *   *_dc  - deci degC  (0.1 degC) -> celsius = value / 10.0
 *   *_dw  - deci watt  (0.1 W)    -> watt    = value / 10.0
 */
typedef struct __attribute__((packed)) {
    uint32_t uptime_s;      /**< host uptime in seconds */
    uint32_t ram_used_mb;   /**< used system RAM, MiB */
    uint32_t ram_total_mb;  /**< total system RAM, MiB */
    uint16_t cpu_load_pm;   /**< total CPU load, per mille */
    int16_t  cpu_temp_dc;   /**< CPU package temperature, 0.1 degC */
    uint16_t cpu_freq_mhz;  /**< average core clock, MHz */
    uint16_t gpu_load_pm;   /**< GPU core utilisation, per mille */
    int16_t  gpu_temp_dc;   /**< GPU temperature, 0.1 degC */
    uint16_t gpu_freq_mhz;  /**< GPU core clock, MHz */
    uint16_t gpu_power_dw;  /**< GPU board power draw, 0.1 W */
    uint16_t gpu_fan_pm;    /**< GPU fan speed, per mille */
    uint16_t vram_used_mb;  /**< used video memory, MiB */
    uint16_t vram_total_mb; /**< total video memory, MiB */
    uint8_t  core_count;    /**< number of valid entries in core_load_pct */
    uint8_t  flags;         /**< PCMON_FLAG_* */
    uint8_t  core_load_pct[PCMON_MAX_CORES]; /**< per-core load, percent */
} pcmon_metrics_t;

_Static_assert(sizeof(pcmon_metrics_t) == 66, "metrics layout changed - update host/pcmon/protocol.py");

/** One-shot host description, sent right after the link comes up. */
typedef struct __attribute__((packed)) {
    char hostname[24]; /**< NUL padded */
    char cpu_name[40]; /**< NUL padded */
    char gpu_name[40]; /**< NUL padded, empty when no NVIDIA GPU was found */
} pcmon_hello_t;

_Static_assert(sizeof(pcmon_hello_t) == 104, "hello layout changed - update host/pcmon/protocol.py");

/**
 * @brief Called for every valid frame extracted from the stream.
 *
 * Runs in the context of whichever task fed the framer, keep it short.
 */
typedef void (*pcmon_frame_cb_t)(void *ctx, uint8_t type, const uint8_t *payload, uint8_t len);

/** Byte-stream frame decoder.  Not thread safe: feed it from a single task. */
typedef struct {
    enum {
        PCMON_ST_SYNC0 = 0,
        PCMON_ST_SYNC1,
        PCMON_ST_VER,
        PCMON_ST_TYPE,
        PCMON_ST_LEN,
        PCMON_ST_PAYLOAD,
        PCMON_ST_CRC_LO,
        PCMON_ST_CRC_HI,
    } state;

    uint8_t  type;
    uint8_t  len;
    uint8_t  idx;
    uint8_t  payload[PCMON_MAX_PAYLOAD];
    uint16_t crc_calc;
    uint16_t crc_rx;

    pcmon_frame_cb_t cb;
    void            *ctx;

    /* Diagnostics, useful when debugging a flaky cable / BLE link. */
    uint32_t rx_frames;
    uint32_t err_crc;
    uint32_t err_proto;
} pcmon_framer_t;

/** @brief CRC-16/CCITT-FALSE over @p len bytes. */
uint16_t pcmon_crc16(const uint8_t *data, size_t len);

/** @brief Reset the decoder and attach the frame callback. */
void pcmon_framer_init(pcmon_framer_t *framer, pcmon_frame_cb_t cb, void *ctx);

/** @brief Push a chunk of received bytes through the decoder. */
void pcmon_framer_feed(pcmon_framer_t *framer, const uint8_t *data, size_t len);

/**
 * @brief Serialise a frame into @p out.
 *
 * @return number of bytes written, or 0 if @p out is too small.
 */
size_t pcmon_frame_encode(uint8_t type, const void *payload, uint8_t len,
                          uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif
