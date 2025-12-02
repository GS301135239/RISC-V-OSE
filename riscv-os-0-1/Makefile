CROSS_COMPILE = riscv64-unknown-elf-
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld

CFLAGS = -Wall -O2 -ffreestanding -nostdlib -Ikernel -mcmodel=medany
LDFLAGS = -T kernel/kernel.ld

# 明确指定汇编文件在前，C文件在后
S_SRCS = kernel/entry.S  # 明确顺序
C_SRCS = $(filter-out kernel/entry.S, $(wildcard kernel/*.S)) $(wildcard kernel/*.c)
OBJS = $(S_SRCS:.S=.o) $(C_SRCS:.c=.o)


all: kernel/kernel.elf

kernel/entry.o: kernel/entry.S
	$(CC) $(CFLAGS) -c $< -o $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kernel.elf: $(OBJS) kernel/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
clean:
	rm -f kernel/*.o kernel/kernel.elf

ikun: all
	qemu-system-riscv64 -machine virt -nographic -bios none -kernel kernel/kernel.elf