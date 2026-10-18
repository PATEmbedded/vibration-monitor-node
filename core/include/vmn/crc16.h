#ifndef VMN_CRC16_H
#define VMN_CRC16_H

#include <stddef.h>
#include <stdint.h>

/** Modbus CRC-16: polynomial 0xA001 (reflected 0x8005), initial value 0xFFFF. */
uint16_t vmn_crc16(const uint8_t *data, size_t len);

#endif
