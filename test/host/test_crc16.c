#include "unity.h"
#include "vmn/crc16.h"

void setUp(void) {}
void tearDown(void) {}

static void test_known_request_frames(void)
{
    /* Frames from the Modbus specification examples: the CRC goes on the wire low byte first. */
    const uint8_t read_holding[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, vmn_crc16(read_holding, sizeof read_holding));
    const uint8_t read_input[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX16(0xCA31, vmn_crc16(read_input, sizeof read_input));
}

static void test_empty_input_is_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, vmn_crc16(NULL, 0));
}

static void test_frame_with_its_crc_checks_to_zero(void)
{
    uint8_t frame[8] = {0x11, 0x06, 0x00, 0x01, 0x00, 0x03};
    const uint16_t crc = vmn_crc16(frame, 6);
    frame[6] = (uint8_t)(crc & 0xFF);
    frame[7] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_HEX16(0x0000, vmn_crc16(frame, sizeof frame));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_request_frames);
    RUN_TEST(test_empty_input_is_initial_value);
    RUN_TEST(test_frame_with_its_crc_checks_to_zero);
    return UNITY_END();
}
