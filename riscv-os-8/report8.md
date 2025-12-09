# 实验8：实验拓展：Copy-On-Write（COW）的实现

## 系统设计部分

### 系统架构部分

- 这个为拓展功能，主要实现在于文件的修改与功能提升，没有新增的文件
- kalloc.c：补充引用计数的kfree机制
- vm.c：实现COW的核心代码，处理页表分配
- proc.c：沿用lab5的内核线程代码对其进行绕开用户态的测试
- start.c：处理initproc


### 与 xv6 对比分析

- 这个实验就没有什么xv6的思路成分了……自己根据学过的知识来进行一次实践

## 实验过程部分

### 实验步骤

#### 1）修改kalloc.c实现引用计数

```c

void pmm_init(void){

    initlock(&kmem.lock, "kmem");
    initlock(&ref_Lock, "ref_count_lock");
    char *p = (char *)PGROUNDUP((uint64) end);//page align
    char *pa_end = (char *)PHYSTOP;
    for(;p + PGSIZE <= pa_end;p += PGSIZE){
        ref_count[(uint64)p / PGSIZE] = 1; 
        kfree(p);
    } 
}

void kfree(char *pa){
    struct page *r;
    if(((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP){
        printf("KFREE ERROR: pa=%x\n", pa);
        panic("kfree");
    }
    
    acquire(&ref_Lock);
    int idx = (uint64)pa / PGSIZE;
    if(ref_count[idx] <= 0){
        release(&ref_Lock);
        panic("kfree: ref_count <= 0");
    }

    ref_count[idx]--;

    if(ref_count[idx] > 0){
        // 还有引用，不能释放物理页
        release(&ref_Lock);
        return;
    }

    release(&ref_Lock);

    memset(pa, 1, PGSIZE);// fill with junk
    r = (struct page *)pa;

    acquire(&kmem.lock);
    r -> next = kmem.free_list;
    kmem.free_list = r;
    release(&kmem.lock);
}

```

- 这个文件的改进主要是在页表分配和释放的过程中添加了对引用计数的检测，对于还有子进程的父进程不进行释放，只有当引用计数归零后才对进程的页表等物理页释放

#### 2）修改vm.c

```c

// 处理 COW 分配
// 返回 0 成功，-1 失败
int cow_alloc(pagetable_t pagetable, uint64 va){
  uint64 pa;
  pte_t *pte;
  uint flags;
  char *mem;

  if(va >= MAXVA)
    return -1;

  pte = walk(pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
    return -1;

  // 如果不是 COW 页面，不需要处理（或者是非法访问）
  if(!(*pte & PTE_COW))
    return -1;

  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  // 分配新内存
  if((mem = alloc()) == 0)
    return -1;

  memmove(mem, (char *)pa, PGSIZE);

  // 修改页表项：指向新物理页，设置可写 (PTE_W)，清除 COW 标志
  flags = (flags | PTE_W) & (~PTE_COW);
  *pte = PA2PTE((uint64)mem) | flags;

  kfree((void*)pa);

  return 0;

}

// uvmcopy里增加对COW的复制检测

// COW 处理
    // 去掉写权限
    if(flags & PTE_W){
      flags &= ~PTE_W;
      flags |= PTE_COW;
      *pte = PA2PTE(pa) | flags;
    }

    // 映射到新页表
    if(mappages(new, i, PGSIZE, pa, flags) != 0){
      goto err;
    }

    kref_inc((void *)pa);//增加引用计数

// copyout的修改

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
  
    pte = walk(pagetable, va0, 0);

    // 1. 先处理 COW
    if(pte && (*pte & PTE_V) && (*pte & PTE_COW)) {
        if(cow_alloc(pagetable, va0) < 0)
            return -1;
        // cow_alloc 可能改变了 pte 指向的内容，重新获取 pte (虽然通常不需要，但为了保险)
        pte = walk(pagetable, va0, 0);
    }
    
    // 2. 处理完 COW 后，再获取物理地址
    // 这样保证 pa0 指向的是（可能新分配的）可写物理页
    pa0 = walkaddr(pagetable, va0);
    
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    // ...existing code...
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

```

- 这个文件主要是针对采用COW策略分配页表后对于页表间复制的判断和处理，首先是对于采取COW策略的物理页，需要去掉其写权限，保证内存隔离，对于单纯的copy只需要将其物理页映射到同一块物理内存，不需要任何操作，而在写入物理页时才进行新的alloc物理页

#### 3）proc.c实现内核线程测试函数

```c

void cow_child_entry(void) {
    struct proc *p = myproc();
    // 必须先释放锁（因为调度器切换过来时持有锁，就像 forkret 做的那样）
    release(&p->lock);

    printf("Child (pid %d) started in kernel mode\n", p->pid);
    
    uint64 va = 0x2000;
    int initial_value = 0x1234;
    int new_value = 0xDEAD;
    int val;

    // 3.1 读取数据
    if(copyin(p->pagetable, (char*)&val, va, sizeof(int)) < 0)
        panic("child: copyin failed");
    printf("Child: read value %x (expected %x)\n", val, initial_value);
    
    if(val != initial_value) 
        panic("child: initial value mismatch");

    // 3.2 写入数据，触发 COW
    printf("Child: attempting to write %x to va %x\n", new_value, va);
    
    // 这里 copyout 会触发 cow_alloc
    if(copyout(p->pagetable, va, (char*)&new_value, sizeof(int)) < 0)
        panic("child: copyout (COW write) failed");

    printf("Child: write success\n");

    // 3.3 再次读取确认
    if(copyin(p->pagetable, (char*)&val, va, sizeof(int)) < 0)
        panic("child: copyin failed");
    if(val != new_value) 
        panic("child: value not updated");

    printf("Child: exiting\n");
    kexit(0);
}

void cow_kernel_test(void){
    printf("\n=== Starting COW Kernel Test ===\n");

    struct proc *p = myproc();
    
    // 1. 为当前进程分配一页用户内存
    uint64 va = 0x2000;
    if(uvmalloc(p->pagetable, va, va + PGSIZE, PTE_W | PTE_R | PTE_U) == 0) {
        panic("cow_test: uvmalloc failed");
    }
    p->sz = va + PGSIZE;

    // 2. 写入初始数据
    int initial_value = 0x1234;
    if(copyout(p->pagetable, va, (char*)&initial_value, sizeof(int)) < 0) {
        panic("cow_test: copyout failed");
    }
    printf("Parent: wrote initial value %x to va %x\n", initial_value, va);

    // 3. Fork 子进程
    int pid = kfork();
    if(pid < 0) panic("cow_test: kfork failed");

    // 因为 kfork 默认让子进程去跑 forkret，我们需要把它改到 cow_child_entry
    if(pid > 0) {
        struct proc *child = 0;
        // 遍历进程表找到子进程
        for(struct proc *pp = proc; pp < &proc[NPROC]; pp++){
            acquire(&pp->lock);
            if(pp->pid == pid){
                child = pp;
                // 修改执行入口：不再去 forkret，而是去我们的测试函数
                child->context.ra = (uint64)cow_child_entry;
                release(&pp->lock);
                break;
            }
            release(&pp->lock);
        }
    }

    // 父进程逻辑
    int status;
    kwait((uint64)&status); // 等待子进程结束
    
    // 4. 检查父进程的数据是否被修改
    int val;
    if(copyin(p->pagetable, (char*)&val, va, sizeof(int)) < 0)
        panic("parent: copyin failed");
        
    printf("Parent: read value %x after child exit\n", val);
    
    if(val == 0xDEAD) {
        panic("TEST FAILED: Child modified Parent's memory! COW not working.");
    } else if(val == initial_value) {
        printf("TEST PASSED: Parent memory preserved.\n");
    } else {
        panic("TEST FAILED: Unexpected value in Parent.");
    }

    // 清理内存
    uvmdealloc(p->pagetable, va + PGSIZE, va);
    
    printf("=== COW Kernel Test Finished ===\n");
    for(;;) asm volatile("wfi");
}

```

- 这段代码主要是沿用了lab5的设计思路（没想到lab5改来改去的难看无比的fork机制这里还能用），在内核态创建内核线程并将这个测试函数挂载到这个内核线程中交给调度器中进行调度测试
- p->sz = va + PGSIZE;// 更新进程大小
- 这一行操作至关重要，因为lab5里面用的是内核线程跳板的方式来进行内核线程的挂载，但是这个设计在lab6中系统调用的initcode被占用了（当时为了测试直接把userinit写死成了lab6的测试函数），因此这里就必须要走forkret的路径，这里改变子进程的大小，使其不为0，是为了能够“欺骗”forkret，避开forkret的“首次运行检查”逻辑，不然会崩溃
- 同时还需要将子进程进行“劫持”，让其直接执行测试的入口函数，不走forkret这一个部分，否则也会产生一个PANIC！

#### 4）修改start.c

```c

extern struct proc *initproc;
    acquire(&initproc->lock);
    initproc->state = SLEEPING; // 防止 initproc 运行
    release(&initproc->lock);

    if(kthread_create(cow_kernel_test, "cow_test") < 0) {
        panic("failed to create cow test thread");
    }
    printf("COW test thread created.\n");
  
```

- 这里主要是因为initproc直接跟initcode写死绑定在一起了，如果不对initproc的状态作出限制，会导致initproc跟我测试函数创建的新内核线程发生竞争，而由于initcode里面有一个砍掉死循环的中断，一旦触发就会直接陷入PANIC，所有测试都无法进行，因此直接把initproc的状态变成SLEEPING，避免产生竞争。

### 源码理解总结

- 终于到最后一个实验了（泪流满面），这个学期对于xv6的学习和应用以及我自己的操作系统的构建过程确实不容易，但是在这个过程中，我对C语言和底层硬件的交互以及上个学期学到的操作系统设计哲学有了更深刻的理解和认识


## 测试验证部分

- 上一部分3）已经给出了所有的测试代码，make fs.img && make run运行即可

（运行结果附在实验文件中）