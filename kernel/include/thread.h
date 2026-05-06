#ifndef THREAD_H
#define THREAD_H

#include "trap.h"
#define TASK_RUNNING 0
#define TASK_ZOMBIE  1

// Maximum number of signal
#define MAX_SIGNALS 32

struct task_struct {
    struct thread_struct {
        unsigned long ra;
        unsigned long sp;
        unsigned long s[12];
    } thread;
    int pid;
    int state;
    void (*entry_point)();
    unsigned long kernel_sp;
    unsigned long user_sp;
    struct task_struct* next;
    unsigned long kernel_stack;
    unsigned long user_stack;
    unsigned long signal_stack;
    // 1. 信號處理函數註冊表 (Signal Handlers)
    // 陣列索引代表信號代碼 (Signal ID)，存放的值是 User Program 註冊的 Function Address
    unsigned long signal_handlers[MAX_SIGNALS]; 
    
    // 2. 待處理信號遮罩 (Pending Signals)
    // 用 Bitmask (位元遮罩) 來記錄，例如第 2 個 bit 為 1 代表收到了 Signal 2
    unsigned long pending_signals; 
    
    // 3. 備份暫存器 (Signal Saved Context)
    // 當我們要強迫程式去執行 handler 時，必須把被打斷的當下狀態 (sepc, a0-a7 等) 備份在這裡，
    // 等到 sys_sigreturn 被呼叫時，再從這裡還原回去。
    struct pt_regs signal_saved_context; 
    
    // 4. 執行狀態旗標 (Is Handling Signal)
    // 避免在處理信號的過程中，又被新的信號打斷 (避免巢狀信號處理導致備份被覆蓋)
    int is_handling_signal;
};

struct task_struct* get_current();
struct task_struct* get_task_by_pid(int pid);
void schedule();
struct task_struct* thread_create(void (*threadfn)());
struct task_struct* user_process_create(void (*entry)());
long fork_process(struct pt_regs *regs);
long thread_wait(long pid);
void thread_exit();
void thread_handle_signals(struct pt_regs *regs);
void kill_zombies();
void init_thread_queue();
void idle();
void foo();

#endif // THREAD_H
