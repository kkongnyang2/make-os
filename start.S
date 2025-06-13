/*======================================*/
/*              start.S                */
/* 부팅 엔트리 + UART 디버그 출력 테스트 */
/*======================================*/
    .section .text
    .align  4
    .global _start

/* QEMU‑virt용 PL011 UART0 레지스터 */
.equ UARTDR, 0x09000000      // Data Register
.equ UARTFR, 0x09000018      // Flag Register (TXFF = bit5)
/* QEMU-virt GICv2 베이스 주소 */
.equ GIC_BASE_ADDR, 0x08000000
.equ GICC_BASE,     GIC_BASE_ADDR + 0x1000

_start:
    /*--------------------------------------------------*/
    /*  (A) 부팅 직후 UART로 '@' 한 글자 찍어보기       */
    /*--------------------------------------------------*/
    ldr     x2, =UARTFR
1:  ldr     w3, [x2]         // 플래그 읽기
    tst     w3, #0x20         // TX FIFO Full?
    b.ne    1b                // 가득 차 있으면 대기
    ldr     x2, =UARTDR
    mov     w3, #'@'          // 디버그용 문자
    str     w3, [x2]          // 전송

    /*--------------------------------------------------*/
    /*  (B) 스택 설정                                  */
    /*--------------------------------------------------*/
    ldr     x0, =_stack_top
    mov     sp, x0

    /*--------------------------------------------------*/
    /*  (C) 예외 벡터 테이블 등록                      */
    /*--------------------------------------------------*/
    ldr     x1, =vector_table
    msr     VBAR_EL1, x1
    isb

    /*--------------------------------------------------*/
    /*  (D) C 진입점 호출                              */
    /*--------------------------------------------------*/
    bl      main

/*------------------------------------------------------*/
/*  (E) 메인에서 돌아오면 WFI 루프                       */
/*------------------------------------------------------*/
hang:
    wfi
    b   hang

/* ---------- 예외 벡터 ---------- */
    .section .rodata
vector_table:
    .quad   0           /* Synchronous EL1t */
    .quad   irq_handler /* IRQ        EL1t ★*/
    .quad   0           /* FIQ        EL1t */
    .quad   0           /* SError     EL1t */

/* ---------- IRQ handler (GICv3, EL1) ---------- */
    .text
    .align  4
irq_handler:
    /* IAR — Interrupt Acknowledge */
    mrs     x0, ICC_IAR1_EL1     // x0 = IRQ ID
    mov     x1, x0               // EOIR/ DIR 용 백업

    cmp     x0, #30              // 30 = EL1 물리 타이머 CNTP
    b.ne    irq_eoi
    bl      timer_handler        // C 핸들러 호출

irq_eoi:
    /* EOIR — End Of Interrupt + Deactivate */
    msr     ICC_EOIR1_EL1, x1
    msr     ICC_DIR_EL1,  x1
    eret


/*------------------------------------------------------*/
/*  스택(4 KiB)                                         */
/*------------------------------------------------------*/
    .section .bss
    .align  3
_stack_bottom:
    .space  0x1000
_stack_top:

