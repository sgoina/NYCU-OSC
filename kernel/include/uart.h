void uart_init(unsigned long dtb_ptr);
char uart_getc();
void uart_putc(char c);
void uart_puts(const char* s);
void uart_hex(unsigned long h);
void uart_dec(unsigned long h);
void handle_uart_interrupt();
