#define UART0 0x10000000L

#define THR 0 
#define IER 1 
#define FCR 2 
#define LCR 3 
#define LSR 5 

#define LCR_EIGHT_BITS (3 << 0)
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR (3 << 1) 
#define LSR_TX_IDLE (1 << 5)

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define WriteReg(reg, val) (*(Reg(reg)) = (val))
#define ReadReg(reg) (*(Reg(reg)))

void uart_init() {
    WriteReg(IER, 0x00);

    WriteReg(LCR, LCR_EIGHT_BITS);

    WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
}

void uart_putc(char c) {
    while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
        ;// 空转，等待硬件准备好
    WriteReg(THR, c);
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

