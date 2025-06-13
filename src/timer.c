/*======================================*/
/*            src/timer.c              */
/* 타이머 초기화 및 핸들러 구현        */
/*======================================*/
#include "timer.h"
#include "gic.h"
#include "board.h"
#include "uart.h"

void timer_init(void) {
    uint64_t cntfrq = read_cntfrq_el0();
    write_cntp_tval_el0(cntfrq);
    write_cntp_ctl_el0( (1<<0) | (1<<1) ); 
    irq_enable(IRQ_TIMER);
}

void timer_handler(void) {
    uint64_t cntfrq = read_cntfrq_el0();
    write_cntp_tval_el0(cntfrq);
    uart_puts("Tick!\n");
    irq_clear(IRQ_TIMER);
}