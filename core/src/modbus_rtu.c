#include "vmn/modbus_rtu.h"

#include <string.h>

#include "vmn/crc16.h"

#define FC_READ_HOLDING 0x03u
#define FC_READ_INPUT 0x04u
#define FC_WRITE_SINGLE 0x06u
#define FC_WRITE_MULTIPLE 0x10u

#define MAX_READ_QTY 125u
#define MAX_WRITE_QTY 123u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)(p[0] << 8) | p[1]);
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

void vmn_modbus_init(vmn_modbus_t *mb, uint8_t address, const vmn_modbus_map_t *map)
{
    memset(mb, 0, sizeof(*mb));
    mb->address = address;
    mb->map = *map;
}

/* Each handler gets the PDU data after the function code and writes the response data after the
 * function code to out. It returns an exception code and sets *out_len on success. */

static uint8_t read_registers(const vmn_modbus_t *mb, uint8_t fc, const uint8_t *data, size_t len,
                              uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (len != 4u) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    const uint16_t start = be16(data);
    const uint16_t qty = be16(data + 2);
    if (qty < 1u || qty > MAX_READ_QTY || out_cap < 1u + 2u * qty) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    if ((uint32_t)start + qty > 0x10000u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    uint8_t (*read)(void *, uint16_t, uint16_t *) =
        fc == FC_READ_HOLDING ? mb->map.read_holding : mb->map.read_input;
    if (read == NULL) {
        return VMN_MODBUS_ILLEGAL_FUNCTION;
    }
    out[0] = (uint8_t)(2u * qty);
    for (uint16_t i = 0; i < qty; ++i) {
        uint16_t value = 0;
        const uint8_t ex = read(mb->map.ctx, (uint16_t)(start + i), &value);
        if (ex != VMN_MODBUS_OK) {
            return ex;
        }
        put_be16(out + 1 + 2u * i, value);
    }
    *out_len = 1u + 2u * qty;
    return VMN_MODBUS_OK;
}

static uint8_t write_registers(const vmn_modbus_t *mb, uint16_t start, uint16_t qty, const uint8_t *values)
{
    if (mb->map.write_holding == NULL) {
        return VMN_MODBUS_ILLEGAL_FUNCTION;
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool commit = pass == 1;
        for (uint16_t i = 0; i < qty; ++i) {
            const uint8_t ex = mb->map.write_holding(mb->map.ctx, (uint16_t)(start + i),
                                                     be16(values + 2u * i), commit);
            if (ex != VMN_MODBUS_OK) {
                /* Validation catches every error, so a commit failure is a device failure. */
                return commit ? VMN_MODBUS_DEVICE_FAILURE : ex;
            }
        }
    }
    return VMN_MODBUS_OK;
}

static uint8_t write_single(const vmn_modbus_t *mb, const uint8_t *data, size_t len, uint8_t *out,
                            size_t *out_len)
{
    if (len != 4u) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    const uint8_t ex = write_registers(mb, be16(data), 1u, data + 2);
    if (ex == VMN_MODBUS_OK) {
        memcpy(out, data, 4u); /* the response echoes the request */
        *out_len = 4u;
    }
    return ex;
}

static uint8_t write_multiple(const vmn_modbus_t *mb, const uint8_t *data, size_t len, uint8_t *out,
                              size_t *out_len)
{
    if (len < 5u) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    const uint16_t start = be16(data);
    const uint16_t qty = be16(data + 2);
    const uint8_t byte_count = data[4];
    if (qty < 1u || qty > MAX_WRITE_QTY || byte_count != 2u * qty || len != 5u + byte_count) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    if ((uint32_t)start + qty > 0x10000u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    const uint8_t ex = write_registers(mb, start, qty, data + 5);
    if (ex == VMN_MODBUS_OK) {
        memcpy(out, data, 4u); /* start and quantity */
        *out_len = 4u;
    }
    return ex;
}

static size_t finish(uint8_t *resp, size_t len)
{
    const uint16_t crc = vmn_crc16(resp, len);
    resp[len] = (uint8_t)(crc & 0xFFu);
    resp[len + 1u] = (uint8_t)(crc >> 8);
    return len + 2u;
}

size_t vmn_modbus_handle(vmn_modbus_t *mb, const uint8_t *req, size_t len, uint8_t *resp,
                         size_t resp_cap)
{
    if (req == NULL || len < 4u || len > VMN_MODBUS_MAX_ADU || resp == NULL || resp_cap < 5u) {
        mb->stats.ignored++;
        return 0;
    }
    const uint16_t crc = (uint16_t)(req[len - 2u] | (uint16_t)(req[len - 1u] << 8));
    if (vmn_crc16(req, len - 2u) != crc) {
        mb->stats.crc_errors++;
        return 0;
    }

    const uint8_t address = req[0];
    const uint8_t fc = req[1];
    const bool broadcast = address == VMN_MODBUS_BROADCAST;
    if (!broadcast && address != mb->address) {
        mb->stats.ignored++;
        return 0;
    }
    /* A broadcast may only write, and it is never answered. */
    if (broadcast && fc != FC_WRITE_SINGLE && fc != FC_WRITE_MULTIPLE) {
        mb->stats.ignored++;
        return 0;
    }
    mb->stats.frames_ok++;

    const uint8_t *data = req + 2;
    const size_t data_len = len - 4u;
    uint8_t *out = resp + 2;
    const size_t out_cap = resp_cap - 4u; /* address, function code and CRC */
    size_t out_len = 0;
    uint8_t ex;

    switch (fc) {
    case FC_READ_HOLDING:
    case FC_READ_INPUT:
        ex = read_registers(mb, fc, data, data_len, out, out_cap, &out_len);
        break;
    case FC_WRITE_SINGLE:
        ex = write_single(mb, data, data_len, out, &out_len);
        break;
    case FC_WRITE_MULTIPLE:
        ex = write_multiple(mb, data, data_len, out, &out_len);
        break;
    default:
        ex = VMN_MODBUS_ILLEGAL_FUNCTION;
        break;
    }

    if (broadcast) {
        return 0;
    }
    resp[0] = address;
    if (ex != VMN_MODBUS_OK) {
        mb->stats.exceptions++;
        resp[1] = (uint8_t)(fc | 0x80u);
        resp[2] = ex;
        return finish(resp, 3u);
    }
    resp[1] = fc;
    return finish(resp, 2u + out_len);
}
