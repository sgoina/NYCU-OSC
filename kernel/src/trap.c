#include "trap.h"
#include "uart.h"
#include "sbi.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "syscall.h"

extern int uart_irq; // from plic.c

void do_trap(struct pt_regs* regs) {
    unsigned long cause = regs->scause;
    // The highest bit of scause decides whether it is interrupt
    unsigned long is_interrupt = cause & (1ULL << 63);
    // Get the Exception Code
    unsigned long exception_code = cause & ~(1ULL << 63);

    if (is_interrupt) {
        if (exception_code == 9) { // 9 = Supervisor external interrupt
            int irq = plic_claim();
            
            if (irq == uart_irq) 
                handle_uart_interrupt();
            
            if (irq)
                plic_complete(irq);
        }
        else if (exception_code == 5) { // 5 = Supervisor timer interrupt
            handle_timer_interrupt();
        }
        else {
            uart_puts("Unknown Interrupt!\n");
        }
    }
    // (1) Print the sepc and scause registers
    else {
        // === 這裡是處理 Exception (Trap) 的地方 ===
        if (exception_code == 8) {
            // (1) 系統呼叫 (ecall from U-mode)
            /*uart_hex(regs->a7);
            uart_putc('\n');*/
            // 🚨 必須將 sepc 推進 4 byte，否則返回 User Mode 後會無限執行 ecall
            regs->sepc += 4;
            
            // 開啟中斷：讓系統呼叫在執行時是可以被 Timer 搶佔的 (Kernel Preemption)
            asm volatile("csrsi sstatus, 2"); 

            // 派發系統呼叫
            syscall_handler(regs);

            // 關閉中斷：準備離開 Trap，確保 Context Switch 的安全性
            asm volatile("csrci sstatus, 2");

        }
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
            //regs->sepc += 4;
        }
    }
    // SIE (enables or disables interrupts) for higher priority tasks
    asm volatile("csrsi sstatus, 2"); 
    
    // execute lower priority tasks
    run_tasks();
    
    // disables interrupts to avoid get wrong when context switching
    asm volatile("csrci sstatus, 2");
}
