
#include "def.h"
#include "riscv.h"


//用于防止程序退出的自旋函数
void spin() {
    printf("Exception handler entered. Spinning...\n");
    while (1);
}

int main(void){
    // 不会被调用：启动路径经 mret -> call_main -> userinit -> scheduler
    printf("main(): unused (boot goes to call_main)\n");
    for(;;) asm volatile("wfi");
    return 0;
}
