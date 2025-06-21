### > 파일 구조

[1] 부트 진입
[2] 커널 초기화
[3] 트랩 핸들러 등록
[4] 타이머 인터럽트 활성화
[5] init 프로세스 시작
[6] 유저 스페이스 → 시스템 콜 → 커널 진입

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



### > 메모리 배치 kernel/kernel.ld

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


### > M모드 및 스택 설정 kernel/entry.S

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


### > M->S 전환, timer 세팅 kernel/start.c

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

### > 커널 초기화 루틴 kernel/main.c

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

### > 모드 전환
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

### > kernel/trap.c

trap handler : trap(인터럽트/예외/시스템콜)이 발생했을 때 실행되는 C 함수
trampoline : 유저 <-> 커널 모드 전환 시 사용되는 어셈블리 코드 블록
stvec : trap 발생 시 jump 할 주소를 저장하는 CSR 레지스터



```c
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;              // 타이머 인터럽트 보호용 스핀락
uint ticks;                             // 시스템 틱 수(시계 단위)

extern char trampoline[], uservec[], userret[]; // trampline 주소 심볼

void kernelvec();                             // 커널 벡터 어셈블리 루틴

extern int devintr();                         // 디바이스 인터럽트 처리 함수

// S 모드에서 인터럽트 처리 루틴
void
trapinit(void)
{
  initlock(&tickslock, "time");         // 타이머 인터럽트용 ticks 값을 보호하기 위한 락 초기화
}

void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);           // 현재 코어의 stvec에 kernelvec라는 c함수 주소를 써넣음
                                        // stvec = supervisor trap vector base address register.
                                        // s 모드에서 trap이 발생했을때 어디서부터 처리를 시작할지 주소를 담는 레지스터
}


// 유저 프로그램이 trap 걸릴때
void
usertrap(void)
{
  int which_dev = 0;                    // 디바이스 인터럽트 식별용 변수

  if((r_sstatus() & SSTATUS_SPP) != 0)      // 진짜 유저 모드에서 온게 맞는지 확인
    panic("usertrap: not from user mode");

  w_stvec((uint64)kernelvec);               // trap이 들어왔으니 stvec을 커널모드로 전환

  struct proc *p = myproc();                // 현재 실행 중인 프로세스 포인터
  
  p->trapframe->epc = r_sepc();             // 유저의 pc를 저장 (복귀 시 필요)
  
  if(r_scause() == 8){                      // 8번은 시스템 콜(ecall)

    if(killed(p))
      exit(-1);                             // 이미 죽은 프로세스면 즉시 종료

    p->trapframe->epc += 4;                 // pc+4 (다음 명령어)

    intr_on();                              // syscall 도중 인터럽트 허용

    syscall();                              // 시스템 콜 핸들링
  } else if((which_dev = devintr()) != 0){

  } else {                                  // 알 수 없는 예외 발생
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);                           // 프로세스 종료 예약
  }

  if(killed(p))
    exit(-1);

  if(which_dev == 2)                        // 2번은 타이머 인터럽트
    yield();                                // CPU를 양보함

  usertrapret();                            // 유저 공간 복귀 준비
}

// 유저 모드로 복귀할 준비
void
usertrapret(void)
{
  struct proc *p = myproc();

  intr_off();                                 // 복귀 도중 trap 발생하지 않게 인터럽트 비활성화

  // stvec을 다시 다시 trampoline의 uservec으로 설정
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // uservec에서 사용할 정보들을 trapframe에 세팅
  p->trapframe->kernel_satp = r_satp();         // 커널 페이지 테이블
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 커널 스택 최상단
  p->trapframe->kernel_trap = (uint64)usertrap; // 다시 trap 들어올 때 실행할 함수
  p->trapframe->kernel_hartid = r_tp();         // 하트 번호

  
  // 유저 모드 복귀를 위한 sstatus 설정
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // SPP=0(U)로
  x |= SSTATUS_SPIE; // SPIE=1 유저 모드에서 인터럽트 허용
  w_sstatus(x);

  // 실행 재개 위치 설정
  w_sepc(p->trapframe->epc);

  // 유저 페이지 테이블 설정
  uint64 satp = MAKE_SATP(p->pagetable);

  // trampoline 상단에 있는 userret()을 호출하여 실제 복귀 수행
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// 커널 모드에서 trap 발생 시 호출
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // 원인을 알 수 없는 인터럽트
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  if(which_dev == 2 && myproc() != 0)       // 타이머 인터럽트면 CPU 양보
    yield();

  // trap 전 레지스터 복원
  w_sepc(sepc);
  w_sstatus(sstatus);
}

// 타이머 인터럽트 핸들러
void
clockintr()
{
  if(cpuid() == 0){                     // 0번 하트만 ticks 증가 및 wakeup. 0번 하트는 초기화 담당인 동시에 전역 시계(ticks) 기준 하트임.
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // 100만 cpu 사이클 뒤에 또 깨워줘, 즉 1 tick = 1 timer interrupt = 100Hz = 약  10ms
  w_stimecmp(r_time() + 1000000);
}

// 다른 디바이스면 1, 모르면 0, 타이머 인터럽트면 2
int
devintr()
{
  uint64 scause = r_scause();       // scause 레지스터. 현재 trap의 원인을 나타냄

  // 외부 인터럽트 (PLIC을 통해 들어오는 디바이스 인터럽트)
  if(scause == 0x8000000000000009L){      // 이 값이면 Supervisor external interrupt

    // 어떤 장치가 인터럽트를 발생시켰는지 확인.
    int irq = plic_claim();               // PLIC에서 IRQ 번호 확인

    if(irq == UART0_IRQ){                 // UART(시리얼 통신 장치)에서 인터럽트 발생
      uartintr();                         // UART 관련 인터럽트 핸들러 실행
    } else if(irq == VIRTIO0_IRQ){        // VirtIO 디스크에서 인터럽트 발생
      virtio_disk_intr();                 // 디스크 인터럽트 핸들러 실행
    } else if(irq){                       // 알 수 없는 장치에서 인터럽트 발생
      printf("unexpected interrupt irq=%d\n", irq);
    }

    if(irq)                               // 인터럽트 처리가 끝났음을 PLIC에 알림
      plic_complete(irq);

    return 1;                             // 장치 인터럽트 처리 완료
  } else if(scause == 0x8000000000000005L){   // 이 값이면 Supervisor timer interrupt
    clockintr();                           // 타이머 인터럽트 핸들러 실행
    return 2;                         // 타이머 인터럽트 처리 완료
  } else {
    return 0;                   // 알 수 없는 인터럽트
  }
}
```