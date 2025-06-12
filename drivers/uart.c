#include "uart.h"
#include "platform.h"
static inline void mmio_write(unsigned long r, unsigned int v) {
    *(volatile unsigned int *)r = v;
}
void uart_putc(char c) { mmio_write(UART0_BASE, (unsigned int)c); }
void uart_puts(const char *s) { while(*s) uart_putc(*s++); }