#include "def.h"
#include "type.h"
#include "riscv.h"
#include "spinlock.h"

struct spinlock ref_Lock;//保护引用计数
int ref_count[PHYSTOP/PGSIZE];

struct page{
    struct page *next;
};

struct {
    struct spinlock lock;// 保护空闲链表
    struct page *free_list;
} kmem;

extern char end[]; //kernel.ld中定义的end符号

void kref_inc(void* pa){
    acquire(&ref_Lock);
    ref_count[(uint64)pa/PGSIZE]++;
    release(&ref_Lock);
}

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

void *alloc(void){
    struct page *r;

    acquire(&kmem.lock);

    r = kmem.free_list;
    if(r){
        kmem.free_list = r -> next;
    }

    release(&kmem.lock);

    if(r){
        memset((char *)r, 5, PGSIZE);// fill with junk

        // 初始化引用计数为1
        acquire(&ref_Lock);
        ref_count[(uint64)r / PGSIZE] = 1;
        release(&ref_Lock);
    }

    return (void *)r;
}
