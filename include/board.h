/*======================================*/
/*          include/board.h            */
/* 보드별 설정 헤더 포함               */
/*======================================*/
#ifndef BOARD_H
#define BOARD_H

// 기본적으로 QEMU 설정 사용
#ifndef BOARD_RASPI4
#include "qemu_virt.h"
#else
#include "raspi4.h"
#endif

#endif // BOARD_H