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