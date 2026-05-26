#ifndef THREAD_H
#define THREAD_H

#include "trap.h"

#define TASK_RUNNING 0
#define TASK_ZOMBIE  1

// Maximum number of signal
#define MAX_SIGNALS 64

#define MAX_VMAS 16

struct vm_area_struct {
    unsigned long vm_start; // 區塊起始虛擬位址
    unsigned long vm_end;   // 區塊結束虛擬位址 (vm_start + size)
    int vm_prot;            // 區塊權限 (R/W/X)
    int vm_flags;
    int used;               // 是否被使用
};

struct task_struct {
    struct thread_struct {
        unsigned long ra; // return address for "ret"
        unsigned long sp; // kernel SP
        unsigned long s[12];
    } thread;
    int pid;
    int state;
    void (*entry_point)(); // for kernel thread
    unsigned long kernel_sp; // the highest address in kernel stack
    unsigned long user_sp; // the highest address in user stack
    struct task_struct* next;
    unsigned long kernel_stack;
    unsigned long user_stack;
    unsigned long signal_stack;
    unsigned long pending_signals; // Pending Signals (bit mask)
    int is_handling_signal;// Is Handling Signal (boolean)
    unsigned long signal_handlers[MAX_SIGNALS]; // signal handlers, find by signal num
    struct pt_regs signal_saved_context; // regs backup, sigreturn will return back to original regs 
    // 👇 [新增] Virtual Memory 的專屬欄位：指向該 Process 的 Page Table
    unsigned long *pgd;
    // 👇 [新增] 紀錄程式碼的實體位址與大小，供 fork 使用
    void* code_frame;
    unsigned int code_size;
    unsigned long cpio_addr;
    
    struct vm_area_struct vmas[MAX_VMAS];
};

struct task_struct* get_current();
struct task_struct* get_task_by_pid(int pid);
void schedule();
struct task_struct* thread_create(void (*threadfn)());
struct task_struct* user_process_create(unsigned long filesize, void* prog_va);
long fork_process(struct pt_regs *regs);
long thread_wait(long pid);
void thread_exit();
void thread_handle_signals(struct pt_regs *regs);
void kill_zombies();
void init_thread_queue();
int is_overlap(struct task_struct *curr, unsigned long start, unsigned long len);
unsigned long find_free_vma_region(struct task_struct *curr, unsigned long len);
void idle();
void foo();

#endif // THREAD_H
