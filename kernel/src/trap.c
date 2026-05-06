#include "trap.h"
#include "uart.h"
#include "sbi.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "thread.h"
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
        // 🌟 修正：同時處理 User Mode (8) 和 Kernel Mode (9) 的系統呼叫！
        if (exception_code == 8 || exception_code == 9) {
            
            // 🚨 必須將 sepc 推進 4 byte，否則返回後會無限執行 ecall
            regs->sepc += 4;
            
            // 🚨 移除了危險的 csrsi/csrci，防止巢狀中斷引發 S-Mode Trap 崩潰
            
            // 派發系統呼叫
            syscall_handler(regs);
            
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
    // ====================================================================
    // 🌟 Advanced Part: 魔法發生的瞬間！
    // 就在完成所有 Trap 處理、準備執行 sret 回到 User Mode 之前：
    // 檢查有沒有被發送信號。如果有，這個函數會把 regs->sepc 偷偷改成 
    // handler 的位址，並備份原本的狀態！
    // ====================================================================
    thread_handle_signals(regs);
}
