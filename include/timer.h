#ifndef TIMER_H
#define TIMER_H

// ARM Generic Timer 초기화 함수
// - 1초 후에 인터럽트 발생하도록 설정
// - 인터럽트가 발생하면 handler에서 다시 이 함수를 호출해야 함 (주기 설정)
void timer_init(void);

#endif // TIMER_H
