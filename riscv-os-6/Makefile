# 工具链配置
TOOLCHAIN = riscv64-unknown-elf-
CC = $(TOOLCHAIN)gcc
LD = $(TOOLCHAIN)ld
OBJCOPY = $(TOOLCHAIN)objcopy

# 编译选项
CFLAGS = -Wall -O2 -fno-omit-frame-pointer -ggdb
CFLAGS += -march=rv64g -mabi=lp64d
CFLAGS += -mcmodel=medany -ffreestanding -nostdlib
CFLAGS += -Ikernel/

USER_INIT_ASM = user/initcode.S
USER_INIT_ELF = user/initcode.elf
USER_INIT_BIN = user/initcode
USER_INIT_OBJ = kernel/initcode.o


# 明确分开汇编文件和C文件
ASM_SRCS = \
    kernel/entry.S \
    kernel/kernelvec.S \
    kernel/swtch.S \
    kernel/trampoline.S

C_SRCS = \
    kernel/start.c \
    kernel/uart.c \
    kernel/console.c \
    kernel/print.c \
    kernel/kalloc.c \
    kernel/vm.c \
    kernel/trap.c \
    kernel/proc.c \
    kernel/syscall.c \
    kernel/sysproc.c \
	kernel/string.c \
    kernel/spinlock.c \
    kernel/main.c

OBJS = $(ASM_SRCS:.S=.o) $(C_SRCS:.c=.o)

.PHONY: all clean run initcode

all: kernel.bin

# 关键：汇编文件使用 GCC 编译（会自动预处理）
%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_INIT_ELF): $(USER_INIT_ASM)
	$(CC) -nostdlib -nostartfiles -march=rv64g -mabi=lp64d \
		-Wl,--build-id=none -Ttext=0 -Ikernel/ -o $@ $<

$(USER_INIT_BIN): $(USER_INIT_ELF)
	$(OBJCOPY) -O binary $< $@

$(USER_INIT_OBJ): $(USER_INIT_BIN)
	$(LD) -r -b binary -o $@ $<

initcode: $(USER_INIT_OBJ)

kernel.elf: $(OBJS) $(USER_INIT_OBJ) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS) $(USER_INIT_OBJ)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f kernel.elf kernel.bin $(OBJS) \
		$(USER_INIT_ELF) $(USER_INIT_BIN) $(USER_INIT_OBJ)

run: kernel.bin
	qemu-system-riscv64 -machine virt -bios none -kernel kernel.bin -nographic