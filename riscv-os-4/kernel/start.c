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