#ifndef UART_H
#define UART_H

// UART 관련 함수 선언

// 문자 하나 출력 (UART 전송)
void uart_putc(char c);

// 문자열 출력 (null 문자까지 반복)
void uart_puts(const char *s);

#endif // UART_H
