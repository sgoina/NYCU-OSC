#include "uart.h"
#include "deviceTree.h"
#include "ring_buffer.h"
#include "thread.h"

static unsigned long uart_base_addr = 0;

#define UART_RBR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Receive Buffer Register
#define UART_THR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Transmit Holding Register
#define UART_IER  (volatile unsigned char*)(uart_base_addr + 0x4)  // Interrupt Enable Register
#define UART_IIR  (volatile unsigned char*)(uart_base_addr + 0x8)  // Interrupt Identification Register
#define UART_MCR  (volatile unsigned char*)(uart_base_addr + 0x10) // Modem Control Register
#define UART_LSR  (volatile unsigned char*)(uart_base_addr + 0x14) // Line Status Register

#define LSR_DR    (1 << 0) // Data Ready (In Receive Buffer Register)
#define LSR_TDRQ  (1 << 5) // Transmit Data Request (Transferred from the Transmit Holding Register)

RingBuffer rx_buf = { .head = 0, .tail = 0 };
RingBuffer tx_buf = { .head = 0, .tail = 0 };

void uart_init(unsigned long dtb_ptr) {
    int uart_base_offset = fdt_path_offset(dtb_ptr, "/soc/serial"); // find the offset of "/soc/serial" in the device tree
    if (uart_base_offset == -1)
        return;
    int len = 0;
    const void* prop = fdt_getprop(dtb_ptr, uart_base_offset, "reg", &len); // find the base address of uart
    if (prop != NULL){
        const uint32_t* reg = (const uint32_t*)prop;    
        uint32_t uart_reg = bswap32(reg[1]);
        uart_base_addr = uart_reg;
    }
    else
        return;
    // Enable RX Interrupt
    *UART_IER |= (1 << 0);
    // MCR bit 3 (OUT2) is 1 => UART interrupt is enabled
    *UART_MCR |= (1 << 3);
    return;
}
// Get a char from UART
char uart_getc() {
    while (is_empty(&rx_buf)) {
        //asm volatile("wfi");
        schedule();
    }
    
    // Critical Section
    unsigned long sstatus;
    // csrrci: read the CSR and clear the specific bit with immediate number
    // The 2nd bit in sstatus => SIE (enables or disables interrupts)
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus)); 
    char c = pop(&rx_buf);
    // csrs : CSR set the specific bits with a variable
    // return the sstatus
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
    return c;
}
// Print a char
void uart_putc(char c) {
    if (c == '\n') 
        uart_putc('\r'); 
    // When TX buffer is full, waiting
    while (is_full(&tx_buf)) {
        
    }
    // Critical Section
    unsigned long sstatus;
    // csrrci: read the CSR and clear the specific bits with an immediate number
    // The 2nd bit in sstatus => SIE (enables or disables interrupts)
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    push(&tx_buf, c);
    // Set the 2nd bit in IER => TIE (Transmit Data Request Interrupt Enable)
    *UART_IER |= (1 << 1); 
    // csrs : CSR set the specific bits with a variable
    // return the sstatus
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}
// Print a string
void uart_puts(const char* s) {
    while (*s) uart_putc(*s++);
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
// Call from do_trap in trap.c
void handle_uart_interrupt() {
    unsigned char lsr = *UART_LSR;
    // If RX interrupt
    if (lsr & LSR_DR) {
        char c = *UART_RBR;
        push(&rx_buf, c);
    }
    // If TX interrupt
    if (lsr & LSR_TDRQ) {
        // Print if buffer isn't empty
        if (!is_empty(&tx_buf))
            *UART_THR = pop(&tx_buf);
        else
            *UART_IER &= ~(1 << 1); // Clear the 2nd bit in IER => TIE (Transmit Data Request Interrupt Enable)
    }
}
