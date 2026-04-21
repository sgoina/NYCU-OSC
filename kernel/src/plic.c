#include "plic.h"
#include "deviceTree.h"

unsigned int uart_irq = 0;
unsigned long plic_base = 0;

#define PLIC_PRIORITY(irq)   (volatile unsigned int*)(plic_base + (irq) * 4)
#define PLIC_ENABLE(hart)    (volatile unsigned int*)(plic_base + 0x002080 + (hart) * 0x0100)
#define PLIC_THRESHOLD(hart) (volatile unsigned int*)(plic_base + 0x201000 + (hart) * 0x2000)
#define PLIC_CLAIM(hart)     (volatile unsigned int*)(plic_base + 0x201004 + (hart) * 0x2000)

extern unsigned long boot_cpu_hartid;

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
    // 1. 設定 UART 中斷優先級為 1 (大於 0 即可)
    *PLIC_PRIORITY(uart_irq) = 1;
    
    // 2. 為目前的 Hart 啟用 UART 中斷
    // ⭐  支援大於 31 的 IRQ
    volatile unsigned int *enable_array = PLIC_ENABLE(boot_cpu_hartid);
    int word_index = uart_irq / 32;   // 算出在哪個 32-bit 暫存器 (42 / 32 = 1)
    int bit_offset = uart_irq % 32;   // 算出在該暫存器的第幾個 bit (42 % 32 = 10)
    enable_array[word_index] |= (1 << bit_offset);
    
    // 3. 設定閾值為 0 (接受所有優先級 > 0 的中斷)
    *PLIC_THRESHOLD(boot_cpu_hartid) = 0;
    
    // 4. 開啟 Supervisor External Interrupt (sie.SEIE, bit 9)
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 9);
    asm volatile("csrw sie, %0" : : "r"(sie));
}

int plic_claim() {
    // TODO: Implement this function
    return *(volatile unsigned int*)PLIC_CLAIM(boot_cpu_hartid);
}

void plic_complete(int irq) {
    // TODO: Implement this function
    *(volatile unsigned int*)PLIC_CLAIM(boot_cpu_hartid) = irq;
}
