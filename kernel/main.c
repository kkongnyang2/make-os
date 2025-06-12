#include "uart.h"
#include "timer.h"
#include "gic.h"
#include "platform.h"

void irq_handler_c(void) {
    unsigned int id = gic_acknowledge();
    uart_puts("IRQ="); // print id
    // ... print_dec(id);
    uart_puts("\nTick!\n");
    gic_eoi(id);
    timer_init();
}

void main(void) {
    uart_puts("Starting...\n");
    gic_init();
    uart_puts("GIC OK\n");
    timer_init();
    uart_puts("Timer OK\n");
    while(1) { asm("wfi"); }
}