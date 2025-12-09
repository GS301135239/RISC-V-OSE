#include "def.h"
#include "type.h"
#include "riscv.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

extern void scheduler();
extern void userinit();
extern void trap_init();
extern void kvminit();
extern void kvminithart();
extern void pmm_init();
extern void uart_init();
extern void plicinit(void);
extern void plicinithart(void);
extern void virtio_disk_init(void);
extern void binit(void);
extern void iinit(void);
extern void file_init(void);
extern void fsinit(int);
extern void fs_test(void);
extern void cow_kernel_test(void);
extern int kthread_create(void (*start)(void), const char *name); // 声明创建线程函数
void call_main(void);

// 设置将要 mret 跳转的地址与目标特权级 (S)
static void perm_init(void) {
    uint64 mstatus = read_mstatus();
    mstatus = (mstatus & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_S;
    write_mstatus(mstatus);
    write_mepc((uint64)call_main); // mret 后进入 S 模式的 call_main
}

// 委托异常与中断给 S 模式
static void intr_init_mmode(void) {
    // 全部异常与中断委托 S
    write_medeleg(0xffff);
    write_mideleg(0xffff);

    // 允许 S 模式使用定时器与外部中断
    write_sie(read_sie() | SIE_STIE | SIE_SEIE);

    // 打开 S 模式全局中断使能位（SPIE 留给返回时）
    write_sstatus(read_sstatus() | SSTATUS_SIE);
}

// 正确配置 PMP：开放全部物理地址给 S 模式访问
static void enable_physical_protection(void) {
    // NAPOT 匹配：全地址空间（56 位物理地址），使 R/W/X 有效，L=0 不锁定
    write_pmpaddr0(0x3fffffffffffffull);
    // 0x1F = R|W|X|(NAPOT)；避免使用 0x0F(TOR|锁定) 导致 S 模式权限问题
    write_pmpcfg0(0x1F);
}

// 初始化机器定时器（委托给 S 后仍需设置 stimecmp）
static void timer_init_mmode(void) {
    write_mie(read_mie() | MIE_STIE);
    write_menvcfg(read_menvcfg() | (1L << 63));
    write_mcounteren(read_mcounteren() | (1L << 1)); // 允许 S 模式读取 time
    write_stimecmp(read_time() + 1000000);
}

// S 模式真正的内核初始化
void call_main(void) {
    // 建立并初始化内核页表（仅 S 模式启用分页）
    kvminit();
    kvminithart();
    printf("Kernel page table initialized.\n");

    // 初始化进程表与其内核栈映射（在 kvminit 后）
    procinit();
    printf("Process table initialized.\n");

    // 初始化陷阱入口（设置 stvec 指向 kernelvec）
    trap_init();
    printf("Trap handler initialized.\n");

    plicinit();
    plicinithart();
    printf("PLIC initialized.\n");

    virtio_disk_init();
    printf("Virtio disk initialized.\n");

    binit();
    printf("Buffer cache initialized.\n");

    iinit();
    printf("Inode table initialized.\n");

    file_init();
    printf("File table initialized.\n");

    fsinit(1);
    printf("File system initialized.\n");

    // fs_test();

    // 创建第一个进程（其 context.ra 指向测试入口，不走用户态 sret）
    userinit();
    printf("First process initialized.\n");

    extern struct proc *initproc;
    acquire(&initproc->lock);
    initproc->state = SLEEPING; // 防止 initproc 运行
    release(&initproc->lock);

    if(kthread_create(cow_kernel_test, "cow_test") < 0) {
        panic("failed to create cow test thread");
    }
    printf("COW test thread created.\n");
    // 进入调度器（不返回）
    scheduler();

    panic("scheduler returned unexpectedly");
}

// ---------------- M 态启动 ----------------

void start(void) {
    // UART
    uart_init();
    printf("UART initialized.\n");

    // 物理内存分配器（为 alloc() 等服务）
    pmm_init();
    printf("Physical memory allocator initialized.\n");

    // 委托中断与异常给 S 模式
    intr_init_mmode();

    // 禁用分页（清 satp），确保后续 S 模式自行加载页表
    write_satp(0);

    // PMP 全空间开放
    enable_physical_protection();

    // 定时器初始化
    timer_init_mmode();

    // 设置 mret 跳转目标与特权级
    perm_init();

    // 切换到 S 模式执行 call_main
    asm volatile("mret");
}