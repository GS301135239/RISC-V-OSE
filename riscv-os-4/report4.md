# 实验4：中断处理与时钟管理

## 系统设计部分

### 系统架构部分


其中核心文件的作用如下：

- start.c：系统启动文件，通过entry.S的汇编进入start.c中完成各组件的初始化，需要与系统特权级别结合
- trap.c：分析中断的原因以及对内核中断进行处理，这里是一个最简单的全面覆盖处理
- riscv.h/riscv.c：仿照xv6，提供较为全面的一些内联CPU硬件寄存器的读写，实现内存保护、特权级切换、中断处理等
- kernelvec.S：中断向量表，实现现场的保护和恢复


### 与 xv6 对比分析

- 这个实验是我目前做到的最难的，不知道以后有没有更难的（），因为实验报告和xv6源代码没什么办法联系起来，而最后我设计出来的操作系统，主要与xv6思路较为相似，就是简单的把所有的异常都抛出，直接中断并保护现场。

## 实验过程部分

### 实验步骤

#### 1）实现kernelvec.S

- 这个文件没什么特别的，就是仿照xv6保护现场的方式，除了zero以及栈指针不保护外，其他的寄存器中的值均保存在栈帧中。

#### 2）实现 riscv.c

- 这个文件也并没有什么特别之处，就是类似于xv6中的代码，为内核每一个与中断处理、特权级分发相关的寄存器提供对应的write和read函数，主要就是用asm volatile的实现

#### 3）实现 trap.c

```c

#include "def.h"
#include "type.h"
#include "riscv.h"

//定义在 kernelvec.S 中的内核 trap 入口函数
extern void kernelvec();

//启用 trap 处理程序
void tarp_init_hart() {
    // 设置内核的 trap 入口地址
    write_stvec((uint64) kernelvec );
}

void trap_init() {
    tarp_init_hart();
}

int ticks = 0;

//处理时钟中断
void handle_clock_intr() {
    ticks++;

    // 记录下一个时钟中断时间
    write_stimecmp(read_time() + 1000000);
}

extern void spin();

//检查是否是外部中断或者是软件中断，并且调用相应的处理函数
//2：时钟中断；1：外部中断；-1：无效
int handle_device_intr( uint64 scause ) {
    // int msb = ( scause >> 63 ) & 1;

    switch ( scause ) {
        case 0x8000000000000005L:
            handle_clock_intr();
            return 2;

        case 0x2L:
            printf( "handle_device_intr: scause = 0x%x\n", scause );
            write_sepc((uint64) spin );
            return 1;

        default:
            return -1;
    }
}

/// 内核态的 trap 处理函数
void kernel_trap() {
    //printf( "kernel_trap called\n" );

    uint64 scause = read_scause();
    uint64 sepc = read_sepc();
    uint64 sstatus = read_sstatus();

    if ( ( sstatus & SSTATUS_SPP ) == 0 ) {
        // 并非由 supervisor 模式进入 kernel trap
        panic( "kernel_trap: not from supervisor mode" );
    }

    if ( is_interupt_on() ) {
        // handle trap 时不应该开启中断
        panic( "kernel_trap: interrupt enabled" );
    }

    if ( handle_device_intr(scause) == -1 ) {
        printf("scause %x\n", scause);
        printf("sepc %x\n", sepc);
        panic("kernel_trap: unexpected scause");
    }

    // write_spec( sepc );
    // write_sstatus( sstatus );
}

```

- trap.c主要是类似于xv6中的实现方式，编写发生中断时的处理方法，在xv6中貌似是不处理，所有的中断都视为出现严重的错误，直接杀进程，这里为了实现时钟中断的一些测试，因此对时钟中断进行了一些处理，由于没有实现进程管理方面的内容，因此其他的错误直接进入空转，避免主函数直接返回

#### 4）实现start.c

```c

#include "def.h"
#include "type.h"
#include "riscv.h"

int main(); // 声明 main 函数

void call_main() {
    // 初始化内核页表
    kvminit();

    kvminithart();

    printf( "Kernel page table initialized.\n" );

    // 初始化 trap 处理程序
    trap_init();

    printf( "Trap handler initialized.\n" );

    // 最后调用 main
    if (main() != 0) {
        panic("main() returned with error\n");
    }

    // 正常情况下不应该返回，因此自旋
    while (1);
}

void perm_init() {
    // 设置 mstatus 寄存器的 MPP 位为 S 模式
    // 用于在发生中断、异常时切换到 S 模式进行处理
    uint64 mstatus = read_mstatus();

    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_S;

    write_mstatus(mstatus);

    // 设置 mret 地址为 call_main 函数
    write_mepc((uint64) call_main);
}

void intr_init() {
    // 将所有中断和异常都委托给 S 模式处理
    write_medeleg(0xffff);
    write_mideleg(0xffff);

    // 使能 S 模式下的外部中断和定时器中断
    write_sie(read_sie() | SIE_SEIE | SIE_STIE);

    // 开启 S 模式的总中断开关
    write_sstatus(read_sstatus() | SSTATUS_SIE);
}

void enable_physical_protection() {
    write_pmpaddr0(0x3fffffffffffffull);
    write_pmpcfg0(0xf);
}

void timer_init() {
    // 使能机器模式下的定时器中断
    write_mie(read_mie() | MIE_STIE);

    // 使能机器模式下的计数器访问
    write_menvcfg(read_menvcfg() | ( 1L << 63 ));

    // 使能计数器寄存器的访问
    write_mcounteren(read_mcounteren() | 1L << 1);

    // 设置下一个时钟中断时间（大约 0.1 秒后？）
    write_stimecmp(read_time() + 1000000);
}


//内核入口函数，完成各种组件初始化
void start() {
    // 清零 .bss 段
    //cleanup_bss();
    //由于链接脚本已经将 .bss 段清零，这里不需要再清零一次

    // 初始化 UART
    uart_init();

    printf( "UART initialized.\n" );

    // 初始化内存分配器
    // 注意：由于pmm_init()函数中的memset等是在M模式下运行的，因此初始化一定
    // 要在启用物理内存保护进入S模式之前完成！
    pmm_init();

    printf( "Memory allocator initialized.\n" );

    // 初始化委托中断和异常
    intr_init();

    // 禁止页表转换
    write_satp(0);

    // 应用物理内存保护
    enable_physical_protection();

    // 初始化定时器
    timer_init();

    perm_init();

    // 这里应该会以 S 模式（MPP设定）进入 call_main 函数
    asm volatile("mret");
}

```

- 这段代码是在这个实验中花费时间最长的部分，因为xv6的start.c是多核系统的初始化，因此需要简化成单核操作系统的，而且xv6貌似并没有在start.c中实现初始化各组件，如uart、physical memory等，需要在此基础上再进行代码的优化，使得在启动过程中就能够实现初始化。
- 同时这个部分最难的部分在于特权级的处理，因为一部分的初始化在M模式下就要完成，一部分的初始化需要进入S模式中才能完成初始化（特别是trap_init，整个部分就是在S模式下），因此在开启物理内存保护之前就要完成pmm_init，因为pmm_init中使用了memset等函数，因此在S模式下，pmm_init中访问到的end~PHYSTOP部分有一部分就有可能因为物理内存保护的启动而没法访问到，会导致初始化失败！

### 源码理解总结

- 这一次实验可以说是最难调试的一次实验了，实验本体代码编写还是比较简单的，主要还是仿照xv6的思路来进行，而调试由于新增了start.c文件（我此前的设计是直接进main函数，通过main函数调各个Test，在这些Test文件中初始化来启动操作系统）以及特权级这个比较容易混淆的部分，导致在调试的时候难度骤增，经常不知道崩溃是在哪个环节……



## 测试验证部分

因为这个实验观察的东西较为简单，因此在main.c函数中编写测试函数

（运行结果附在实验文件中）