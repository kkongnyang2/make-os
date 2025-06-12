#ifndef GIC_H
#define GIC_H

// GIC (Generic Interrupt Controller) 관련 함수 선언

// GIC 초기화
// - Distributor와 CPU Interface 활성화
// - 특정 인터럽트(IRQ 30번)만 허용하도록 설정
void gic_init(void);

// 현재 발생한 인터럽트 ID를 받아옴
// - IRQ 핸들러 내부에서 어떤 인터럽트인지 확인하는 데 사용
unsigned int gic_acknowledge(void);

// 인터럽트 처리가 끝났다고 GIC에 알림 (EOI: End of Interrupt)
void gic_eoi(unsigned int intid);

#endif // GIC_H
