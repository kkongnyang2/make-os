## xv6-riscv의 vm.c 파일을 읽어보자

작성자: kkongnyang2 작성일: 2025-06-25

---
### 개념 정리

페이지? 공간을 연속적으로 할당하지 않고 페이지라는 단위로 쪼개서 사용한다.
프로세스의 메모리도, 실제 physical 메모리도 페이지 4KB 단위로 쪼갠다.

그리고 이러한 VPN(virtual page number)과 PEN(physical frame number)로 매핑해주는 표가 page table이다.(원소는 PTE)
한 프로세스에 대해 page table 절반은 프로세스 PTE, 절반은 공통으로 사용하는 커널 PTE 이다.

그럼 이 페이지 테이블 실체는 어디에 저장되냐? 공통 커널 PTE는 부팅시 .text, .data 밖 kalloc() 동적할당에 존재하고 satp CSR을 통해 부른다. 각 프로세스 PTE는 역시 kalloc() 동적할당에 저장되고, 개별 root PT 주소를 저장한다.

그럼 페이지 테이블에서 각 PTE를 어떻게 찾느냐? VA를 4조각으로 끊어 3개의 index와 page offset으로 사용한다. 이 목차로 페이지 테이블을 구성하기에 한영사전 처럼 찾을수 있는 것이다.

참고로 디스크에서는 블록이라는 단위를 쓰고, inode 메타데이터를 통해 블록끼리 연결된다. 이 디스크의 데이터를 메모리로 올릴 때 xv6은 각 페이지마다 VA 페이지를 할당하고 memcpy한다. 


### 용어 정리

```
가상주소 (VA, Virtual Address)        // 사용자·커널 코드가 쓰는 논리적 주소
물리주소 (PA, Physical Address)        // 실제 DRAM 칩의 셀 위치
MMU (Memory Management Unit)           // VA→PA 변환을 담당하는 하드웨어
TLB (Translation Look-aside Buffer)    // 변환 결과를 캐시해 속도를 높이는 MMU 내부 캐시
SATP (System Address Translation & Protection) // RISC-V가 현재 사용할 최상위 페이지테이블의 물리주소, 모드, ASID를 담는 CSR
PTE (Page Table Entry)                 // VA 범위 하나와 대응하는 설정(PPN·권한 비트 등)을 담은 64bit 구조체
VPN (Virtual Page Number)              // VA를 페이지 크기(4 KiB)로 나눈 인덱스 3단( Sv39 )/4단( Sv48 )
PPN (Physical Page Number)             // 실제 물리 페이지 프레임 번호
R/W/X 비트                             // Read/Write/Execute 허가 비트
U/S 비트                               // U-mode(유저)/S-mode(커널) 접근 허가
A/D 비트                               // Accessed/Dirty – 페이지 사용·쓰기 발생 시 MMU가 자동 set
sfence.vma zero,zero                   // TLB 전부 무효화(페이지테이블 바꾼 뒤 필수)
csrw satp, t1                          // SATP CSR에 새로운 페이지테이블(root PPN) 등록
```


### 흐름 정리

```
start.c      → kvminit()         → vm.c
              (커널전용)              ├─ kvmmake()      // 커널 자체 map
                                    ├─ kvmmap()       // 실제 page → VA 매핑 헬퍼
                                    ├─ kvminithart()  // CSR·TLB init
                fork(), exec() ↓
                                    ├─ uvminit()      // 유저 초기 page
                                    ├─ uvmalloc()     // 유저 주소공간 성장
                                    └─ walk(), mappages() 등
```

```
물리주소
0x8000_0000 ─┬─ .text
             │
             ├─ .rodata
             ├─ .data                  ★ kernel_pagetable 포인터 변수
             ├─ .bss                   (값은 아직 0)
end ─────────┘
             ├─────────── Free RAM managed by kalloc() ────────────┐
             │                                                     │
             │ 0x8010_0000  ── kernel_pagetable (root PT 4 KiB) ◀─┐│
             │ 0x8010_1000  ── per-process A root PT             ││
             │ 0x8010_2000  ── per-process B root PT             ││
             │   ...         ── L1/L0 PT, kernel stacks, buffers ││
             └────────────────────────────────────────────────────┘
0x8800_0000 ─ PHYSTOP  (xv6 가용 RAM 상한)
```

```
 63             39 38           30 29           21 20           12 11            0
┌────────────────┬──────────────┬──────────────┬──────────────┬────────────────┐
|   sign-extend  |   VPN[2]     |   VPN[1]     |   VPN[0]     | page offset    |
└────────────────┴──────────────┴──────────────┴──────────────┴────────────────┘
   (25 bits)        (9 bits)       (9 bits)       (9 bits)         (12 bits)
```
페이지 크기 4KB -> offset 12bit
레벨 수 3단계. root PTE -> 중간 -> leaf

```
root PT (9 bits)
┌─────0         255─────┬────256        511────┐
│   user area entries   │  kernel area entries │
│ (각 프로세스마다 달라) │ (모든 프로세스가 동일)│
└───────────────────────┴──────────────────────┘
```
root의 아래 영역까지 합치면 38비트는 user area, 38비트는 kernel area이다.

```
 63          10 9    8 7 6 5 4 3 2 1 0
┌─────────────┬──────┬───────────────┐
|  PPN[2:0]   | RSVD |  D A G U X W R|
└─────────────┴──────┴───────────────┘
   44 bits      2      권한·상태 비트
```
PTE 형식(64bit=8byte) 


### vm.c

```c
#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

// pagetable_t : 하나당 8byte인 PTE 512개를 담은 테이블 한 페이지를 가리키는 타입.
// 커널 루트 페이지테이블 명명
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld에서 .text 끝나는 표시

extern char trampoline[];

// 커널 테이블 생성
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();      // 페이지 하나 할당
  memset(kpgtbl, 0, PGSIZE);            // PTE 512개 모두 0으로 초기화 

  // MMIO 영역들 PTE 만들기. 읽기/쓰기 허용
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // 커널 .text 영역들 PTE 만들기. 읽기/실행 허용
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 커널 .data 영역들 PTE 만들기. 읽기/쓰기 허용
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 커널 .trampoline 영역들 PTE 만들기.(최고 VA) 읽기/실행 허용
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 각 프로세스용 커널 스택 2페이지 영역을 예약 및 매핑
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// 부팅시 hart0에서 단 한번 커널 테이블 initialize
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// 부팅시 각 하트마다 단 한번 커널 테이블 initialize.
void
kvminithart()
{
  sfence_vma();                 // 펜스

  w_satp(MAKE_SATP(kernel_pagetable));  // satp CSR에 kernel_pagetable 적용

  sfence_vma();                 // TLB 캐시 무효화
}

// PTE 찾기 or 생성(alloc에 따라)
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)               // 유저 범위(38bit) 초과 접근은 즉시 패닉
    panic("walk");

  for(int level = 2; level > 0; level--) {      // L2,L1까지 탐색
    pte_t *pte = &pagetable[PX(level, va)];     // va에서 해당 레벨 9bit 추출해서 페이지테이블에서 그 인덱스 pte 가져오기
    if(*pte & PTE_V) {                          // valid(존재)하면
      pagetable = (pagetable_t)PTE2PA(*pte);    // 하위 PT 물리주소 반환
    } else {                                    // 없으면
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) // alloc도 안켜졋으면
        return 0;                               // 실패
      memset(pagetable, 0, PGSIZE);             // 새 하위 PT set
      *pte = PA2PTE(pagetable) | PTE_V;         // 상위 PTE에 기록(V=1)
    }
  }
  return &pagetable[PX(0, va)];                 // L0 인덱스랑 똑같은 leaf PTE 물리주소 반환
}

// PPN 추출
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)       // 유저 범위(38bit) 초과시 거부
    return 0;

  pte = walk(pagetable, va, 0);       // leaf PTE 찾기
  if(pte == 0)                        // 존재안하면 실패
    return 0;
  if((*pte & PTE_V) == 0)             // valid 안하면 실패
    return 0;
  if((*pte & PTE_U) == 0)             // U 모드 아니면 실패
    return 0;
  pa = PTE2PA(*pte);                  // PPN 추출
  return pa;
}

// 커널 전용(kvmmake에서만 호출)
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  // mappages가 0을 리턴하면 PT 만들기에 성공한 것. 실패하면 패닉
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 테이블 만들기
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)          // 페이지당 첫 VA(page offset=0)만 PTE 만들어줌
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)        // 만들 테이블 크기도 4KB 단위로
    panic("mappages: size not aligned");

  if(size == 0)               // 길이 0은 의미가 없음
    panic("mappages: size");
  
  a = va;                       // 현재 VA
  last = va + size - PGSIZE;    // 마지막 VA
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)    // leaf PTE 생성
      return -1;                              // 실패시 -1
    if(*pte & PTE_V)                          // 이미 valid하면 오류
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;         // PTE 내용 (PPN과 권한비트) 세팅
    if(a == last)                             // last면 break
      break;
    a += PGSIZE;                              // 아직 안끝났으면 다음 페이지
    pa += PGSIZE;
  }
  return 0;
}

// PTE 무효화 및 해당 PA free
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)          // leaf PTE 찾기
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)                         // valid 없으면 오류
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)                    // leaf가 아니면 오류
      panic("uvmunmap: not a leaf");
    if(do_free){                                    // 해당 PA free
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;                                       // PTE 무효화
  }
}

// 빈 사용자 테이블 만들기
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();     // 4KB 페이지 한장 -> root PT
  if(pagetable == 0)            
    return 0;
  memset(pagetable, 0, PGSIZE);           // 512개 PTE 전부 0으로 초기화
  return pagetable;
}

// initcode 물리 공간 할당하고 테이블(앞쪽 유저 프로세스)도 만들기
// 커널은 파일 시스템을 초기화할때까지 S모드이지만 이 PT는 U모드 전용으로 쓸 수 있게
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)                        // initcode 크기(주소 범위) 검사
    panic("uvmfirst: more than a page");
  mem = kalloc();                         // 물리 공간 mem 4KB만큼 할당
  memset(mem, 0, PGSIZE);                 // 전부 0으로 초기화
  // 0x00000000(VA) 과 mem(물리) 매핑하는 테이블 만들기
  // WRX 허용하고 U 모드도 접근 가능하게
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);                // 가지고 있던 initcode 바이트를 해당 물리페이지로 복사
}

// 주소 확장
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)               // 이전보다 작아진 요청? -> 그대로 둠
    return oldsz;

  oldsz = PGROUNDUP(oldsz);       // 시작점을 페이지 경계로 올림
  for(a = oldsz; a < newsz; a += PGSIZE){   // 한 페이지씩 전진
    mem = kalloc();                         // 물리 공간 할당
    if(mem == 0){                           // 실패 -> 롤백
      uvmdealloc(pagetable, a, oldsz);      // 정리 후 0 반환
      return 0;
    }
    memset(mem, 0, PGSIZE);                 // 공간 0으로 초기화
    // 매핑 실패시
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);                           // 공간 반납
      uvmdealloc(pagetable, a, oldsz);      // 다시 oldsz로 테이블 롤백
      return 0;
    }
  }
  return newsz;                             // 매핑 성공시 새 sz
}

// 주소 축소
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  // PTE 무효화 및 해당 PA free
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// tree 자체를 재귀적 free
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// 사용자 공간 전체 free(PTE + PA + 트리)
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
    pa0 = PTE2PA(*pte);
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
```