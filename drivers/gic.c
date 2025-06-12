#include "gic.h"

// GIC의 물리적 레지스터 시작 주소 (QEMU에서 고정된 위치)
#define GICD_BASE 0x08000000  // Distributor: 어떤 인터럽트를 허용할지 정함
#define GICC_BASE 0x08010000  // CPU Interface: 어떤 인터럽트를 CPU에게 전달할지 결정

// GIC 전체 초기화 (인터럽트 시스템 시작 준비)
void gic_init(void) {
    // ① GIC Distributor 활성화
    // 모든 인터럽트를 제어하는 "총괄 관리자"를 켠다
    *(volatile unsigned int *)(GICD_BASE + 0x000) = 1;

    // ② GIC CPU 인터페이스 활성화
    // 실제 CPU에 인터럽트를 전달할 수 있게 인터페이스를 연다
    *(volatile unsigned int *)(GICC_BASE + 0x000) = 1;

    // ③ 타이머 인터럽트 (IRQ ID 30)만 허용하도록 설정
    // 인터럽트 ID는 32개씩 4바이트 블록으로 묶여 있고, 30번은 첫 번째 블록
    *(volatile unsigned int *)(GICD_BASE + 0x100 + 4 * (30 / 32)) |= (1 << (30 % 32));

    // IRQ 30 -> CPU 0에만 전달되도록 설정 (GICD_ITARGETSR)
    *(volatile unsigned char *)(GICD_BASE + 0x800 + 30) = 0x01;

}

// 현재 어떤 인터럽트가 들어왔는지 확인
unsigned int gic_acknowledge(void) {
    // GICC_IAR (Interrupt Acknowledge Register) 읽기
    // → 어떤 IRQ 번호가 들어왔는지 반환됨
    return *(volatile unsigned int *)(GICC_BASE + 0x0C);
}

// 인터럽트 처리가 끝났다고 GIC에 알림
void gic_eoi(unsigned int intid) {
    // GICC_EOIR (End Of Interrupt Register)에 IRQ ID를 써서 종료 알림
    *(volatile unsigned int *)(GICC_BASE + 0x10) = intid;
}
