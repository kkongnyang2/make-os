## mini-os를 완성하자

### 목표: os의 이해
작성자: kkongnyang2 작성일: 2025-07-01

### 0> 프로젝트

모든 프로젝트는 소스파일 -> 규칙 -> 컴파일 및 빌드 -> 설치 -> 런 으로 이루어진다.

그리고 이 프로젝트의 소재는 mini-os 이다. qemu로 가상 하드웨어를 생성하고 (그 내부에서 벌어지는 실제 일은 나중에 공부하고 설치만) 그 하드웨어를 운용하기 위한 커널 이미지와, 디스크 이미지와, 사용자 공간을 만드는 것을 목적으로 한다.

이를 위한 과정은 다음과 같다. MMU와 TRAP, PCB, MMIO 함수들과, mkfs 함수 등의 소스 파일을 만든다.그리고 GNU make로 빌드할 수 있도록 makefile로 규칙을 작성한다. 크로스 툴체인(riscv64-unknown-elf)으로 컴파일하고 linker.ld 링커 파일로 빌드하여 커널 이미지를 만들도록 한다. 그리고 호스트 툴체인으로 디스크 이미지를 만든다. 해당 이미지들을 빌드 산출물로 내놓고 홈에 설치해 qemu로 런하도록 한다.


### 1> 프로세스 흐름
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

### 2> 기존 xv6-riscv 공부

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
xv6 kernel is booting
hart 1 starting
hart 2 starting
init: starting sh
~$ ctrl+A+X
QEMU: Terminated
```


### 3> 소스 파일 디렉토리 구조

```
xv6-riscv/            ← 프로젝트 루트
├─ Makefile           ← 전체 빌드 스크립트(커널·유틸·디스크 이미지까지)
├─ README, LICENSE
├─ .gdbinit.tmpl-riscv│ .gitignore │ .editorconfig …
│
├─ kernel/            ← **커널 소스 & 헤더**
│  ├─ entry.S         ← M-모드(부트) 진입점, 스택 마련 :contentReference[oaicite:0]{index=0}
│  ├─ start.c         ← mret로 S-모드 전환 후 main() 호출 :contentReference[oaicite:1]{index=1}
│  ├─ main.c          ← 부팅 초기화(콘솔·메모리·CPU·initproc 등)
│  ├─ trap.c          ← 인터럽트/시스템콜 핸들러
│  ├─ syscall.c │ sysfile.c │ sysproc.c
│  ├─ exec.c          ← ELF 적재(loadseg 등)
│  ├─ proc.c │ swtch.S│ scheduler (프로세스 관리)
│  ├─ vm.c │ kalloc.c │ plic.c │ timer.c (메모리·장치)
│  ├─ fs.c │ log.c │ bio.c │ buf.c │ file.c │ pipe.c (파일시스템)
│  ├─ console.c │ uart.c  (드라이버)
│  ├─ spinlock.c │ printf.c │ string.c (커널 lib)
│  └─ *.h             ← types.h, riscv.h, defs.h, param.h, …
│
├─ user/              ← **사용자 영역 코드 & 유틸리티** :contentReference[oaicite:2]{index=2}
│  ├─ Makefile        ← 각 소스를 실행파일로 링크, fs 이미지에 복사
│  ├─ initcode.S      ← 첫 PID 1(boot strap) 800B 미니 ELF
│  ├─ init.c          ← “init: starting sh” + 콘솔 FD 설정
│  ├─ sh.c            ← 파이프·리다이렉션 지원 소형 셸
│  ├─ ulib.c │ umalloc.c │ printf.c  (미니 libc 루틴)
│  ├─ usys.S │ usys.pl   ← 시스템콜 stub 자동 생성
│  ├─ cat.c, ls.c, grep.c, find.c, wc.c, …  ← Unix 명령 예제들
│  ├─ forktest.c, stressfs.c, pingpong.c, zombie.c, usertests.c …
│  └─ (빌드 후) _ls, _sh …  ← ‘_’ 접두어 실행파일이 생성돼 루트(/)로 복사
│
├─ mkfs/              ← **디스크 이미지 생성 유틸** :contentReference[oaicite:3]{index=3}
│  ├─ Makefile
│  └─ mkfs.c          ← fs.img를 만들며 /kernel, /init 등 파일 삽입
│
└─ (빌드 산출물) kernel, fs.img, *.o, *.d, *.sym …
```


### 4> arm64 특성

1. 바이트 순서: little endian사용. 0x12345678은 메모리에 78 56 34 12 순서로 저장.
2. 메모리 정렬: 4바이트 단위의 word alignment가 기본. 0x40080001같은건 정렬되지 않은 주소.
3. 레지스터: x0 ~ x30은 범용 레지스터, sp는 스택 포인터, pc는 프로그램 카운터, VBAR_EL1은 인터럽트 벡터 베이스, TTBR_EL1은 페이지 테이블 베이스, SCTLR_EL1은 시스템 제어 레지스터(MMU,캐시 켜기)
4. 함수 호출 규약: 리턴값은 x0, 파라메터 x0~x7. 스택은 16바이트 정렬(align 16)


### 5> 디버깅

다른 터미널에서
gdb-multiarch ~/make-os/kernel.elf
(gdb) target remote localhost:1234  #연결
(gdb) continue                      #코드 실행
(gdb) quit                          #그만두면 멈춘 주소 나타남
(gdb) info registers                #레지스터 정보
(gdb) disassemble                   #어셈블리 코드 보기
(gdb) x/i $pc                       #현재 위치(pc)


### 6> 전용 명령어 추가

step 1. user 디렉토리에 hi.c 생성
```
// user/hi.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("hi,kkongnyang2\n");
  exit(0);
}
```

step 2. makefile에 $U/_hi 추가
```
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
	$U/_hi		# kkongnyang2
```
이거 하나 추가.
hi.c가 _hi 실행파일로 바껴서 hi라는 명령어 사용가능

step 3. 기존 빌드 산출물 make clean하고 다시 make && make qemu
```
xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ hi
hi,kkongnyang2
```


### 7> 흐름 메시지 추가

// 새 프로세스 생성 - PCB 스택이 막 잡힌 시점
kernel/proc.c/fork() allocproc 후
printf("[kkong] fork: child pid=%d\n", np->pid);

// 자식이 사용자 공간으로 첫 진입하기 직전
kernel/pro.c/forkret() 진입
printf("[kkong] forkret to user\n");

// ELF 적재 & exec("hi") 시스템콜 도착
kernel/exec.c/exec() 진입
printf("[kkong] exec: path=%s\n", path);

// 각 프로그램 세그먼트를 디스크에서 읽어 메모리에 복사
kernel/exec.c/loadseg() 진입
printf("[kkong] loadseg: va=0x%lx sz=%u\n", va, sz);

// 사용자 프로그램 시작
user/hi.c/main() 중심
printf("hi,kkongnyang2\n");

// ecall 트랩 진입
kernel/trap.c/usertrap() if문 처리 직전
printf("[kkong] usertrap: scause=0x%lx\n", r_scause());

// 번호 해석 후 분기 전
kernel/syscall.c/syscall() if문 처리 직전
printf("[kkong] syscall no=%d pid=%d\n", num, p->pid);

// 사용자 버퍼/길이를 커널에 복사한 직후
kernel/sysfile.c/sys_write() return 직전
printf("[kkong] sys_write count=%d\n", n);

// FD->디바이스/파일 타입 판별
kernel/file.c/filewrite() if문 직전
printf("[kkong] filewrite type=%d\n", f->type);

// 터미널로 넘기기 직전
kernel/console.c/consolewrite() 진입
printf("[kkong] consolewrite n=%d\n", n);

// 사용자로 돌아가기 직전
kernel/trap.c/usertrapret() 진입
printf("[kkong] usertrapret to user\n");



xv6 kernel is booting

hart 2 starting
hart 1 starting
[kkong] forkret to user
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=7 pid=1
[kkong] exec: path=/init
[kkong] loadseg: va=0x0 sz=2401
[kkong] loadseg: va=0x1000 sz=16
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=15 pid=1
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=17 pid=1
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=15 pid=1
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=10 pid=1
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=10 pid=1
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
i[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
n[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
i[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
t[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
:[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
 [kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
s[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
t[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
a[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
r[kkong] usertrapret to user
[kkong] usertrap: scause=0x8000000000000005
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
t[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
i[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
n[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
g[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
 [kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
s[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
h[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=1
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1

[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=1 pid=1
[kkong] fork: child pid=2
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] forkret to user
[kkong] usertrapret to user
[kkong] syscall no=3 pid=1
[kkong] usertrap: scause=0x8000000000000009
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=7 pid=2
[kkong] exec: path=sh
[kkong] loadseg: va=0x0 sz=4841
[kkong] loadseg: va=0x2000 sz=16
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=15 pid=2
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=21 pid=2
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=2
[kkong] sys_write count=2
[kkong] filewrite type=3
[kkong] consolewrite n=2
$ [kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=5 pid=2
hi
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=5 pid=2
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=5 pid=2
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=1 pid=2
[kkong] fork: child pid=3
[kkong] usertrapret to user
[kkong] forkret to user
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=3 pid=2
[kkong] usertrap: scause=0x8
[kkong] syscall no=12 pid=3
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=7 pid=3
[kkong] exec: path=hi
[kkong] loadseg: va=0x0 sz=2137
[kkong] loadseg: va=0x1000 sz=0
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
h[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
i[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
,[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
k[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
k[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
o[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
n[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
g[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
n[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
y[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
a[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
n[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
g[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1
2[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=3
[kkong] sys_write count=1
[kkong] filewrite type=3
[kkong] consolewrite n=1

[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=2 pid=3
[kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=16 pid=2
[kkong] sys_write count=2
[kkong] filewrite type=3
[kkong] consolewrite n=2
$ [kkong] usertrapret to user
[kkong] usertrap: scause=0x8
[kkong] syscall no=5 pid=2
QEMU: Terminated
