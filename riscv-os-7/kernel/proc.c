#include "type.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "def.h"

static struct cpu single_cpu; // 单核 CPU 状态

struct proc proc[NPROC];
struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
extern void run_all_tests(void);

extern char trampoline[]; // trampoline.S

// wait_lock 用于保护 parent/child 关系，防止错过唤醒。
struct spinlock wait_lock;

// 为每个进程分配一页内核栈并映射。
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = alloc();
    if(pa == 0)
      panic("alloc");
    uint64 va = KSTACK((int)(p - proc));
    
    // 内核页表映射
    kvmmake(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

extern pagetable_t kernel_pagetable;
// 初始化进程表和相关锁
void procinit(void)
{
  struct proc *p;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  // 单核 CPU 结构清零
  memset(&single_cpu, 0, sizeof(single_cpu));

  for(p = proc; p < &proc[NPROC]; p++){
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }

  proc_mapstacks(kernel_pagetable);// 映射内核栈
}

// 单核下 cpuid() 直接返回 0。
int cpuid()
{
  return 0;
}

// 返回单核 cpu 结构指针
struct cpu* mycpu(void)
{
  return &single_cpu;
}

// 当前进程指针
struct proc* myproc(void)
{
  push_off();
  struct proc *p = mycpu()->proc;
  pop_off();
  return p;
}

// 分配进程的pid
int allocpid()
{
  int pid;
  acquire(&pid_lock);
  pid = nextpid;
  nextpid++;
  release(&pid_lock);
  return pid;
}

// 分配一个新的进程结构
static struct proc* allocproc(void)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED){
      goto found;
    }
    release(&p->lock);
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  if((p->trapframe = (struct trapframe *)alloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 创建进程页表
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置进程上下文以从 forkret 返回
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  return p;
}

// 释放进程资源
static void freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->channel = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// 创建进程页表并映射必要的内存区域
pagetable_t proc_pagetable(struct proc *p)
{
  pagetable_t pagetable = create_pagetable();
  if(pagetable == 0)
    return 0;

  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放进程页表及其映射的内存
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 内核线程启动函数
static void kthread_boot0(void)
{
  struct proc *p = myproc();
  kstart0_t start = p->kstart0;
  // 进程是从调度器带锁切入的，这里先释放
  if(holding(&p->lock)) release(&p->lock);
  if (start)
    start();
  kexit(0);
}

extern uchar _binary_user_initcode_start[];
extern uchar _binary_user_initcode_size[];
// 初始化第一个用户进程
void userinit(void)
{
  struct proc *p = allocproc();
  if(p == 0){
    panic("userinit: allocproc failed");
  }
  initproc = p;
  safestrcpy(p->name, "init", sizeof(p->name));

  // 分配一页用户内存并拷贝 initcode
  p->sz = PGSIZE;
  if(uvmalloc(p->pagetable, 0, p->sz, PTE_W | PTE_R | PTE_X) == 0)
    panic("userinit: uvmalloc failed");
  // 将 initcode 放到用户虚拟地址 0 开始
  uint64 init_sz = (uint64)_binary_user_initcode_size;
  if(copyout(p->pagetable, 0, (char*)_binary_user_initcode_start, init_sz) < 0)
    panic("userinit: copyout initcode failed");

  // 设置 trapframe 初始寄存器
  memset(p->trapframe, 0, sizeof(*p->trapframe));
  p->trapframe->epc = 0;            // 从虚拟地址 0 开始执行
  p->trapframe->sp  = PGSIZE;       // 用户栈顶（简单：同一页的顶部）

  // 让调度器从 forkret 进入 prepare_return
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->state = RUNNABLE;
  release(&p->lock);
}

// 增加或减少进程内存大小
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
      return -1;
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 创建一个新进程，复制父进程的内存和状态
int kfork(void)
{
  int pid;
  struct proc *np;
  struct proc *p = myproc();

  if((np = allocproc()) == 0)
    return -1;

  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->a0 = 0;

/*for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);*/ // 未实现文件系统，暂时不需要
  safestrcpy(np->name, p->name, sizeof(np->name));

  pid = np->pid;
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// 将进程的子进程重新分配给 init 进程
void reparent(struct proc *p)
{
  struct proc *pp;
  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// 终止当前进程并释放资源
void kexit(int status)
{
  struct proc *p = myproc();
  if(p == initproc)
    panic("init exiting");

  /*for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;*/ // 未实现文件系统，暂时不需要

  acquire(&wait_lock);
  reparent(p);
  wakeup(p->parent);

  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);

  sched();
  panic("zombie exit");
}

// 等待子进程退出并回收资源
int kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);
  for(;;){
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        acquire(&pp->lock);
        havekids = 1;
        if(pp->state == ZOMBIE){
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0){
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          // 调试输出：父进程、子 pid、退出码
            printf("kwait: parent %d collected child %d status %d\n",
                p->pid, pp->pid, pp->xstate);
            freeproc(pp);
            release(&pp->lock);
            release(&wait_lock);
            return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.

    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }

    sleep(p, &wait_lock);
  }
}


// 去掉多 CPU 循环的含义，仅对单核进行循环调度。
// 如果没有 RUNNABLE 进程，执行 wfi 等待中断。

void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    intr_on();   // 允许外设中断唤醒
    intr_off();  // 关闭中断避免和调度切换竞态 (保持语义简单)

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        p->state = RUNNING;
        c->proc = p;
        printf("scheduler: running process %d\n", p->pid);
        swtch(&c->context, &p->context);
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0){
      printf("scheduler: no RUNNABLE process, CPU idle\n");
      asm volatile("wfi"); // 无进程可运行，等待中断
    }
  }
}

// 切换到调度器
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 让出 CPU，进入调度器
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// 进程首次运行时调用，返回用户态
void forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  release(&p->lock);

  if(first){
    // 尚未实现文件系统：跳过 fsinit() 与 kexec("/init", ...)
    first = 0;
    __sync_synchronize();

    // 安全检查：若未建立任何用户内存，直接报错，避免返回用户态崩溃。
    if (p->sz == 0) {
      panic("no filesystem and no user image; implement userinit()");
    }
  }
    // 返回用户态

  prepare_return();
  
  panic("forkret: prepare_return returned");
}

// 进程睡眠，等待 chan 上的事件
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  release(lk);

  p->channel = chan;
  p->state = SLEEPING;
  sched();

  p->channel = 0;
  release(&p->lock);
  acquire(lk);
}

// 唤醒所有在 chan 上睡眠的进程
void wakeup(void *chan)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->channel == chan){
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 根据 pid 杀死进程
int kkill(int pid)
{
  struct proc *p;
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// 设置进程为已杀死状态
void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

// 检查进程是否已被杀死
int killed(struct proc *p)
{
  int k;
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// 在用户或内核空间之间复制数据
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  }else{
    memmove((char*)dst, src, len);
    return 0;
  }
}

// 在用户或内核空间之间复制数据
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  }else{
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 打印进程列表，供调试使用
void procdump(void)
{
  static char *states[] = {
    [UNUSED]    "unused",
    [USED]      "used",
    [SLEEPING]  "sleep",
    [RUNNABLE]  "runble",
    [RUNNING]   "run",
    [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s\n", p->pid, state, p->name);
  }
}

// 创建内核线程(主要用于测试)
int kthread_create(void (*start)(void), const char *name)
{
  struct proc *np;
  struct proc *p = myproc();

  if ((np = allocproc()) == 0)
    return -1;

  np->kstart0 = start;
  // 内核线程不走用户态：不要覆盖 np->pagetable，也不要改 trapframe->epc/sp
  memset(&np->context, 0, sizeof(np->context));
  np->context.ra = (uint64)kthread_boot0; // 设置入口为内核线程启动函数
  np->context.sp = np->kstack + PGSIZE;

  safestrcpy(np->name, name ? name : "kthread", sizeof(np->name));

  int pid = np->pid;
  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}