.section .text
.global _start
_start:
    // 1) Initialize stack pointer
    ldr x0, =_stack_top
    mov sp, x0

    // 2) Set vector table base
    ldr x0, =vector_table
    msr VBAR_EL1, x0
    isb

    // 3) Enable IRQ
    msr DAIFClr, #2

    // 4) Call main()
    bl main

// Idle loop
1:  wfi
    b 1b

.align 11
vector_table:
    b default
    b irq_handler
    b default
    b default

irq_handler:
    bl irq_handler_c
    eret

default:
    b default

.section .bss
    .align 12
    .space 0x1000
_stack_top: