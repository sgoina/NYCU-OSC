#include "defint.h"
#include "mem_alloc.h"
#include "trap.h"
#include "thread.h"
#include "timer.h"
#include "ramfs.h"
#include "uart.h"
#include "utils.h"
#include "video.h"
#include "vm.h"

#define align(size, align_val) (((size) + (align_val) - 1) & ~((align_val) - 1))

extern struct task_struct* run_queue; // thread.c
extern unsigned int CPU_FREQ; // timer.c
extern unsigned long pgd[]; // vm.c


// 0: getpid
long sys_getpid() {
    return get_current()->pid;
}

// 1: uart_read
long sys_uart_read(char *buf, long count) {
    long read_bytes = 0;
    for (long i = 0; i < count; i++) {
        buf[i] = uart_getc();
        read_bytes++;
    }
    return read_bytes;
}

// 2: uart_write
long sys_uart_write(const char *buf, long count) {
    long written_bytes = 0;
    for (long i = 0; i < count; i++) {
        uart_putc(buf[i]);
        written_bytes++;
    }
    return written_bytes;
}

// 3: exec
long sys_exec(const char *path, struct pt_regs *regs) {
    unsigned int filesize = 0;
    
    // get the address in CPIO and size of this program
    void* cpio_code_addr = find_program(path, &filesize);
    if (cpio_code_addr == NULL)
        return -1; // Can't find
        
    unsigned long aligned_filesize = align(filesize, PAGE_SIZE); // align file size to 4KB
    struct task_struct *current = get_current();

    unsigned long *new_pgd = (unsigned long *)allocate(PAGE_SIZE);
    memset(new_pgd, 0, PAGE_SIZE);

    // Copy kernel space to higher-half pgd
    for (int i = 256; i < 512; i++) {
        new_pgd[i] = pgd[i]; 
    }
    
    // update process pgd
    if (current->pgd != NULL)
        free(current->pgd);
    current->pgd = new_pgd;
        
    current->cpio_addr = (unsigned long)cpio_code_addr;
    current->code_size = filesize;
    
    current->user_stack = 0; 
    current->user_sp = USER_SP_VA; 
    
    regs->sepc = USER_CODE_VA;
    regs->sp = USER_SP_VA;
    
    // record vmas for code and user stack
    current->vmas[0].vm_start = USER_CODE_VA;
    current->vmas[0].vm_end   = USER_CODE_VA + aligned_filesize;
    current->vmas[0].vm_prot  = PROT_READ | PROT_EXEC | PROT_WRITE;
    current->vmas[0].used     = 1;

    unsigned long stack_vma_size = 20 * PAGE_SIZE; 

    current->vmas[1].vm_start = USER_SP_VA - stack_vma_size;
    current->vmas[1].vm_end   = USER_SP_VA;
    current->vmas[1].vm_prot  = PROT_READ | PROT_WRITE;
    current->vmas[1].used     = 1;

    // clear the info of other regions
    for (int i = 2; i < MAX_VMAS; i++) {
        current->vmas[i].used = 0;
    }

    // Update pgd and flush TLB, ready to go to new process
    unsigned long pgd_pa = (unsigned long)virt_to_phys(new_pgd);
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd_pa))
        : "memory"
    );

    return 0;
}

// 4: fork
long sys_fork(struct pt_regs *regs) {
    return fork_process(regs);
}

// 5: waitpid
long sys_waitpid(long pid) {
    return thread_wait(pid);
}

// 6: exit
void sys_exit(int status) {
    thread_exit();
}

// 7: stop
int sys_stop(long pid) {
    if (pid <= 1) {
        uart_puts("Can't stop idle or kernel shell thread.\n");
        return -1;
    }

    struct task_struct *find = get_task_by_pid(pid);
    
    if (find != NULL){
        // Stop itself
        if (find->pid == get_current()->pid)
            thread_exit();
        else
            find->state = TASK_ZOMBIE;
        return 0;
    }
    // Can't find pid
    return -1; 
}

// 8: display
void sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height){
    video_bmp_display(bmp_image, width, height);
}

// 9: usleep
int sys_usleep(unsigned int usec) {
    if (usec < 0)
        return -1; // Failed
    unsigned long long ticks = (unsigned long long)usec * (CPU_FREQ / 1000000);
    unsigned long long start_time = get_time();
    
    // Do schedule() until wake up time
    while ((get_time() - start_time) < ticks) {
        schedule();
    }
    return 0;
}

// 10: signal
long sys_signal(int signum, void *handler) {
    if (signum < 0 || signum >= MAX_SIGNALS)
        return -1; // Failed
    
    struct task_struct *curr = get_current();
    curr->signal_handlers[signum] = (unsigned long)handler;
    return 0;
}

// 11: sigreturn
void sys_sigreturn(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // Recycle the signal stack
    if (curr->signal_stack != 0) {
        free((void *)curr->signal_stack);
        curr->signal_stack = 0;
    }

    // Return back original regs
    char *src = (char *)&curr->signal_saved_context;
    char *dst = (char *)regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst[i] = src[i];
    }
    // Clear is_handling bit for next signal
    curr->is_handling_signal = 0;
    uart_puts("This is sigreturn\n");
}

// 12: kill
int sys_kill(int pid, int signum) {
    if (signum < 0 || signum >= MAX_SIGNALS)
        return -1; // Failed
    
    struct task_struct *target = get_task_by_pid(pid);
    if (!target || target->state == TASK_ZOMBIE)
        return -1; // Can't find target task, failed
    // mark specified bit for determining the signal number
    target->pending_signals |= (1ULL << signum);
    return 0;
}

// 13: mmap
void* sys_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct task_struct *curr = get_current();
    unsigned long aligned_len = align(length, PAGE_SIZE); // align size to 4KB
    unsigned long target_addr = (unsigned long)addr;

    // Check flags
    if (!(flags & MAP_ANONYMOUS))
        return (void*)-1;
    
    // Check whether address is NULL. If it is NULL, find the region by kernel
    if (target_addr != 0) {
        target_addr = target_addr & ~(PAGE_SIZE - 1); // align begin address to 4KB
        // Check whether the region be used. If there is used, find another space
        if (is_overlap(curr, target_addr, aligned_len)) 
            target_addr = find_free_vma_region(curr, aligned_len);
    } 
    else 
        target_addr = find_free_vma_region(curr, aligned_len);

    // Can't find suitable region
    if (target_addr == 0)
        return (void*)-1;

    // Record the region in vmas
    int vma_idx = -1;
    for (int i = 0; i < MAX_VMAS; i++) {
        if (!curr->vmas[i].used) {
            curr->vmas[i].vm_start = target_addr;
            curr->vmas[i].vm_end = target_addr + aligned_len;
            curr->vmas[i].vm_prot = prot;
            curr->vmas[i].vm_flags = flags;
            curr->vmas[i].used = 1;
            vma_idx = i;
            break;
        }
    }
    // curr->vmas is full
    if (vma_idx == -1)
        return (void*)-1; 

    // Convert PROT to PTE permission
    unsigned long pte_prot = PROT_USER_BASE; 
    if (prot & PROT_READ)
        pte_prot |= PTE_R;
    if (prot & PROT_WRITE)
        pte_prot |= PTE_W;
    if (prot & PROT_EXEC)
        pte_prot |= PTE_X;

    // MAP_POPULATE: Allocate physical pages immediately 
    if (flags & MAP_POPULATE) {
        for (unsigned long va = target_addr; va < target_addr + aligned_len; va += PAGE_SIZE) {
            void *new_page = allocate(PAGE_SIZE); 
            if (!new_page)
                return (void*)-1;
            memset(new_page, 0, PAGE_SIZE);       
            
            unsigned long page_pa = virt_to_phys((unsigned long)new_page);
            map_pages(curr->pgd, va, PAGE_SIZE, page_pa, pte_prot);
        }
        asm volatile("sfence.vma zero, zero" ::: "memory");
    } 

    return (void*)target_addr; // return virtual address of the begin of this region
}

void syscall_handler(struct pt_regs *regs) {
    // a7 stores system call number
    unsigned long syscall_num = regs->a7;
    
    void* ret = (void*)-1;

    switch (syscall_num) {
        case 0: // getpid
            ret = (void*)sys_getpid();
            break;
            
        case 1: // uart_read
            // a0 = *buf, a1 = count
            ret = (void*)sys_uart_read((char*)regs->a0, regs->a1);
            break;
            
        case 2: // uart_write
            // a0 = *buf, a1 = count
            ret = (void*)sys_uart_write((const char*)regs->a0, regs->a1);
            break;
            
        case 3: // exec
            // a0 = *path
            ret = (void*)sys_exec((const char*)regs->a0, regs);
            break;
            
        case 4: // fork
            ret = (void*)sys_fork(regs);
            break;
            
        case 5: // waitpid
            // a0 = pid
            ret = (void*)sys_waitpid(regs->a0);
            break;
            
        case 6: // exit
            // a0 = status (ignored)
            sys_exit(regs->a0);
            break;
            
        case 7: // stop
            // a0 = pid
            ret = (void*)(long)sys_stop(regs->a0);
            break;
            
        case 8: // display
            // a0 = bmp_image, a1 = width, a2 = height
            sys_display((unsigned int*)regs->a0, (unsigned int)regs->a1, (unsigned int)regs->a2);
            break;
            
        case 9: // usleep
            // a0 = usec
            ret = (void*)(long)sys_usleep((unsigned int)regs->a0);
            break;
            
        case 10: // signal
            // a0 = signum, a1 = *handler
            ret = (void*)sys_signal((int)regs->a0, (void *)regs->a1); // ret value ignored
            break;
                        
        case 11: // sigreturn
            sys_sigreturn(regs);
            return;
            
        case 12: // kill
            // a0 = pid, a1 = signum
            ret = (void*)(long)sys_kill((int)regs->a0, (int)regs->a1); 
            break;
        case 13: // mmap
            // a0 = addr, a1 = length, a2 = prot, a3 = flags
            ret = (void*)sys_mmap((void *)regs->a0, (unsigned long)regs->a1, (int)regs->a2, (int)regs->a3); 
            break;
            
        default:
            uart_puts("Unknown syscall number: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            break;
    }
    // a0 = return value
    regs->a0 = (unsigned long)ret;
}
