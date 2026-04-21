int uart_init(unsigned long uart_address);
char uart_getc();
void uart_putc(char c);
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_dec(unsigned long h);
char uart_getc_raw();
