# 디렉토리
K := kernel
M := mkfs
U := user

# 오브젝트 파일들
# .c->.o는 명령어로 적어주지 않아도 내장 규칙으로 수행
OBJS := \
	$(K)/entry.o \
	$(K)/start.o \
	$(K)/main.o \
	$(K)/vm.o \
	$(K)/trap.o \
	$(K)/proc.o \
	$(K)/uart.o \
	$(K)/exec.o \
	$(K)/file.o \
	$(K)/bio.o \
	$(K)/log.o \
	$(K)/fs.o \
	$(K)/virtio_disk.o

USERS := $(U)/hi.o

# 툴체인
TOOLPREFIX := riscv64-unknown-elf-
CC := $(TOOLPREFIX)gcc
AS := $(TOOLPREFIX)as
LD := $(TOOLPREFIX)ld
OBJCOPY := $(TOOLPREFIX)objcopy
OBJDUMP := $(TOOLPREFIX)objdump

CFLAGS  := -ffreestanding -nostdlib -march=rv64imac -mabi=lp64
ASFLAGS := -march=rv64imac -mabi=lp64
LDFLAGS := -nostdlib

# 커널 elf
$(K)/kernel.elf: $(OBJS) $(K)/linker.ld
	$(LD) $(LDFLAGS) -T $(K)/linker.ld -o $(K)/kernel.img $(OBJS)
	$(OBJDUMP) -S $(K)/kernel.img > $(K)/kernel.asm

# 파일 이미지
$(M)/fs.img: $(U)/hi.elf $(M)/mkfs.elf
	$(M)/mkfs.elf $(M)/fs.img $(U)/hi.elf

$(M)/mkfs.elf: $(M)/mkfs.c
	gcc -O2 -Wall -Werror -o $(M)/mkfs.elf $(M)/mkfs.c

# 사용자 프로세스
$(U)/hi.elf: $(USERS)
	$(LD) -Ttext=0 -nostdlib -o $(U)/hi.elf $(USERS)


# qemu 실행
QEMU := qemu-system-riscv64
QEMUOPTS = -machine virt -bios none -kernel $(K)/kernel -nographic \
			-drive file=fs.img,if=none,format=raw,id=x0 \
			-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: $(K)/kernel.elf $(M)/fs.img
	$(QEMU) $(QEMUOPTS)

clean:
	rm -f */*.o */*.asm */*.elf */*.img