#include "def.h"
#include "riscv.h"

//写 mstatus 寄存器
inline void write_mstatus( uint64 x ) {
    asm volatile( "csrw mstatus, %0" : : "r" ( x ) );
}

//读 mstatus 寄存器
inline uint64 read_mstatus() {
    uint64 x;

    asm volatile( "csrr %0, mstatus" : "=r" ( x ) );

    return x;
}

//读 mcause 寄存器
inline uint64 read_mcause() {
    uint64 x;

    asm volatile( "csrr %0, mcause" : "=r" ( x ) );

    return x;
}

//写 satp 寄存器
inline void write_satp( uint64 x ) {
    asm volatile( "csrw satp, %0" : : "r" ( x ) );
}

//写 medeleg 寄存器
inline void write_medeleg( uint64 x ) {
    asm volatile( "csrw medeleg, %0" : : "r" ( x ) );
}

//写 mideleg 寄存器
inline void write_mideleg( uint64 x ) {
    asm volatile( "csrw mideleg, %0" : : "r" ( x ) );
}

//读 sie 寄存器
inline uint64 read_sie() {
    uint64 x;

    asm volatile( "csrr %0, sie" : "=r" ( x ) );

    return x;
}

//写 sie 寄存器
inline void write_sie( uint64 x ) {
    asm volatile( "csrw sie, %0" : : "r" ( x ) );
}

//读 mie 寄存器
inline uint64 read_mie() {
    uint64 x;

    asm volatile( "csrr %0, mie" : "=r" ( x ) );

    return x;
}

//写 mie 寄存器
inline void write_mie( uint64 x ) {
    asm volatile( "csrw mie, %0" : : "r" ( x ) );
}

//写 mtvec 寄存器
inline void write_mtvec( uint64 x ) {
    asm volatile( "csrw mtvec, %0" : : "r" ( x ) );
}

//写 stvec 寄存器
inline void write_stvec( uint64 x ) {
    asm volatile( "csrw stvec, %0" : : "r" ( x ) );
}

//写 scause 寄存器
inline void write_scause( uint64 x ) {
    asm volatile( "csrw scause, %0" : : "r" ( x ) );
}

//读 sepc 寄存器
inline uint64 read_sepc() {
    uint64 x;

    asm volatile( "csrr %0, sepc" : "=r" ( x ) );

    return x;
}

//写 sepc 寄存器
inline void write_sepc( uint64 x ) {
    asm volatile( "csrw sepc, %0" : : "r" ( x ) );
}

//写 stval 寄存器
inline void write_stval( uint64 x ) {
    asm volatile( "csrw stval, %0" : : "r" ( x ) );
}

//读 stval 寄存器
inline uint64 read_stval() {
    uint64 x;

    asm volatile( "csrr %0, stval" : "=r" ( x ) );

    return x;
}

//写 sstatus 寄存器
inline void write_sstatus( uint64 x ) {
    asm volatile( "csrw sstatus, %0" : : "r" ( x ) );
}

//读 sstatus 寄存器
inline uint64 read_sstatus() {
    uint64 x;

    asm volatile( "csrr %0, sstatus" : "=r" ( x ) );

    return x;
}

//判断当前中断是否开启
inline int is_interupt_on() {
    uint64 x = read_sstatus();

    return ( x & SSTATUS_SIE ) != 0;
}

//读 scause 寄存器
inline uint64 read_scause() {
    uint64 x;

    asm volatile( "csrr %0, scause" : "=r" ( x ) );

    return x;
}

//读 time 寄存器
inline uint64 read_time() {
    uint64 x;

    asm volatile( "csrr %0, time" : "=r" ( x ) );

    return x;
}

//写 stimecmp 寄存器
inline void write_stimecmp( uint64 x ) {
    asm volatile( "csrw stimecmp, %0" : : "r" ( x ) );
}

//读 stimecmp 寄存器
inline uint64 read_stimecmp() {
    uint64 x;

    asm volatile( "csrr %0, stimecmp" : "=r" ( x ) );

    return x;
}

//写 mcounteren 寄存器
inline void write_mcounteren( uint64 x ) {
    asm volatile( "csrw mcounteren, %0" : : "r" ( x ) );
}

//读 mcounteren 寄存器
inline uint64 read_mcounteren() {
    uint64 x;

    asm volatile( "csrr %0, mcounteren" : "=r" ( x ) );

    return x;
}

//写 menvcfg 寄存器
inline void write_menvcfg( uint64 x ) {
    asm volatile( "csrw menvcfg, %0" : : "r" ( x ) );
}

//读 menvcfg 寄存器
inline uint64 read_menvcfg() {
    uint64 x;

    asm volatile( "csrr %0, menvcfg" : "=r" ( x ) );

    return x;
}

//写 mepc 寄存器
inline void write_mepc( uint64 x ) {
    asm volatile( "csrw mepc, %0" : : "r" ( x ) );
}

//写 pmpaddr0 寄存器
inline void write_pmpaddr0( uint64 x ) {
    asm volatile( "csrw pmpaddr0, %0" : : "r" ( x ) );
}

//写 pmpcfg0 寄存器
inline void write_pmpcfg0( uint64 x ) {
    asm volatile( "csrw pmpcfg0, %0" : : "r" ( x ) );
}

//写 pmpaddr1 寄存器
inline void write_pmpaddr1( uint64 x ) {
    asm volatile( "csrw pmpaddr1, %0" : : "r" ( x ) );
}

//写 pmpcfg1 寄存器
inline void write_pmpcfg1( uint64 x ) {
    asm volatile( "csrw pmpcfg1, %0" : : "r" ( x ) );
}   


//读 sp 寄存器
uint64 read_sp() {
    uint64 x;

    asm volatile( "mv %0, sp" : "=r" ( x ) );

    return x;
}

//刷新页表
inline void sfence_vma() {
    // the zero, zero means flush all TLB entries.
    asm volatile( "sfence.vma zero, zero" );
}   