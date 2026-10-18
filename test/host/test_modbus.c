#include <string.h>

#include "unity.h"
#include "vmn/crc16.h"
#include "vmn/modbus_rtu.h"

/* A small register map: 4 holding registers that accept 0..1000, 8 read-only input registers. */
static uint16_t s_holding[4];
static uint16_t s_input[8];
static vmn_modbus_t s_mb;
static uint8_t s_resp[VMN_MODBUS_MAX_ADU];

static uint8_t read_holding(void *ctx, uint16_t reg, uint16_t *value)
{
    (void)ctx;
    if (reg >= 4u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    *value = s_holding[reg];
    return VMN_MODBUS_OK;
}

static uint8_t read_input(void *ctx, uint16_t reg, uint16_t *value)
{
    (void)ctx;
    if (reg >= 8u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    *value = s_input[reg];
    return VMN_MODBUS_OK;
}

static uint8_t write_holding(void *ctx, uint16_t reg, uint16_t value, bool commit)
{
    (void)ctx;
    if (reg >= 4u) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    if (value > 1000u) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    if (commit) {
        s_holding[reg] = value;
    }
    return VMN_MODBUS_OK;
}

void setUp(void)
{
    for (int i = 0; i < 4; ++i) {
        s_holding[i] = (uint16_t)(10 * (i + 1));
    }
    for (int i = 0; i < 8; ++i) {
        s_input[i] = (uint16_t)(0x0100 + i);
    }
    const vmn_modbus_map_t map = {read_holding, read_input, write_holding, NULL};
    vmn_modbus_init(&s_mb, 1, &map);
    memset(s_resp, 0, sizeof s_resp);
}

void tearDown(void) {}

/* Sends a request without CRC; the helper appends it. */
static size_t request(const uint8_t *pdu, size_t len)
{
    uint8_t frame[VMN_MODBUS_MAX_ADU];
    memcpy(frame, pdu, len);
    const uint16_t crc = vmn_crc16(frame, len);
    frame[len] = (uint8_t)(crc & 0xFF);
    frame[len + 1] = (uint8_t)(crc >> 8);
    return vmn_modbus_handle(&s_mb, frame, len + 2, s_resp, sizeof s_resp);
}

static void assert_valid_crc(size_t len)
{
    TEST_ASSERT_TRUE(len >= 4);
    TEST_ASSERT_EQUAL_HEX16(0x0000, vmn_crc16(s_resp, len));
}

static void assert_exception(size_t len, uint8_t fc, uint8_t code)
{
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_HEX8(fc | 0x80, s_resp[1]);
    TEST_ASSERT_EQUAL_HEX8(code, s_resp[2]);
    assert_valid_crc(len);
}

static void test_read_holding_registers(void)
{
    const uint8_t req[] = {0x01, 0x03, 0x00, 0x01, 0x00, 0x02};
    const size_t len = request(req, sizeof req);
    TEST_ASSERT_EQUAL_size_t(9, len);
    const uint8_t expect[] = {0x01, 0x03, 0x04, 0x00, 20, 0x00, 30};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, s_resp, sizeof expect);
    assert_valid_crc(len);
}

static void test_read_input_registers(void)
{
    const uint8_t req[] = {0x01, 0x04, 0x00, 0x06, 0x00, 0x02};
    const size_t len = request(req, sizeof req);
    const uint8_t expect[] = {0x01, 0x04, 0x04, 0x01, 0x06, 0x01, 0x07};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, s_resp, sizeof expect);
    assert_valid_crc(len);
}

static void test_write_single_register_echoes_request(void)
{
    const uint8_t req[] = {0x01, 0x06, 0x00, 0x02, 0x01, 0x2C};
    const size_t len = request(req, sizeof req);
    TEST_ASSERT_EQUAL_size_t(8, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(req, s_resp, sizeof req);
    TEST_ASSERT_EQUAL_UINT16(300, s_holding[2]);
}

static void test_write_single_rejects_bad_value_and_address(void)
{
    const uint8_t bad_value[] = {0x01, 0x06, 0x00, 0x00, 0x03, 0xE9};
    assert_exception(request(bad_value, sizeof bad_value), 0x06, VMN_MODBUS_ILLEGAL_VALUE);
    TEST_ASSERT_EQUAL_UINT16(10, s_holding[0]);

    const uint8_t bad_address[] = {0x01, 0x06, 0x00, 0x09, 0x00, 0x01};
    assert_exception(request(bad_address, sizeof bad_address), 0x06, VMN_MODBUS_ILLEGAL_ADDRESS);
}

static void test_write_multiple_registers(void)
{
    const uint8_t req[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x00, 0x0B};
    const size_t len = request(req, sizeof req);
    const uint8_t expect[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_size_t(8, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, s_resp, sizeof expect);
    TEST_ASSERT_EQUAL_UINT16(10, s_holding[0]);
    TEST_ASSERT_EQUAL_UINT16(11, s_holding[1]);
}

static void test_write_multiple_is_atomic(void)
{
    /* The second value is out of range, so the first must not be written either. */
    const uint8_t req[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x05, 0x07, 0xD0};
    assert_exception(request(req, sizeof req), 0x10, VMN_MODBUS_ILLEGAL_VALUE);
    TEST_ASSERT_EQUAL_UINT16(10, s_holding[0]);
    TEST_ASSERT_EQUAL_UINT16(20, s_holding[1]);
}

static void test_write_multiple_checks_byte_count(void)
{
    const uint8_t req[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x03, 0x00, 0x05, 0x00};
    assert_exception(request(req, sizeof req), 0x10, VMN_MODBUS_ILLEGAL_VALUE);
}

static void test_read_quantity_and_address_limits(void)
{
    const uint8_t zero[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
    assert_exception(request(zero, sizeof zero), 0x03, VMN_MODBUS_ILLEGAL_VALUE);
    const uint8_t too_many[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x7E};
    assert_exception(request(too_many, sizeof too_many), 0x03, VMN_MODBUS_ILLEGAL_VALUE);
    const uint8_t wraps[] = {0x01, 0x03, 0xFF, 0xFF, 0x00, 0x02};
    assert_exception(request(wraps, sizeof wraps), 0x03, VMN_MODBUS_ILLEGAL_ADDRESS);
    const uint8_t past_map[] = {0x01, 0x04, 0x00, 0x07, 0x00, 0x02};
    assert_exception(request(past_map, sizeof past_map), 0x04, VMN_MODBUS_ILLEGAL_ADDRESS);
}

static void test_unknown_function(void)
{
    const uint8_t req[] = {0x01, 0x2B, 0x0E, 0x01, 0x00};
    assert_exception(request(req, sizeof req), 0x2B, VMN_MODBUS_ILLEGAL_FUNCTION);
    TEST_ASSERT_EQUAL_UINT32(1, s_mb.stats.exceptions);
}

static void test_bad_crc_is_dropped(void)
{
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, vmn_modbus_handle(&s_mb, frame, sizeof frame, s_resp, sizeof s_resp));
    TEST_ASSERT_EQUAL_UINT32(1, s_mb.stats.crc_errors);
}

static void test_other_address_and_short_frames_are_ignored(void)
{
    const uint8_t other[] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, request(other, sizeof other));
    const uint8_t tiny[] = {0x01, 0x03, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, vmn_modbus_handle(&s_mb, tiny, sizeof tiny, s_resp, sizeof s_resp));
    TEST_ASSERT_EQUAL_UINT32(2, s_mb.stats.ignored);
    TEST_ASSERT_EQUAL_UINT32(0, s_mb.stats.frames_ok);
}

static void test_broadcast_writes_without_reply(void)
{
    const uint8_t write[] = {0x00, 0x06, 0x00, 0x03, 0x00, 0x63};
    TEST_ASSERT_EQUAL_size_t(0, request(write, sizeof write));
    TEST_ASSERT_EQUAL_UINT16(99, s_holding[3]);

    const uint8_t read[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, request(read, sizeof read));
    TEST_ASSERT_EQUAL_UINT32(1, s_mb.stats.ignored);
}

static void test_largest_read_fits_the_buffer(void)
{
    /* 125 registers is the protocol maximum; the map ends at 8, so expect an address exception
     * rather than an overflow. */
    const uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x7D};
    assert_exception(request(req, sizeof req), 0x03, VMN_MODBUS_ILLEGAL_ADDRESS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_holding_registers);
    RUN_TEST(test_read_input_registers);
    RUN_TEST(test_write_single_register_echoes_request);
    RUN_TEST(test_write_single_rejects_bad_value_and_address);
    RUN_TEST(test_write_multiple_registers);
    RUN_TEST(test_write_multiple_is_atomic);
    RUN_TEST(test_write_multiple_checks_byte_count);
    RUN_TEST(test_read_quantity_and_address_limits);
    RUN_TEST(test_unknown_function);
    RUN_TEST(test_bad_crc_is_dropped);
    RUN_TEST(test_other_address_and_short_frames_are_ignored);
    RUN_TEST(test_broadcast_writes_without_reply);
    RUN_TEST(test_largest_read_fits_the_buffer);
    return UNITY_END();
}
