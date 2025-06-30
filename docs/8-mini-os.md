## mini-os를 완성하자

### 목표: os의 이해
작성자: kkongnyang2 작성일: 2025-06-30

### 0> 프로젝트

모든 프로젝트는 소스파일 -> 규칙 -> 컴파일 및 빌드 -> 설치 -> 런 으로 이루어진다.

그리고 이 프로젝트의 소재는 mini-os 이다. qemu로 가상 하드웨어를 생성하고 (그 내부에서 벌어지는 실제 일은 나중에 공부하고 설치만) 그 하드웨어를 운용하기 위한 커널 이미지와, 디스크 이미지와, 사용자 공간을 만드는 것을 목적으로 한다.

이를 위한 과정은 다음과 같다. MMU와 TRAP, PCB, MMIO 함수들과, mkfs 함수 등의 소스 파일을 만든다.그리고 GNU make로 빌드할 수 있도록 makefile로 규칙을 작성한다. 크로스 툴체인(riscv64-unknown-elf)으로 컴파일하고 linker.ld 링커 파일로 빌드하여 커널 이미지를 만들도록 한다. 그리고 호스트 툴체인으로 디스크 이미지를 만든다. 해당 이미지들을 빌드 산출물로 내놓고 홈에 설치해 qemu로 런하도록 한다.


### >
```
                ┌──────────┐
   사용자 모드    │ Syscall   │ ← trap/exception/interrupt로 진입
                └────┬─────┘
                     ▼
 ┌──────── Boot & Init (start.S, main.c …) ─────┐
 │                                              │
 │ ┌── Process 관리 & Scheduler (proc.c) ────┐  │
 │ │  • PCB/TCB      • context-switch         │ │
 │ └──────────────────────────────────────────┘ │
 │                                              │
 │ ┌── Memory 관리 (vm.c) ────────────────────┐ │
 │ │  • MMU/Pagetable  • 물리 페이지 할당기     │ │
 │ │  • Copy-on-write  • Page cache            │ │
 │ └──────────────────────────────────────────┘ │
 │                                              │
 │ ┌── Trap/Interrupt/Timer (trap.c, plic.c) ─┐ │
 │ │  • Syscall 디스패치  • 외부 IRQ           │ │
 │ │  • 시계 틱 갱신      • 소프트IRQ          │ │
 │ └──────────────────────────────────────────┘ │
 │                                              │
 │ ┌── VFS & Filesystem (fs.c, log.c) ────────┐ │
 │ │  • inode 캐시   • 디렉터리 탐색          │ │
 │ │  • log-based FS • 버퍼 캐시              │ │
 │ └──────────────────────────────────────────┘ │
 │                                              │
 │ ┌── Device I/O & Drivers (uart.c, virtio.c)┐ │
 │ │  • 콘솔/TTY   • 블록 디바이스           │ │
 │ │  • (네트워크 스택은 xv6엔 없음)         │ │
 │ └──────────────────────────────────────────┘ │
 │                                              │
 │ ┌── 동기화 & IPC (spinlock.c, sleeplock.c) ┐│
 │ │  • spin/sleep lock • pipe, futex 류      ││
 │ └──────────────────────────────────────────┘│
 │                                              │
 │ ┌── 시간·알람·전원 (clock.c, pm.c*) ───────┐│
 │ │  • timekeeping  • RTC                    ││
 │ └──────────────────────────────────────────┘│
 └──────────────────────────────────────────────┘
```

### 1> 기존 xv6-riscv 공부

step 1. qemu-riscv64 설치
```
~$ sudo apt install ninja-build         # qemu는 기본 내장 gnu make가 아니라 ninja라는 builder 사용
~$ ninja --version
1.10.1
~$ git clone https://gitlab.com/qemu-project/qemu.git
~$ cd qemu
~$ ./configure --target-list=riscv64-softmmu
~$ ninja -C build -j$(nproc)
~$ ninja -C build install
```

step 2. 베어메탈용 크로스 툴체인 설치
```
~$ sudo apt install gcc-riscv64-unknown-elf
~$ riscv64-unknown-elf-gcc --version
riscv64-unknown-elf-gcc () 10.2.0
```

step 3. xv6-riscv 실행
```
~$ git clone https://gitlab.com/xv6-riscv.git
~$ cd xv6-riscv
~$ make
~$ make qemu
끝내고 싶으면 ctrl+A+X
```

### 2> 필요 파일 확인
                              |


`

### > arm64 특성

1. 바이트 순서: little endian사용. 0x12345678은 메모리에 78 56 34 12 순서로 저장.
2. 메모리 정렬: 4바이트 단위의 word alignment가 기본. 0x40080001같은건 정렬되지 않은 주소.
3. 레지스터: x0 ~ x30은 범용 레지스터, sp는 스택 포인터, pc는 프로그램 카운터, VBAR_EL1은 인터럽트 벡터 베이스, TTBR_EL1은 페이지 테이블 베이스, SCTLR_EL1은 시스템 제어 레지스터(MMU,캐시 켜기)
4. 함수 호출 규약: 리턴값은 x0, 파라메터 x0~x7. 스택은 16바이트 정렬(align 16)









### > 디버깅

다른 터미널에서
gdb-multiarch ~/make-os/kernel.elf
(gdb) target remote localhost:1234  #연결
(gdb) continue                      #코드 실행
(gdb) quit                          #그만두면 멈춘 주소 나타남
(gdb) info registers                #레지스터 정보
(gdb) disassemble                   #어셈블리 코드 보기
(gdb) x/i $pc                       #현재 위치(pc)


