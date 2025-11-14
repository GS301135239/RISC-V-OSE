#include "def.h"
#include "type.h"
#include "riscv.h"


pagetable_t kernel_pagetable;
extern char etext[];//boundary of kernel text segment, defined in kernel.ld

//创建一个新的页表
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