#include<stdarg.h>
#include "def.h"
#include "type.h"

static char digits[] = "0123456789ABCDEF";

static void print_Number(int num, int base){

    if(num == 0){
        console_putc('0');
        return;
    }

    uint32 n;

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