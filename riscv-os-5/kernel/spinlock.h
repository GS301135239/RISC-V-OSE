#include "type.h"

struct spinlock{

    uint locked;
    char *name;

    struct cpu *cpu;// 记录持有锁的CPU
};