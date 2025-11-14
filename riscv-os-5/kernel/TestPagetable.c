#include "type.h"
#include "def.h"
#include "riscv.h"

void test_pagetable(){

    pmm_init();
    pagetable_t pagetable = create_pagetable();
    printf(pagetable == 0 ? "create_pagetable failed\n" : "create_pagetable success\n");

    uint64 va = 0x1000000;
    uint64 pa = (uint64)alloc();

    if(mappages(pagetable, va, PGSIZE, pa, PTE_R | PTE_W) == 0){
        printf("mappages success\n");
    }
    else{
        printf("mappages failed\n");
    }

    pte_t *pte = walk_lookup(pagetable, va);
    if(pte != 0 && (*pte & PTE_V) && PTE2PA(*pte) == pa){
        printf("walk_lookup success\n");
    }
    else{
        printf("walk_lookup failed\n");
    }

    if(*pte & PTE_R){
        printf("Read permission check success\n");
    }
    else{
        printf("Read permission check failed\n");
    }

    if(*pte & PTE_W){
        printf("Write permission check success\n");
    }
    else{
        printf("Write permission check failed\n");
    }

    if(!(*pte & PTE_X)){
        printf("Execute permission check success\n");
    }
    else{
        printf("Execute permission check failed\n");
    }

}