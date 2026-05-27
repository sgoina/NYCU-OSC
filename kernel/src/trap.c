#include "defint.h"
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
        // 🌟 攔截 Page Fault (12: 執行錯誤, 13: 讀取錯誤, 15: 寫入錯誤)
        else if (exception_code == 12 || exception_code == 13 || exception_code == 15) {
            struct task_struct *curr = get_current();
            unsigned long stval = regs->stval;
            int handled = 0; // 手術是否成功的標記

            for (int i = 0; i < MAX_VMAS; i++) {
                if (curr->vmas[i].used && stval >= curr->vmas[i].vm_start && stval < curr->vmas[i].vm_end) {
                    
                    int valid = 1;
                    if (exception_code == 15 && !(curr->vmas[i].vm_prot & PROT_WRITE)) valid = 0;
                    if (exception_code == 13 && !(curr->vmas[i].vm_prot & PROT_READ))  valid = 0;
                    if (exception_code == 12 && !(curr->vmas[i].vm_prot & PROT_EXEC))  valid = 0;
                    
                    
                    if (valid) {
                        unsigned long fault_page_va = stval & ~(PAGE_SIZE - 1); 
                        unsigned long *pte = get_pte(curr->pgd, fault_page_va);
                        
                        // 🌟🌟🌟 攔截 Copy-on-Write (COW) 🌟🌟🌟
                        // 檢查這個 PTE 是否存在，並且貼有 COW 標籤 (1 << 8)
                        if (exception_code == 15 && pte != NULL && (*pte & PTE_V) && (*pte & PTE_COW)) {
                            unsigned long old_pa = (*pte >> 10) << 12;
                            void *old_page_kernel_va = (void *)(old_pa + PAGE_OFFSET);
                            
                            int page_refs = get_page_ref(old_pa);
                            
                            // 準備新的 PTE 權限：撕掉 COW，恢復 PTE_W
                            unsigned long pte_prot = PROT_USER_BASE;
                            if (curr->vmas[i].vm_prot & PROT_READ)  pte_prot |= PTE_R;
                            if (curr->vmas[i].vm_prot & PROT_WRITE) pte_prot |= PTE_W;
                            if (curr->vmas[i].vm_prot & PROT_EXEC)  pte_prot |= PTE_X;

                            if (page_refs > 1) {
                                // 情況 A：還有別人在用，Allocate 新 frame 並 Copy data
                                dec_page_ref(old_pa);
                                void *new_page = allocate(PAGE_SIZE);
                                if (!new_page) {
                                    uart_puts("[COW] OOM!\n");
                                    thread_exit();
                                }
                                memcpy(new_page, old_page_kernel_va, PAGE_SIZE);
                                
                                // Update PTE to be writable and point to the new frame
                                unsigned long new_pa = (unsigned long)new_page - PAGE_OFFSET;
                                map_pages(curr->pgd, fault_page_va, PAGE_SIZE, new_pa, pte_prot);
                            }
                            else {
                                // 情況 B：只剩自己在用，直接 Update PTE to be writable
                                *pte = ((old_pa >> 12) << 10) | pte_prot;
                            }
                            
                            // 🌟 嚴格遵守作業要求：Log the permission fault
                            uart_puts("[Permission fault]: ");
                            uart_hex(fault_page_va); // 根據你的其他 log，這裡通常預期是對齊的位址 (addr)
                            uart_puts("\n");
                            
                            asm volatile("sfence.vma zero, zero" ::: "memory");
                            handled = 1;
                            break;
                        }
                        // 🌟🌟🌟 COW 攔截結束 🌟🌟🌟

                        // ==========================================
                        // Demand Paging 邏輯
                        // ==========================================
                        void *new_page = allocate(PAGE_SIZE);
                        if (new_page) {
                            memset(new_page, 0, PAGE_SIZE); 
                            
                            if (curr->vmas[i].vm_start == USER_CODE_VA && curr->cpio_addr != 0) {
                                unsigned long offset = fault_page_va - USER_CODE_VA;
                                if (offset < curr->code_size) {
                                    unsigned long copy_size = curr->code_size - offset;
                                    if (copy_size > PAGE_SIZE) copy_size = PAGE_SIZE;
                                    memcpy(new_page, (void *)(curr->cpio_addr + offset), copy_size);
                                }
                            }
                            
                            uart_puts("[Translation fault]: ");
                            uart_hex(fault_page_va); 
                            uart_puts("\n");

                            unsigned long pte_prot = PROT_USER_BASE;
                            if (curr->vmas[i].vm_prot & PROT_READ)  pte_prot |= PTE_R;
                            if (curr->vmas[i].vm_prot & PROT_WRITE) pte_prot |= PTE_W;
                            if (curr->vmas[i].vm_prot & PROT_EXEC)  pte_prot |= PTE_X;

                            unsigned long page_pa = (unsigned long)new_page - PAGE_OFFSET;
                            map_pages(curr->pgd, fault_page_va, PAGE_SIZE, page_pa, pte_prot);

                            asm volatile("sfence.vma zero, zero" ::: "memory");
                            handled = 1; 
                            break;
                        }
                    }
                }
            }

            // 🌟 嚴格遵守作業要求：Generate a segmentation fault and terminate
            if (!handled) {
                uart_puts("[Segmentation fault]: Kill Process\n");
                uart_puts("Invalid memory access at ");
                uart_hex(stval);
                uart_putc('\n');
                thread_exit();
            }
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
