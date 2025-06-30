## xv6-riscv의 mkfs 폴더을 읽어보자

### 목표: 디스크 이미지 이해
작성자: kkongnyang2 작성일: 2025-06-26

---
### 0> 개념 정리

mkfs란 make filesystem의 약자이다.
파일 시스템은 세 가지 요소를 지닌다. 추상화, 무결성, 보호. 추상화란 디스크처럼 블록 단위(4KB)로만 읽고 쓰는 장치를 논리적으로 /home/kkong/note.txt 같은 공간으로 바꿔준다. 무결성이란 다양한 에러에 대해 기법을 사용해 보호한다. bitmap+블록 그룹, 저널링, copy-on-write, log-structured 등의 기법이 대중적이다. 보호란 퍼미션, 네임스페이스를 통해 여러 프로세스가 안전하게 사용한다.

만든 디스크 이미지 구조는 다음과 같이 생겼다. (파일시스템이 디스크 전체를 어떻게 구획했느냐).

디스크(파티션)
┌────────┬──────────┬───────────┬─────────┬─────────┐
│ Boot   │ Super-   │ Inode     │ Block   │ Data    │
│ block  │ block    │  table    │ bitmap  │ blocks  │
└────────┴──────────┴───────────┴─────────┴─────────┘


---
### 1> 용어 정리

struct inode                // 퍼미션, uid/gid, size, time, 블록 포인터 등의 필드가 있다. 참고로 루트는 고정 inode 번호 2를 가지고 있다.

---
### 2> 흐름 정리

빌드 시점에 고정되는 mkfs 디스크 구조 만들기와 런타임에 파일을 읽고 쓰기 위한 로직인 fs.c는 다르다.

fs.c는 다음과 같은 순서로 호출된다

```
user read()      ─▶ ① glibc  (버퍼·errno 관리)
                ─▶ ② sys_read()           ← VFS 진입 (fd → struct file*)
                ─▶ ③ 파일시스템 드라이버   (ext4_read_iter, xv6 readi 등)
                ─▶ ④ 페이지 캐시          (struct page / buffer_head 연결)
          ┌────▶ ⑤ 캐시에 ‘HIT’ → 바로 복사 후 리턴
          │
캐시 miss ┘
                ─▶ ⑥ 블록 I/O 계층 (bio ▸ blk-mq ▸ scheduler)
                ─▶ ⑦ 디바이스 드라이버 (NVMe, SATA, virtio…)
                ─▶ ⑧ 컨트롤러 DMA   (PCIe/NVMe queue)
                ─▶ ⑨ 저장 장치      (SSD/NAND/NVCache)
```


