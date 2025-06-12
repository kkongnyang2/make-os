PLATFORM ?= qemu   # or rpi4
CROSS=aarch64-linux-gnu-
CC=$(CROSS)gcc
AS=$(CROSS)as
LD=$(CROSS)ld
OBJCOPY=$(CROSS)objcopy
CFLAGS=-nostdlib -ffreestanding -O2 -Wall -Iinclude -DPLATFORM_$(shell echo $(PLATFORM) | tr a-z A-Z)

OBJS = start.o \
       drivers/uart.o drivers/gic.o drivers/timer.o \
       kernel/main.o

all: build/kernel.img

build/kernel.img: build/kernel.elf
	$(OBJCOPY) -O binary $< $@

build/kernel.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o drivers/*.o kernel/*.o build/*

run: build/kernel.img
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a53 \
		-nographic \
		-kernel build/kernel.img

gdb: build/kernel.img
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a53 \
		-nographic \
		-kernel build/kernel.img \
		-s -S

log: build/kernel.img
	qemu-system-aarch64 \
		-M virt \
		-cpu cortex-a53 \
		-nographic \
		-kernel build/kernel.img \
		-d int,guest_errors \
    	-D build/qemu.log \
	2>&1 | tee build/terminal.log