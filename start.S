.section .text
.global _start

_start:
    // [1] 스택 초기화
    // - C 코드에서 함수 호출, 지역 변수 등은 스택 필요
    ldr x0, =_stack_top    // 스택 최상단 주소 로드
    mov sp, x0             // 스택 포인터 설정

    // [2] 벡터 테이블 주소 등록
    // - 인터럽트 발생 시 CPU가 이 테이블을 참고함
    ldr x0, =vector_table  // 벡터 테이블 시작 주소
    msr VBAR_EL1, x0       // VBAR_EL1 (Exception Vector Address Register)에 설정
    isb                    // Instruction Synchronization Barrier (즉시 반영)

    // [3] 인터럽트 허용
    // - DAIFClr의 비트: D(디버그), A(SError), I(IRQ), F(FIQ)
    msr DAIFClr, #0xf      // 모든 인터럽트 비트 클리어 → 허용 상태로

    // [4] 커널 진입점(main)으로 이동
    bl main                // main() 함수 호출 (C로 진입)

hang:
    wfe                    // Wait For Event (CPU를 저전력 대기 상태로)
    b hang                 // 무한 대기 루프

//-------------------------------------------------------
// [5] 인터럽트 벡터 테이블
// - Exception Level 1 (EL1)에서 발생하는 예외를 처리할 위치 지정
// - 총 4가지 종류: Synchronous, IRQ, FIQ, SError
//-------------------------------------------------------
.align 11                 // 2048바이트 정렬 (ARM 벡터 테이블 요구사항)
vector_table:
    b sync_handler        // 0x000: Synchronous Exception (예: system call)
    b irq_handler         // 0x080: IRQ (타이머, 외부 인터럽트)
    b fiq_handler         // 0x100: FIQ (고속 인터럽트)
    b error_handler       // 0x180: SError (시스템 에러)

sync_handler:
    b .                   // 무한 루프 (아직 처리 안 함)

irq_handler:
    bl irq_handler_c      // C로 만든 IRQ 핸들러 호출
    eret                  // Exception Return → 인터럽트 전으로 복귀

fiq_handler:
    b .                   // 처리 안 함

error_handler:
    b .                   // 처리 안 함

//-------------------------------------------------------
// [6] 스택 공간 정의 (.bss 섹션)
// - 초기화되지 않은 데이터(메모리 공간)를 정의하는 영역
//-------------------------------------------------------
.section .bss
    .align 12             // 4096바이트 정렬 (페이지 크기 기준)
    .space 0x1000         // 4KB 스택 공간 확보
_stack_top:



/*
┌──────────────┐
│ _start       │   ← QEMU가 처음 이 지점부터 실행
├──────────────┤
│ 스택 초기화  │   ← C 코드 실행에 필수
│ 벡터 테이블  │   ← 인터럽트 종류별 핸들러 주소 설정
│ 인터럽트 ON  │   ← IRQ 등을 받기 위해 허용
│ main() 호출  │   ← 우리가 만든 C 커널 진입
└─────┬────────┘
      ↓
[인터럽트 발생]
      ↓
VBAR_EL1 → vector_table → irq_handler → irq_handler_c()
*/