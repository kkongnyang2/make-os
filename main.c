/*======================================*/
/*            main.c 파일             */
/* 진입점 이후 기본 초기화 및 루프     */
/*======================================*/
#include "exception.h"
#include "uart.h"
#include "timer.h"
#include "gic.h"
#include <stdint.h>

void main(void) {
    uart_init();
    uart_puts("Hello, Minimal ARM64 OS!\n");
    gic_init();
    timer_init();
    enable_interrupts();
    while (1) {
        __asm__("wfi");
    }
}