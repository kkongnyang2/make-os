#======================================
#            start.S 파일             
# minimal ARM64 OS 진입 어셈블리 코드 
#======================================
    .section .text
    .global _start

_start:
    ldr     x0, =_stack_top
    mov     sp, x0
    ldr     x1, =vector_table
    msr     VBAR_EL1, x1
    isb
    bl      main
hang:
    wfi
    b       hang

    .section .rodata
vector_table:
    .quad   0
    .quad   0
    .quad   0
    .quad   0

    .section .bss
    .align  3
_stack_bottom:
    .space  0x1000
_stack_top:

    .section .text
undefined_handler:
    b       undefined_handler
