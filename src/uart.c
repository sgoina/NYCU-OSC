#include "uart.h"
#include "defint.h"
#include "deviceTree.h"

unsigned long uart_base_addr = 0;

#define UART_RBR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Receive Buffer Register
#define UART_THR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Transmit Holding Register
#define UART_LSR  (volatile unsigned char*)(uart_base_addr + 0x14) // Line Status Register
#define LSR_DR    (1 << 0) // Data Ready (In Receive Buffer Register)
#define LSR_TDRQ  (1 << 5) // Transmit Data Request (Transferred from the Transmit Holding Register)

// setup uart_base_addr
int uart_init(unsigned long dtb_ptr) {
    int uart_base_offset = fdt_path_offset(dtb_ptr, "/soc/serial");
    if (uart_base_offset == -1)
        return -1;
    int len = 0;
    const void* prop = fdt_getprop(dtb_ptr, uart_base_offset, "reg", &len);
    if (prop != NULL){
        const uint32_t* reg = (const uint32_t*)prop;    
        uint32_t uart_reg = bswap32(reg[1]);
        uart_base_addr = uart_reg;
    }
    else
        return -1;
    return 0;
}

// Read input from uart
char uart_getc() {
    // When data is in Receive Buffer Register
    while ((*UART_LSR & LSR_DR) == 0) {

    }
    // Get the character
    char c = *UART_RBR;
    
    if (c == '\r') 
        return '\n';
    return c;
}

// Print a character by uart
void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');
    // When wanting to transmit data
    while ((*UART_LSR & LSR_TDRQ) == 0) {

    }
    // Set the data in Transmit Holding Register
    *UART_THR = c;
}

// Print string by uart
void uart_puts(const char* s) {
    while (*s != '\0') {
        uart_putc(*s);
        s++; 
    }
}

// Print hex number by uart
void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}

// Print decimal number by uart
void uart_dec(unsigned long h) {
    char buf[30];
    int cnt = 0;
    if (h == 0) {
        uart_putc('0');
        return;
    }
    while (h){
        buf[cnt++] = '0' + (h % 10);
        h /= 10;
    }
    for (int i = cnt - 1; i >= 0; i--){
        uart_putc(buf[i]);
    }
}

// Get the data without handling by uart
char uart_getc_raw() {
    while ((*UART_LSR & LSR_DR) == 0);
    return (char)*UART_RBR;
}
