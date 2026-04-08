#include "shell.h"
#include "deviceTree.h"
#include "uart.h"
#include "ramfs.h"
#include "mem_alloc.h"

extern char _start[];
extern char _end[];

void start_main(unsigned long hartid, unsigned long dtb_ptr) {
    if (uart_init(dtb_ptr) != -1){
        uart_puts("New Kernel! New Kernel! New Kernel! New Kernel!\n");
        if (initrd_addr(dtb_ptr) != -1)
            uart_puts("File system initialization is successful!\n");
        else
            uart_puts("Can't initialize file system!\n");
    }
    
    init_mem(dtb_ptr);
    
    const struct fdt_header *header = (const struct fdt_header *)dtb_ptr;
    unsigned long dtb_size = bswap32(header->totalsize);
    uart_puts("Reserve DTB Blob, ");
    memory_reserve((uint64_t)header, dtb_size);
    show_mem_alloc();
    
    uint64_t kernel_start = (uint64_t)_start;
    uint64_t kernel_end   = (uint64_t)_end;
    
    if (kernel_start < kernel_end) {
        uint64_t kernel_size = kernel_end - kernel_start;
        
        uart_puts("Reserve kernel, ");
        
        memory_reserve(kernel_start, kernel_size);
        show_mem_alloc();
    } else {
        uart_puts("Error: Kernel end is not greater than start.\n");
    }
    
    int ramfs_offset = fdt_path_offset(dtb_ptr, "/chosen");
    if (ramfs_offset != -1){
        int len1 = 0;
        int len2 = 0;
        const void* start_prop = fdt_getprop(dtb_ptr, ramfs_offset, "linux,initrd-start", &len1);
        const void* end_prop = fdt_getprop(dtb_ptr, ramfs_offset, "linux,initrd-end", &len2);
        
        if (start_prop != NULL && end_prop != NULL){
            uint64_t initrd_start = 0;
            uint64_t initrd_end = 0;

            if (len1 == 8 && len2 == 8) {
                // 64-bit
                initrd_start = bswap64(*(const uint64_t*)start_prop);
                initrd_end   = bswap64(*(const uint64_t*)end_prop);
            } else if (len1 == 4 && len2 == 4) {
                // 32-bit
                initrd_start = bswap32(*(const uint32_t*)start_prop);
                initrd_end   = bswap32(*(const uint32_t*)end_prop);
            } else {
                uart_puts("Warning: Unexpected initrd property length.\n");
            }

            uart_puts("Initrd start: ");
            uart_hex(initrd_start);
            uart_putc('\n');
            
            uart_puts("Initrd end: ");
            uart_hex(initrd_end);
            uart_putc('\n');

            // 確保有抓到有效的範圍，才進行 Reserve
            if (initrd_start < initrd_end) {
                uint64_t ramfs_size = initrd_end - initrd_start;
                uart_puts("Reserve ramfs, begin: ");
                uart_hex(initrd_start);
                uart_puts(", end: ");
                uart_hex(initrd_end);
                uart_putc('\n');
                memory_reserve(initrd_start, ramfs_size);
                show_mem_alloc();
            } else {
                uart_puts("Error: Initramfs end is not greater than start.\n");
            }
        }
    }
    
    fdt_reserve_memory_nodes(dtb_ptr);
    show_mem_alloc();
    
    start_kernel_shell(hartid, dtb_ptr);
}
