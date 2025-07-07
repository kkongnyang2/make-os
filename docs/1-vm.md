## qemu의 구조를 알아보자

### 목표: 가상머신 이해
작성자: kkongnyang2 작성일: 2025-06-20

---
### 0> 개념 정리

가상머신? 물리적 컴퓨터 위에서 돌아가는 소프트웨어 컴퓨터

하이퍼바이저 -> 그 가짜 컴퓨터를 관리하는 관리자

### 1> 분류

하이퍼바이저 위치

type 1. bare-metal
하드웨어 바로 위에 하이퍼바이저가 동작. os 설치 없이 바로 vm 실행 가능

```
 [물리 컴퓨터]
 ┌────────────────────────────┐
 │        Hypervisor          │  ← 하이퍼바이저 (Xen, VMware ESXi, Hyper-V)
 │  ┌────────────┐ ┌────────┐ │
 │  │ VM #1      │ │ VM #2 │ │
 │  │ Ubuntu     │ │ Win10 │ │  ← 가상머신 (게스트 OS + 커널)
 │  └────────────┘ └────────┘ │
 └────────────────────────────┘
```

type 2. hosted
일반 os 위에서 프로그램처럼 실행되는 하이퍼바이저

```
 [물리 컴퓨터]
 ┌────────────────────────────┐
 │       Host OS              │  ← 일반 데스크탑 OS
 │  ┌──────────────────────┐  │
 │  │ Hypervisor           │  │  ← 소프트웨어 기반 (QEMU, VirtualBox)
 │  │  ┌────────────┐      │  │
 │  │  │ VM #1      │      │  │
 │  │  │ Fedora     │      │  │  ← 게스트 OS
 │  │  └────────────┘      │  │
 │  └──────────────────────┘  │
 └────────────────────────────┘
```

별첨. 컨테이너
모두 호스트 커널 공유

```
 [물리 컴퓨터]
 ┌────────────────────────────┐
 │     Host OS (리눅스)        │  ← 커널 공유
 │  ┌──────────────────────┐  │
 │  │ 컨테이너 런타임       │  │  ← Docker
 │  └───────┬──────────────┘  │
 │     ┌────▼────┐  ┌────▼────┐
 │     │ Container│ │ Container│  ← 가벼운 격리 환경
 │     │  Nginx   │ │  Python  │  ← rootfs + 라이브러리 + 앱
 │     └──────────┘ └──────────┘
 └────────────────────────────┘
```

cpu 가상화 기법

trap-and-emulate
특권 명령 -> 트랩 발생 -> 하이퍼바이저가 소프트웨어로 에뮬레이션. 쉽게 말해 울면 어른이 대신 처리.

binary translation
실행 코드를 동적 재작성. 쉽게 말해 안울도록 미리 사전 작업.

HW-assist
intel VT-x, AMD-V 등 하드웨어가 명령-페이지테이블 가상화 지원. 쉽게 말해 아이 전용방이 따로 있음.

장치 가상화

풀 에뮬레이션
qemu가 소프트웨어로 가상 하드웨어 구현. 쉽게 말해 가짜 수취함

파라가상화
게스트가 virtio 통신. 전용 고속 택배 전용

패스-스루
실제 장치 기능 직접 넘김. 집 열쇠를 통째로 줌.


### 2> 구성요소

vcpu
실제 cpu 코어와 논리 코어 매핑. 컨텍스트 스위치, 타미어 인터럽트, TLB flush 등.
```
┌─ Host CPU Core ──────────────────────────┐
│  …실행 중…                               │
│  (호스트 코드)                           │
│  ↙ 「World-switch」 ↘                   │
│  게스트 레지스터 세트 로드              │
│  게스트 코드 실행 (user → kernel → …)   │
│  VM-Exit (트랩 발생)                    │
│  …다시 호스트 코드…                     │
└──────────────────────────────────────────┘
```
world-switch= 레지스터,TLB를 싹 바꿔 끼우는 순간
스케줄러가 이번 타임슬라이스는 vCPU #2라고 결정하여 실 cpu에 태우는 형식


가상 메모리
nested page table
```
게스트 가상 addr 0x7fff_1234
   │ (게스트 페이지 테이블)          ← xv6가 아는 세계
   ▼
게스트-물리 addr 0x8010_1234
   │ (Nested page table / EPT)      ← 하이퍼바이저가 속임
   ▼
호스트-물리 addr 0x3c20_51234
   │ (MMU 실제 DRAM)                ← 진짜 하드웨어
```

i/o 가상화
mmio 트랩, DMA 리맵, virtio ring buffer
1. 게스트 커널이 store 0x10001014 <- 0x1 (virtio 디스크 큐 등록)
2. MMIO 주소이므로 VM-Exit -> QEMU에게 디스크 쓰자 요청
3. QEMU가 호스트 파일(fs.img)에 write -> 완료 후 가짜 인터럽트 재주입
4. 게스트에서 ISR 실행 -> 시스템콜로 사용자에게 쓰기 완료 알림

이때 virtio는 2,3단계에서 링 버퍼/IOVRING을 써서 트랩 횟수를 확 줄이는 최적화

스토리지 네트워킹
QCOW2같은 이미지 형식


### 3> QEMU-KVM 흐름

qemu? type2 풀 에뮬레이션 하이퍼바이저
kvm? HW-assist 방식 전용 커널 모듈(type 1)
큰 틀은 qemu를 쓰고 cpu 가상화를 추가로 진행하여 qemu+kvm 방식으로 많이 씀.

A. 부팅 전 `make qemu`
qemu가 risc-v virt 머신을 생성 -> /dev/kvm에 vCPU 3개 요청
B. vCPU 진입 `hart 2 starting`
kvm 모듈이 KVM_RUN 진입 -> mepc=0x80000000 로드. 게스트 첫 명령 실행
C. 시계 인터럽트 `tick`
RISC-V mtimecmp 만료 -> 하드웨어가 'supervisor timer interrupt' -> VM-Exit -> KVM가 인터럽트 재주입
D. 시스템콜 `read`
사용자 프로그램이 ecall -> trap -> xv6-kernel sys_open -> I/O 요청 생성
E. 디스크 I/O
virtio-blk MMIO 접근 -> VM-Exit -> QEMU가 fs.img에 블록 read -> 종료 IRQ 재주입
F. 콘솔 출력 `$ls`
게스트가 UART MMIO(0x10000000) write -> QEMU가 호스트 stderr로 바로 프린트
G. 호스트로 복귀
단축키 Ctrl-a x 누르면 QEMU 종료 -> vCPU context 저장, 파일 flush 후 make 종료


cpu 권한 레벨

x86에서는 Ring0(커널) Ring3(유저)
ARM에서는 EL0(유저) EL1(커널) EL2(하이퍼바이저) EL3(보안)
RISC-V에서는 U(유저) S(커널) M(최상위)

게스트 OS -> 직접 CPU에서 실행 (EL1)
특권 명령어 -> trap -> EL2 -> KVM -> 결과처리 -> 복귀