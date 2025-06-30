#include "kernel/syscall.h"   # SYS_write, SYS_exit 정의
#include "user/user.h"

.macro SYSCALL name, num
    .globl \name
\name:
    li   a7, \num
    ecall
    ret
.endm

SYSCALL write, SYS_write
SYSCALL exit , SYS_exit