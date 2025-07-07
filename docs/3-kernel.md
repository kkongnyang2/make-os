## xv6-riscv의 시작 파일을 읽어보자

### 목표: 커널의 이해
작성자: kkongnyang2 작성일: 2025-06-21

---
### 0> 파일 구조

[1] 부트 진입
[2] 커널 초기화
[3] 트랩 핸들러 등록
[4] 타이머 인터럽트 활성화
[5] init 프로세스 시작
[6] 유저 스페이스 → 시스템 콜 → 커널 진입

```
xv6-riscv/
├── Makefile           ← 빌드 명령 정의
├── kernel/            ← 커널 코드가 담긴 디렉토리
│   ├── entry.S        ← 커널 진입점 (Assembly)
│   ├── start.c        ← 초기화 루틴
│   ├── main.c         ← 커널 main 함수
│   ├── trap.c         ← 트랩/인터럽트 처리
│   ├── syscall.c      ← 시스템 콜 디스패치
│   ├── proc.c         ← 프로세스 생성/스케줄링
│   ├── vm.c           ← 페이지 테이블 및 메모리 관리
│   ├── kalloc.c       ← 물리 메모리 할당자
│   ├── file.c, fs.c   ← 파일 시스템
│   ├── uart.c         ← UART 드라이버
│   ├── spinlock.c     ← 스핀락
│   └── ...            ← 다양한 커널 서브시스템
├── user/              ← 사용자 영역 프로그램
│   ├── init.c         ← 최초 유저 프로그램
│   ├── sh.c           ← 셸
│   └── ...            ← ls, cat, echo 등 유틸들
├── fs/                ← 파일 시스템
│   └── fs.img         ← 초기 파일 시스템 이미지
├── mkfs/              ← 파일 시스템 이미지 생성 도구
│   └── mkfs.c
├── include/           ← 커널/유저 공용 헤더
│   └── *.h
├── .gdbinit           ← GDB 초기화 설정
└── README.md
```




### 1> 메모리 배치 kernel/kernel.ld

링커 스크립트란? .o 들을 하나로 묶어 실행 가능한 .elf 바이너리를 만들때, 어떤 섹션을 어디에 배치할지 결정하는 배치도 역할을 한다.
```
OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  /*
   * ensure that entry.S / _entry is at 0x80000000,
   * where qemu's -kernel jumps.
   */
  . = 0x80000000;

  .text : {                     // 실행 가능한 코드들을 이 섹션에 배치
    *(.text .text.*)            // 일반 C/어셈블리 코드
    . = ALIGN(0x1000);          // 4KB 정렬 (페이지 단위)
    _trampoline = .;            // 트램폴린 시작 위치 저장
                                // 트램폴린이란 유저 모드에서 커널 모드로 돌아올 때 필요한 임시 코드 공간. 각 프로세스의 trap handler가 점프하는 장소
    *(trampsec)                 // 트램폴린 코드 삽입
    . = ALIGN(0x1000);          // 다시 4KB 정렬
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    //크기 체크. 혹여라도 1페이지(4KB)로 정렬 안됐을까봐.
    PROVIDE(etext = .);         // etext라는 심볼로 현재 주소 커서 저장. 텍스트 끝 주소를 알 수 있게.
  }

  .rodata : {                   // 읽기 전용 데이터들(const)
    . = ALIGN(16);              // 16바이트 정렬
    *(.srodata .srodata.*)      // srodata는 작은 상수들을 위한 별도 섹션
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {                     // 초기화된 전역변수
    . = ALIGN(16);
    *(.sdata .sdata.*)          // sdata는 작은 초기화된 전역 변수
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {                      // 초기화 안된 전역변수(초기값x)
    . = ALIGN(16);
    *(.sbss .sbss.*)            // 작은
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);             // end라는 심볼로 현재 주소 커서 저장. 커널의 끝 주소를 알 수 있게. 이 이후부턴 heap, user memory, alloc 등 시작.
}
```
```
주소 ↓ 높은 주소
──────────────────────────────────────────────
|                                            |
|                사용자 공간 (user)         | ← 프로세스 메모리
|                                            |
|--------------------------------------------| ← 사용자가 사용하는 마지막 주소 (~0xFFFFFFFF?)
|                커널 heap                  | ← `end[]` 이후 동적 할당
|                                            |
|--------------------------------------------| ← end (PROVIDE(end = .))
|                .bss 섹션                  | ← 초기화되지 않은 전역 변수
|                                            |
|--------------------------------------------|
|                .data 섹션                 | ← 초기화된 전역 변수
|                                            |
|--------------------------------------------|
|                .rodata 섹션               | ← const 문자열 등
|                                            |
|--------------------------------------------|
|                .text 섹션                 | ← C/ASM 코드, 함수들
|                                            |
|        ┌──────────────┐                    
|        │ trampoline   │ ← trap 진입용 코드 (한 페이지)
|        └──────────────┘                    
|                                            |
|--------------------------------------------| ← 0x80000000
|              entry point (_entry)         | ← QEMU가 jump 하는 시작 주소
──────────────────────────────────────────────
주소 ↑ 낮은 주소
```
각 섹션들은 링커가 .o들을 모아 하나의 elf를 생성하며 그때 크기가 결정된다.
커널 heap은 런타임 공간이라 섹션이 아님. 저주소부터 증가하며 요청 시 크기만큼 동적으로 메모리를 할당한다.
c언어 함수들을 위해 사용하는 stack0은 초기화되지 않았으므로 .bss 섹션에 포함


### 2> M모드 및 스택 설정 kernel/entry.S

cpu가 제일 먼저 읽는 파일은? _entry 라벨.

```
        # qemu는 커널을 0x80000000에 로드
        # 각 하트(CPU)가 해당 위치로 점프하도록 함.
        # kernel.ld로 인해 다음 코드가 0x8000000위치에 배치됨.
.section .text
.global _entry
_entry:
        # C 함수에서 사용할 스택 설정.
        # stack0은 start.c에서 정의.
        # 각 CPU마다 4096Byte 스택.
        # sp = 스택포인터 = stack0 + (hartid * 4096)
        la sp, stack0                   # sp = stack0
        li a0, 1024*4                   # a0 = 4096
        csrr a1, mhartid                # 현재 하트 ID 읽어오기(0,1,2)
        addi a1, a1, 1                  # +1 (왜냐하면 stack0[0]은 쓰지 않음)
        mul a0, a0, a1                  # hartid * 4096
        add sp, sp, a0                  # sp = stack0 + (hartid * 4096)
        # start.c에 있는 start()로 점프
        call start
spin:
        j spin                          # start()는 절대 리턴하면 안되므로 혹시 리턴했을때 cpu가 멈추지 않도록 무한 루프를 둔것.
```
```
stack0:  ┌────────────────────┐ ← 스택 바닥
         │                    │
         │      스택           │
         │                    │
         └────────────────────┘ ← sp 설정 (최상단 = stack0 + STACK_SIZE)
```


### 3> M->S 전환, timer 세팅 kernel/start.c

```c
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S는 각 CPU마다 하나의 스택 필요.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S는 여기 stack0의 M 모드로 점프함.
void
start()
{
  // RISC-V에서는 U(유저) S(커널) M(최상위)가 있다는거 기억할거임.
  // 지금이 그 machine mode.


  // mret(machine mode return) 후 S 모드로 점프할 수 있게 준비.
  // MPP(machine previous privilege mode)비트 = 그 이전 모드가 어디냐 저장. 00은 U, 01은 S, 11은 M
  unsigned long x = r_mstatus();        // 현재의 mstatus 레지스터 값을 읽어서 x에 저장. r은 read
                                        // x = 0x 0000 000a bc12 3800 (예시)
  x &= ~MSTATUS_MPP_MASK;               // MSTATUS_MPP_MASK = MPP 부분인 11,12비트 마스크
                                        // 11을 11비트에 설정, 따라서 0x 0000 0000 0000 1800
                                        // ~MSTATUS_MPP_MASK는 0x FFFF FFFF FFFF E7FF
                                        // 즉 이건 MPP 비트(11,12)를 0으로 초기화해라
  x |= MSTATUS_MPP_S;                   // MSTATUS_MPP_S = S모드를 의미하는 값.
                                        // 01을 11비트에 설정, 따라서 0x 0000 0000 0000 0800
                                        // 즉 이건 MPP 비트를 01(S모드)로 설정해라
  w_mstatus(x);                         // 조작한 값을 다시 mstatus에 기록. w는 write

  // MEPC 레지스터 = mret시 복귀할 pc를 담음(주로 main. S 모드의 커널 진임점)
  w_mepc((uint64)main);

  // 지금은 MMU(가상 메모리) 끄고 물리 주소 모드로 작동
  w_satp(0);

  // 16개의 예외 레지스터, 16개의 인터럽트 레지스터를 모두 S 모드로 넘기기 위한 함수.
  // 1111 1111 1111 1111 16개의 비트에 대해 전부.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  // S 모드에서 허용할 인터럽트 종류 설정.
  // supervisor interrupt enable 레지스터 현재 값을 읽고 or 연산으로 3개 인터럽트 추가.
  // SEIE: 외부 인터럽트, STIE: 타이머 인터럽트, SSIE: 소프트웨어 인터럽트
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);      // 현재 설정값을 읽고 각 비트를 1로 만들어 저장

  // S 모드가 메모리 접근 가능하도록 PMP(physical memory protection) 설정
  w_pmpaddr0(0x3fffffffffffffull);      // 최대 메모리 주소 0x 3f ffff ffff ffff * 하나당 4바이트
  w_pmpcfg0(0xf);                       // 모든 권한(RWX)을 부여

  // 타이머 인터럽트 설정
  timerinit();

  // tp 레지스터에 hartid 저장. cpuid()함수가 이를 참조함.
  int id = r_mhartid();
  w_tp(id);

  // mret 명령어로 S모드로 전환하고 main으로 점프
  asm volatile("mret");
}


void
timerinit()
{
  // 타이머 인터럽트 활성화
  // machine interrupt enable register
  // STIE(spuervisor timer interrupt enable): 타이머 인터럽트
  w_mie(r_mie() | MIE_STIE);            // 현재 설정값을 읽고 5번 비트를 1로 만들어 저장
  
  // sstc 확장 활성화
  // 기존에는 M 모드에서만 mtimecmp를 설정할 수 있었지만 sstc가 활성화되면 S모드도 stimecpm 사용가능
  w_menvcfg(r_menvcfg() | (1L << 63));  // 1L << 63이 sstc를 활성화함
  
  // mcounteren 레지스터는 어떤 카운터를 s모드에서 읽을 수 있게 허용할지 결정
  // 0번째 비트는 사이클 수 관련, 1비트는 시간 카운터, 2비트는 명령어 실행 수를 의미.
  w_mcounteren(r_mcounteren() | 2);     // 1번째 비트를 킴 -> 시간 카운터 허용

  // S모드 timecompare 타이머 비교 레지스터
  // stimecmp보다 커지면 타이머 인터럽트 발생
  w_stimecmp(r_time() + 1000000);       // 현재 시간+100만 tick(1초)으로 stimecmp 설정
}
```
참고로 타이머는 유일하게 cpu 안에 있는 순수 하드웨어 타이머로, M모드 소속 자원이자 모든 스케줄링의 기반이기에 여기에 정의되어 있다.


### 4> 커널 초기화 루틴 kernel/main.c

```c
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;        // 부트 hart가 초기화 끝났는지 표시하는 플래그
                                        // volatile: 컴파일러가 최적화로 생략하지 말고 꼭 메모리에서 읽게 함

void
main()
{
  if(cpuid() == 0){                     // cpu0 = 부트 hart만이 전체 시스템 초기화를 맡음
    consoleinit();                      // UART, printf, getc 등 콘솔 입출력 초기화
    printfinit();                       // printf 내부 버퍼 초기화
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();                            // 물리 메모리 페이지 할당기 초기화
    kvminit();                          // 커널 전용 페이지 테이블 생성
    kvminithart();                      // 현재 하트에 페이지 테이블 적용. MMU 활성화
    procinit();                         // 프로세스 테이블 초기화
    trapinit();                         // trap 관련 전역 설정. 트랩 핸들러 vector table 준비
    trapinithart();                     // 해당 hart에 트랩 핸들러 벡터 주소 등록
    plicinit();                         // 플랫폼 레벨 interrupt controller 초기화
    plicinithart();                     // 현재 하트에 연결된 인터럽트를 활성화
    binit();                            // 블록 버퍼 캐시 초기화
    iinit();                            // inode 테이블 초기화
    fileinit();                         // 전역 파일 디스크립터 테이블 초기화
    virtio_disk_init();                 // qemu 가상 디스크 장치 초기화
    userinit();                         // 최초 유저 프로세스를 생성
    __sync_synchronize();               // 메모리 순서를 강제 정렬
    started = 1;                        // 플래그 1로
  } else {                              // 나머지 하트는
    while(started == 0)                 // 부트 하트가 started = 1 해주기까지 대기하는 루프
      ;
    __sync_synchronize();               // 메모리 순서를 강제 정렬
    printf("hart %d starting\n", cpuid());
    kvminithart();                      // 현재 하트에 페이지 테이블 적용. MMU 활성화
    trapinithart();                     // 해당 hart에 트랩 핸들러 벡터 주소 등록
    plicinithart();                     // 현재 하트에 연결된 인터럽트를 활성화
  }

  scheduler();                          // 모든 cpu가 스케줄러에 진입하여 프로세스 루프  
}
```

### 5> 모드 전환
mret : M->S
mstatus.MPP를 보고 다음 모드를 결정
MPP=00(U모드) MPP=01(S모드) MPP=11(M모드)
mepc에 저장된 주소(main)로 점프

sret : S->U
sstatus.SPP을 보고 다음 모드를 결정
SPP=0(U모드) SPP=1(S모드)
sepc에 저장된 주소(유저 앱)로 점프
유저 앱 실행 시 SPP=0, sepc=유저코드, sret();

trap : U->S
직접적으로 상위 모드로 못올라가지만 예외 발생시.(ecall)
stvec에 등록된 trap handler로 점프. SPP=U모드. sepc에 복귀 주소 저장됨

trap이 언제 발생하나요?
예외 : ecall, 잘못된 주소 접근
인터럽트 : 타이머, 장치I/O

```
[U 모드] 유저 코드 실행 중
    ↓
  trap 발생 (ex. ecall, timer)
    ↓
[하드웨어]
  sepc ← 복귀 주소 저장
  sstatus ← 현재 모드 저장
  PC ← stvec 에 등록된 handler로 점프
    ↓
[S 모드] 커널의 trap handler 동작
```

### > makefile

[ test.S ] --(어셈블러)--> [ test.o ] --(링커)--> [ kernel.elf ] --(objcopy)--> [ kernel.img ]
어셈블리 → 오브젝트	: CPU가 이해할 수 있는 기계어(.o)로 변환
오브젝트 → ELF 실행파일	: 코드와 데이터(섹션)들을 주소에 맞게 배치
ELF → 바이너리 이미지 : ELF 헤더 제거, 부트로더/에뮬레이터가 바로 읽을 수 있게

툴체인이란 컴파일 전 과정을 담당하는 도구 세트이다.
(gcc / as / ld / objdump / gdb …)
gcc : .c -> 중간 IR -> 어셈블리 -> .o
이때 -c 옵션을 주면 여기까지만 하고 링크는 하지 않음
as : .S -> .o
ld : 여러 개의 .o와 라이브러리를 하나의 실행가능 elf로 묶음. 링커 스크립트에 따라 주소, 섹션 배치 결정
objcopy : 불필요한 섹션 제거 및 elf를 순수 바이너리로 바꾸기
objdump : 합쳐진 elf파일을 우리가 볼 수 있도록 다시 어셈블리어로 뽑아준 참고용 해설서.
-S 옵션은 어셈블리, -t 옵션은 심볼 테이블 등.
gdb : 실행 파일 조사 디버깅

크로스 컴파일이란 x86-64 노트북에서 RISC-V용 커널을 빌드하는 것을 말한다.
동일 아키텍트면 네이티브 컴파일.

따라서 크로스 컴파일용 툴체인은 이름으로 CPU와 OS 대상을 표시한다. (네이티브 컴파일 툴체인 이름과 구분)
<CPU>-<VENDOR>-<OS/ABI>
vendor? 툴체인 제작사. 없으면 unknown
OS/ABI? C 라이브러리 규약. 즉 어떤 상황에서 런 시킬건지 미리 대비해 그에 맞춰 커널 이미지를 준비함.(elf 환경인지, glibc가 존재하는 환경인지)

riscv64-unknown-elf는 RISC-V용 베어메탈 elf 실행포맷 환경 크로스 컴파일 툴체인. 즉 펌웨어, 커널, 임베디드 대상. OS 없음 → 직접 MMIO·폴링·trap 구현
riscv64-linux-gnu는 리눅스에서 만든 RISC-V용 GNU libc 환경 크로스 컴파일 툴체인. 즉 유저 공간 프로그램 대상.

이때 xv6은 os가 리눅스가 아니기에 gnu libc나 시스템 콜이 필요하지 않고, qemu가 커널 elf를 0x80000000에 바로 로드하기에 부트로더 또한 필요하지 않고, 커널 안에서 자체 printf와 memcpy를 구현하기에 libc 또한 필요하지 않다.
riscv64-unknown-elf로 충분하다. 그 도구만 있어도 컴파일(.o 생성), 링크(커널.ld로 elf 만들기), 실행(qemu가 elf를 메모리에 배치)가 모두 가능하다.

컴파일 단계 – Host PC에 RISC-V 크로스-툴체인이 있어야 Makefile이 통과.
실행 단계 – 생성된 kernel·fs.img 등을 QEMU가 부팅.
만약 컴파일한 kernel elf를 qemu가 아닌 실제 하드웨어 기기에서 부팅시키고 싶다면 openSBI 등을 활용해라. 걔가 커널 elf를 0x80000000으로 데려가는 부트로더 역할을 한다.

```
# 여기부터 별칭 명명과 소스파일과 툴 준비

# 별칭
# GNU make에서 =는 재귀확장 변수(매번 변수를 참초), :=는 단순확장 변수(대입순간 단 한번 그뒤로 상수)
K=kernel
U=user

# 커널 오브젝트 파일 목록
# .c -> .o는 명령어로 적어주지 않아도 내장 규칙으로 수행
OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o

# riscv64-unknown-elf-나 riscv64-linux-gnu-는 /opt/riscv/bin에 있을 거임
# TOOLPREFIX = 로 따로 설정도 가능
# make toolprefix로 따로 명명하지 않았을때 자동 탐색
# objdump = 설명서. 여기선 헤더 확인한거임
# -i는 지원 포맷 목록을 출력
# 2>&1 = 리다이렉션 명령어. stderr를 stdout으로 합쳐라
# >/dev/null = 출력은 폐기하고 성공 실패 여부만 남김
# 탐색에 성공하면 출력하여 shell의 반환값이 됨
# 세 후보가 모두 실패했으면 exit 1로 make 자체를 중단
# 1>&2 = stdout을 stderr로 합쳐서 빨간 글씨로 눈에 잘띄게.
ifndef TOOLPREFIX
TOOLPREFIX := $(shell \
  if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64

# as vs gas ― 이름만 다르고 실체는 같다
CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump



# 여기부터 c compiler flags와 linker flags 설정

# -Wall 모든 경고 켜기 -Werror 경고를 오류로 취급 -O 기본 최적화(O1)
# -fno~ 프레임 포인터 유지 -ggdb~ dwarf-2 디버그 심볼 삽입
# -MD .c 타킷과 헤더 연결로 이루어진 의존성 .d 파일. 헤더를 수정하면 해당 .o만 재컴파일하도록 하기 위함.
# -mcmodel medlow 모델이면 텍스트 섹션도 데이터 섹션도 2GB(31bit) 이내에 있어야함(작은 펌웨어). medany 모델이면 텍스트는 현재 pc에서 2GB, 데이터는 어디든 32bit이어야 함. large 모델이면 64bit 절대주소를 즉시값으로 넣음
# 예를 들어 0x80004000(텍스트 섹션) pc 지점에서 전역변수 0x80011234(데이터 섹션)를 찾으려 하면 이는 0x0000D234 즉 52KB 차이이다. auipc t0, hi20(+0x0d000)과 addi t0, t0, lo12(0x234)로 도달가능.
# 주석 부분은 완전 bare-metal 옵션 세트. -ffreestanding 표준 C 라이브러리 존재를 가정하지 말라 -mno-relax 릴랙스 최적화를 끔
# 그 중 두개만 열어놓음 -fno-common 중복 전역 심볼을 오류 처리 -nostdlib 커널이니 crt0/libc 링크 안함
# -fno-buildtin-17종 컴파일러 자체 최적화 대상에서 제외(커널이 구현한 함수와 충돌x)
# -Wno-main main 반환값 없다고 경고x
# -I 현재 디렉토리를 include 탐색 경로에 추가(include 파일이 어디에 있든 찾기 가능)
# 마지막 줄은 cc가 스택 보호기를 인식(-E로 컴파일 없이 전처리 했을 때 종료코드 0)하면 추가하라. 실패하면 조용히 넘어가라.
# 즉 커널은 자급자족 코드라 libc/시작파일이 필요 없고, 컴파일러 내장 builtin/스택보호/PIE를 꺼둔다
CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += -mcmodel=medany
# CFLAGS += -ffreestanding -fno-common -nostdlib -mno-relax
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# PIE(포지션 독립 실행가능) 끄기 -우분투 16.10 툴체인에서 기본으로 켜질 수 있음
# ifnoteq 같지 않으면 실행하라는 뜻. , 다음이 빈칸이니까 앞의 결과가 출력물이 존재하면 실행하라는게 됨.
# cc의 스펙 파일 전체를 출력하여 그 중 'f'가 아닌 no-pie를 찾아라
# 그래서 존재하면 -no-pie 옵션을 켜 PIE 끄기
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# ELF 섹션 정렬 시 페이지 크기 4KB로 고정
LDFLAGS = -z max-page-size=4096



# 여기부터 타깃 설정

# kernel = .o들 + 링커파일  + initcode
# 설정플래그 + 링커파일과 + .o들 -> 커널.elf 만들기
# 디스어셈블로 설명서 만들기
# 불필요 헤더 삭제하고 심볼 테이블 추출
$K/kernel: $(OBJS) $K/kernel.ld $U/initcode
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS) 
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

# initcode란 xv6가 부팅직후 userinit()에서 메모리에 복사 실행하는 최초 사용자 공간 코드
# 커널은 앞에서 만든거고 이건 프로세스다. 유일하게 이것만 커널에도 넣어줌
# 설정플래그 + .S -> .o 만들기
# -march-rv64g 모든 표준 확장 사용 -nostdinc 시스템 헤더 무시
# .o -> .out 만들기
# -N .text(RX)/.data(RW)를 모두 하나의 RWX 덩어리로 만들어서 재배치 없음
# -e start 엔트리포인트 심볼을 start로 설정
# -Ttext 0 가상주소 0부터 배치하여 xv6커널이 유저 공간에 로드하기 편함
# 순수 바이너리 파일 만들고 설명서 만들기
$U/initcode: $U/initcode.S
	$(CC) $(CFLAGS) -march=rv64g -nostdinc -I. -Ikernel -c $U/initcode.S -o $U/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $U/initcode.out $U/initcode.o
	$(OBJCOPY) -S -O binary $U/initcode.out $U/initcode
	$(OBJDUMP) -S $U/initcode.o > $U/initcode.asm

# 모든 c와 S에 대해 심볼 색인 파일 생성. 전부 탐색하도록 빌드 산출물 의존을 걸어둠.
tags: $(OBJS) _init
	etags *.S *.c



# 여기부터 유저 프로세스 설정

# 유저 프로그램용 정적 라이브러리 묶음
ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

# 라이브러리 참조해 유저 프로그램 _cat같은 실행파일 만들기
# _%는 패턴 규칙에 의해 알아서 _cat을 만들어야겠네? 하고 타깃 발동
# $@은 정해진 타깃이름 참조 $^는 모든 의존 목록
_%: %.o $(ULIB)
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $^
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

# perl 스크립트 usys.pl을 .o로 만들기
$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

# 프로세스 테이블을 채우는 테스트 실행파일 만들기
# -N .text/.data를 한 덩어리 RWX에
# -e main 엔트리 심볼을 main으로
# -Ttext 0 가상주소 0으로 링크
$U/_forktest: $U/forktest.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

# 호스트(내pc)용 실행파일 mkfs 만들기
# 커널과 mkfs간 구조체와 상수가 같도록 커널에서 fs.h 파일시스템 정의 param.h 상수 정의 가져옴
# 네이티브 gcc
mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Werror -Wall -I. -o mkfs/mkfs mkfs/mkfs.c

# GNU make에서는 중간 파일 .o를 디폴트로 삭제하지만, 공유하거나 fs.img 재생성을 막기 위해 남겨둠
.PRECIOUS: %.o

# 사용할 유저 프로그램 목록
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\

# 유저 프로그램 이미지 파일 생성(호스트에서)
fs.img: mkfs/mkfs README $(UPROGS)
	mkfs/mkfs fs.img README $(UPROGS)

# 파일이 아직 없더라도(첫빌드) 넘어가는 장치
-include kernel/*.d user/*.d



# 여기부터 추가 기능

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$U/initcode $U/initcode.out $K/kernel fs.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

# 호스트 UID로부터 고유한 GDB TCP 포트 계산. 몰리지 않게.
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# qemu 도움말에 -gdb 옵션이 있으면 -gdb tcp port 형식 사용, 없으면 구버전 -s -p port 사용
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
# 하트수
ifndef CPUS
CPUS := 3
endif

# -bios none: OpenSBI같은 펌웨어 건너뜀 -kernel: 커널 elf 직접 로드
# -m 128M: RAM 128MB -smp $(CPUS): 하트수 -nographic: 콘솔만
# virtio-mmio 추가. fs.img 넣기.
QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

# GDBPORT를 로컬에선 1234로 치환. 그리고 자동실행.
.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

# -S: qemu를 리셋 직후 정지. 다른 창에서 gdb 실행하면 됨
qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
```
```
               ┌─ kernel (S-mode) ───────────────────────────┐
               │ main() → userinit()                         │
               │            │                                │
1. 부팅 완료 ──┘            ▼                                │
2. initproc 생성          (커널 내장)                         │
      → `p->pagetable`   +--------------------------------+  │
      → `memmove()`      |   RAW   initcode   binary      |  │
      → `p->trapframe`   +--------------------------------+  │
      → `epc = 0`        ^  (VA 0x0)                       │
               │         │  4 KiB  |R/W/X|                 │
               └─────────┴────────┴────────────────────────┘
                         ↓ sret (U-mode 진입)
```
**initcode** 실행 (U-mode)  
   *간단한 hand-written 어셈블리*  
```asm
li  a0, 0        # argv=0
auipc a1, init   # "init" 문자열
ecall SYS_exec   # exec("/init")
ecall SYS_exit   # 만약 실패하면 종료
```