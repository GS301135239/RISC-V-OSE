
#include "type.h"
#include "param.h"

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

//
struct trapframe{
    uint64 kernel_satp;// 内核页表
    uint64 kernel_sp;// 内核栈指针
    uint64 kernel_trap;// 内核trap处理程序入口
    uint64 epc;// 保存用户程序计数器
    uint64 kernel_hartid;// 内核hartid
    uint64 ra;// 返回地址
    uint64 sp;// 用户栈指针
    uint64 gp;// 全局指针
    uint64 tp;// 线程指针
    uint64 t0;// 临时寄存器
    uint64 t1;
    uint64 t2;
    uint64 s0;// 保存寄存器
    uint64 s1;
    uint64 a0;// 函数参数/返回值
    uint64 a1;
    uint64 a2;
    uint64 a3;
    uint64 a4;
    uint64 a5;
    uint64 a6;
    uint64 a7;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
    uint64 t3;// 临时寄存器
    uint64 t4;
    uint64 t5;
    uint64 t6;
};

struct context{
    uint64 ra;
    uint64 sp;

    // 保存寄存器
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

struct cpu{
    struct proc *proc;// 当前运行在该CPU上的进程
    struct context context;// 该CPU上下文切换时保存
    int noff;// 记录push_off的嵌套深度
    int intena;// 记录中断开启状态
};

//单核CPU操作系统，不需要
//extern struct cpu cpus[ NCPU ];

// 记录单核CPU
typedef void (*kstart0_t)(void);

struct proc{
    struct spinlock lock;// 保护进程表

    enum procstate state;// 进程状态
    void *channel;// 如果进程在睡眠，则表示睡眠的channel
    int killed;// 如果非0，则表示进程已被杀死
    int xstate;// 退出状态
    int pid;// 进程ID

    //wait process
    struct proc *parent;// 父进程指针

    uint64 kstack;// 内核栈底地址
    uint64 sz;// 进程内存大小
    pagetable_t pagetable;// 进程页表
    struct trapframe *trapframe;// 进程陷阱帧
    struct context context;// 进程上下文切换时保存
    //未实现文件系统，暂时不需要
    struct file *ofile[NOFILE];// 进程打开的文件表
    struct inode *cwd;// 进程当前工作目录
    char name[16];// 进程名称

    kstart0_t kstart0;// 进程的内核线程入口函数
};