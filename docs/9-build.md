## 프로젝트 빌드 과정을 알아보자

### 목표: 프로젝트의 이해
작성자: kkongnyang2 작성일: 2025-06-30

---
### 0> 기본

소스파일 가져오는건 
 git clone https://gitlab.com/qemu-project/qemu.git

(사람)             (Generator)                 (Builder)
소스 + 옵션 ──▶  [Autotools / CMake / Meson] ──▶  [Make or Ninja]
                              │                      │
                    ┌─────────┴──────────┐           │
                    │  Makefile / Ninja  │───────────┘
                    │  (빌드 규칙 파일)  │
                    └────────────────────┘


generator: 빌드 규칙 생성
builder: 빌드 산출물 리턴
install: prefix 경로에 안착

Cmake는 요즘 범용성 높아 많이 쓰임. make도 ninja도 사용 가능.

### 1> riscv-gnu-toolchain
riscv-gnu-toolchain의 조합
Generator  : Autotools(./configure로 makefile파일 자동생성)
Builder    : GNU Make

riscv-gnu-toolchain/
├── configure            ← 자동 생성된 셸 스크립트(Autoconf) ──▶ Generator
├── configure.ac         ← configure를 “찍어내는” 소스
├── Makefile.in          ← configure가 변수 치환해 최종 Makefile 생성
├── scripts/             ← build 순서·패치 적용용 셸 스크립트 모음
├── build              (← 빌드 시 자동 생성)
│   ├── .../Makefile     ← Builder(메이크)가 실제 해석
│   └── .../*.o / *.a / gcc / ld ...
│
├── gcc/                 (submodule)  ┐
├── binutils/            (submodule)  │— 각 컴포넌트도 **자체 Autotools** 구조
├── gdb/                 (submodule)  │
├── newlib/              (submodule)  ┘
└── linux/               (옵션 glibc 빌드용 커널 헤더)

# 1) Generator 단계
./configure --prefix=$HOME/opt/riscv --enable-multilib
     ↳ configure  →  최상위 + 각 서브디렉터리 Makefile 생성
# 2) Builder 단계
make -j$(nproc)          # 메인 Makefile이 scripts/build-*.sh 호출
     ↳ 각 script 내부   →  binutils, gcc, newlib … 를 차례로 make
# 3) Install 단계
make install             # 결과물을 $HOME/opt/riscv/{bin,lib,include,…}로 복사

### 2> qemu
qemu의 조합
Generator   : Meson
Builder     : Ninja
최상위 ./configure 스크립트는 Autotools가 아니라 **“Meson 설정 래퍼”**입니다.

qemu/
├── configure               ← 셸 래퍼: 옵션 파싱 후 meson 호출
├── meson.build             ← 루트 Meson 규칙
├── meson_options.txt       ← --enable-xxx 스위치 정의
├── block/meson.build       ← 서브디렉터리별 규칙 (C 소스·libqemu-block...)
├── hw/{arm,riscv,x86}/...  ← 각 가상 HW, 역시 meson.build 포함
├── scripts/meson-buildoptions.sh  ← ./configure가 Meson 옵션으로 매핑
└── build/            (meson setup 후 생성)
    ├── build.ninja          ← **Ninja 규칙 파일**
    ├── config-host.h        ← 호스트 기능 매크로 (#define)
    ├── Makefile             ← 단순 “all: ninja -C .” 래퍼
    └── meson-logs/…

# 1) Generator
./configure --target-list=riscv64-softmmu --prefix=$HOME/opt/qemu
    ↳ 원래 사용하는 meson setup build/ 는 자동으로 되어 build.ninja 생성
# 2) Builder
ninja -C build -j$(nproc)                        # .o → libqemu* → qemu-system-riscv64
# 3) Install
ninja -C build install               # 디폴트는 실행파일 /usr/local/bin 에 복사. 아까 prefix 설정했으면 $HOME/opt/qemu/{bin,lib,share,…}로 복사됨

### 3> xv6-riscv
xv6-riscv의 조합
Generator  : 없음.(generator가 아무것도 없기에 손수 makefile파일 작성)
Builder    : GNU Make

xv6-riscv/
├── Makefile          ← 최상위 빌드 규칙 ― 핵심
├── kernel/           ← 커널 소스 (.c, .S)
├── user/             ← 유저 프로그램 소스
│   └── user.ld       ← 사용자 실행파일용 링크스크립트
├── kernel/entry.S
├── mkfs.c            ← 호스트 툴: 파일시스템 이미지 생성
├── *.lds             ← 커널 링크스크립트
└── README, LICENSE …

# 1) Generator
손수 makefile파일 작성
# 2) Builder
make
# 3) Install
따로 경로는 안하고 이 소스파일안에 냅두고 qemu에 올려서 부팅(make qemu)

### 3> 명령어 의미
--prefix=$HOME/opt/qemu
설치 결과물을 시스템 루트(/usr)가 아닌 내 홈 폴더 아래에 복사해라. (안쓰면 디폴트는 /usr/local일거임) root 권한 없이, 다른 qemu 버전과 충돌 없이 관리 가능.

echo 'export PATH=$HOME/opt/qemu/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
셸은 기본적으로 /usr/bin, /usr/local/bin 같은 표준 디렉토리만 찾기 때문에, 홈 안에 설치했으면 이 디렉터리를 PATH에 넣어야 편하게 호출가능. 아니면 매번 경로 입력해 호출해야함.

이거 안해줄거면 그냥 디폴트로 하나만 쓰고 루트 디렉토리로 sudo make install 해주면 됨.

### 4> 크로스 툴체인 위치
호스트 툴체인 실행파일은 usr/bin에 있음. 크로스 툴체인은 usr/riscv64-linux-gnu/bin에 저장되기에 구분

### 5> 삭제 방법

riscv-gnu-toolchain
설치 실행파일 제거 rm -rf usr/riscv-gnu-toolchain
path 되돌리기 nano ~/.bashrc에서 export PATH 라인 삭제
빌드 산출물 삭제 make clean
소스 트리 초기화 make distclean
소스 폴더 삭제 rm -rf ~/riscv-gnu-toolchain

qemu
설치 실행파일 제거 rm -rf usr/local/bin/qemu
path 되돌리기 nano ~/.bashrc에서 export PATH 라인 삭제
빌드 산출물 삭제 ninja -C build clean
소스 트리 초기화 rm -rf build meson-logs meson-private
소스 폴더 삭제 rm -rf ~/qemu

xv6-riscv
설치 실행파일 제거  # 소스 폴더에 그대로 있음(공유 라이브러리가 필요하지 않기때문)
path 되돌리기 nano ~/.bashrc에서 export PATH 라인 삭제
빌드 산출물 삭제 make clean
소스 트리 초기화 make distclean
소스 폴더 삭제 rm -rf ~/xv6-riscv

```
~$ ps aux | grep qemu           #실행중인 프로세스 번호
~$ kill 숫자                     #강제종료
```