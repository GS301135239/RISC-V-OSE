# 实验6：系统调用

## 系统设计部分

### 系统架构部分

其中核心文件的作用如下：

- syscall.h：系统调用号的头文件
- syscall.c：系统调用的参数解析和分发函数实现
- sysproc.c：调用实验5中实现的真正的fork、exit、wait等函数
- trap.c：补全关于系统调用中断的处理函数
- user/initcode.S：汇编代码，在实现文件系统之前将测试代码转换成汇编代码，模拟用户态的调用


### 与 xv6 对比分析

- 实现思路与xv6基本还是相同的，有一些细节上的完善，因为如果完全按照xv6的思路来写的话经常会在一些很奇怪的地方，比如说页映射、trapframe来回切换等过程中卡死，也没有panic之类的提示，就是纯粹操作系统跑不起来……

## 实验过程部分

### 实验步骤

#### 1）实现syscall.h/.c

```c

int fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

int fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64 argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void argint(int n, int *ip)
{
  *ip = argraw(n);
}


void argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

int argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_getpid(void);
extern uint64 sys_kill(void);

static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_getpid]  sys_getpid,
[SYS_kill]    sys_kill,
};

void syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}

```

- 这段代码主要仿照的xv6实现系统调用解析参数、系统调用分发的实现
- 主要实现的方式是根据函数调用号从调用表中找到对应调用的地址，将其写入trapframe的函数返回地址中，当发生系统调用陷入中断时，系统判断是否为系统调用，若为系统调用，则调用分发函数进行系统调用，然后调用usertrapret函数返回后回到用户态

#### 2）实现sysproc.c

```c

uint64 sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void)
{
  return myproc()->pid;
}

uint64 sys_fork(void)
{
  return kfork();
}

uint64 sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64 sys_kill(void)
{
  int pid;
  argint(0, &pid);
  return kkill(pid);
}

```

- 这段代码主要作用是实现分发表中的函数实现，提供参数解析、调用真正进程管理的接口功能

#### 3）补全trap.c中有关用户态系统调用的代码

```c

else if(scause == 8) {
        printf( "usertrap: syscall\n" );
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

```

- 这段代码是通过读取中断产生码（在RISC-V结构下系统调用为8），然后调用系统调用分发函数完成这一次的系统调用，其中p->trapframe->epc += 4是由于在汇编代码中系统调用是通过ecall命令触发的，这里+4是为了跳过ecall指令，防止重复调用

#### 4）修改proc.c

- 由于这个时候我们已经实现了用户态的代码，因此在init第一个进程的时候以及以后allocproc进程的时候就不再需要将其返回地址绑定到内核线程的测试函数上，而是绑定到forkret中，在调度器调度进程后通过trap.c中的函数返回用户态

```c

// 分配一页用户内存并拷贝 initcode
  p->sz = PGSIZE;
  if(uvmalloc(p->pagetable, 0, p->sz, PTE_W | PTE_R | PTE_X) == 0)
    panic("userinit: uvmalloc failed");
  // 将 initcode 放到用户虚拟地址 0 开始
  uint64 init_sz = (uint64)_binary_user_initcode_size;
  if(copyout(p->pagetable, 0, (char*)_binary_user_initcode_start, init_sz) < 0)
    panic("userinit: copyout initcode failed");

```

- 注意：因为我们并没有实现文件系统，没有使用系统启动调用代码，而是通过硬编码汇编代码直接触发ecall指令来触发系统调用的，因此这里需要为initcode.S直接分配一块内存空间

#### 5）实现user/initcode.S

- 这段代码是一个主要用于测试的代码，通过汇编代码的形式来实现初步的用户态fork、wait、exit系统调用的测试，通过将系统调用号写入寄存器中，然后调用ecall触发系统调用中断实现系统调用。

#### 6）修改Makefile

- 由于initcode.S是用户态的文件，因此需要与内核态的各部分分开编译，将initcode.S单独编译成.o和ELF文件，然后通过链接将用户态文件和内核态文件绑定起来。

#### 7）修改kernel.ld

- 这个文件真的给我挖了巨大一个坑！！因为引入了trampoline.S中的trampsec符号，而在前5个实验里面一直在内核态里面，而第6个实验需要进入用户态，因此需要将trampoline.S在kernel.ld中的.text段进行链接，否则会因为陷阱帧的映射失败而导致整个系统崩掉！这个地方卡了不少时间，还是翻看xv6细细对比才发现缺了这一小块！

### 源码理解总结

- 这个实验本来以为第5个跑通了就会比较容易，毕竟kfork、kexit之类的函数也都在第5个实验里面跑通了，但是做的时候才发现不简单，经常在forkret之后就直接崩溃掉了，而且在trap.c里面加调试信息也没办法找到哪里崩了（因为Kernel.ld里没有trampoline.S里面定义的那个trampsec，因此会被直接分配到地址为0的不知名内存里面……，陷阱帧一映射下去就直接失败），不过通过这个实验，也是学会了如何将用户态和内核态链接起来的知识


## 测试验证部分

- 运行initcode.S即可完成测试，测试输出由proc.c里面的kwait函数给出

```c

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

```
（运行结果附在实验文件中）