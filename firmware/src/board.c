#include "board.h"

#include "stm32f4xx.h"

#define DE_PIN 1u          /* PA1 */
#define UART_IRQ_PRIORITY 6u /* must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */

static StreamBufferHandle_t s_rx;
static volatile uint32_t s_overruns;

void USART2_IRQHandler(void);

static void set_mode(uint32_t pin, uint32_t mode)
{
    GPIOA->MODER = (GPIOA->MODER & ~(3u << (2u * pin))) | (mode << (2u * pin));
}

static void set_alternate(uint32_t pin, uint32_t af)
{
    GPIOA->AFR[pin / 8u] = (GPIOA->AFR[pin / 8u] & ~(0xFu << (4u * (pin % 8u)))) | (af << (4u * (pin % 8u)));
}

void board_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR; /* read back: the clock needs two cycles before the peripheral is usable */

    GPIOA->BSRR = 1u << (DE_PIN + 16u); /* receive by default */
    set_mode(DE_PIN, 1u);               /* output */
    set_mode(2u, 2u);                   /* PA2 alternate function: USART2_TX */
    set_mode(3u, 2u);                   /* PA3 alternate function: USART2_RX */
    set_alternate(2u, 7u);
    set_alternate(3u, 7u);
    GPIOA->OSPEEDR |= 3u << (2u * 2u);                             /* fast edges on TX */
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (2u * 3u))) | (1u << (2u * 3u)); /* pull-up on RX */
}

void board_uart_init(uint32_t baud, StreamBufferHandle_t rx)
{
    s_rx = rx;
    USART2->CR1 = 0;
    /* APB1 runs at the core clock (no PLL, no prescaler); oversampling by 16. */
    USART2->BRR = (SystemCoreClock + baud / 2u) / baud;
    USART2->CR2 = 0; /* 1 stop bit */
    USART2->CR3 = 0;
    /* 8 data bits + even parity = 9-bit word with parity control: the Modbus RTU default. */
    USART2->CR1 = USART_CR1_M | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    NVIC_SetPriority(USART2_IRQn, UART_IRQ_PRIORITY);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    const uint32_t sr = USART2->SR;
    if ((sr & (USART_SR_RXNE | USART_SR_ORE)) != 0u) {
        const uint8_t byte = (uint8_t)(USART2->DR & 0xFFu); /* reading SR then DR clears the flags */
        if ((sr & USART_SR_ORE) != 0u) {
            s_overruns++;
        }
        /* A byte with a parity or framing error is dropped; the frame's CRC check then fails. */
        if ((sr & (USART_SR_PE | USART_SR_FE)) == 0u) {
            (void)xStreamBufferSendFromISR(s_rx, &byte, 1, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}

void board_uart_write(const uint8_t *data, size_t len)
{
    GPIOA->BSRR = 1u << DE_PIN; /* drive the bus */
    for (size_t i = 0; i < len; ++i) {
        while ((USART2->SR & USART_SR_TXE) == 0u) {
        }
        USART2->DR = data[i];
    }
    while ((USART2->SR & USART_SR_TC) == 0u) {
    }
    GPIOA->BSRR = 1u << (DE_PIN + 16u); /* release the bus */
}

uint32_t board_uart_overruns(void)
{
    return s_overruns;
}

void board_watchdog_start(void)
{
    IWDG->KR = 0xCCCCu; /* start */
    IWDG->KR = 0x5555u; /* unlock the prescaler and reload registers */
    IWDG->PR = 4u;      /* LSI (about 32 kHz) / 64 */
    IWDG->RLR = 2000u;  /* about 4 s */
    for (uint32_t i = 0; i < 100000u && IWDG->SR != 0u; ++i) {
        /* wait for the registers to update */
    }
    IWDG->KR = 0xAAAAu;
}

void board_watchdog_kick(void)
{
    IWDG->KR = 0xAAAAu;
}
