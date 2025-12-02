#include "type.h"
#include "def.h"
#include "riscv.h"
#include "proc.h"

// 1) 进程创建与退出测试（内核线程语义）

static void child_exit_entry(void) {
    printf("fork_exit_test: child running, will exit(0)\n");
    kexit(0);
}

void fork_exit_test() {
    printf("=== fork_exit_test start ===\n");
    int pid = kthread_create(child_exit_entry, "child_exit");
    if (pid < 0) {
        printf("fork_exit_test: create failed\n");
    } else {
        printf("fork_exit_test: parent created pid=%d, wait...\n", pid);
        int reaped = kwait(0);
        printf("fork_exit_test: kwait collected pid=%d\n", reaped);
    }
    printf("=== fork_exit_test end ===\n\n");
}

// 2) 调度/让出CPU测试（多线程轮转）

struct yield_arg {
    int id;
    int loops;
};

static struct yield_arg yargs[4];

static void yield_worker0(void) {
    struct yield_arg *a = &yargs[0];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}
static void yield_worker1(void) {
    struct yield_arg *a = &yargs[1];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}
static void yield_worker2(void) {
    struct yield_arg *a = &yargs[2];
    for (int i = 0; i < a->loops; i++) {
        printf("yield_test: worker %d iter %d\n", a->id, i+1);
        yield();
    }
    kexit(0);
}

void yield_test() {
    printf("=== yield_test start ===\n");

    const int loops = 3;
    for (int i = 0; i < 3; i++) {
        yargs[i].id = i;
        yargs[i].loops = loops;
    }

    int p0 = kthread_create(yield_worker0, "yield0");
    int p1 = kthread_create(yield_worker1, "yield1");
    int p2 = kthread_create(yield_worker2, "yield2");

    if (p0 < 0 || p1 < 0 || p2 < 0) {
        printf("yield_test: create failed p0=%d p1=%d p2=%d\n", p0, p1, p2);
    }

    // 等待全部线程退出
    kwait(0);
    kwait(0);
    kwait(0);

    printf("=== yield_test end ===\n\n");
}

// 3) 生产者-消费者（单元素缓冲区 + sleep/wakeup）

static int buffer = 0;
static int buffer_full = 0;
static struct spinlock buf_lock;

#define BUF_ITERS 5

static void producer_entry(void) {
    printf("producer: start\n");
    for (int i = 0; i < BUF_ITERS; i++) {
        acquire(&buf_lock);
        while (buffer_full) {
            // sleep 会在内部释放 buf_lock，被唤醒后重新获取
            sleep((void*)&buffer_full, &buf_lock);
        }
        buffer = i + 1;
        buffer_full = 1;
        printf("producer: produced %d\n", buffer);
        wakeup((void*)&buffer_full); // 唤醒消费者
        release(&buf_lock);
        yield(); // 让消费者更快拿到CPU
    }
    printf("producer: finished\n");
    kexit(0);
}

static void consumer_entry(void) {
    printf("consumer: start\n");
    for (int i = 0; i < BUF_ITERS; i++) {
        acquire(&buf_lock);
        while (!buffer_full) {
            sleep((void*)&buffer_full, &buf_lock);
        }
        printf("consumer: consumed %d\n", buffer);
        buffer_full = 0;
        wakeup((void*)&buffer_full); // 唤醒生产者
        release(&buf_lock);
        yield();
    }
    printf("consumer: finished\n");
    kexit(0);
}

void producer_consumer_test() {
    printf("=== producer_consumer_test start ===\n");
    buffer = 0;
    buffer_full = 0;
    initlock(&buf_lock, "buf_lock");

    int pp = kthread_create(producer_entry, "producer");
    int pc = kthread_create(consumer_entry, "consumer");

    if (pp < 0 || pc < 0) {
        printf("producer_consumer_test: create failed p=%d c=%d\n", pp, pc);
    }

    kwait(0);
    kwait(0);

    printf("=== producer_consumer_test end ===\n\n");
}


void run_all_tests() {
    printf("=== run_all_tests begin ===\n");

    fork_exit_test();
    yield_test();
    producer_consumer_test();

    printf("All tests finished. System halt.\n");
    for (;;)
        asm volatile("wfi");
}