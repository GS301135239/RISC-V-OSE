//uart.c

void uart_init();
void uart_putc(char c);
void uart_puts(const char *s);


//console接口层，现在改为调用接口层，由接口层调用串口
void console_init();
void console_putc(char c);
void console_puts(const char *s);

//print.c
void printf(const char *fmt, ...);
void clear_screen();
void clear_line();

//TestPrint.c
void test_basic();
void test_edge_cases();