#include "uart.h"

// QEMU virt 머신에서 UART0 장치가 매핑된 물리 주소
#define UART0_BASE 0x09000000UL  // PL011 UART 기본 주소 (QEMU에서 고정)

// 메모리-매핑된 레지스터에 값을 쓰는 함수
static inline void mmio_write(unsigned long reg, unsigned int data) {
    // volatile: 최적화 방지 → 하드웨어 레지스터는 반드시 실제로 접근해야 함
    *(volatile unsigned int *)reg = data;
}

// 문자 하나 출력 (UART로 전송)
void uart_putc(char c) {
    // UART0 레지스터에 ASCII 코드 한 글자 씀 → QEMU가 이를 터미널에 출력
    mmio_write(UART0_BASE, c);
}

// 문자열 출력 (문자열 끝까지 반복 출력)
void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);  // 문자 하나씩 출력하고 다음 문자로 이동
    }
}
