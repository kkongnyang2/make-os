/*======================================*/
/*           include/timer.h           */
/* 타이머 드라이버 인터페이스 헤더    */
/*======================================*/
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "gic.h"

/* 타이머 초기화/핸들러 */
void timer_init(void);
void timer_handler(void);

/*--------------------------------------------------*/
/*   AArch64 시스템 레지스터 접근 헬퍼 인라인 함수   */
/*   QEMU‑virt & Raspberry Pi4 모두 공통 사용 가능    */
/*--------------------------------------------------*/
static inline uint64_t read_cntfrq_el0(void)
{
    uint64_t v;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (v));
    return v;
}

static inline void write_cntp_tval_el0(uint64_t v)
{
    __asm__ volatile ("msr cntp_tval_el0, %0" :: "r" (v));
}

static inline void write_cntp_ctl_el0(uint32_t v)
{
    __asm__ volatile ("msr cntp_ctl_el0, %0" :: "r" (v));
}

#endif // TIMER_H