#include "shell.h"
#include "uart.h"
#include "deviceTree.h"
#include "defint.h"

// a0 = hartid, a1 = dtb_ptr
void start_boot_loader(unsigned long hartid, unsigned long dtb_ptr) {
    int uart_base_offset = fdt_path_offset(dtb_ptr, "/soc/serial");
    if (uart_base_offset != -1){
        int len = 0;
        const void* prop = fdt_getprop(dtb_ptr, uart_base_offset, "reg", &len);
        if (prop != NULL){
            const uint32_t* reg = (const uint32_t*)prop;    
            uint32_t uart_reg = bswap32(reg[1]);
            uart_init(uart_reg);
            uart_puts("\nOSC 314553022\n");
            uart_puts("\nThis is a boot loader!\n");
            start_shell(hartid, dtb_ptr, uart_reg);
        }
    }
}
