#include "uart.h"

static int console_lock = 0;// 简单的自旋锁

static void acquire_lock(){

    console_lock = 1;
}

static void release_lock(){

    console_lock = 0;
}

//暂时输出单个字符到控制台
void console_putc(char c){

    if(c == '\b'){
        uart_putc('\b');
        uart_putc(' ');
        uart_putc('\b');
        //仿照xv6的退格符处理
    }

    else{
        uart_putc(c);
    }
}

void console_puts(const char *s){

    acquire_lock();

    while(*s){
        console_putc(*s);
        s++;
    }

    release_lock();
}

void console_init(){

    uart_init();//Test Temporarily
    console_lock = 0;

}