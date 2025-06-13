/*======================================*/
/*            include/uart.h           */
/* UART 드라이버 인터페이스 헤더      */
/*======================================*/
#ifndef UART_H
#define UART_H
#include <stdint.h>
void uart_init(void);
void uart_puts(const char *str);
#endif // UART_H