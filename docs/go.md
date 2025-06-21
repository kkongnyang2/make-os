qemu는 전용 가상 주소가 존재. 하지만 내가 만들 os는 qemu 전용이 아니라 범용으로 사용하길 원하기에 DTB를 읽어서 동적으로 얻은 주소 사용.
아키텍처만 동일하면 어느 머신에서도 돌아갈 수 있음.


[QEMU]
 │
 └───> 가상 하드웨어 생성 + dtb 생성
         │
[부트로더]
 │   └──> dtb 전달
[커널 (Ubuntu)]
 └──> dtb 파싱 → 디바이스 주소, IRQ 등 설정
[드라이버]
 └──> 실제 장치 제어

.
├── start.S             → x0로 dtb 주소 받기
├── main.c              → uart 초기화 호출
├── uart.c              → 실제 MMIO 주소로 초기화
├── dtb.c               → 간단한 dtb 파서 (serial@ 찾기)
└── link.ld             → 로드 주소는 0x40080000 (QEMU 기본)

1. start.S
x0 레지스터로 넘어온 


✅ 최종 정리

✔️ 목표: Bare-metal에서 하드웨어 제어
✔️ 테스트 환경: QEMU ARM64 Virt Machine (LED 대신 UART 출력)
✔️ 방법: Bare-metal → UART → Timer → MMU → Shell
✔️ 기존 예제 코드 참고해서 복붙, 주석 추가로 구조 이해
✔️ 결국 Buildroot 수준까지 목표 → rootfs 만들어 BusyBox, 네트워크 추가

### > 설치
```
~$ sudo apt install qemu-system-aarch64         #qemu arm64 설치
~$ qemu-system-aarch64 --version                #설치 확인
QEMU emulator version 6.2.0 (Debian 1:6.2+dfsg-2ubuntu6.26)

~$ sudo apt install gcc-aarch64-linux-gnu       #크로스컴파일러 설치   
~$ aarch64-linux-gnu-gcc --version              #설치 확인
aarch64-linux-gnu-gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
```

### > 만들 구조

baremetal/
├── Makefile
├── link.ld
├── start.S               # 부트 코드(초기화)
├── include/              # 헤더파일
│   ├── uart.h            # UART 헤더
│   ├── timer.h           # Timer 헤더
│   └── mmu.h             # MMU 헤더
├── drivers/              # 하드웨어 드라이버 모음
│   ├── uart.c
│   ├── timer.c
│   └── mmu.c
├── kernel/               # 커널 로직
│   ├── main.c            # 진입점, 초기화 순서
│   ├── shell.c          # Shell
│   └── scheduler.c     # (나중에 추가)
└── build/                # 빌드 산출물
    ├── kernel.elf
    └── kernel.img

### > 모듈 설명

UART : 직접 레지스터에 값 쓰기 → 출력
Timer : 인터럽트 → tick 발생
MMU : 페이지 테이블 → 주소 변환
Shell : UART I/O로 기본 명령어 인터프리터

### > 주소

qemu virt machine 기준

0x09000000: UART0 (직렬 통신 장치. 컴퓨터와 터미널 간 텍스트 주고받을 때 사용)
0x08000000: GIC (인터럽트 관리 컨트롤러. timer, uart, virtio 등 하드웨어 인터렙트를 gic가 받아서 cpu에 전달)
0x0A000000: Virtio (가상화 디바이스 인터페이스. 네트워크, 블록 디바이스 등)
0x40000000: DRAM 시작 주소. 여기부터 메모리
그 사이: bootloader, MMU, DTB 로드
0x40080000: OS/커널 이미지 로딩 주소. 코드(.text)
그 뒤로 데이터,

[ 프로그램의 메모리 공간 ]
┌───────────────┐
│ 벡터 테이블     │  ← 인터럽트 처리 함수 포인터들
├───────────────┤
│ 코드 (.text)   │  ← 실행 명령어(펌웨어, 커널, 앱)
├───────────────┤
│ 상수 (.rodata) │  ← 상수 문자열, 테이블
├───────────────┤
│ 데이터 (.data) │  ← 전역 변수 (초기값 있음)
├───────────────┤
│ BSS (.bss)    │  ← 전역 변수 (초기값 0)
├───────────────┤
│ 힙 (heap)     │  ← malloc, new 등 동적할당
├───────────────┤
│ 스택 (stack)   │  ← 함수 호출, 지역 변수
└───────────────┘

```
~$ cat > link.ld                      #link.ld 생성
SECTIONS
{
    . = 0x40080000; /* QEMU virt machine에서 기본 로드주소 */
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
Ctrl+D
```

### > arm64 특성

1. 바이트 순서: little endian사용. 0x12345678은 메모리에 78 56 34 12 순서로 저장.
2. 메모리 정렬: 4바이트 단위의 word alignment가 기본. 0x40080001같은건 정렬되지 않은 주소.
3. 레지스터: x0 ~ x30은 범용 레지스터, sp는 스택 포인터, pc는 프로그램 카운터, VBAR_EL1은 인터럽트 벡터 베이스, TTBR_EL1은 페이지 테이블 베이스, SCTLR_EL1은 시스템 제어 레지스터(MMU,캐시 켜기)
4. 함수 호출 규약: 리턴값은 x0, 파라메터 x0~x7. 스택은 16바이트 정렬(align 16)

---
### > qemu 테스트

[ hello.S ] --(어셈블러)--> [ hello.o ] --(링커)--> [ kernel.elf ] --(objcopy)--> [ kernel.img ]
어셈블리 → 오브젝트	: CPU가 이해할 수 있는 기계어(.o)로 변환
오브젝트 → ELF 실행파일	: 코드와 데이터(섹션)들을 주소에 맞게 배치
ELF → 바이너리 이미지 : ELF 헤더 제거, 부트로더/에뮬레이터가 바로 읽을 수 있게

```
~$ cat > test.S                      #test.S 생성
.section .text
.globl _start
_start:

    b .
Ctrl+D
```

```
~$ aarch64-linux-gnu-as hello.S -o hello.o                      #어셈블리>오브젝트
~$ aarch64-linux-gnu-ld -T link.ld hello.o -o kernel.elf        #링커
~$ aarch64-linux-gnu-objcopy -O binary kernel.elf kernel.img    #바이너리로 변환

~$ qemu-system-aarch64 \
    -M virt \                                                   #표준 ARM64 가상머신
    -cpu cortex-a53 \                                           #ARM64 CPU
    -nographic \                                                #시리얼로만 입출력
    -kernel kernel8.img                                         #커널 바이너리 지정
Ctrl+A X
```

```
~$ ps aux | grep qemu           #실행중인 프로세스 번호
~$ kill 숫자                     #강제종료
```

---
### > Makefile
위 과정을 하나로 정리



### > 디버깅

다른 터미널에서
gdb-multiarch ~/make-os/kernel.elf
(gdb) target remote localhost:1234  #연결
(gdb) continue                      #코드 실행
(gdb) quit                          #그만두면 멈춘 주소 나타남
(gdb) info registers                #레지스터 정보
(gdb) disassemble                   #어셈블리 코드 보기
(gdb) x/i $pc                       #현재 위치(pc)


### > Uart

1. 전원 On → 커널 코드 실행
2. UART로 "Hello" 같은 문자 출력
3. Timer를 설정해서 "1초마다 인터럽트(IRQ) 주세요"라고 요청
4. 진짜 1초마다 CPU한테 인터럽트가 오면 → 우리가 만든 함수(IRQ 핸들러) 호출됨
5. 거기서 UART로 "Tick!" 출력


✅ 전체 흐름 요약: Tick 출력이 나오기까지


🧱 0. QEMU 실행
```bash
qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel kernel.img
```
- QEMU는 커널 바이너리를 `0x40080000`에 로드하고,
- `_start`라는 심볼(시작 주소)부터 실행을 시작함.


🔥 1. `_start` 실행 (start.S)

| 동작 | 설명 |
|------|------|
| `sp` 설정 | 스택 초기화 (`_stack_top`을 `sp`로 설정) |
| `VBAR_EL1 = vector_table` | 인터럽트 벡터 테이블 주소 등록 |
| `DAIFClr, #0xf` | 모든 인터럽트 허용 (IRQ, FIQ, SError 등) |
| `bl main` | C 코드 진입점 `main()` 호출 |


⚙️ 2. `main()` 진입 (main.c)
```c
void main(void) {
    uart_puts("Starting Timer...\n");

    gic_init();   // GIC 인터럽트 컨트롤러 초기화
    timer_init(); // 1초 뒤 IRQ 30 인터럽트 설정

    while (1) {}  // 무한 대기 (인터럽트만 처리함)
}
```

🛠️ 3. GIC 초기화 (gic.c)

| 동작 | 설명 |
|------|------|
| `GICD_CTLR = 1` | Distributor 켬 (전역 인터럽트 관리자) |
| `GICC_CTLR = 1` | CPU Interface 켬 (CPU로 인터럽트 전달 허용) |
| `GICD_ISENABLER0 |= (1 << 30)` | IRQ ID 30 (타이머 인터럽트)만 허용 |

⏱️ 4. Timer 초기화 (timer.c)

| 동작 | 설명 |
|------|------|
| `cntfrq_el0` 읽기 | 타이머 주파수 (ex. 62.5MHz) |
| `cntp_tval_el0 = cntfrq` | 1초 후 인터럽트 발생 예약 |
| `cntp_ctl_el0 = 1` | 타이머 시작 (IRQ 발생 가능) |

💤 5. 메인 루프 진입
- `main()`은 `while(1)` 루프에 진입하여 아무 일도 하지 않음
- 이후의 동작은 **인터럽트에 의해서만** 발생함

⚡ 6. 1초 후 타이머 인터럽트 발생
- 타이머 내부적으로 `cntp_tval_el0`에서 카운트 감소
- 0에 도달하면 **IRQ 30번 발생**
- CPU는 벡터 테이블을 따라 **`irq_handler` 실행**

🧭 7. 벡터 테이블 진입 (start.S)
```asm
vector_table:
    b sync_handler       // Synchronous Exception
    b irq_handler        // IRQ 발생 시 여기가 실행됨
    b fiq_handler        // FIQ
    b error_handler      // SError

irq_handler:
    bl irq_handler_c     // 실제 C 핸들러 호출
    eret                 // 복귀
```

📦 8. `irq_handler_c()` 실행 (main.c)
```c
unsigned int intid = gic_acknowledge();  // IRQ ID 읽기

uart_puts("IRQ ID: ");
print_dec(intid);                        // "IRQ ID: 30" 출력

if (intid == 30) {
    uart_puts("Tick!\n");               // "Tick!" 출력
    timer_init();                        // 다시 타이머 재설정 (1초 후 반복)
}

gic_eoi(intid);                          // GIC에 처리 완료 알림
```

🔁 9. 루프 반복
- 다시 타이머 설정됨 (`timer_init()` 호출)
- 또 1초 뒤 IRQ 30 발생 → 위 과정 반복



.section .text
.global _start

_start:
    // 벡터 테이블 등록
    ldr x0, =vector_table
    msr VBAR_EL1, x0
    isb

    // GIC 초기화
    bl gic_init

    // CNTV Timer 설정 (1초)
    ldr x1, =0x3B9ACA00      // 1,000,000,000 ticks
    msr cntv_tval_el0, x1
    mov x2, #1
    msr cntv_ctl_el0, x2

    // IRQ 언마스크
    msr DAIFClr, #2

1:  wfe
    b 1b

.align 11
vector_table:
    // Synchronous EL1t
    b default_handler
    .space 0x80 - 4

    // IRQ EL1t
    b default_handler
    .space 0x80 - 4

    // FIQ EL1t
    b default_handler
    .space 0x80 - 4

    // SError EL1t
    b default_handler
    .space 0x80 - 4

    // Synchronous EL1h
    b default_handler
    .space 0x80 - 4

    // IRQ EL1h
    b irq_handler
    .space 0x80 - 4

    // FIQ EL1h
    b default_handler
    .space 0x80 - 4

    // SError EL1h
    b default_handler
    .space 0x80 - 4

default_handler:
    b .

irq_handler:
    // 메시지 출력
    ldr x0, =tick_msg
print_loop:
    ldrb w1, [x0], #1
    cbz w1, tick_done
    bl putc
    b print_loop

tick_done:
    // 타이머 재설정
    ldr x1, =0x3B9ACA00
    msr cntv_tval_el0, x1

    // 인터럽트 EOI 처리
    ldr x0, =0x08010010
    mov w1, #27            // Timer IRQ ID 27
    str w1, [x0]

    eret

putc:
    ldr x2, =0x09000000    // UART MMIO
wait_uart:
    ldr w3, [x2, #0x18]
    tst w3, #0x20
    b.ne wait_uart
    str w1, [x2]
    ret

gic_init:
    ldr x0, =0x08000000    // GIC Distributor
    mov w1, #1
    str w1, [x0]           // GICD_CTLR

    add x1, x0, #0x100     // GICD_ISENABLER0
    mov w2, #(1 << 27)     // Timer IRQ ID 27
    str w2, [x1]

    ldr x0, =0x08010000    // GICC base
    mov w1, #1
    str w1, [x0]           // GICC_CTLR

    add x1, x0, #0x4       // GICC_PMR
    mov w2, #0xFF
    str w2, [x1]

    ret

tick_msg:
    .asciz "Tick!\n"


결과
Tick!
Tick!
Tick!



### > mmu(메모리 매니지먼트 유닛)

물리 주소와 가상 주소를 매핑해준다. 따라서 유저모드/커널모드를 분리하여 프로세스를 격리하고 메모리를 보호한다.
step 1. 페이지 테이블 준비
arm64에서는 기본으로 L1 -> L2 -> L3 테이블로 계층화를 하지만 여기선 identity mapping으로 구성
step 2. mmu 레지스터
TTBRO_EL1 : 페이지 테이블 베이스 주소
TCR_EL1 : 주소 크기 설정
MAIR_EL1 : 메모리 속성
SCTLR_EL1 : mmu 활성화