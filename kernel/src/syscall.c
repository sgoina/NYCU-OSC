#include "trap.h"
#include "thread.h"
#include "ramfs.h"
#include "uart.h"

extern struct task_struct* run_queue;

// 0: getpid
long sys_getpid() {
    return get_current()->pid;
}

// 1: uart_read
long sys_uart_read(char *buf, long count) {
    long read_bytes = 0;
    for (long i = 0; i < count; i++) {
        // 這裡假設 uart_getc 是一個會阻塞 (blocking) 等待字元的函式
        buf[i] = uart_getc();
        read_bytes++;
        // 簡單的 Enter 處理，視你的終端機行為可微調
        if (buf[i] == '\r' || buf[i] == '\n')
            break; 
    }
    return read_bytes;
}

// 2: uart_write
long sys_uart_write(const char *buf, long count) {
    long written = 0;
    for (long i = 0; i < count; i++) {
        uart_putc(buf[i]);
        written++;
    }
    return written;
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
    regs->sp = current->user_sp;

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
    while (1) {
        int found = 0;
        int is_zombie = 0;

        struct task_struct *curr = run_queue;
        struct task_struct *entry = run_queue;
        
        if (!curr) return -1;

        // 走訪 Queue 尋找指定的子行程
        do {
            if (curr->pid == pid) {
                found = 1;
                if (curr->state == TASK_ZOMBIE) {
                    is_zombie = 1;
                }
                break;
            }
            curr = curr->next;
        } while (curr != entry);

        // 如果找不到該 PID，或是該 PID 已經變成 ZOMBIE 了，就結束等待
        // (在進階的 OS 中，這裡應該要把 ZOMBIE 從 Queue 移除並釋放記憶體)
        if (!found || is_zombie) {
            return pid;
        }

        // 行程還活著，父行程主動交出 CPU 繼續等
        schedule();
    }
}

// 6: exit
void sys_exit(int status) {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE; // 標記為殭屍行程
    // status 可以存在 task_struct 裡供 waitpid 讀取，Lab 5 不強制
    
    schedule(); // 交出 CPU
    while(1);   // 防呆
}

// 7: stop
int sys_stop(long pid) {
    // 防呆：不能終止自己 (不然誰來回傳結果？)，也不能終止不存在的 run_queue
    if (pid == get_current()->pid || run_queue == 0) {
        return -1; 
    }

    struct task_struct *curr = run_queue;
    struct task_struct *entry_node = run_queue;

    // 走訪 Queue 尋找目標 PID
    do {
        if (curr->pid == pid) {
            // 找到目標，將其標記為 ZOMBIE
            curr->state = TASK_ZOMBIE;
            return 0; // 成功
        }
        curr = curr->next;
    } while (curr != entry_node);

    // 找不到對應的 PID
    return -1; 
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
            
        default:
            uart_puts("Unknown syscall number: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            break;
    }

    // 將回傳值寫回 a0，這樣 User 程式從 ecall 醒來時，就會在 a0 看到結果
    regs->a0 = (unsigned long)ret;
}
