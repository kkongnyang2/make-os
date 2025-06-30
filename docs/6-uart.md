## xv6-riscv의 uart.c 파일을 읽어보자

### 목표: uart 이해
작성자: kkongnyang2 작성일: 2025-06-26

---
### 0> 개념 정리

uart는 cpu, 메모리 쪽의 병렬 데이터를 두 가닥(TX보내기와 RX받기)의 직렬 신호로 바꿔 주는 컨버터다. 원래 칩 내부에서는 64개의 선으로 클럭마다 64비트를 보낸다. 하지만 외부에서는 클럭을 동기화할 수 없기에 비동기 직렬로 전송하고, 8비트(1바이트)마다 체크를 한다. 그리고 이러한 선들을 버스라고 부르는데, 이미 cpu에서는 하드웨어 제작 단계에서 이러한 uart mmio의 존재를 알고 있기에 주소 0x10000000으로 사용한다.

```
  CPU 파이프라인 (5~10 stage 예)          메모리·I/O
┌─ IF ─┬─ ID ─┬─ EX/AGU ─┬─ MEM  ─┬─ WB ─┐
| 명령  | 해석  | 주소계산   |캐시/TLB | 결과  |
└──────┴──────┴──────────┴────────┴──────┘
               ▲
               │ 가상주소(VA)
          ┌────┴────┐
          │ TLB HIT │ 1~2 cycle
          └────┬────┘
               │ 물리주소(PA)
               ▼
         L1 D-Cache HIT? 1~4 cycle
               │
        ┌──────┴───────┐
        │     MISS     │ (10~20 cyc L2, 30~50 cyc L3, 100~200 cyc DRAM…)
        └──────────────┘
```
버스? 누가 언제 DRAM에 말을 걸 수 있는지 규정하는 교통로 규칙이다. CPU 말고도 디스크나 I/O 컨트롤러가 버스 마스터가 되어 직접 메모리를 읽고 쓰는 메커니즘이다. CPU는 목적지, 길이, 옵션을 레지스터에 써서 미션을 주고, 인터럽트가 올때까지 다른일을 하며 기다린다. 마스터는 "나 DRAM 0x12340000 4KB 읽을래" 라고 외치는 쪽,(과거에는 CPU 한명이었지만 현대에는 DMA,GPU,NIC 등 다수), 슬레이브는 주소가 자기사이즈 범위에 들어오면 "여긴 DRAM이야" "여긴 UART야" 응답하는 측이다. 만약 동시에 두 마스터가 버스를 요구하면 우선순위로 차례를 지정한다.
```
층                      물리 폭               한번에 다루는 논리 단위
CPU <-> L1/L2/L3        64~512bit           캐리라인 64B를 버스트로 여러 클럭
CPU <-> DDRS DRAM       64bit+ECC           64bit*burst16 = 128B 메모리 버스트
PCle 4*4                4lane*1bit(복호시128)    126B TLP 페이로드, 256B MPS 추천
UART                    1bit(TX), 1bit(RX)      8bit payload+1start+1stop
```
```
     ┌─ Start(0) │  bit0 … bit7 │ Stop(1) ─┐
TX───┤────────────┴─────────────┴───────────┤→ 시간

```
```
      ┌───── 물리층(선) ─────┐
ADDR[31:0]  DATA[63:0]  CLK  RESET …          ← parallel AXI 예시
TX   RX  GND  REFCLK                       ← serial PCIe 예시
      └────────────────────┘
              ▲
              ▼
      ┌─── 프로토콜층 ───┐
• 버스 요청/허가 (arbitration)  
• 주소 → 타깃 장치 인식 (decoding)  
• 읽기 vs 쓰기 구분 (control)  
• 데이터 크기·정렬·burst 규칙 …  
      └───────────────────┘
```

### 1> 용어 정리

```
baud rate                       // 초당 전송 비트 수. xv6은 38.4kbis/s이다
THR/RHR(transmit/receive holding register)  // cpu에 오가는 바이트를 담는 8bit 레지스터
LSR(line status register)       // TX_IDLE, RX_READY 같은 플래그로 하드웨어 상태 보고
IER/ISR(interrupt enable/status)  // 어떤 이벤트로 인터럽트가 왔고 뭘 활성화햇는지 저장
LCR(line control)               // 데이터 길이, stop비트, parity, baud-latch 모드 전환 등을 지정
FIFO                            // 추가된 16B 하드웨어 버퍼. FCR_FIFO_ENABLE로 키고 FCR_FIFO_CLEAR로 초기
Uart interrupt                  // PLIC이 IRQ 10번으로 cpu에 전달
top/bottom half                 // 인터럽트 진입 직후 처리(top)과 버퍼 비우기 깨우기 등 후처리(bottom)
console subsystem               // 실제 하드웨어는 uart.c, 라인 처리는 console.c
```

### 2> 흐름 정리

```
+-------------+        +-------------+         +----------------+
| user space  | sys_write → consolewrite() →   | circular TX buf|
|  printf()   |        |  (console.c) |         | uart_tx_buf[]  |
+-------------+        +-------------+         +----------------+
                                     │ (1)enqueue & uartstart()
                                     ▼
                                uartputc()
                                     │            ┌─┐ HW raises
                                     │ (top-half) │ │ IRQ
                                     ▼            ▼ ▼
                               uartstart()──▶[THR empty?]──▶ PLIC ─► devintr()
                                     ▲                            │
                                     │                            ▼
 (2) sleep(&uart_tx_r) <── buffer full?                    uartintr()
                                     ▲                            │
    wakeup(&uart_tx_r) ◄─────────────┘         while(uartgetc()!=-1) consoleintr()
```

### 3> uart.c
```c
//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}


// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      ReadReg(ISR);
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);
    
    WriteReg(THR, c);
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from devintr().
void
uartintr(void)
{
  // read and process incoming characters.
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
```