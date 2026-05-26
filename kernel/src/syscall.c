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
    
    // 1. 取得新程式在 CPIO 中的位址
    void* cpio_code_addr = find_program(path, &filesize);
    if (cpio_code_addr == NULL) {
        return -1; // Can't find
    }

    struct task_struct *current = get_current();

    // 2. 打造新的記憶體宇宙 (分配新 PGD)
    unsigned long *new_pgd = (unsigned long *)allocate(PAGE_SIZE);
    memset(new_pgd, 0, PAGE_SIZE);

    // 複製 Kernel 的記憶體宇宙 (避免切換 satp 後瞬間當機)
    for (int i = 256; i < 512; i++) {
        new_pgd[i] = pgd[i]; 
    }

    // 🌟 關鍵修正 1：拔除所有實體記憶體的 allocate 與 map_pages！
    unsigned long aligned_filesize = align(filesize, PAGE_SIZE);

    // 3. 清理舊的實體記憶體
    // 在 Demand Paging 下，我們無法用單一指標來釋放散落的分頁。
    // 因此，我們拔除了 free(current->user_stack) 與 code_frame，
    // 僅釋放 PGD 目錄 (在基礎 OS 課程中暫時接受底層 Page Table 的 Leak)。
    if (current->pgd != NULL) {
        free(current->pgd);
    }

    // 4. 更新 TCB (Task Control Block) 資訊
    current->pgd = new_pgd;
    
    // 抹除舊時代的單一實體指標
    current->user_stack = 0; 
    
    // 🌟 關鍵修正 2：記錄 CPIO 位址與大小，讓 Page Fault Handler 去搬運
    current->cpio_addr = (unsigned long)cpio_code_addr;
    current->code_size = filesize;
    
    current->user_sp = USER_SP_VA; 
    
    // 5. 更新 Exception Return 狀態
    regs->sepc = USER_CODE_VA;
    regs->sp = USER_SP_VA;
    
    // 6. 註冊 VMA 帳本
    // 註冊 Code 區塊
    current->vmas[0].vm_start = USER_CODE_VA;
    current->vmas[0].vm_end   = USER_CODE_VA + aligned_filesize;
    current->vmas[0].vm_prot  = PROT_READ | PROT_EXEC | PROT_WRITE;
    current->vmas[0].used     = 1;

    // 註冊 Stack 區塊 (給足 20 個 Page 以通過 demand 測試)
    unsigned long stack_vma_size = 20 * PAGE_SIZE; 
    unsigned long stack_top = (USER_SP_VA + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); 

    current->vmas[1].vm_start = stack_top - stack_vma_size;
    current->vmas[1].vm_end   = stack_top;
    current->vmas[1].vm_prot  = PROT_READ | PROT_WRITE;
    current->vmas[1].used     = 1;

    // 其餘清空
    for (int i = 2; i < MAX_VMAS; i++) {
        current->vmas[i].used = 0;
    }

    // 7. 立即切換硬體 MMU 並刷新 TLB
    unsigned long pgd_pa = (unsigned long)new_pgd - PAGE_OFFSET;
    asm volatile(
        "sfence.vma zero, zero\n"
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
long sys_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct task_struct *curr = get_current();
    unsigned long aligned_len = align(length, PAGE_SIZE); // 向上對齊 4KB
    unsigned long target_addr = (unsigned long)addr;

    // 🌟 1. 檢查 Flags 支援度
    if (!(flags & MAP_ANONYMOUS)) {
        // 目前基礎實作只支援匿名映射 (不綁定檔案)
        uart_puts("Only MAP_ANONYMOUS is supported now.\n");
        // 如果作業有要求 File-backed mapping 可以未來實作，這邊先放行或擋下
    }

    // 2. 決定目標虛擬位址
    if (target_addr != 0) {
        target_addr = target_addr & ~(PAGE_SIZE - 1); 
        if (is_overlap(curr, target_addr, aligned_len)) {
            target_addr = find_free_vma_region(curr, aligned_len);
        }
    } else {
        target_addr = find_free_vma_region(curr, aligned_len);
    }

    if (target_addr == 0) return -1; // Out of memory

    // 3. 登記到 VMA 結構中 (記帳)
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
    if (vma_idx == -1) return -1; // VMA 滿了

    // 4. 轉換 PROT 到 PTE 權限
    unsigned long pte_prot = PROT_USER_BASE; 
    if (prot & PROT_READ)  pte_prot |= PTE_R;
    if (prot & PROT_WRITE) pte_prot |= PTE_W;
    if (prot & PROT_EXEC)  pte_prot |= PTE_X;

    // 🌟 5. 根據 MAP_POPULATE 決定是否立即分配實體記憶體
    if (flags & MAP_POPULATE) {
        // 立刻分配並映射 (Advanced 1 模式)
        for (unsigned long va = target_addr; va < target_addr + aligned_len; va += PAGE_SIZE) {
            void *new_page = allocate(PAGE_SIZE); 
            if (!new_page) return -1;
            memset(new_page, 0, PAGE_SIZE);       
            
            unsigned long page_pa = (unsigned long)new_page - PAGE_OFFSET;
            map_pages(curr->pgd, va, PAGE_SIZE, page_pa, pte_prot);
        }
        asm volatile("sfence.vma zero, zero" ::: "memory");
    } 

    return target_addr; // 發放空虛擬位址鑰匙給 User
}

void syscall_handler(struct pt_regs *regs) {
    // a7 stores system call number
    unsigned long syscall_num = regs->a7;
    
    long ret = -1;

    switch (syscall_num) {
        case 0: // getpid
            ret = sys_getpid();
            break;
            
        case 1: // uart_read
            // a0 = *buf, a1 = count
            ret = sys_uart_read((char*)regs->a0, regs->a1);
            break;
            
        case 2: // uart_write
            // a0 = *buf, a1 = count
            ret = sys_uart_write((const char*)regs->a0, regs->a1);
            break;
            
        case 3: // exec
            // a0 = *path
            ret = sys_exec((const char*)regs->a0, regs);
            break;
            
        case 4: // fork
            ret = sys_fork(regs);
            break;
            
        case 5: // waitpid
            // a0 = pid
            ret = sys_waitpid(regs->a0);
            break;
            
        case 6: // exit
            // a0 = status (ignored)
            sys_exit(regs->a0);
            break;
            
        case 7: // stop
            // a0 = pid
            ret = sys_stop(regs->a0);
            break;
            
        case 8: // display
            // a0 = bmp_image, a1 = width, a2 = height
            sys_display((unsigned int*)regs->a0, (unsigned int)regs->a1, (unsigned int)regs->a2);
            break;
            
        case 9: // usleep
            // a0 = usec
            ret = sys_usleep((unsigned int)regs->a0);
            break;
            
        case 10: // signal
            // a0 = signum, a1 = *handler
            ret = sys_signal((int)regs->a0, (void *)regs->a1); // ret value ignored
            break;
                        
        case 11: // sigreturn
            sys_sigreturn(regs);
            return;
            
        case 12: // kill
            // a0 = pid, a1 = signum
            ret = sys_kill((int)regs->a0, (int)regs->a1); 
            break;
        case 13: // mmap
            // a0 = addr, a1 = length, a2 = prot, a3 = flags
            ret = sys_mmap((void *)regs->a0, (unsigned long)regs->a1, (int)regs->a2, (int)regs->a3); 
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
