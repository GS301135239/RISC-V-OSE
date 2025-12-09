#include "type.h"
#include "def.h"
#include "riscv.h"

extern int main(void);
void test_kernel_memory();

void test_virtual_memory(){
    pmm_init();
    
    printf("Before enabling virtual memory\n");

    kvminit();

    kvminithart();

    printf("After enabling virtual memory\n");

    printf("Testing kernel memory still works...\n");

    test_kernel_memory();
}

void test_kernel_memory(){

    // 测试 1: 控制台输出 (测试 UART 映射)
  printf("Test 1: Console output via UART. If you see this, UART mapping is OK.\n");

  // 测试 2: 访问内核代码 (测试 .text 映射)
  printf("Test 2: Accessing kernel code. Address of main() is %x\n", main);

  // 测试 3: 访问和修改内核数据 (测试 .data/.bss 映射)
  static int test_data = 123; // 这是一个在 .data 或 .bss 段的变量
  printf("Test 3: Accessing kernel data. Initial value: %d\n", test_data);
  test_data = 456;
  if(test_data == 456) {
    printf("Test 3: Data write successful. New value: %d\n", test_data);
  } else {
    printf("Test 3: FATAL: Data write FAILED!\n");
  }

}