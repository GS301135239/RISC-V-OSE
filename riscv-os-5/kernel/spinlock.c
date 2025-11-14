#include "proc.h"
#include "memlayout.h"
#include "riscv.h"
#include "def.h"

// 初始化自旋锁
void initlock(struct spinlock *lk, char *name) {
    lk->locked = 0;
    lk->name = name;
    lk->cpu = 0;
}

// 关闭中断并记录之前的中断状态
void push_off(){
    int old = intr_get();

    intr_off();// 关闭中断
    if(mycpu()->noff == 0) {
        mycpu()->intena = old;
    }
    mycpu()->noff += 1;// 记录嵌套深度
}

// 恢复之前的中断状态
void pop_off(){
    struct cpu *c = mycpu();
    if(intr_get()){
        panic("pop_off - interruptible");
    }

    if(c->noff < 1){
        panic("pop_off");
    }

    c->noff -= 1;
    if(c->noff == 0 && c->intena){
        intr_on();
    }
}

// 检查当前CPU是否持有自旋锁
int holding(struct spinlock *lk){
    return lk->locked && lk->cpu == mycpu();
}

// 获取自旋锁
void acquire(struct spinlock *lk){
    push_off();// 关闭中断以保证原子操作

    if(holding(lk)){
        panic("acquire");
    }// 已经持有锁，死锁

    while(__sync_lock_test_and_set(&lk->locked, 1) != 0);
        // 自旋等待锁释放

    __sync_synchronize();// 内存屏障，防止编译器重排序

    lk->cpu = mycpu();// 记录持有锁的CPU
}

// 释放自旋锁
void release(struct spinlock *lk){
    if(!holding(lk)){
        panic("release");
    }// 未持有锁，错误释放

    lk->cpu = 0;// 清除持有锁的CPU记录

    __sync_synchronize();// 内存屏障，防止编译器重排序
    __sync_lock_release(&lk->locked);// 释放锁
    pop_off();// 恢复之前的中断状态
}