#include "plic.h"
#include "deviceTree.h"

unsigned int uart_irq = 0;
unsigned long plic_base = 0;

// each hart has 2 context (M-mode and S-mode)
#define PLIC_PRIORITY(irq)   (volatile unsigned int*)(plic_base + (irq) * 4) // every interrupt have 4 bytes to save priority and start from plic_base
 // 0x2000 + (2*hart + 1) * 0x80, each context has 1K bits for interrupt enables, and start from plic_base + 0x2000
#define PLIC_ENABLE(hart)    (volatile unsigned int*)(plic_base + 0x002080 + (hart) * 0x0100)
// each context has 4K bytes for priority threshold and start from plic_base + 0x200000
#define PLIC_THRESHOLD(hart) (volatile unsigned int*)(plic_base + 0x201000 + (hart) * 0x2000)
// each context has 4K bytes for Interrupt Claim Process and start from plic_base + 0x200004
#define PLIC_CLAIM(hart)     (volatile unsigned int*)(plic_base + 0x201004 + (hart) * 0x2000)

extern unsigned long boot_cpu_hartid; // from main.c

void plic_init(unsigned long dtb_ptr) {
    // Find uart interrupt code in device tree
    int uart_offset = fdt_path_offset(dtb_ptr, "/soc/serial");
    if (uart_offset == -1)
        return;
    int len = 0;
    const void* prop = fdt_getprop(dtb_ptr, uart_offset, "interrupts", &len);
    if (prop != NULL){
        const uint32_t* irq = (const uint32_t*)prop;
        uart_irq = bswap32(*irq);
    }
    else
        return;
        
    // Find plic base address in device tree
    int plic_offset = fdt_path_offset(dtb_ptr, "/soc/interrupt-controller");
    if (plic_offset == -1)
        return;
    len = 0;
    prop = fdt_getprop(dtb_ptr, plic_offset, "reg", &len);
    if (prop != NULL){
        const uint32_t* reg = (const uint32_t*)prop;    
        uint32_t plic_reg = bswap32(reg[1]);
        plic_base = plic_reg;
    }
    else
        return;
        
    // Set the priority of UART_IRQ to 1 (greater than 0 to enable it)
    *PLIC_PRIORITY(uart_irq) = 1;
    
    // Enable the UART_IRQ for the boot hart
    volatile unsigned int *enable_array = PLIC_ENABLE(boot_cpu_hartid);
    // over 32 bits, needs to convert
    int word_index = uart_irq / 32;
    int bit_offset = uart_irq % 32;
    enable_array[word_index] |= (1 << bit_offset);
    
    // Set the priority threshold of the boot hart to 0 (accepting all active interrupts).
    *PLIC_THRESHOLD(boot_cpu_hartid) = 0;
    
    // The 10th bit of sie => SEIE (Supervisor External Interrupts Enable)
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 9);
    asm volatile("csrw sie, %0" : : "r"(sie));
}

// Get the irq number
int plic_claim() {
    return *(volatile unsigned int*)PLIC_CLAIM(boot_cpu_hartid);
}
// Write irq number back to PLIC CLAIM register to unlock gateway for future interrupt from this source 
void plic_complete(int irq) {
    *(volatile unsigned int*)PLIC_CLAIM(boot_cpu_hartid) = irq;
}
