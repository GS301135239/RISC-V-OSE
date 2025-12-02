
void uart_init();
void uart_putc(char c);
void uart_puts(const char *s);
//console接口层，现在改为调用接口层，由接口层调用串口
void console_init();
void console_putc(char c);
void console_puts(const char *s);

void main() {

    uart_init();

    uart_puts("Hello, World!\n");

    while (1)
        ;
}