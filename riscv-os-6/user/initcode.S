// 简单 fork/wait/exit 测试：循环 3 次
// 第 i 次：child 直接 exit(10+i)，parent wait(&status) 并校验 pid/status。
// 通过后进入 getpid 循环，保持 init 不退出。

#include "syscall.h"     // 需要包含 SYS_fork / SYS_wait / SYS_exit / SYS_getpid 宏

  .section .text
  .globl _start
_start:
    li     s0, 0          // i = 0
    li     s1, 3          // N = 3

1:  # fork
    li     a7, SYS_fork
    ecall
    beqz   a0, 2f         # a0==0 => child path

    # parent path
    mv     s2, a0         # 记录子进程 pid

    addi   sp, sp, -16    # 为 status 预留 16 字节
    mv     a0, sp         # a0 = &status
    li     a7, SYS_wait
    ecall                  # 返回值 a0 = 子 pid 或 -1

    mv     t3, a0         # t3 = returned pid
    lw     t4, 0(sp)      # t4 = status
    addi   sp, sp, 16

    # 校验 pid
    bne    t3, s2, 3f     # mismatch => fail

    # 期望的 status = 10 + i
    li     t0, 10
    add    t0, t0, s0
    bne    t4, t0, 3f     # mismatch => fail

    addi   s0, s0, 1
    blt    s0, s1, 1b     # 下一轮
    j      4f             # 全部通过

2:  # child path: exit(10 + i)
    li     t0, 10
    add    t0, t0, s0     # t0 = 10 + i
    mv     a0, t0
    li     a7, SYS_exit
    ecall
    j      2b             # 不应返回，若返回则自旋

3:  # fail 分支：保持存活（避免 init 退出导致 panic）
    li     a7, SYS_getpid
    ecall
    j      3b

4:  # pass 分支：保持存活
    li     a7, SYS_getpid
    ecall
    j      4b