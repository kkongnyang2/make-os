## xv6-riscv의 trap.c 파일을 읽어보자

작성자: kkongnyang2 작성일: 2025-06-23

### 개념 정리

트랩? CPU가 현재 흐름을 즉시 끊고 커널로 점프하는 모든 사건의 총칭이다.
종류는 시스템 콜(8번), 페이지 폴트(12,13,15번), 0 나누기 등 명령어에 기인하는 exception 와 타이머(7번), 디바이스, 외부 인터럽트(5번) 등 외부에 기인하는 interrupt 가 있다.
RISC-V는 둘 모두를 scause 코드로 구분하고 같은 통로로 들어온다.

유저/커널을 U/S 모드로 구분해 실행하고, 트랩 진입 시 PC와 레지스터 일부를 CSR에 잠깐 보관한 후
trampoline.S(가장 높은 VA 페이지 0xFFFF..F000) 코드가 전체 레지스터를 struct trapframe(그 바로 아래 VA이고 스택구조)으로 푸시하고 C함수 kernel_trap() 호출.
커널이 원인 처리 후 usertrapret()을 거쳐 다시 트램펄린으로 돌아가 레지스터를 복원하고 sret으로 U 복귀.

### 용어 정리
```
stvec (Supervisor Trap-Vector)      // 트랩 진입 시 점프할 VA를 담는 CSR
sepc (Supervisor Exception PC)      // 트랩 직전 PC 보관 CSR
scause(Supervisor Cause)            // 트랩 원인 코드 & 인터럽트 플래그
sstatus.SPP/SPIE                    // 트랩 전 실행 모드·인터럽트 상태 비트
sscratch                            // xv6는 여기다 커널 스택 VA를 보관
SRET (Supervisor RETurn)            // sepc→PC, SPP 모드로 복귀하는 명령
trapframe                           // 레지스터 31개+α를 담는 구조체(최고 바로 아래 VA)
trampoline.S                        // 진입/복귀 1 page 어셈블리(최고 VA)
kernel_trap()                       // trap.c 공통 C 처리 함수
usertrap()/usertrapret()            // U-mode 전용 예외 처리 & 복귀 준비
devintr()                           // PLIC·CLINT 인터럽트 번호 판별 헬퍼
PLIC (Platform-Level Int. Ctrl.)    // 외부 디바이스 인터럽트 컨트롤러
CLINT (Core-Local Int. Ctrl.)       // 타이머·소프트웨어 인터럽트 MMIO
SPP (Previous Privilege)            // sstatus 내 ‘트랩 전 모드(U/S)’ 비트
SPIE (Previous Interrupt Enable)    // sstatus 내 ‘U-mode에서의 인터럽트 허용’ 비트
```

### 흐름 정리
```
┌─────────────┐          ┌────────────────────┐
│ User space  │  trap    │  trampoline.S      │
│   code      │─────────▶│  (VA 0x...FFF000)  │
└─────────────┘          │ 1) sscratch → sp   │
                         │ 2) RA/GP…  push    │
                         │ 3) sp = kernel_sp  │
                         │ 4) call kernel_trap│
                         └─────────┬──────────┘
                                   │C 함수
                         ┌─────────▼──────────┐
                         │   trap.c           │
                         │ kernel_trap():     │
                         │  ├─ if (U-mode) → usertrap() │
                         │  └─ else        → kern-trap  │
                         ├── usertrap():              │
                         │  · 시스콜? dev? fault?     │
                         │  · devintr()               │
                         │  · usertrapret()           │
                         └─────────┬──────────┘
                                   │
                         ┌─────────▼──────────┐
                         │ trampoline.S       │
                         │ 1) trapframe pop   │
                         │ 2) sstatus.SPP=U   │
                         │ 3) sret (PC=sepc)  │
                         └─────────▼──────────┘
                         ┌─────────────┐
                         │ User space  │  ← 복귀
                         └─────────────┘
```
uservec/kernelvec -> vector라는 뜻. stvec이 가르키는 주소
trampoline -> 도약판
userret/usertrapret -> return이라는 뜻.


한줄정리 : 유저 모드에서 trap이 발생! stvec 보기! 보통 uservec(최고 VA인 trampoline 페이지)이 써져있음!
가서 모든 레지스터를 커널의 trapframe 스택에 저장! (a0는 잠깐 딴데에 저장해놨다가 저장) trapframe에 넣어놨던 커널의 sp, tp, usertrap주소, satp을 불러와 레지스터에 저장! 펜스 설치하고 satp CSR을 커널 satp로 갈아끼고 tlb를 비우고 usertrap으로 진입!
stvec을 kernelvec로 갈아껴서 내부 trap 대비! trapframe에 돌아갈 유저 pc를 저장! 어떤 트랩인지 scause를 읽어와 진짜 핸들러를 실행하고 이제 리턴으로 진입!
usertrapret에서는 다시 원상태로 세팅해주는 거! stvec을 다시 uservec으로 써놔! trapframe에 커널의 satp, sp, usertrap주소, tp를 넣어줘! sstatus를 U로 바꾸고 sepc CSR에 아까 trapframe에 넣어둔 프로세스 pc를 써넣어. 유저 페이지테이블을 인자로 넘기면서 userret을 호출해.
아까 uservec에서 스택에 담아뒀던 걸 다 꺼내! 그리고 satp CSR을 유저 satp로 갈아껴.

```
0   kernel_satp      // 커널 페이지테이블 주소
8   kernel_sp        // 커널 스택 꼭대기
16  kernel_trap      // usertrap() 주소
24  padding
32  kernel_hartid    // hartid
40  ra               <-- 여기서부터 34개 사용자 레지스터

112 a0               <-- 특별히 a0 백업 위치

280 t6
```

### kernel/trampoline.S

```
#include "riscv.h"
#include "memlayout.h"

# tramsec라는 새 섹션으로 전환.
# 이 전체를 trampoline 가상주소(가장 높은 VA 한 페이지)에 정렬해 넣도록.
# 커널, 유저 페이지테이블 모두 같은 VA로 매핑해 페이지테이블 스위치 후에도 코드 주소가 변하지 않도록
.section trampsec

# 두개 다 전역으로 공개해 다른 오브젝트에서 이 섹션의 시작 주소를 참조할 수 있게
.globl trampoline
.globl usertrap

# trampsec 섹션의 맨 첫 바이트에 심볼 trampoline 이름 부여
trampoline:
# 2^4 = 16Byte로 다음 코드 정렬. 4바이트로 해도 되지만 넉넉히 잡아 캐시 성능 향상
.align 4
.globl uservec

uservec:    
        # 유저 -> 커널

        # sscratch는 s모드의 임시 저장소, a0는 함수 인자 레지스터
        # trap 발생 직전의 유저 함수 인자 레지스터를 임시로 sscratch에 보관
        csrw sscratch, a0

        # TRAPFRAME = 커널이 모든 프로세스 페이지테이블에서 같은 가상주소(0xFFFFFFFFE000)로 매핑해둔 상수.
        # a0에 TRAPFRAME 즉시값 넣기
        li a0, TRAPFRAME
        
        # 34개 레지스터(ra~t6)값을 TRAPFRAME+40~280 위치에 저장하여 유저 컨텍스트 보존
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	      # sscratch를 읽어와 t0로 복사하고 그걸 TRAPFRAME+112에 저장
        csrr t0, sscratch
        sd t0, 112(a0)

        # sp에 kernel_sp 저장
        ld sp, 8(a0)

        # tp에 kernel_hartid 저장
        ld tp, 32(a0)

        # t0에 kernel_trap(usertrap()주소) 저장
        ld t0, 16(a0)

        # t1에 kernel_satp(커널 페이지테이블 주소) 저장
        ld t1, 0(a0)

        # satp가 아직 유저 테이블을 가리키므로 지금까지 연산은 확실히 끝내라.(펜스 역할)
        sfence.vma zero, zero

        # satp에 커널 페이지테이블 주소를 써서 전환하라
        csrw satp, t1

        # 유저 -> 커널 전환 위해 TLB 비우기.
        # 트램폴린 그 한페이지만 커널 유저 VA가 동일할 뿐 나머진 다르기 때문에 혼동없게 비워야 함
        # TLB란? 페이지테이블 캐시. 자주 쓰이는 전화번호부 즐겨찾기 해놓은거.
        sfence.vma zero, zero

        # usertrap()로 진입 (S모드)
        jr t0

        # usertrap() -> 커널 처리 -> usertrapret() -> userret

.globl userret
userret:
        # 커널 -> 유저.

        # a0: 유저 페이지 테이블 주소 적혀있음
        # 펜스 치고 satp에 유저 페이지테이블 주소 써서 전환하고 TLB 비우기
        sfence.vma zero, zero
        csrw satp, a0
        sfence.vma zero, zero

        # a0에 TRAPFRAME 베이스 값 넣기
        li a0, TRAPFRAME

        # 다시 레지스터 값 restore하기
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	      # 유저 a0 값도 restore
        ld a0, 112(a0)
        
        sret
```


### kernel/trap.c

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

extern char trampoline[], uservec[], userret[];

void kernelvec();

extern int devintr();

// 부팅시 hart 0에서 단 한번 initalize
void
trapinit(void)
{
  // tickslock: ticks(타이머 인터럽트마다 +1)을 보호하는 스핀락
  // 락 구조체 내부 필드를 0으로 초기화하고 디버깅용으로 'time'이름 설정
  initlock(&tickslock, "time");
}

// 부팅시 각 하트마다 단 한번 initalize
void
trapinithart(void)
{
  // trap이 발생했을 때 어디서 처리를 시작할지 알려주는 CSR 레지스터
  // 현재 커널상태이기에 지금 발생하는 모든 트랩은 kernelvec으로 가야하기 때문
  w_stvec((uint64)kernelvec);
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
                                            // 이 trapframe은 아까 TRAPFRAME 상수로 불렀던 거하고 같은 거임.
  
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