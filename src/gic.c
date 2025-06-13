/*======================================*/
/*            src/gic.c                */
/* GIC 초기화 및 기본 인터럽트 관리   */
/*======================================*/
#include "gic.h"
#include "board.h"
#include <stdint.h>

#define GICD_BASE (GIC_BASE_ADDR + 0x0000)
#define GICD_CTLR (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_ISENABLER (*(volatile uint32_t *)(GICD_BASE + 0x100))

void gic_init(void)
{
    /* Distributor ON (그대로 v2 레지스터 호환) */
    GICD_CTLR = 1;

    /* CPU-IF 시스템 레지스터 초기화 (GICv3) */
    __asm__ volatile (
        "mov x0, #0          \n" /* BPR = 0 */
        "msr ICC_BPR1_EL1, x0\n"
        "mov x0, #0xFF       \n" /* 모든 우선순위 수용 */
        "msr ICC_PMR_EL1, x0 \n"
        "mov x0, #1          \n" /* ICC_CTLR_EL1[0] = EnableGrp1 */
        "msr S3_0_C12_C12_4, x0\n" /* == ICC_CTLR_EL1 */
        :::"x0","memory");
}

/* PPI(IRQ_TIMER) enable ― Distributor ISENABLER */
void irq_enable(int irq)
{
    GICD_ISENABLER = (1u << (irq % 32));
}

/* irq_clear — GICv3 에선 EOIR + DIR 로 끝났으므로 할 일 없음 */
void irq_clear(int irq) { (void)irq; }
