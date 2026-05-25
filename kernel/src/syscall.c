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

#define STACK_SIZE 0x1000
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

    // 🌟 關鍵修正 1：複製 Kernel 的記憶體宇宙 (避免切換 satp 後瞬間當機)
    for (int i = 256; i < 512; i++) {
        new_pgd[i] = pgd[i]; 
    }

    // 🌟 關鍵修正 2：分配新記憶體並複製 Code (解決對齊與唯讀問題)
    unsigned long aligned_filesize = align(filesize, PAGE_SIZE);
    void* user_code_ptr = allocate(aligned_filesize);
    if (!user_code_ptr) return -1;
    
    memset(user_code_ptr, 0, aligned_filesize);
    memcpy(user_code_ptr, cpio_code_addr, filesize);

    unsigned long new_stack = (unsigned long)allocate(STACK_SIZE);

    // 3. 實體位址轉換
    unsigned long code_pa = (unsigned long)user_code_ptr - PAGE_OFFSET;
    unsigned long stack_pa = (unsigned long)new_stack - PAGE_OFFSET;

    // 4. 映射 Code 與 Stack (使用與 create 相同的佈局與權限)
    map_pages(new_pgd, USER_CODE_VA, aligned_filesize, code_pa, PROT_USER_BASE | PTE_R | PTE_W | PTE_X);
    map_pages(new_pgd, USER_STACK_VA, STACK_SIZE, stack_pa, PROT_USER_RW);

    // 5. 清理舊的實體記憶體
    // 注意：目前的寫法只 free 掉了舊的 PGD 這一層，
    // 嚴格來說，舊 PGD 底下的 PMD、PTE 還有舊的 user_code_ptr 也會 Memory Leak，
    // 但在基礎 OS 作業中，這暫時可以接受。
    if (current->pgd != NULL) {
        free(current->pgd);
    }
    if (current->user_stack != 0) {
        free((void*)current->user_stack);
        // (如果有紀錄舊的 user_code_ptr，也應該在這裡 free 掉)
    }
    
    // 👇 新增：既然我們現在有紀錄 code_frame 了，就在這裡把舊的程式碼實體記憶體釋放掉！
    if (current->code_frame != 0) {
        free((void*)current->code_frame);
    }

    // 6. 更新 TCB (Task Control Block) 資訊
    current->pgd = new_pgd;
    current->user_stack = new_stack;
    current->user_sp = USER_SP_VA; 
    
    // 👇 新增：紀錄新程式的實體位址與大小，讓未來的 fork 可以正確複製！
    current->code_frame = user_code_ptr;
    current->code_size = aligned_filesize;
    // 7. 更新 Exception Return 狀態
    regs->sepc = USER_CODE_VA;
    regs->sp = USER_SP_VA;

    // 8. 立即切換硬體 MMU 並刷新 TLB
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
/*
long sys_exec(const char *path, struct pt_regs *regs) {
    // find the program entry
    void* entry_point = find_program(path);
    if (entry_point == 0) {
        return -1; // Can't find
    }

    struct task_struct *current = get_current();
    // setting program counter new program entry
    regs->sepc = (unsigned long)entry_point;
    // clear user stack for new program
    regs->sp = current->user_sp;

    return 0;
}
*/

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
            
        default:
            uart_puts("Unknown syscall number: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            break;
    }
    // a0 = return value
    regs->a0 = (unsigned long)ret;
}
