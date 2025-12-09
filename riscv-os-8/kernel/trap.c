#include "type.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "def.h"

extern int plic_claim(void);
extern void plic_complete(int irq);
// extern void uart_intr();
extern void virtio_disk_intr(void);
extern char trampoline[], uservec[], userret[];
//定义在 kernelvec.S 中的内核 trap 入口函数
extern void kernelvec();
//定义在 usertrap.S 中的用户 trap 入口函数
// extern void usertrap(uint64, uint64);
void usertrap();

//启用 trap 处理程序
void tarp_init_hart() {
    // 设置内核的 trap 入口地址
    write_stvec((uint64) kernelvec );

    write_sie(read_sie() | SIE_STIE);

    // 设置第一次时钟中断的时间
    write_stimecmp(read_time() + 1000000);
}

void trap_init() {
    tarp_init_hart();
}

int ticks = 0;

//处理时钟中断
void handle_clock_intr() {
    ticks++;

    //printf( "tick %d\n", ticks );

    // 记录下一个时钟中断时间
    write_stimecmp(read_time() + 1000000);
}

// 准备从内核返回到用户空间
void prepare_return() {
    struct proc *p = myproc();

    // 1. 设置用户态的陷阱处理入口
    uint64 stvec_val = TRAMPOLINE + (uint64)(uservec - trampoline);
    
    // --- 调试代码 START ---
    // 只打印一次，避免刷屏
    /* static int debug_printed = 0;
    if (!debug_printed) {
        printf("DEBUG: prepare_return\n");
        printf("       trampoline: %x\n", trampoline);
        printf("       uservec:    %x\n", uservec);
        printf("       stvec_val:  %x\n", stvec_val);
        debug_printed = 1;
    }*/
    // --- 调试代码 END ---

    write_stvec(stvec_val);

    // 2. 准备 sstatus 寄存器以返回用户态
    uint64 sstatus = read_sstatus();
    sstatus &= ~SSTATUS_SPP; // 清除 SPP 位，表示 sret 后将进入 U 模式
    sstatus |= SSTATUS_SPIE; // 设置 SPIE 位，以便返回用户态后开启中断
    write_sstatus(sstatus);

    // 3. 设置返回到用户态后要执行的指令地址
    // p->trapframe->epc 在 exec 系统调用或进程创建时被设置
    write_sepc(p->trapframe->epc);

    // 为用户态 trap 返回内核时准备必要字段
    // 这些字段在 trampoline 的 uservec 中会被使用
    extern pagetable_t kernel_pagetable;
    p->trapframe->kernel_satp   = MAKE_SATP(kernel_pagetable);
    p->trapframe->kernel_sp     = p->kstack + PGSIZE;
    p->trapframe->kernel_trap   = (uint64)usertrap;
    p->trapframe->kernel_hartid = 0; // 单核，或使用 read_tp()

    // 生成用户页表的 satp 值
    uint64 satp = MAKE_SATP(p->pagetable);

    // 通过 TRAMPOLINE 地址调用 userret(trapframe, satp)，最终执行 sret
    uint64 fn = TRAMPOLINE + (uint64)(userret - trampoline);
    ((void (*)(uint64, uint64))fn)((uint64)p->trapframe, satp);
    // 不会返回
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

            // 外部中断
        case 0x8000000000000009L:
            int irq = plic_claim();
            if ( irq == VIRTIO0_IRQ ) {
                virtio_disk_intr();
            } else if ( irq ) {
                printf( "handle_device_intr: unexpected interrupt irq = %d\n", irq );
            }
            if ( irq ) {
                plic_complete( irq );
            }
            return 1;

        default:
            return -1;
    }
}

/// 用户态的 trap 处理函数
void usertrap() {

    // printf( "usertrap called\n" );

    uint64 scause = read_scause();

    // 确保我们是从用户态进入的
    if ((read_sstatus() & SSTATUS_SPP) != 0) {
        panic("usertrap: not from user mode");
    }

    // 由于 prepare_return 会设置 stvec，所以这里需要重新设置为内核的 trap 入口
    // 以便在内核中处理中断（例如，在 yield() 期间发生的时钟中断）
    write_stvec((uint64)kernelvec);

    struct proc *p = myproc();
    // 保存 sepc，因为 prepare_return 会覆盖它
    p->trapframe->epc = read_sepc();

    // 检查是否是时钟中断
    if (scause == 0x8000000000000005L) {
        // 处理时钟中断
        handle_clock_intr();
        
        // 放弃CPU，进行调度
        yield();

    }

    else if(scause == 8) {
        // printf( "usertrap: syscall\n" );
        // 系统调用

        if(p->killed) {
            kexit(-1);
        }

        // 增加 pc 以跳过 ecall 指令
        p->trapframe->epc += 4;

        intr_on();
        // 处理系统调用
        syscall();

    }

    // 其他设备中断
    else if(scause == 15){
        uint64 va = read_stval();
        if(cow_alloc(p->pagetable, va) < 0){
            p->killed = 1;
        }
    }

    else {
        // 其他未知陷阱，先杀死进程
        printf("usertrap: unexpected scause %x, pid=%d\n", scause, p->pid);
        printf("            sepc=%x\n", read_sepc());
        p->killed = 1;
    }

    // 检查进程是否被杀死或已退出
    if (p->killed) {
        kexit(-1);
    }

    // 准备返回用户空间
    prepare_return();
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

    if ( intr_get() ) {
        // handle trap 时不应该开启中断
        panic( "kernel_trap: interrupt enabled" );
    }

    if ( handle_device_intr(scause) == -1 ) {
        printf("scause %x\n", scause);
        printf("sepc %x\n", sepc);
        panic("kernel_trap: unexpected scause");
    }

    write_sepc( sepc );
    write_sstatus( sstatus );
}


