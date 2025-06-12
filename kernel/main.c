#include "uart.h"
#include "gic.h"
#include "timer.h"

void print_hex(unsigned int val) {
    char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        uart_putc(hex[(val >> i) & 0xF]);
}

void print_label(const char *label, unsigned int val) {
    uart_puts(label);
    uart_puts(": 0x");
    print_hex(val);
    uart_putc('\n');
}

// 숫자를 10진수로 UART 출력하는 보조 함수
// - UART에는 문자열만 출력할 수 있어서, 숫자도 직접 문자로 만들어야 함
static void print_dec(unsigned int n) {
    char buf[10];
    int i = 0;

    if (n == 0) {
        uart_putc('0');
        return;
    }

    // 숫자를 뒤집어서 문자 배열로 저장 (ex. 123 → ['3', '2', '1'])
    while (n > 0 && i < sizeof(buf)) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    // 배열을 거꾸로 출력해서 원래 순서대로 보이게 함
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// IRQ가 발생했을 때 호출되는 인터럽트 핸들러 함수
// - 벡터 테이블에서 이 함수로 연결됨
void irq_handler_c(void) {
    uart_puts(">> irq_handler_c entered\n");
    // 어떤 인터럽트인지 GIC에게 물어봄 → IRQ 번호를 알려줌
    unsigned int intid = gic_acknowledge();

    // UART로 IRQ ID 출력 (디버깅용)
    uart_puts("IRQ ID: ");
    print_dec(intid);
    uart_putc('\n');

    // 타이머 인터럽트(30번)라면
    if (intid == 30) {
        uart_puts("Tick!\n");      // Tick 메시지 출력
        timer_init();              // 타이머를 다시 설정해서 주기 반복
    }

    // 인터럽트 처리 완료를 GIC에 알림
    gic_eoi(intid);
}

// 커널 진입 지점
// - Bare-metal 환경에서 main()이 바로 실행됨
void main(void) {
    uart_puts("Starting Timer...\n");

    // 인터럽트 컨트롤러 초기화
    gic_init();
    uart_puts(">> gic_init complete\n");

    // 타이머 설정 (1초 뒤 IRQ 30번 발생하도록)
    timer_init();
    uart_puts(">> timer_init complete\n");

    unsigned int frq, ctl, tval, daif;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frq));
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
    asm volatile("mrs %0, cntp_tval_el0" : "=r"(tval));
    asm volatile("mrs %0, daif" : "=r"(daif));

    print_label("cntfrq_el0", frq);
    print_label("cntp_ctl_el0", ctl);
    print_label("cntp_tval_el0", tval);
    print_label("DAIF", daif);

    // 무한 루프 → 이후는 인터럽트로만 반응
    while (1) { }
}

/*
[boot entry] ─┬→ uart: "Starting Timer..."
              ├→ GIC: 인터럽트 수신 준비
              ├→ Timer: 1초 뒤 IRQ 30 발생 예약
              └→ 무한 루프 대기

[IRQ 30 발생] ─→ irq_handler_c()
                  ├→ "IRQ ID: 30"
                  ├→ "Tick!"
                  └→ Timer 다시 설정
*/