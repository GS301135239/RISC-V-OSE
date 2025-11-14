# 实验5：进程管理与调度

## 系统设计部分

### 系统架构部分


其中核心文件的作用如下：

- spinlock.h：自旋锁的类结构
- spinlock.c：spinlock的实现，主要是实现自旋锁，主要思路与xv6源码一致
- proc.h：进程的结构体定义
- proc.c：进程管理与调度的实现函数，以及一些在内核态和用户态之间切换控制的函数实现
- swtch.S：实现上下文的切换
- trampoline.S：在发生中断时保存用户的trapframe以及用户从内核态切换回去之后要重新加载的内容
- test.c：测试函数，主要测试进程的创建退出以及调度器


### 与 xv6 对比分析

- 这个实验是我暂时做到的内容最多的实验，主要是由于xv6源码是多核代码，在我的单核系统里面需要将xv6中proc.c的所有有关于多核循环轮转等内容简化为单核多进程调度循环。
- 同时这个实验也是调试内容最多的一次了，因为在第4个实验的中断处理中操作系统是在S模式下处理的，而有关于用户态下fork的系统调用却在第6个实验里面才实现，因此第5个实验的调度测试等就不能调用fork等来创建测试函数，需要在内核态里面创建内核线程来进行测试，导致我一开始尝试像xv6一样使用用户态函数时发生了很多麻烦

## 实验过程部分

### 实验步骤

#### 1）实现spinlock相关

这一部分主要与xv6源码相同，因此在这不再多赘述

```c

#include "type.h"

struct spinlock{

    uint locked;
    char *name;

    struct cpu *cpu;// 记录持有锁的CPU
};

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

```

#### 2）实现swtch.S

- 这个.S文件主要实现的是内核调度上下文之间的切换，在RISC-V的标准之下只需要保存那些在函数调用间保持不变的寄存器等，其他用户态寄存器由trapframe以及trampoline.S来保存，由trap.c之中的代码进行恢复

#### 3）实现trampoline.S

- 这个.S文件主要实现的是trapframe的保存和恢复，通过保存trapframe的返回就可以恢复到发生中断时的用户态状态，主要参考xv6的源码实现

#### 4）补齐vm.c

- 在进程管理部分，需要对虚拟内存进行页表映射以及父子进程之间的内存复制、用户态与内核态之间相互复制之类的操作，因此需要对vm.c中未实现的uvmcopy、uvmfault等进行实现

```c

uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = alloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// copy from old to new pagetable
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;   // page table entry hasn't been allocated
    if((*pte & PTE_V) == 0)
      continue;   // physical page hasn't been allocated
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = alloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
  
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
      
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间或内核空间复制数据到内核空间
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

uint64 vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64) alloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}

```

#### 5）实现proc相关（proc.h和proc.c）

- proc.c主要实现参考xv6的源代码来进行实现，同时将xv6中的关于多核之间轮转调度的内容进行简化

```c

struct cpu* single_cpu

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

```
- 这段代码主要实现的是将xv6的多核调度简化到单核调度，通过将cpuid写死成return 0以及mycpu只返回单核cpu来实现。

- 而其他部分，如页表映射、内存管理、进程创建、fork、exit、wait、sleep、wakeup、kill、检测进程是否被杀死、内核态与用户态相互交换数据等基本根据xv6的思路完成

- 这个实验由于并没有任何关于系统调用的实现，因此要完成对于exit、scheduler、yield、producer-consumer等测试，必须要在内核态中建立内核线程来“模仿”用户态进程的各类操作。

```c

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

```

- 这里主要是在proc.h中增加一段typedef void (*kstart0_t)(void);
- 增加这个主要是为了记录内核线程入口函数
- 同时在procinit中还需要对内核栈进行映射，避免在内核线程创建时发生缺页错误，这里映射内核栈主要目的就是为了内核线程测试的进行
- 在内核线程创建后，则需要将ra指向这个内核线程启动函数，然后进入kthread_boot0函数中触发start.c中的start函数，而通过start函数调用userinit函数对initproc进行初始化，将initproc的kstart0字段定义为测试函数的地址，scheduler的返回地址均指向kthread_boot0，保证了调度器在上下文切换后仍能指向正确的地址，而调用scheduler进行进程调度后，才从initproc调度到test函数进行执行，而通过将所有进程以及测试函数挂载到内核线程跳板中也是可以为了避免在首次acquire锁的时候发生“同一CPU递归加锁”的panic

#### 6）编写test.c进行测试

```c

// 1) 进程创建与退出测试（内核线程语义）

static void child_exit_entry(void) {
    printf("fork_exit_test: child running, will exit(0)\n");
    kexit(0);
}

void fork_exit_test() {
    printf("=== fork_exit_test start ===\n");
    int pid = kthread_create(child_exit_entry, "child_exit");
    if (pid < 0) {
        printf("fork_exit_test: create failed\n");
    } else {
        printf("fork_exit_test: parent created pid=%d, wait...\n", pid);
        int reaped = kwait(0);
        printf("fork_exit_test: kwait collected pid=%d\n", reaped);
    }
    printf("=== fork_exit_test end ===\n\n");
}

// 2) 调度/让出CPU测试（多线程轮转）

struct yield_arg {
    int id;
    int loops;
};

static struct yield_arg yargs[4];

static void yield_worker0(void) {
    struct yield_arg *a = &yargs[0];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}
static void yield_worker1(void) {
    struct yield_arg *a = &yargs[1];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}
static void yield_worker2(void) {
    struct yield_arg *a = &yargs[2];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}

void yield_test() {
    printf("=== yield_test start ===\n");

    const int loops = 3;
    for (int i = 0; i < 3; i++) {
        yargs[i].id = i;
        yargs[i].loops = loops;
    }

    int p0 = kthread_create(yield_worker0, "yield0");
    int p1 = kthread_create(yield_worker1, "yield1");
    int p2 = kthread_create(yield_worker2, "yield2");

    if (p0 < 0 || p1 < 0 || p2 < 0) {
        printf("yield_test: create failed p0=%d p1=%d p2=%d\n", p0, p1, p2);
    }

    // 等待全部线程退出
    kwait(0);
    kwait(0);
    kwait(0);

    printf("=== yield_test end ===\n\n");
}

// 3) 生产者-消费者（单元素缓冲区 + sleep/wakeup）

static int buffer = 0;
static int buffer_full = 0;
static struct spinlock buf_lock;

#define BUF_ITERS 5

static void producer_entry(void) {
    printf("producer: start\n");
    for (int i = 0; i < BUF_ITERS; i++) {
        acquire(&buf_lock);
        while (buffer_full) {
            // sleep 会在内部释放 buf_lock，被唤醒后重新获取
            sleep((void*)&buffer_full, &buf_lock);
        }
        buffer = i + 1;
        buffer_full = 1;
        printf("producer: produced %d\n", buffer);
        wakeup((void*)&buffer_full); // 唤醒消费者
        release(&buf_lock);
        yield(); // 让消费者更快拿到CPU
    }
    printf("producer: finished\n");
    kexit(0);
}

static void consumer_entry(void) {
    printf("consumer: start\n");
    for (int i = 0; i < BUF_ITERS; i++) {
        acquire(&buf_lock);
        while (!buffer_full) {
            sleep((void*)&buffer_full, &buf_lock);
        }
        printf("consumer: consumed %d\n", buffer);
        buffer_full = 0;
        wakeup((void*)&buffer_full); // 唤醒生产者
        release(&buf_lock);
        yield();
    }
    printf("consumer: finished\n");
    kexit(0);
}

void producer_consumer_test() {
    printf("=== producer_consumer_test start ===\n");
    buffer = 0;
    buffer_full = 0;
    initlock(&buf_lock, "buf_lock");

    int pp = kthread_create(producer_entry, "producer");
    int pc = kthread_create(consumer_entry, "consumer");

    if (pp < 0 || pc < 0) {
        printf("producer_consumer_test: create failed p=%d c=%d\n", pp, pc);
    }

    kwait(0);
    kwait(0);

    printf("=== producer_consumer_test end ===\n\n");
}


void run_all_tests() {
    printf("=== run_all_tests begin ===\n");

    fork_exit_test();
    yield_test();
    producer_consumer_test();

    printf("All tests finished. System halt.\n");
    for (;;)
        asm volatile("wfi");
}

```

### 源码理解总结

- 这个实验的代码思路总体来说还是较为简单的，调度的算法也算是比较直观的，但是最难的部分是如何在没有系统调用的前提下对调度器、轮转调度等内容进行“仿真”，通过这个实验，我对内核态和用户态有了更深入的理解，对实验6、实验7的方向和思路有了一定的认识和理解。

## 测试验证部分

- 通过将test.c的函数挂载到内核线程跳板中，运行代码，在start.c中调用scheduler调度器调度后即可通过initproc来对test代码进行测试

（运行结果附在实验文件中）