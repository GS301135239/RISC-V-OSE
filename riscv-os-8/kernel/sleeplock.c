#include "type.h"
#include "riscv.h"
#include "def.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

// 初始化睡眠锁
void initsleeplock(struct sleeplock *lk, char *name) 
{
    initlock(&lk->lk, "sleeplock");// 初始化保护睡眠锁的自旋锁
    lk->locked = 0;
    lk->name = name;
    lk->pid = 0;
}

// 获取睡眠锁
void acquiresleep(struct sleeplock *lk)
{
    acquire(&lk->lk);// 先获取保护睡眠锁的自旋锁

    while(lk->locked) {
        if(myproc() == 0)
            panic("acquiresleep");
        sleep(lk, &lk->lk);// 睡眠等待锁释放
    }

    lk->locked = 1;// 获取锁成功
    
    if(myproc())
        lk->pid = myproc()->pid;// 记录持有锁的进程ID
    else
        lk->pid = 0;

    release(&lk->lk);// 释放保护睡眠锁的自旋锁
}

// 释放睡眠锁
void releasesleep(struct sleeplock *lk)
{
    acquire(&lk->lk);// 先获取保护睡眠锁的自旋锁

    if(lk->locked == 0) {
        panic("releasesleep");
    }// 锁未被持有或不是当前进程持有，报错

    if(myproc() != 0 && lk->pid != myproc()->pid) {
        panic("releasesleep");
    }// 不是当前进程持有锁，报错

    lk->locked = 0;// 释放锁
    lk->pid = 0;// 清除持有锁的进程ID

    if(myproc() != 0)
        wakeup(lk);// 唤醒等待该锁的进程

    release(&lk->lk);// 释放保护睡眠锁的自旋锁
}

// 检查当前进程是否持有睡眠锁
int holdingsleep(struct sleeplock *lk)
{
    int r;
    acquire(&lk->lk);// 获取保护睡眠锁的自旋锁
    if(myproc())
        r = (lk->locked && lk->pid == myproc()->pid);
    else
        r = lk->locked;// 如果没有当前进程，检查锁是否被持有
    release(&lk->lk);// 释放保护睡眠锁的自旋锁
    return r;
}

