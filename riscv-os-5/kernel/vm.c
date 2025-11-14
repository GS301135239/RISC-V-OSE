#include "proc.h"
#include "type.h"
#include "riscv.h"
#include "def.h"

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
    write_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

pte_t *walk(pagetable_t pagetable, uint64 va, int kalloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!kalloc || (pagetable = (pde_t*)alloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

uint64 walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;   
    if((*pte & PTE_V) == 0)  // has physical page been allocated?
      continue;
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}



void uvmfree(pagetable_t pagetable, uint64 sz) {
    if(sz > 0){
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    }
    destroy_pagetable(pagetable);
}

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