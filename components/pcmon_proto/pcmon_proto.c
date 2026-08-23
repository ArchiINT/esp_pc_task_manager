/*
 * PC Monitor - wire protocol implementation.
 *
 * Deliberately free of ESP-IDF dependencies so the decoder can be unit tested
 * on the host with a plain C compiler.
 */
#include "pcmon_proto.h"

#include <string.h>

uint16_t pcmon_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/** Fold a single byte into a running CRC-16/CCITT-FALSE. */
static inline uint16_t crc16_byte(uint16_t crc, uint8_t byte)
{
    crc ^= (uint16_t)byte << 8;
    for (int bit = 0; bit < 8; bit++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void pcmon_framer_init(pcmon_framer_t *framer, pcmon_frame_cb_t cb, void *ctx)
{
    memset(framer, 0, sizeof(*framer));
    framer->state = PCMON_ST_SYNC0;
    framer->cb    = cb;
    framer->ctx   = ctx;
}

void pcmon_framer_feed(pcmon_framer_t *framer, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const uint8_t byte = data[i];

        switch (framer->state) {
        case PCMON_ST_SYNC0:
            if (byte == PCMON_SYNC0) {
                framer->state = PCMON_ST_SYNC1;
            }
            break;

        case PCMON_ST_SYNC1:
            /* 0xA5 0xA5 0x5A must still be accepted: stay armed on a repeated sync byte. */
            if (byte == PCMON_SYNC1) {
                framer->state    = PCMON_ST_VER;
                framer->crc_calc = 0xFFFF;
            } else if (byte != PCMON_SYNC0) {
                framer->state = PCMON_ST_SYNC0;
            }
            break;

        case PCMON_ST_VER:
            if (byte != PCMON_PROTO_VERSION) {
                framer->err_proto++;
                framer->state = PCMON_ST_SYNC0;
                break;
            }
            framer->crc_calc = crc16_byte(framer->crc_calc, byte);
            framer->state    = PCMON_ST_TYPE;
            break;

        case PCMON_ST_TYPE:
            framer->type     = byte;
            framer->crc_calc = crc16_byte(framer->crc_calc, byte);
            framer->state    = PCMON_ST_LEN;
            break;

        case PCMON_ST_LEN:
            if (byte > PCMON_MAX_PAYLOAD) {
                framer->err_proto++;
                framer->state = PCMON_ST_SYNC0;
                break;
            }
            framer->len      = byte;
            framer->idx      = 0;
            framer->crc_calc = crc16_byte(framer->crc_calc, byte);
            framer->state    = (byte == 0) ? PCMON_ST_CRC_LO : PCMON_ST_PAYLOAD;
            break;

        case PCMON_ST_PAYLOAD:
            framer->payload[framer->idx++] = byte;
            framer->crc_calc               = crc16_byte(framer->crc_calc, byte);
            if (framer->idx >= framer->len) {
                framer->state = PCMON_ST_CRC_LO;
            }
            break;

        case PCMON_ST_CRC_LO:
            framer->crc_rx = byte;
            framer->state  = PCMON_ST_CRC_HI;
            break;

        case PCMON_ST_CRC_HI:
            framer->crc_rx |= (uint16_t)byte << 8;
            if (framer->crc_rx == framer->crc_calc) {
                framer->rx_frames++;
                if (framer->cb) {
                    framer->cb(framer->ctx, framer->type, framer->payload, framer->len);
                }
            } else {
                framer->err_crc++;
            }
            framer->state = PCMON_ST_SYNC0;
            break;

        default:
            framer->state = PCMON_ST_SYNC0;
            break;
        }
    }
}

size_t pcmon_frame_encode(uint8_t type, const void *payload, uint8_t len,
                          uint8_t *out, size_t out_size)
{
    if (len > PCMON_MAX_PAYLOAD || out_size < (size_t)len + PCMON_FRAME_OVERHEAD) {
        return 0;
    }

    out[0] = PCMON_SYNC0;
    out[1] = PCMON_SYNC1;
    out[2] = PCMON_PROTO_VERSION;
    out[3] = type;
    out[4] = len;
    if (len > 0 && payload != NULL) {
        memcpy(&out[5], payload, len);
    }

    const uint16_t crc = pcmon_crc16(&out[2], (size_t)len + 3);
    out[5 + len]       = (uint8_t)(crc & 0xFF);
    out[6 + len]       = (uint8_t)(crc >> 8);

    return (size_t)len + PCMON_FRAME_OVERHEAD;
}
