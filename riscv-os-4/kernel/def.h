#include "type.h"

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

//trap.c
void trap_init();
void kernel_trap();
int  handle_device_intr( uint64 scause );
void handle_clock_intr();
void tarp_init_hart();