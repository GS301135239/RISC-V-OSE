# 实验2：内核printf与清屏功能实现

## 系统设计部分

### 系统架构部分


其中核心文件的作用如下：

- print.c：处理字符串、数字的解析和输出，提供一个统一的接口printf()方法
- console.c：uart.c和print.c之间的中间层，提供线程安全的调用uart.c层的方法，同时仿照xv6处理退格等字符
- TestPrint.c：测试类文件，供main.c调用


### 与 xv6 对比分析

- 我设计的操作系统将字符串解析的步骤进行了简化，暂时做到支持`%`后一位的转义字符的解析

## 实验过程部分

### 实验步骤

#### 1）实现 print.c

```c

#include<stdarg.h>
#include "def.h"

static char digits[] = "0123456789ABCDEF";

static void print_Number(int num, int base){

    if(num == 0){
        console_putc('0');
        return;
    }

    unsigned int n;

    if(num < 0){
        console_putc('-');
        n = -num;
    }
    else{
        n = num;
    }

    char buf[32];//Int to String
    int i = 0;

    while(n){
        buf[i] = digits[n % base];
        n /= base;
        i++;
    }

    for(int j = i - 1;j >= 0;j--){
        console_putc(buf[j]);
    }//逆序输出

    return;
}

void printf(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);

    for(int i = 0; fmt[i] != '\0';i++){
        if(fmt[i] != '%'){
            console_putc(fmt[i]);
            continue;
        }

        if(fmt[i + 1] == '\0') break;

        if(fmt[i] == '%'){

            if(fmt[i + 1] == 'd'){
                int num = va_arg(ap, int);
                print_Number(num, 10);
                i++;
            }
            else if(fmt[i + 1] == 'x'){
                int num = va_arg(ap, int);
                print_Number(num, 16);
                i++;
            }
            else if(fmt[i + 1] == 'c'){
                char c = (char)va_arg(ap, int);//char will be promoted to int
                console_putc(c);
                i++;
            }
            else if(fmt[i + 1] == 's'){
                char *s = va_arg(ap, char *);
                if(s == 0) s = "NULL POINTER!";
                else if(s[0] == '\0') s = "EMPTY STRING!";
                console_puts(s);
                i++;
            }
            else if(fmt[i + 1] == '%'){
                console_putc('%');
                i++;
            }
            else{
                console_putc(fmt[i]);// no matching, print '%'
            }
        }
    }

    va_end(ap);

    return;
}

void clear_screen(){
    console_puts("\033[2J\033[H");//清屏并将光标移动到左上角
}

void clear_line(){
    console_puts("\033[K");//清除从光标位置到行尾的内容
}

```

- 这段代码通过模仿xv6的解析方式，将实数转化成字符串再进行逆序输出
- 针对INT_MIN取相反数之后会出现溢出的问题，我设计的解决方案与xv6相似，就是用可以表示更大正数范围的unsigned型变量来承接INT_MIN取相反数之后的结果，保证了值的正确性
- printf函数中当扫描到`%c`时，将`int`类型的传入变量映射到`char`类型适应console_putc()函数的需要
- clear的清屏函数则通过发送转义字符来实现


#### 2）实现 console.c

```c

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

```

- 这里实现了一个简单的自旋锁机制，保证线程安全
- console_putc()函数使用了xv6同款机制来处理退格字符的场景
- 通过在uart.c和print.c之间构建中间层console.c层，使得print.c可以直接使用console.c层的接口来在控制台中输出内容


### 源码理解总结

- 通过简单的控制台输出代码的学习和编写，我对操作系统设计的一些保证数据完整性、安全性的想法有了一定的理解
- 同时我也理解到了操作系统的构建本质上是数据结构和算法的实际运用，如何用高效的算法和数据结构优化和提升效率是操作系统进步所必需要钻研的课题



## 测试验证部分

编写 TestPrint.c ，在main.c中编译运行，测试结果如下：

（已经附在源代码中）