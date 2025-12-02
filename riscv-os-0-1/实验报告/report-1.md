# 实验1：RISC-V引导与裸机启动

## 启动方式

```bash
>> make clean && make ikun
```

## 系统设计部分

### 系统架构部分


其中核心文件的作用如下：

- kernel/entry.S：实现栈顶指针的设置、BSS段的清空以及main函数的跳转
- kernel/kernel.ld：实现链接并指定可执行文件入口点为`_entry`，以及entry.S中声明使用的各变量
- kernel/uart.c：实现串口驱动以及对寄存器线程安全的读和写
- main.c：主函数类，实现调用并测试


### 与 xv6 对比分析

- 我编写的操作系统为单核多进程的操作系统，因此无需像xv6一般在entry.S中使用mul来计算当前是哪个核
- 我的操作系统在kernel.ld中才为栈指针指定栈空间大小，简化entry.S，使kernel.ld文件作为变量的存储区域特点更加鲜明且统一

## 实验过程部分

### 实验步骤

#### 1）实现 entry.S

```S

.section .text.entry
.global _entry

_entry:
    
    la sp, stack_top

    la t0, sbss
    la t1, ebss
bss_zero_loop:
    bgeu t0, t1, bss_done
    sw zero, 0(t0)
    addi t0, t0, 4
    j bss_zero_loop
bss_done:

    # 跳转到 C 代码的 main 函数。
    call main

# 如果 main 函数返回，则进入无限循环。
spin:
    j spin

```

此处利用汇编语言实现一个循环，每4个字节扫描一次并清空BSS段，避免将初始化过程硬编码进main.c中，同时提前清空BSS段的时机

#### 2）实现 main.c

此处由于函数量较少，因此先直接将其硬编码进main.c中作为调用前的声明，当操作系统逐步完善之后将函数的声明提取至`def.h`文件之中，防止main.c中函数声明过多影响测试

```c

void uart_init();
void uart_putc(char c);
void uart_puts(const char *s);
//console接口层，现在改为调用接口层，由接口层调用串口
void console_init();
void console_putc(char c);
void console_puts(const char *s);

void main() {

    uart_init();

    uart_puts("Hello, World!\n");

    while (1)
        ;
}

```

#### 3）实现 uart.c

```c

#define UART0 0x10000000L

#define THR 0 
#define IER 1 
#define FCR 2 
#define LCR 3 
#define LSR 5 

#define LCR_EIGHT_BITS (3 << 0)
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1) 
#define LSR_TX_IDLE (1 << 5)

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define WriteReg(reg, val) (*(Reg(reg)) = (val))
#define ReadReg(reg) (*(Reg(reg)))

void uart_init() {
    WriteReg(IER, 0x00);

    WriteReg(LCR, LCR_EIGHT_BITS);

    WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
}

void uart_putc(char c) {
    while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
        ;// 空转，等待硬件准备好
    WriteReg(THR, c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

```

- 此处是根据xv6源代码中的uart.c文件中的编写方法，将涉及到寄存器操作需要定义的宏、掩码位等提取出来作为我的操作系统的基础，我的uart.c基本就是根据xv6源码编写的
- 这里有一个很有意思的点在于，xv6巧妙地运用了一个Reg函数，将其返回值定义为`volatile unsigned char *`，这里我通过查询资料得知是由于编译过程中编译器可能会为了优化，将寄存器的值读取完之后就直接存放在实际CPU的寄存器中，而引入`volatile`类型，可以保证每次都需要在内存中读取，这样可以保证在硬件每次更新了状态位之后能够被真正地读取出来
- 而且在代码中有`while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)`这样的一段代码，这里也有一个很有意思的点，就是CPU发送和串口接收的速率不一样，通过这一段代码，可以保证当串口不可用时，CPU空转等待，实现了CPU和串口之间的同步

#### 4）实现kernel.ld

```ld

OUTPUT_ARCH("riscv")
ENTRY(_entry)

SECTIONS
{
    . = 0x80000000;
    
    .text : {
        *(.text._entry)
        *(.text .text.*)
    }

    .rodata : {
        /* Read Only Data */
        . = ALIGN(16);
        *(.rodata .rodata.*)
    }

    .data : {
        . = ALIGN(16);
        *(.data .data.*)
    }

    .bss : {
        /*16 Byte*/
        . = ALIGN(16);
        sbss = .;

        *(.sbss .sbss.*)

        . = ALIGN(16);
        *(.bss .bss.*)

        ebss = .;
    }

    . = ALIGN(4096);
    . = . + 4096;
    stack_top = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}

```

- 此处声明了编译链接的入口点为entry.S，同时仿照xv6组建编写.data段、.bss段、.rodata段，同时在此处为栈空间预留4KB

#### 5）实现Makefile

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

- Makefile文件指定了编译方法以及编译规则
- OBJS = $(S_SRCS:.S=.o) $(C_SRCS:.c=.o)，这条规则可以让每一次编译都是将kernel目录下的全部.S、.c文件进行编译，通过这条规则，当我们增加.c文件时，就无需反复编写增加Makefile需要编译的.c文件
- 同时这段Makefile也有一个需要注意的点，就是entry.S一定要先于其他文件进行编译，因为kernel.ld需要从entry.S文件中进入，如果不指定文件编译的返回顺序，则可能会出现无法通过entry.S进入调用的函数的情况

### 源码理解总结

- 我对源码的理解已经全部在实验步骤中体现，唯一想补充的就是通过源代码的理解，我更加能体会到xv6设计的精妙以及其中体现的软件工程的原则



## 测试验证部分

编写 Makefile 编译运行，测试结果如下：

（已经附在源代码中）
