#ifndef VMN_MODBUS_RTU_H
#define VMN_MODBUS_RTU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Modbus RTU server (slave) for one complete frame at a time. It owns no buffers and does no
 * I/O: the caller detects the end of a frame (3.5 character times of silence), passes it in,
 * and sends back the response it gets. Supported functions:
 *
 *   0x03 read holding registers     0x06 write single register
 *   0x04 read input registers       0x10 write multiple registers
 */

#define VMN_MODBUS_MAX_ADU 256u
#define VMN_MODBUS_BROADCAST 0u

/** Exception codes; 0 means success. */
enum {
    VMN_MODBUS_OK = 0,
    VMN_MODBUS_ILLEGAL_FUNCTION = 1,
    VMN_MODBUS_ILLEGAL_ADDRESS = 2,
    VMN_MODBUS_ILLEGAL_VALUE = 3,
    VMN_MODBUS_DEVICE_FAILURE = 4
};

/**
 * The application's register map. Each callback returns VMN_MODBUS_OK or an exception code.
 * write_holding is called twice for every register of a request: first with commit false to
 * validate the whole request, then with commit true, so a rejected request changes nothing.
 */
typedef struct {
    uint8_t (*read_holding)(void *ctx, uint16_t reg, uint16_t *value);
    uint8_t (*read_input)(void *ctx, uint16_t reg, uint16_t *value);
    uint8_t (*write_holding)(void *ctx, uint16_t reg, uint16_t value, bool commit);
    void *ctx;
} vmn_modbus_map_t;

typedef struct {
    uint32_t frames_ok;     /**< frames with a good CRC for this address or broadcast */
    uint32_t crc_errors;    /**< frames dropped for a bad CRC */
    uint32_t exceptions;    /**< exception responses sent */
    uint32_t ignored;       /**< frames too short or long, for another address, or bad broadcasts */
} vmn_modbus_stats_t;

typedef struct {
    uint8_t address;        /**< 1 to 247 */
    vmn_modbus_map_t map;
    vmn_modbus_stats_t stats;
} vmn_modbus_t;

void vmn_modbus_init(vmn_modbus_t *mb, uint8_t address, const vmn_modbus_map_t *map);

/**
 * Handles one request frame, including its CRC. Writes the response with its CRC to resp and
 * returns its length, or returns 0 when no response must be sent (bad CRC, other address,
 * broadcast). resp_cap of VMN_MODBUS_MAX_ADU always suffices.
 */
size_t vmn_modbus_handle(vmn_modbus_t *mb, const uint8_t *req, size_t len, uint8_t *resp,
                         size_t resp_cap);

#endif
