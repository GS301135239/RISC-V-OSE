#include "type.h"

struct proc;
struct context;
struct pagetable_t;
struct spinlock;

//uart.c

void uart_init();
void uart_putc(char c);
void uart_puts(const char *s);


//console接口层，现在改为调用接口层，由接口层调用串口
void console_init();
void console_putc(char c);
void console_puts(const char *s);

//print.c
void printf(const char *fmt, ...);
void clear_screen();
void clear_line();
void panic(const char *s);

//TestPrint.c
void test_basic();
void test_edge_cases();

//kalloc.c
void pmm_init(void);
void kfree(char *pa);
void* alloc(void);

//TestAlloc.c
void test_physical_memory_allocator();

//TestPagetable.c
void test_pagetable();

//TestVirtualMemory.c
void test_virtual_memory();

//string.c
void* memset(void *dst, int c, uint n);
int   memcmp(const void *v1, const void *v2, uint n);
void* memmove(void *dst, const void *src, uint n);
void* memcpy(void *dst, const void *src, uint n);
int   strncmp(const char *p, const char *q, uint n);
char* strncpy(char *s, const char *t, int n);
char* safestrcpy(char *s, const char *t, int n);
int   strlen(const char *s);

//vm.c
pagetable_t create_pagetable();
pte_t *walk_create(pagetable_t pagetable, uint64 va);
pte_t *walk_lookup(pagetable_t pagetable, uint64 va);
int   mappages(pagetable_t pagetable, uint64 va, uint64 size,uint64 pa, int perm);
void  destroy_pagetable(pagetable_t pagetable);
void  kvmmake(pagetable_t kpgtbl, uint64 va, uint64 pa,uint64 sz, int perm);
void  kvminit();
pagetable_t map_region();
void  kvminithart();
void  uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
void  uvmfree(pagetable_t pagetable, uint64 sz);
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm);
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len);
uint64 vmfault(pagetable_t pagetable, uint64 va, int read);
int ismapped(pagetable_t pagetable, uint64 va);
pte_t *walk(pagetable_t, uint64, int);
uint64 walkaddr(pagetable_t, uint64);
int copyinstr(pagetable_t, char * , uint64, uint64);

//trap.c
void trap_init();
void kernel_trap();
int  handle_device_intr( uint64 scause );
void handle_clock_intr();
void tarp_init_hart();
void prepare_return();
//void userret(uint64, uint64);

//proc.c
void proc_mapstacks(pagetable_t);
void procinit(void);
int  cpuid();
struct cpu* mycpu(void);
struct proc* myproc(void);
int  allocpid();
pagetable_t proc_pagetable(struct proc*);
void proc_freepagetable(pagetable_t, uint64);
void userinit(void);
int  growproc(int);
int kfork(void);
void reparent(struct proc*);
void kexit(int);
int kwait(uint64);
void scheduler(void);
void sched(void);
void yield(void);
void forkret(void);
void sleep(void*, struct spinlock*);
void wakeup(void*);
int kkill(int);
void setkilled(struct proc*);
int killed(struct proc*);
int either_copyout(int, uint64, void*, uint64);
int either_copyin(void*, int, uint64, uint64);
void procdump(void);
int kthread_create(void (*start)(void), const char *name);

//test.c
void run_all_tests();

//swtch.S
void swtch(struct context*, struct context*);

//spinlock.c
void initlock(struct spinlock *, char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int  holding(struct spinlock *);
void push_off();
void pop_off();

//syscall.c
int fetchaddr(uint64, uint64*);
int fetchstr(uint64, char*, int);
void argint(int, int*);
void argaddr(int, uint64*);
int argstr(int, char*, int);
void syscall();