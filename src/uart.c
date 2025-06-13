#include "uart.h"
#include "board.h"
#include <stdint.h>

#define UARTDR  (*(volatile uint32_t *)(UART0_BASE_ADDR + 0x00))
#define UARTFR  (*(volatile uint32_t *)(UART0_BASE_ADDR + 0x18))
#define UARTCR  (*(volatile uint32_t *)(UART0_BASE_ADDR + 0x30))

#define UARTFR_TXFF (1 << 5)

/* QEMU-virt에서는 하드 reset 직후 이미 UART가 켜져 있으므로
 * 크리티컬 파라미터(클럭·baud 등)를 건드리지 않는다.
 * 필요하면 CR 만 재-활성화해 주는 정도로 충분하다. */
void uart_init(void)
{
    /* 확실히 켜져 있지 않을 경우를 대비해  TXE | RXE | UARTEN 만 ON */
    UARTCR |= (1 << 8) | (1 << 9) | (1 << 0);
}

void uart_puts(const char *s)
{
    while (*s) {
        while (UARTFR & UARTFR_TXFF);  /* TX FIFO full? */
        UARTDR = *s++;
    }
}
