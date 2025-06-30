## xv6-riscv의 proc.c 파일을 읽어보자

### 목표: PCB 이해
작성자: kkongnyang2 작성일: 2025-06-26

---
### 0> 개념 정리

PCB? 커널이 한 프로세스를 식별하는 구조체이다. pagetable, kstack, context, trapframe, state, pid 등의 정보를 담고 있다.

그리고 여러 PCB를 미리 고정된 크기의 배열로 할당해 둔 공간을 PCB 풀이라고 한다. 부팅 시 PCB풀 + kstack풀 + trapframe 등 프로세스 자원을 부팅 시 전부 예약해둔다.

struct proc proc[NPROC]   // .bss 영역에 한꺼번에 자리 예약
┌──────────────┬──────────────┬── … ──┬──────────────┐
│ proc[0]      │ proc[1]      │       │ proc[NPROC-1]│
│ state=UNUSED │ state=UNUSED │       │ state=UNUSED │
│ kstack=…     │ kstack=…     │       │ kstack=…     │
└──────────────┴──────────────┴── … ──┴──────────────┘


스케줄러는 이러한 PCB를 생성/파괴하고 스위치하고 clock 기반 스케줄링, sleep/wakeup을 맡는다.


### 1> 용어 정리

PCB(process control block)  // 커널이 프로레스를 관리하는 구조체
PID(process ID)             // 고유 정수 ID
RUNNABLE                    // cpu를 받을 준비 완료(런 큐에 존재)
SLEEPING                    // sleep으로 블록, chan 이벤트 대기
ZOMBIE                      // exit 후 부모가 wait 할때까지
kstack(kernel stack)        // 하트가 S 모드에서 사용하는 perprocess 스택
swtch                       // 어셈 수준 레지스터 교환 루틴(scheduler<->proc)
chan                        // 슬립 큐 키 - 임의의 void* 값
KERNBASE                    // 커널 가상주소 시작점. 0x80000000
NPROC                       // 동시에 존재할 최대 프로세스 수. 64
lock                        // 둘 이상의 CPU가 동시에 같은 자료를 건드려 깨지는 상황을 막는 문지기 객체.
spin_lock                   // 락을 누군가 쓰고 있으면 CPU가 짧은 루프(spin)로 기다렸다가 빈 순간 바로 잡음

// PCB: 프로세스의 모든 커널측 정보 보관
struct proc -> pagetable, kstack, context, trapframe, state, pid
// 커널 컨텍스트: switch()가 저장/복원하는 최소 레지스터 집합
struct context -> ra, sp, s0~11
// 사용자 <-> 커널 경계 레지스터 백업 버퍼
struct trapframe -> 31개 일반레지스터 + sepc, sstatus, satp, kernel_sp
// 하트 당 전역: 현재 프로세스, 중첩 깊이
struct cpu -> proc, context, noff


### 2> 흐름 정리

```
CPU 하트
┌─────────────────────────────────────────────┐
│   사용자 모드 (U)                             │
│     └─ Trap                                 │
│   커널 모드 (S)                               │
│     ├─ 스케줄러(scheduler)  ◀─┐             │
│     │   └─ swtch.S          │ Context     │
│     ├─ 프로세스 코드(syscall)  │  Switch     │
│     └─ 드라이버               └─┬───────────┘
└─────────────────────────────────────────────┘

```

### 3> proc.c

```c
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];          // 하트별

struct proc proc[NPROC];        // PCB 풀.

struct proc *initproc;          // PID 1(userinit())을 가리키는 포인

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// 프로세스용 커널 스택 2페이지 영역을 예약 및 매핑
// xv6은 고정 pcb 풀이므로 프로세스 개수와 스택 공간을 일괄 예약해두면 런타임 kalloc 오버헤드를 피할 수 있다.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;                 // for문 용
  
  for(p = proc; p < &proc[NPROC]; p++) {  // PCB 별로
    char *pa = kalloc();          // 4KB 물리페이지 할당
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));     // 그 물리페이지를 가리킬 VA 계산. KERNBASE 상단부
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);  // 커널 PT에 1:1 매핑
  }
}

// PCB 풀 초기화
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");     // 커널 부트 단계에서 pid 스핀락을 0으로 초기화하고 디버그용 식별 이름 지어줌.
  initlock(&wait_lock, "wait_lock");  // wait/exit 락
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");     // 각 PCB 전용 스핀락
      p->state = UNUSED;              // 아직 미사용 슬롯
      p->kstack = KSTACK((int) (p - proc)); // 해당 프로세스의 커널 스택 VA
  }
}

// 하트 id 반환. 다른 하트로 프로세스가 넘어가는 인터럽트 비활성화 상태가 필요하기에 이렇게 고정.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 현재 하트의 struct cpu 주소 확보. 인터럽트 비활성화 상태가 필요하기에 이렇게 고정.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 현재 하트의 PCB 포인터 반환
struct proc*
myproc(void)
{
  push_off();               // 인터럽트 off
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();                // 복원
  return p;
}

// 전역 PID 발급
int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);     // 스핀락으로 동시 증가 충돌 방지
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// UNUSED PCB 한칸을 커널에서 실행할 준비로 셋업
static struct proc*
allocproc(void)
{
  struct proc *p;

  // USUSED 슬롯 찾기
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  // 슬롯확보
  p->pid = allocpid();
  p->state = USED;

  // trapframe 할당
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 유저 PT 마련
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 커널 컨텍스트 초기화
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;               // 아직 p->lock은 유지된 상태
}

// free
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);   // 더이상 U/S 전환 안하므로 4KB 물리페이지 회수
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// PCB용 사용자 PT 생성
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  pagetable = uvmcreate();      // 루트 PT 확보.
  if(pagetable == 0)
    return 0;

  // trampoline(0xFFFF_FFFF_FFFF_F000) 매핑
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // trapframe(trampoline 아래 1페이지) 매핑
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// PT와 물리메모리 free
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 길이 53바이트 RISC-V 기계어. 최초 사용자 바이트코드.
// user/initcode.S를 통해 커널에 포함된다
// exec("/init") 시스템콜.
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// PID 1 만들기
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // 물리 페이지 할당하고 va 0 과 매핑하는 PT 만들고 initcode 넣어주기
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // 첫 U모드 진입 세팅
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  // 이름, 디렉터리, state 설정
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->state = RUNNABLE;

  release(&p->lock);                // 이제 락 풀기
}

// 유저 메모리 힙 증가/감소
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
```