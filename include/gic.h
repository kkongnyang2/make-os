/*======================================*/
/*            include/gic.h            */
/* GIC(인터럽트 컨트롤러) 인터페이스  */
/*======================================*/
#ifndef GIC_H
#define GIC_H
#include <stdint.h>

#define IRQ_TIMER 27          /* CNTP PPI */

/* 초기화 & 제어 함수 선언만 */
void gic_init(void);
void irq_enable(int irq);
void irq_clear(int irq);
#endif
