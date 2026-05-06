#include "mem_alloc.h"
#include "trap.h"
#include "thread.h"
#include "timer.h"
#include "ramfs.h"
#include "uart.h"
#include "video.h"

#define STACK_SIZE 0x1000

extern struct task_struct* run_queue;
extern unsigned int CPU_FREQ;

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
    // 1. 尋找並載入新程式
    void* entry_point = find_program(path);
    if (entry_point == 0) {
        return -1; // 找不到檔案，exec 失敗
    }

    struct task_struct *current = get_current();

    // 2. 重設 Program Counter (PC) 到新程式的開頭
    regs->sepc = (unsigned long)entry_point;

    // 3. 重設 User Stack (把指標指回最高位址，相當於清空 Stack)
    regs->sp = current->user_sp + STACK_SIZE;

    // 4. 清理 a0 暫存器 (可選，避免把舊程式的資料洩漏給新程式)
    regs->a0 = 0;

    // exec 成功後，理論上舊的程式碼再也不會執行到了
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

    struct task_struct *curr = run_queue;
    struct task_struct *entry_node = run_queue;
    int found = 0;
    
    // 走訪 Queue 尋找目標 PID
    do {
        if (curr->pid == pid) {
            // 找到目標，將其標記為 ZOMBIE
            curr->state = TASK_ZOMBIE;
            found = 1;
            break;
        }
        curr = curr->next;
    } while (curr != entry_node);
    
    if (found){
        if (pid == get_current()->pid)
            thread_exit();
        return 0;
    }
    // 找不到對應的 PID
    return -1; 
}

void sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height){
    video_bmp_display(bmp_image, width, height);
}

// 實作 Syscall 9: usleep
int sys_usleep(unsigned int usec) {
    if (usec < 0)
        return -1;
    unsigned long long ticks = (unsigned long long)usec * (CPU_FREQ / 1000000);
    unsigned long long start_time = get_time();

    while ((get_time() - start_time) < ticks) {
        schedule(); // 關鍵：讓出 CPU！
    }
    
    return 0;
}

// 10: signal (註冊信號處理器)
long sys_signal(int sig, unsigned long handler) {
    // 檢查信號代碼是否合法
    if (sig < 0 || sig >= MAX_SIGNALS) return -1;
    
    struct task_struct *curr = get_current();
    curr->signal_handlers[sig] = handler;
    return 0;
}

// 11: sigreturn (從信號處理器返回)
void sys_sigreturn(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // ==========================================================
    // 🌟 Advanced Part: 回收臨時的 Signal Stack
    // ==========================================================
    if (curr->signal_stack != 0) {
        // 呼叫 free 將剛才 allocate 的 4KB 記憶體還給系統
        free((void *)curr->signal_stack);
        
        // 🚨 極度重要：釋放後一定要把指標歸零！
        // 避免下次沒發信號時，系統誤以為這裡還有記憶體可以 free (Double Free)
        curr->signal_stack = 0; 
    }

    // 1. 從 signal_saved_context 把暫存器全部「倒回」目前的 pt_regs 中
    char *src = (char *)&curr->signal_saved_context;
    char *dst = (char *)regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst[i] = src[i];
    }
    // 2. 解除處理中的狀態，允許接收下一個信號
    curr->is_handling_signal = 0;
    uart_puts("This is sigreturn\n");
}

// 12: kill (發送信號給指定行程)
int sys_kill(int pid, int sig) {
    if (sig < 0 || sig >= MAX_SIGNALS)
        return -1;
    
    struct task_struct *target = get_task_by_pid(pid);
    if (!target)
        return -1; // 找不到該行程
    // 把對應的 bit 設為 1 (打上標記)
    target->pending_signals |= (1ULL << sig);
    return 0;
}

// System Call 派發中心
void syscall_handler(struct pt_regs *regs) {
    // 直接透過欄位名稱取得 syscall number (a7)
    unsigned long syscall_num = regs->a7;
    
    long ret = -1;

    switch (syscall_num) {
        case 0: // getpid
            ret = sys_getpid();
            break;
            
        case 1: // uart_read
            ret = sys_uart_read((char*)regs->a0, regs->a1);
            break;
            
        case 2: // uart_write
            ret = sys_uart_write((const char*)regs->a0, regs->a1);
            break;
            
        case 3: // exec
            ret = sys_exec((const char*)regs->a0, regs);
            break;
            
        case 4: // fork
            // 實作 fork 時，需要把整份 regs (Trap Frame) 傳進去複製
            ret = sys_fork(regs);
            break;
            
        case 5: // waitpid
            ret = sys_waitpid(regs->a0);
            break;
            
        case 6: // exit
            sys_exit(regs->a0);
            break;
            
        case 7: // stop
            // 強制將特定 PID 設為 ZOMBIE
            ret = sys_stop(regs->a0);
            break;
            
        case 8: // display
            // a0 = bmp_image, a1 = width, a2 = height
            sys_display((unsigned int*)regs->a0, (unsigned int)regs->a1, (unsigned int)regs->a2);
            ret = 0; // void 回傳值不重要，預設給 0
            break;
            
        case 9: // usleep
            // a0 = usec
            ret = sys_usleep((unsigned int)regs->a0);
            break;
            
        case 10: 
            ret = sys_signal((int)regs->a0, regs->a1); 
            break;
                        
        case 11: 
            // 🚨 終極地雷防禦：sigreturn 還原了暫存器，必須立刻離開！
            sys_sigreturn(regs);
            return;
            
        case 12: 
            ret = sys_kill((int)regs->a0, (int)regs->a1); 
            break;
            
        default:
            uart_puts("Unknown syscall number: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            break;
    }

    // 將回傳值寫回 a0，這樣 User 程式從 ecall 醒來時，就會在 a0 看到結果
    regs->a0 = (unsigned long)ret;
}
