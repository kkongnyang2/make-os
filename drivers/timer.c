#include "timer.h"
#include "platform.h"
void timer_init(void) {
    unsigned long f;
    asm("mrs %0, cntfrq_el0" : "=r"(f));
    asm("msr cntp_tval_el0, %0" : : "r"(f));
    asm("mov x0, #1; msr cntp_ctl_el0, x0" : : :"x0");
}