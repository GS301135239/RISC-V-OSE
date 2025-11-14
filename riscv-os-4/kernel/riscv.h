#define PGSIZE 4096 // bytes per page
#define PGSHIFT 12  // bits of offset within a page

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

#define PTE_V (1L << 0) // valid
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4) // user can access

// shift a physical address to the right place for a PTE.
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// extract the three 9-bit page table indices from a virtual address.
#define PXMASK          0x1FF // 9 bits
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA is actually one bit less than the max allowed by
// Sv39, to avoid having to sign-extend virtual addresses
// that have the high bit set.
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))

#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)
#define UART0 0x10000000L
#define MAKE_SATP(pagetable) ((8L << 60) | (((uint64)pagetable) >> 12))

#define MSTATUS_MPP_MASK (3L << 11) // previous mode.
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)

void write_mstatus( uint64 x );
uint64 read_mstatus();

void write_satp( uint64 x );

void write_mideleg( uint64 x );

void write_medeleg( uint64 x );

#define SIE_SEIE (1L << 9) // external
#define SIE_STIE (1L << 5) // timer

void write_sie( uint64 x );
uint64 read_sie();

// Machine-mode Interrupt Enable
#define MIE_STIE (1L << 5)  // supervisor timer

void write_mie( uint64 x );
uint64 read_mie();

void write_mtvec( uint64 x );

void write_stvec( uint64 x );

void write_sepc( uint64 x );
uint64 read_sepc();

void write_stval( uint64 x );
uint64 read_stval();

#define SSTATUS_SPP (1L << 8)  // Previous mode, 1=Supervisor, 0=User
#define SSTATUS_SPIE (1L << 5) // Supervisor Previous Interrupt Enable
#define SSTATUS_UPIE (1L << 4) // User Previous Interrupt Enable
#define SSTATUS_SIE (1L << 1)  // Supervisor Interrupt Enable
#define SSTATUS_UIE (1L << 0)  // User Interrupt Enable

void write_sstatus( uint64 x );
uint64 read_sstatus();

int is_interupt_on();

void write_scause( uint64 x );
uint64 read_scause();

uint64 read_mcause();

uint64 read_time();

void write_stimecmp( uint64 x );
uint64 read_stimecmp();

void write_mcounteren( uint64 x );
uint64 read_mcounteren();

void write_menvcfg( uint64 x );
uint64 read_menvcfg();

void write_mepc( uint64 x );

void write_pmpaddr0( uint64 x );

void write_pmpcfg0( uint64 x );

void write_pmpaddr1( uint64 x );

void write_pmpcfg1( uint64 x );

uint64 read_sp();

void sfence_vma();