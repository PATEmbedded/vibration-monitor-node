#ifndef VMN_BOARD_H
#define VMN_BOARD_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "stream_buffer.h"

/*
 * Board support for the STM32F4 Discovery, at register level with CMSIS.
 *
 *   USART2  PA2 TX, PA3 RX, Modbus RTU framing 8E1
 *   PA1     driver enable of an RS-485 transceiver, high while transmitting
 *   IWDG    independent watchdog, about 4 s
 */

/** Enables the clocks and configures the pins. Call first in main. */
void board_init(void);

/** Starts USART2 at the given baud rate; every received byte goes to rx. */
void board_uart_init(uint32_t baud, StreamBufferHandle_t rx);

/** Sends a frame and waits until the last bit has left, then releases the RS-485 bus. */
void board_uart_write(const uint8_t *data, size_t len);

/** Receive overruns since start-up. */
uint32_t board_uart_overruns(void);

/** Starts the independent watchdog; it cannot be stopped again. */
void board_watchdog_start(void);

void board_watchdog_kick(void);

#endif
