#include "gic.h"
#include "platform.h"
static inline void wr32(unsigned long a, unsigned int v) { *(volatile unsigned int *)a = v; }
void gic_init(void) {
    wr32(GICD_BASE+0x100, 1U<<TIMER_IRQ_ID);
    *(volatile unsigned char *)(GICD_BASE+0x800+TIMER_IRQ_ID) = 1;
    wr32(GICD_BASE,1);
    wr32(GICC_BASE,1);
}
unsigned int gic_acknowledge(void) {
    return *(volatile unsigned int *)(GICC_BASE+0x0C);
}
void gic_eoi(unsigned int id) {
    *(volatile unsigned int *)(GICC_BASE+0x10) = id;
}