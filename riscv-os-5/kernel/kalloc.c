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
