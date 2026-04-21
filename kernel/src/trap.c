#include "trap.h"
#include "uart.h"
#include "sbi.h"
#include "timer.h"
#include "plic.h"

extern int uart_irq;
extern unsigned int CPU_FREQ;
extern unsigned int boot_cpu_hartid;

void do_trap(struct pt_regs* regs) {
    unsigned long cause = regs->scause;
    // 判斷是否為 Interrupt (檢查最高位元 bit 63)
    unsigned long is_interrupt = cause & (1ULL << 63);
    // 濾掉最高位元，取得實際的中斷/例外代碼
    unsigned long exception_code = cause & ~(1ULL << 63);

    if (is_interrupt) {
        // === 這裡是 Interrupt 的處理區塊 ===
        if (exception_code == 9) { // 9 代表 SEI (External Interrupt)
            int irq = plic_claim();
            
            if (irq == uart_irq) 
                handle_uart_interrupt();
            
            if (irq)
                plic_complete(irq);
        }
        else if (exception_code == 5) { // 5 代表 Supervisor Timer Interrupt
            handle_timer_interrupt();
        }
        else {
            uart_puts("Unknown Interrupt!\n");
        }
    }
    // TODO: Implement this function
    // (1) Print the sepc and scause registers
    else {
        uart_puts("=== S-Mode trap ===\n");
        uart_puts("scause: ");
        uart_hex(regs->scause);
        uart_puts("\n");
        
        uart_puts("sepc: ");
        uart_hex(regs->sepc);
        uart_puts("\n");
        
        uart_puts("stval: ");
        uart_hex(regs->stval);
        uart_puts("\n");

        // (2) Increment the sepc register by 4 for traps
        regs->sepc += 4;
    }
}
