#ifndef EXCEPTION_H
#define EXCEPTION_H
static inline void enable_interrupts(void)
{
    __asm__ volatile ("msr daifclr, #2");   // IRQ 비트 클리어
}
#endif
