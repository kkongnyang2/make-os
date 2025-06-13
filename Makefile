#======================================
# Makefile: 빌드, 실행(run), 정리(clean) 타겟 포함
#======================================
CC      = aarch64-linux-gnu-gcc
OBJCOPY = aarch64-linux-gnu-objcopy
CFLAGS  = -Wall -Werror -nostdlib -ffreestanding -O2 -Iinclude -Iboards
LDFLAGS = -T link.ld
OBJS    = start.o main.o uart.o timer.o gic.o

#-----------------------------
# 기본 타겟
#-----------------------------
all: kernel.img

#-----------------------------
# 개별 컴파일
#-----------------------------
start.o: start.S
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

uart.o: src/uart.c
	$(CC) $(CFLAGS) -c $< -o $@

timer.o: src/timer.c
	$(CC) $(CFLAGS) -c $< -o $@

gic.o: src/gic.c
	$(CC) $(CFLAGS) -c $< -o $@

#-----------------------------
# 링크 → ELF
#-----------------------------
kernel.elf: $(OBJS) link.ld
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

#-----------------------------
# ELF → 순수 바이너리 이미지
#-----------------------------
kernel.img: kernel.elf
	$(OBJCOPY) -O binary $< $@

#-----------------------------
# QEMU 실행 (UART0를 STDIO로 연결)
#-----------------------------
run: kernel.img
	qemu-system-aarch64 \
		-M virt -cpu cortex-a53 -nographic \
		-kernel kernel.img 2>&1 | tee qemu.log

#-----------------------------
# 정리
#-----------------------------
clean:
	rm -f *.o kernel.elf kernel.img qemu.log