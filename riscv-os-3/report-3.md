# 实验3：页表与内存管理

## 系统设计部分

### 系统架构部分


其中核心文件的作用如下：

- kalloc.c：提供一个组织内存空间并申请内存空间的物理内存管理器
- vm.c：虚拟内存管理器，提供由物理内存到虚拟内存的恒等映射、SV39地址解析、内核页表申请等功能
- riscv.h：与物理和虚拟内存相关的宏定义头文件，主要定义权限位、地址解析、提取等函数
- Test*.c：主要用于测试物理内存申请、虚拟内存启用等功能，提供逐步的功能测试
- string.c：仿照c语言标准库编写的方法库


### 与 xv6 对比分析

- 我设计的操作系统主要借鉴了xv6的设计思路，同时对于xv6的一些较为糅合的设计进行了拆分和简化，使得操作系统结构更加清晰，代码的可读性更强

## 实验过程部分

### 实验步骤

#### 1）实现 string.c

- 由于设计的操作系统是独立于运行环境的，是直接运行在硬件之中的，没有对应的底层操作系统提供系统调用，因此需要自己编写c语言的标准库，这里直接参考复制xv6提供的string.c方法库来实现memset、memcpy等基础功能

#### 2）实现 kalloc.c

```c

#include "def.h"
#include "type.h"
#include "riscv.h"

struct page{
    struct page *next;
};

struct {
    struct page *free_list;
} kmem;

extern char end[]; //kernel.ld中定义的end符号

void pmm_init(void){
    char *p = (char *)PGROUNDUP((uint64) end);//page align
    char *pa_end = (char *)PHYSTOP;
    for(;p + PGSIZE <= pa_end;p += PGSIZE){
        kfree(p);
    } 
}

void kfree(char *pa){
    struct page *r;
    if(((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP){
        printf("KFREE ERROR\n");
        return;
    }
    memset(pa, 1, PGSIZE);
    r = (struct page *)pa;
    r -> next = kmem.free_list;
    kmem.free_list = r;
}

void *alloc(void){
    struct page *r;
    r = kmem.free_list;
    if(r){
        kmem.free_list = r -> next;
        memset((char *)r, 5, PGSIZE);
    }

    return (void *)r;
}

```

- 这段代码仿照xv6进行设计，使用单向链表的简单设计来进行内存页的管理
- end符号为kernel.ld文件之中定义的符号，这里表示在text段、data段、bss段之后并与4096对齐后的起点，意思为可用物理内存的起点，再通过头插法清空并将end~KERNBASE之间的内存以分页形式用链表存储起来
- alloc函数在链表的头部取出一个物理页提供给调用者，并使用无用信息填满该物理页，将该物理页以(void*)的类型返回给调用者，保证调用者可以将其转换为任何想要的类型，有利于后面pagetable的扩展


#### 3）实现 vm.c

```c

pagetable_t kernel_pagetable;
extern char etext[];
pagetable_t create_pagetable(){
    pagetable_t pagetable;
    pagetable = (pagetable_t)alloc();
    if(pagetable == 0){
        return 0;//分配失败
    }
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

pte_t *walk_create(pagetable_t pagetable, uint64 va){

    if(va >= MAXVA){
        printf("WALK: virtual address out of range!\n");
        return 0;
    }

    for(int level = 2; level > 0; level--){
        pte_t *pte = &pagetable[PX(level, va)];
        if(*pte & PTE_V){
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else{
            //not exist, create a new page table
            if((pagetable = (pde_t*)alloc()) == 0){
                return 0;//Out of memory
            }
            memset(pagetable, 0, PGSIZE);//try to alloc a new page
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }

    return &pagetable[PX(0, va)];
}

pte_t *walk_lookup(pagetable_t pagetable, uint64 va){

    if(va >= MAXVA){
        printf("WALK: virtual address out of range!\n");
        return 0;
    }

    for(int level = 2; level > 0; level--){
        pte_t *pte = &pagetable[PX(level, va)];
        if(*pte & PTE_V){
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else{
            //read only, no create
            return 0;//no such mapping
        }
    }

    return &pagetable[PX(0, va)];
}

int mappages(pagetable_t pagetable, uint64 va, uint64 size,uint64 pa, int perm){

    uint64 a, last;
    pte_t *pte;

    if(va % PGSIZE || size % PGSIZE){
        printf("mappages: not aligned\n");
        return -1;
    }

    if(size == 0){
        printf("mappages: size is 0\n");
        return -1;
    }

    a = va;
    last = va + size - PGSIZE;

    while(1){
        if((pte = walk_create(pagetable, a)) == 0){
            return -1;//failed to create page table
        }
        if(*pte & PTE_V){
            printf("mappages: remap\n");
            return -1;//already mapped
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if(a == last) break;
        a += PGSIZE;
        pa += PGSIZE;//下一页
        //walk perpage
    }

    return 0;

}

void destroy_pagetable(pagetable_t pagetable){
    //9 bits index, 512 entries
    for(int i = 0;i < 512;i++){
        pte_t pte = pagetable[i];
        if((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0){
            pagetable_t child = (pagetable_t)PTE2PA(pte);
            destroy_pagetable(child);
            pagetable[i] = 0;
        }
        else if(pte & PTE_V){
            printf("freewalk: leaf pte\n");
        }
    }
    
    kfree((void *)pagetable);//free this page table
}

void kvmmake(pagetable_t kpgtbl, uint64 va, uint64 pa,uint64 sz, int perm){
    if(mappages(kpgtbl, va, sz, pa, perm) != 0){
        printf("kvmmap failed\n");
    }
}

pagetable_t map_region(){
    pagetable_t kpgtbl = create_pagetable();
    memset(kpgtbl, 0, PGSIZE);

    //uart
    kvmmake(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    //map kernel text segment, read-only
    kvmmake(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    //data
    kvmmake(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - ((uint64)etext), PTE_R | PTE_W);

    return kpgtbl;
}

void kvminit(){
    kernel_pagetable = map_region();
}

void kvminithart(){
    // switch to the kernel page table
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

```

- 这里的页表使用了树形结构，每一个节点都是一个物理页，这样保证了在遍历时的统一性，使用者只需使用pagetable_t一个类型就可以访问任何树状结构中的节点
- walk_create、walk_lookup是两个递归树状结构的函数，尝试将一个虚拟地址转换为物理地址，这两个函数最主要的遍历方式都是先通过解析SV39地址中的最高9位，逐级解析到最后一级，而对于访问到每一级的页表，都先与其标志位进行或运算，解析该页是否有效，若无效，则walk_create函数创建一个中间页表，walk_lookup函数则直接返回，实现了读写和只读功能的分离
- mappages函数则实现了从物理地址到虚拟地址的映射，通过PGSIZE将需要映射的内存区域分块，将其分页尝试映射为虚拟地址，在我的设计中，如果映射失败了，是直接返回失败的结果，不尝试修补，将修补交给调用者进行，体现了与硬件交互的简单原则
- destroy_pagetable函数则通过递归的方式，逐项扫描页表中的每一项，并将其设置为0，这里使用了一个来源于xv6的设计，就是对于非叶子结点，其R、W、X标志位至少有一个不为0，因此这里的递归出口很巧妙地设计在了一旦扫描到某个结点R、W、X标志位均为0，那一定是叶子结点，因此在此一定可以结束本次递归！
- map_region函数提供了从物理内存到虚拟内存的平滑过渡，这里使用了恒等映射的方案，是由于通过satp寄存器启用了分页之后，CPU获取的下一条指令地址会被当作虚拟地址来翻译，如果内核代码的虚拟地址和物理地址不相同的话，会导致CPU从一个错误的物理地址取出指令，造成运行崩溃。


### 源码理解总结

- 对于物理内存管理的源代码编写和学习，我认识到xv6设计的精妙之处，xv6的设计是完全基于内核级以及硬件的运行规律来进行编写的，同时在完全遵守底层硬件的运行规律的前提下还作出了大幅的简化，使得操作系统用最简单的代码和复杂度实现各种功能。



## 测试验证部分

编写 TestAlloc.c、TestPagetable.c、TestVirtualMemory.c ，在main.c中编译运行，测试结果如下：

（已经附在源代码中）