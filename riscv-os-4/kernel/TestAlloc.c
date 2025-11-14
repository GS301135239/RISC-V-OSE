#include "def.h"
#include "type.h"

void test_physical_memory_allocator(){

    pmm_init();
    //basic alloc and free
    void *page1 = alloc();
    void *page2 = alloc();
    
    if(page1 != page2){
        printf("Physical memory allocator basic test passed.\n");
    }
    else{
        printf("Physical memory allocator basic test failed.\n");
        return;
    }

    *(int*) page1 = 0x12345678;//write test
    if(*(int*) page1 == 0x12345678){
        printf("Physical memory allocator write test passed.\n");
    }
    else{
        printf("Physical memory allocator write test failed.\n");
        return;
    }

    kfree(page1);
    void *page3 = alloc();

    kfree(page2);
    kfree(page3);
}