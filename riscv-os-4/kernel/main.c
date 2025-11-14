
#include "def.h"

void test_timer_interupt() {
    printf( "Testing timer interrupt...\n" );

    // 等待几个时钟中断
    for ( int i = 0; i < 5; i++ ) {
        printf("Waiting for timer interrupt %d...\n", i + 1);

        // 空循环，等待中断发生
        for (volatile int j = 0; j < 100000000; j++);
    }

    printf("Timer interrupt test completed.\n\n");
}

void test_exception() {
    printf( "Testing exception handling...\n" );

    // 故意触发一个非法指令异常
    asm volatile( ".word 0xFFFFFFFF" );

    // 这行代码不应该被执行到
    printf( "Exception test failed\n" );
}

//用于防止程序退出的自旋函数
void spin() {
    printf("Exception handler entered. Spinning...\n");
    while (1);
}

//这里就要使用int main()而不是void main()，因为start.c中需要判断中断后main()的返回值
int main() {
    printf("=== main() ===\n\n");

    printf("Running tests...\n\n");

    //test_timer_interupt();

    test_exception();

    printf("All tests passed.\n\n");

    printf("--- main() ---\n");

    return 0;
}
