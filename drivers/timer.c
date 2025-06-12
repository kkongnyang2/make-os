#include "timer.h"

// ARM Generic Timer 초기화
void timer_init(void) {
    unsigned long cntfrq;

    // ① 현재 시스템의 타이머 주파수(cntfrq_el0)를 읽는다
    //    - 예: QEMU에서는 일반적으로 62_500_000 Hz (1초 = 62.5M tick)
    asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq));

    // ② 1초 뒤에 인터럽트 발생하도록 interval 설정
    unsigned long interval = cntfrq;

    // ③ cntp_tval_el0 (Timer Compare Value)에 interval 설정
    //    - 지금부터 몇 tick 뒤에 인터럽트가 발생할지 설정 (절대 시간이 아님)
    asm volatile("msr cntp_tval_el0, %0" : : "r"(interval));

    // ④ cntp_ctl_el0 (Timer Control Register) 활성화
    //    - 비트 0: ENABLE = 1 (타이머 켜기)
    //    - 비트 1: IMASK = 0 (인터럽트 허용)
    //    - 비트 2: ISTATUS = 0 (읽기 전용, IRQ 펜딩 여부)
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(1));
}
