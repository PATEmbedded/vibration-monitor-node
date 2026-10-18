#include <string.h>

#include "unity.h"
#include "vmn/crc16.h"
#include "vmn/fft.h"
#include "vmn/node.h"
#include "vmn/prng.h"
#include "vmn/signal_sim.h"

/* The node's register map behind the real Modbus server: the same path as on the target. */
static vmn_node_t s_node;
static vmn_modbus_t s_mb;
static vmn_workspace_t s_ws;
static uint8_t s_resp[VMN_MODBUS_MAX_ADU];

void setUp(void)
{
    vmn_fft_init();
    vmn_node_init(&s_node);
    const vmn_modbus_map_t map = vmn_node_modbus_map(&s_node);
    vmn_modbus_init(&s_mb, (uint8_t)s_node.settings.slave_address, &map);
    s_node.modbus_stats = &s_mb.stats;
}

void tearDown(void) {}

static size_t request(const uint8_t *pdu, size_t len)
{
    uint8_t frame[VMN_MODBUS_MAX_ADU];
    memcpy(frame, pdu, len);
    const uint16_t crc = vmn_crc16(frame, len);
    frame[len] = (uint8_t)(crc & 0xFF);
    frame[len + 1] = (uint8_t)(crc >> 8);
    return vmn_modbus_handle(&s_mb, frame, len + 2, s_resp, sizeof s_resp);
}

static uint16_t read_reg(uint8_t fc, uint16_t reg)
{
    const uint8_t req[] = {1, fc, (uint8_t)(reg >> 8), (uint8_t)reg, 0, 1};
    TEST_ASSERT_EQUAL_size_t(7, request(req, sizeof req));
    return (uint16_t)((s_resp[3] << 8) | s_resp[4]);
}

static void test_defaults(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, read_reg(0x03, VMN_HR_SLAVE_ADDRESS));
    TEST_ASSERT_EQUAL_UINT16(250, read_reg(0x03, VMN_HR_SHAFT_SPEED));
    TEST_ASSERT_EQUAL_UINT16(0, read_reg(0x03, VMN_HR_SOURCE));
    TEST_ASSERT_EQUAL_UINT16(500, read_reg(0x03, VMN_HR_INTERVAL_MS));
    TEST_ASSERT_EQUAL_UINT16(VMN_FW_VERSION, read_reg(0x04, VMN_IR_FW_VERSION));
    TEST_ASSERT_EQUAL_FLOAT(25.0f, vmn_node_shaft_hz(&s_node.settings));
}

static void test_limits_are_enforced(void)
{
    const uint8_t too_slow[] = {1, 0x06, 0, VMN_HR_SHAFT_SPEED, 0, 49};
    TEST_ASSERT_EQUAL_size_t(5, request(too_slow, sizeof too_slow));
    TEST_ASSERT_EQUAL_HEX8(VMN_MODBUS_ILLEGAL_VALUE, s_resp[2]);
    const uint8_t bad_source[] = {1, 0x06, 0, VMN_HR_SOURCE, 0, 4};
    TEST_ASSERT_EQUAL_size_t(5, request(bad_source, sizeof bad_source));
    const uint8_t no_such_reg[] = {1, 0x06, 0, VMN_HR_COUNT, 0, 1};
    TEST_ASSERT_EQUAL_size_t(5, request(no_such_reg, sizeof no_such_reg));
    TEST_ASSERT_EQUAL_HEX8(VMN_MODBUS_ILLEGAL_ADDRESS, s_resp[2]);
    const uint8_t no_such_input[] = {1, 0x04, 0, VMN_IR_COUNT, 0, 1};
    TEST_ASSERT_EQUAL_size_t(5, request(no_such_input, sizeof no_such_input));
    TEST_ASSERT_EQUAL_UINT16(250, s_node.settings.shaft_speed_dhz);
}

static void test_address_change_is_flagged(void)
{
    const uint8_t same[] = {1, 0x06, 0, VMN_HR_SLAVE_ADDRESS, 0, 1};
    request(same, sizeof same);
    TEST_ASSERT_FALSE(s_node.address_changed);
    const uint8_t change[] = {1, 0x06, 0, VMN_HR_SLAVE_ADDRESS, 0, 17};
    TEST_ASSERT_EQUAL_size_t(8, request(change, sizeof change));
    TEST_ASSERT_TRUE(s_node.address_changed);
    TEST_ASSERT_EQUAL_UINT16(17, s_node.settings.slave_address);
}

static void test_analysis_fills_the_input_registers(void)
{
    uint32_t rng = vmn_prng_seed(77);
    for (uint16_t source = 0; source < VMN_CONDITION_COUNT; ++source) {
        s_node.settings.source = source;
        vmn_features_t f;
        vmn_prediction_t p;
        TEST_ASSERT_EQUAL_INT(0, vmn_node_analyse(&s_node.settings, &rng, &s_ws, &f, &p));
        vmn_node_store_results(&s_node, &f, &p);
        TEST_ASSERT_EQUAL_UINT16(source, read_reg(0x04, VMN_IR_CLASS));
        TEST_ASSERT_TRUE(read_reg(0x04, VMN_IR_CONFIDENCE) > 500);
        TEST_ASSERT_TRUE(read_reg(0x04, VMN_IR_RMS) > 0);
    }
    TEST_ASSERT_EQUAL_UINT16(4, read_reg(0x04, VMN_IR_FRAME_COUNT));
}

static void test_statistics_are_visible(void)
{
    const uint8_t bad_fc[] = {1, 0x2B, 0, 0};
    request(bad_fc, sizeof bad_fc);
    TEST_ASSERT_EQUAL_UINT16(1, read_reg(0x04, VMN_IR_EXCEPTIONS));
    /* Each read_reg is one more accepted frame. */
    TEST_ASSERT_EQUAL_UINT16(3, read_reg(0x04, VMN_IR_FRAMES_OK));
    s_node.uart_overruns = 0x10002u;
    TEST_ASSERT_EQUAL_UINT16(2, read_reg(0x04, VMN_IR_UART_OVERRUNS));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_limits_are_enforced);
    RUN_TEST(test_address_change_is_flagged);
    RUN_TEST(test_analysis_fills_the_input_registers);
    RUN_TEST(test_statistics_are_visible);
    return UNITY_END();
}
