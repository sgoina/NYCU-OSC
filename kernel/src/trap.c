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
        // 12: Instruction page fault, 13: Load page fault, 15: Store/AMO page fault
        else if (exception_code == 12 || exception_code == 13 || exception_code == 15) {
            struct task_struct *curr = get_current();
            unsigned long stval = regs->stval; // Get the address where the exception triggers
            int handled = 0; // Boolean flag for segmentation fault

            for (int i = 0; i < MAX_VMAS; i++) {
                // Check whether this address is in any vmas
                if (curr->vmas[i].used && stval >= curr->vmas[i].vm_start && stval < curr->vmas[i].vm_end) {
                    int valid = 1;
                    // Check PROT in the region
                    if (exception_code == 15 && !(curr->vmas[i].vm_prot & PROT_WRITE))
                        valid = 0;
                    if (exception_code == 13 && !(curr->vmas[i].vm_prot & PROT_READ))
                        valid = 0;
                    if (exception_code == 12 && !(curr->vmas[i].vm_prot & PROT_EXEC))
                        valid = 0;
                    
                    if (valid) {
                        unsigned long fault_page_va = stval & ~(PAGE_SIZE - 1); // Align fault address to 4KB
                        unsigned long *pte = get_pte(curr->pgd, fault_page_va); // Get the pte of this address
                        // Copy-On-Write
                        if (exception_code == 15 && pte != NULL && (*pte & PTE_V) && (*pte & PTE_COW)) {
                            unsigned long old_pa = (*pte >> 10) << 12;
                            void *old_va = (void *)phys_to_virt(old_pa);
                            
                            int page_refs = get_page_ref(old_pa); // reference count of this page frame
                            // Update the permission in this pte
                            unsigned long pte_prot = PROT_USER_BASE;
                            if (curr->vmas[i].vm_prot & PROT_READ)
                                pte_prot |= PTE_R;
                            if (curr->vmas[i].vm_prot & PROT_WRITE)
                                pte_prot |= PTE_W;
                            if (curr->vmas[i].vm_prot & PROT_EXEC)
                                pte_prot |= PTE_X;
                            
                            // Other still need this page frame => copy a new page frame
                            if (page_refs != 1) {
                                dec_page_ref(old_pa);
                                void *new_page = allocate(PAGE_SIZE);
                                if (!new_page) {
                                    uart_puts("[COW] OOM!\n");
                                    thread_exit();
                                }
                                memcpy(new_page, old_va, PAGE_SIZE);
                                
                                // Update PTE to be writable and point to the new frame
                                unsigned long new_pa = (unsigned long)virt_to_phys(new_page);
                                map_pages(curr->pgd, fault_page_va, PAGE_SIZE, new_pa, pte_prot);
                            }
                            // Only this process needs this page frame => update pte permission directly
                            else 
                                *pte = MAKE_PTE(old_pa, pte_prot);
                            
                            uart_puts("[Permission fault]: ");
                            uart_hex(fault_page_va);
                            uart_puts("\n");                                            
                        }
                        // Not Copy-On-Write
                        else {
                            void *new_page = allocate(PAGE_SIZE);
                            if (new_page) {
                                memset(new_page, 0, PAGE_SIZE); 
                                // Check if the page fault is for code text, copy the specific page
                                if (curr->vmas[i].vm_start == USER_CODE_VA && curr->cpio_addr != 0) {
                                    unsigned long offset = fault_page_va - USER_CODE_VA;
                                    // avoid over code text
                                    if (offset < curr->code_size) { 
                                        unsigned long copy_size = curr->code_size - offset; // avoid copy the last page of code text
                                        if (copy_size > PAGE_SIZE)
                                            copy_size = PAGE_SIZE;
                                        memcpy(new_page, (void *)(curr->cpio_addr + offset), copy_size);
                                    }
                                }
                                
                                uart_puts("[Translation fault]: ");
                                uart_hex(fault_page_va); 
                                uart_puts("\n");

                                unsigned long pte_prot = PROT_USER_BASE;
                                if (curr->vmas[i].vm_prot & PROT_READ)
                                    pte_prot |= PTE_R;
                                if (curr->vmas[i].vm_prot & PROT_WRITE) 
                                    pte_prot |= PTE_W;
                                if (curr->vmas[i].vm_prot & PROT_EXEC)
                                    pte_prot |= PTE_X;
                                
                                // Make a pte for the new-allocated frame
                                unsigned long page_pa = (unsigned long)virt_to_phys(new_page);
                                map_pages(curr->pgd, fault_page_va, PAGE_SIZE, page_pa, pte_prot);

                                asm volatile("sfence.vma zero, zero" ::: "memory");
                                handled = 1; 
                            }
                            else {
                                uart_puts("[Translation] OOM!\n");
                                thread_exit();
                            }
                        }
                        asm volatile("sfence.vma zero, zero" ::: "memory");
                        handled = 1;
                    }
                    break;
                }
            }
            // Segmentation fault and terminate the thread
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
