/*
 * libFuzzer target for the Modbus RTU server.
 *
 * Every input is tried twice: as received, which exercises the length and CRC checks, and with a
 * valid CRC appended, so the fuzzer also reaches the parser behind the CRC check. Any response
 * must fit the buffer and carry a correct CRC.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vmn/crc16.h"
#include "vmn/modbus_rtu.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint16_t s_holding[16];

static uint8_t read_holding(void *ctx, uint16_t reg, uint16_t *value)
{
    (void)ctx;
    if (reg >= 16u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    *value = s_holding[reg];
    return VMN_MODBUS_OK;
}

static uint8_t read_input(void *ctx, uint16_t reg, uint16_t *value)
{
    (void)ctx;
    if (reg >= 32u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    *value = (uint16_t)(reg * 3u);
    return VMN_MODBUS_OK;
}

static uint8_t write_holding(void *ctx, uint16_t reg, uint16_t value, bool commit)
{
    (void)ctx;
    if (reg >= 16u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    if (value > 50000u) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    if (commit) {
        s_holding[reg] = value;
    }
    return VMN_MODBUS_OK;
}

static void check_response(const uint8_t *resp, size_t len, size_t cap)
{
    if (len > cap || (len != 0u && (len < 5u || vmn_crc16(resp, len) != 0u))) {
        __builtin_trap();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const vmn_modbus_map_t map = {read_holding, read_input, write_holding, NULL};
    vmn_modbus_t mb;
    uint8_t resp[VMN_MODBUS_MAX_ADU];

    vmn_modbus_init(&mb, 1, &map);
    check_response(resp, vmn_modbus_handle(&mb, data, size, resp, sizeof resp), sizeof resp);

    if (size >= 2u && size <= VMN_MODBUS_MAX_ADU - 2u) {
        uint8_t frame[VMN_MODBUS_MAX_ADU];
        memcpy(frame, data, size);
        const uint16_t crc = vmn_crc16(frame, size);
        frame[size] = (uint8_t)(crc & 0xFFu);
        frame[size + 1u] = (uint8_t)(crc >> 8);
        check_response(resp, vmn_modbus_handle(&mb, frame, size + 2u, resp, sizeof resp), sizeof resp);
    }
    return 0;
}
