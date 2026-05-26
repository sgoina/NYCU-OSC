#include "trap.h"
#include "uart.h"
#include "sbi.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "thread.h"
#include "syscall.h"
#include "utils.h"
#include "mem_alloc.h"
#include "vm.h"

extern int uart_irq; // plic.c

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
        if (exception_code == 8) {
            regs->sepc += 4; // Increment the sepc register by 4 avoid to infinity loop
            syscall_handler(regs);
        }
        // 🌟 新增：攔截 Page Fault (12: 執行錯誤, 13: 讀取錯誤, 15: 寫入錯誤)
        else if (exception_code == 12 || exception_code == 13 || exception_code == 15) {
            struct task_struct *curr = get_current();
            unsigned long stval = regs->stval;
            int handled = 0; // 手術是否成功的標記

            // 1. 翻開 VMA 帳本，看這個位址是不是 User 曾經預約過的
            for (int i = 0; i < MAX_VMAS; i++) {
                if (curr->vmas[i].used && stval >= curr->vmas[i].vm_start && stval < curr->vmas[i].vm_end) {
                    
                    // 2. 檢查權限 (例如他預約了「只能讀」，現在卻「想要寫 (15)」)
                    int valid = 1;
                    if (exception_code == 15 && !(curr->vmas[i].vm_prot & PROT_WRITE)) valid = 0;
                    if (exception_code == 13 && !(curr->vmas[i].vm_prot & PROT_READ))  valid = 0;
                    if (exception_code == 12 && !(curr->vmas[i].vm_prot & PROT_EXEC))  valid = 0;

                    if (valid) {
                        // 🌟 【作業需求 1】Allocate a page frame and map only that page.
                        unsigned long fault_page_va = stval & ~(PAGE_SIZE - 1); // 向下對齊到 4KB 邊界
                        void *new_page = allocate(PAGE_SIZE);
                        
                        if (new_page) {
                            memset(new_page, 0, PAGE_SIZE); // 先全部清零 (這對 Stack 和 .bss 區段非常重要)
                            
                            // 判斷：如果這個 VMA 是 Code 區塊 (起始於 USER_CODE_VA)
                            if (curr->vmas[i].vm_start == USER_CODE_VA && curr->cpio_addr != 0) {
                                // 計算目前缺頁的位址，距離程式碼開頭有多遠
                                unsigned long offset = fault_page_va - USER_CODE_VA;
                                
                                // 確保沒有超出真實的程式大小
                                if (offset < curr->code_size) {
                                    unsigned long copy_size = curr->code_size - offset;
                                    // 每次最多只搬運一個 Page (4KB) 的資料
                                    if (copy_size > PAGE_SIZE) {
                                        copy_size = PAGE_SIZE;
                                    }
                                    // 從 CPIO 的記憶體位址，精準複製對應的程式碼片段過來
                                    memcpy(new_page, (void *)(curr->cpio_addr + offset), copy_size);
                                }
                            }
                            
                            // 🌟🌟🌟 【作業需求 2】Log the translation 
                            // 修正：印出對齊後的 Page 虛擬位址，符合助教測資預期
                            uart_puts("[Translation fault]: ");
                            uart_hex(fault_page_va); 
                            uart_puts("\n");

                            // 轉換 PTE 權限
                            unsigned long pte_prot = PROT_USER_BASE | PTE_A | PTE_D; // 補上 A 和 D bit 避免微小中斷
                            if (curr->vmas[i].vm_prot & PROT_READ)  pte_prot |= PTE_R;
                            if (curr->vmas[i].vm_prot & PROT_WRITE) pte_prot |= PTE_W;
                            if (curr->vmas[i].vm_prot & PROT_EXEC)  pte_prot |= PTE_X;

                            // 映射進 Page Table (也只映射這 4KB)
                            unsigned long page_pa = (unsigned long)new_page - PAGE_OFFSET;
                            map_pages(curr->pgd, fault_page_va, PAGE_SIZE, page_pa, pte_prot);

                            // 刷新 TLB
                            asm volatile("sfence.vma zero, zero" ::: "memory");

                            handled = 1; // 手術成功！
                            break;
                        }
                    }
                }
            }

            // 如果 VMA 沒紀錄，或權限不對，這就是真當機 (Segmentation Fault)
            if (!handled) {
                uart_puts("[Segmentation fault]: Kill Process\n");
                uart_puts("Invalid memory access at ");
                uart_hex(stval);
                uart_putc('\n');
                
                thread_exit();
            }
            // 💡 注意這裡：如果 handled == 1，我們什麼都不做，直接讓程式流往下走。
            // 因為沒有 regs->sepc += 4，所以 sret 回去後，會重新執行剛剛失敗的那行指令！
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
            
            thread_exit();

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
    // check there is any signal need to handle
    thread_handle_signals(regs); 
}
