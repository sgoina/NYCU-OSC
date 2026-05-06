#include "defint.h"
#include "thread.h"
#include "trap.h"
#include "timer.h"
#include "mem_alloc.h"
#include "uart.h"

#define STACK_SIZE 0x1000
extern void ret_from_exception(); 
extern void switch_to(struct task_struct* prev, struct task_struct* next);
extern char sigreturn_trampoline_start[];
extern char sigreturn_trampoline_end[];

struct task_struct* run_queue = 0;
int nr_threads = 0;

static void enqueue(struct task_struct** queue, struct task_struct* task) {
    task->state = TASK_RUNNING; // 初始化狀態
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    if (*queue == 0) {
        *queue = task;
        task->next = task;
    }
    else {
        struct task_struct* tail = *queue;
        // 尋找 tail 節點 (其 next 指向 head)
        while (tail->next != *queue) {
            tail = tail->next;
        }
        tail->next = task;
        task->next = *queue;
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

struct task_struct* get_current() {
    register struct task_struct* current asm("tp");
    return current;
}

// ==========================================================
// 🌟 Advanced Part: 提供給 sys_kill 使用的輔助函式
// 透過 PID 尋找對應的行程控制區塊
// ==========================================================
struct task_struct* get_task_by_pid(int pid) {
    if (!run_queue) return 0;
    
    struct task_struct* curr = run_queue;
    do {
        if (curr->pid == pid) {
            return curr;
        }
        curr = curr->next;
    } while (curr != run_queue);
    
    return 0; // 找不到該行程
}


void schedule() {
    // TODO: Implement this function
    struct task_struct* prev = get_current();
    struct task_struct* next = prev->next;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // 跳過已經變成 ZOMBIE 的任務
    while (next->state == TASK_ZOMBIE && next != prev) {
        next = next->next;
    }
    
    if (prev != next) {
        switch_to(prev, next);
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// 這就是所有 Kernel Thread 甦醒後，會統一進入的大門
void kernel_thread_wrapper() {
    // 1. 🌟 甦醒的第一件事：打開全域中斷！迎接 Timer 和 UART！
    asm volatile("csrsi sstatus, 2");

    // 2. 取得當前的任務結構
    struct task_struct* curr = get_current();

    // 3. 呼叫真正的目標函數 (例如助教的 foo)
    if (curr->entry_point) {
        curr->entry_point();
    }

    // 4. 🌟 防呆機制：目標函數執行完畢後，自動退出！
    // 這樣就算助教的測試檔忘記寫 thread_exit()，你的 OS 也不會崩潰跑飛。
    thread_exit();
}

struct task_struct* thread_create(void (*threadfn)()) {
    struct task_struct* task = allocate(sizeof(struct task_struct));    
    task->pid = nr_threads++;
    task->kernel_stack = (unsigned long)allocate(STACK_SIZE);
    task->kernel_sp = task->kernel_stack + STACK_SIZE;
    task->user_stack = 0;
    task->signal_stack = 0;
    task->thread.sp = task->kernel_sp;
    
    // 🌟 1. 把真正的函數 (foo) 記在 entry_point
    task->entry_point = threadfn;
    
    // 🌟 2. 貍貓換太子：讓 ra 指向我們的包裝函數！
    task->thread.ra = (unsigned long)kernel_thread_wrapper;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    enqueue(&run_queue, task);
    return task;
}

struct task_struct* user_process_create(void (*entry)()){
    struct task_struct* task = (struct task_struct*)allocate(sizeof(struct task_struct));
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    task->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }

    // kernel stack & user stack
    task->kernel_stack = (unsigned long)allocate(STACK_SIZE); 
    task->kernel_sp = task->kernel_stack + STACK_SIZE;
    
    task->user_stack = (unsigned long)allocate(STACK_SIZE);
    task->user_sp = task->user_stack + STACK_SIZE;

    // space for trap frame from kernel stack
    struct pt_regs* regs = (struct pt_regs*)(task->kernel_sp - sizeof(struct pt_regs));
    
    // Init
    for (int i = 0; i < sizeof(struct pt_regs) / 8; i++){
        ((unsigned long*)regs)[i] = 0;
    }

    // Trap Frame
    regs->tp = (unsigned long)task;
    regs->sepc = (unsigned long)entry;      // user space entry
    regs->sp = task->user_sp;               // user stack
    
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    sstatus &= ~(1 << 8);       // SPP = 0 (User Mode)
    sstatus |= (1 << 5);        // SPIE = 1 (Interrupt when user mode)
    regs->sstatus = sstatus;

    // Context (for switch_to)
    task->thread.ra = (unsigned long)ret_from_exception;
    task->thread.sp = (unsigned long)regs;

    enqueue(&run_queue, task);
    return task;
}

long fork_process(struct pt_regs *regs) {
    struct task_struct *parent = get_current();
    
    // 1. 配置子行程的 task_struct
    struct task_struct *child = (struct task_struct *)allocate(sizeof(struct task_struct));
    if (!child)
        return -1;

    // 2. 繼承狀態與分配新 PID (直接使用 thread.c 內部的 nr_threads)
    child->pid = nr_threads++;
    child->state = TASK_RUNNING;
    // ==========================================================
    // 🌟 Advanced Part: 繼承 Signal Handlers
    // POSIX 規定：子行程會繼承父行程註冊的 handler，但未處理的信號不會繼承！
    // ==========================================================
    child->pending_signals = 0;
    child->is_handling_signal = 0;
    child->signal_stack = 0;
    for (int i = 0; i < MAX_SIGNALS; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }

    // 3. 配置獨立的 Kernel Stack 與 User Stack
    child->kernel_stack = (unsigned long)allocate(STACK_SIZE); 
    child->kernel_sp = child->kernel_stack + STACK_SIZE;
    
    child->user_stack = (unsigned long)allocate(STACK_SIZE);
    child->user_sp = child->user_stack + STACK_SIZE;

    // 4. 複製 User Stack 內容 (4KB)
    char *src_user_stack = (char *)parent->user_stack;
    char *dst_user_stack = (char *)child->user_stack;
    for (int i = 0; i < STACK_SIZE; i++) {
        dst_user_stack[i] = src_user_stack[i];
    }

    // 5. 定位並複製 Kernel Stack 上的 Trap Frame (pt_regs)
    unsigned long regs_offset = parent->kernel_sp - (unsigned long)regs;
    struct pt_regs *child_regs = (struct pt_regs *)(child->kernel_sp - regs_offset);
    
    char *src_regs = (char *)regs;
    char *dst_regs = (char *)child_regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst_regs[i] = src_regs[i];
    }

    // 6. 設定子行程的回傳值為 0
    child_regs->a0 = 0;
    // 將 tp 暫存器指向子行程的 task_struct
    child_regs->tp = (unsigned long)child;
    
    // 確保子行程的 sp 偏移量與父行程一致 (指向自己的 User Stack)
    unsigned long user_sp_offset = parent->user_sp - regs->sp;
    child_regs->sp = child->user_sp - user_sp_offset;

    // 7. 設定排程切換 Context
    extern void ret_from_exception();
    child->thread.ra = (unsigned long)ret_from_exception;
    child->thread.sp = (unsigned long)child_regs;

    // 8. 加入排程佇列 (直接使用 thread.c 內部的 enqueue 與 run_queue)
    enqueue(&run_queue, child);

    // 9. 父行程回傳子行程的 PID
    return child->pid;
}

long thread_wait(long pid) {
    while (1) {
        int found = 0;
        int is_zombie = 0;

        struct task_struct *curr = run_queue;
        struct task_struct *entry = run_queue;
        
        if (!curr)
            return -1;

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

void thread_exit() {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE; // 標記為殭屍狀態，等待被回收
    schedule();                   // 交出 CPU，永遠不會再返回這裡
}

// 🌟 給 trap.c 呼叫的「信號發射器」
void thread_handle_signals(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // 如果沒有行程、正在處理信號、或是沒有待處理信號，就直接離開
    if (!curr || curr->is_handling_signal || curr->pending_signals == 0)
        return;

    // 找看看是哪個信號 (從第 0 個 bit 開始找)
    int sig = -1;
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (curr->pending_signals & (1ULL << i)) {
            sig = i;
            break;
        }
    }

    if (sig != -1) {
        unsigned long handler = curr->signal_handlers[sig];
        
        // 把該信號的待處理標記清除
        curr->pending_signals &= ~(1ULL << sig);

        if (handler != 0) {
            // == 準備「偷換」暫存器，強迫跳轉到 handler ==
            
            // 1. 備份當下被打斷的狀態
            char *src = (char *)regs;
            char *dst = (char *)&curr->signal_saved_context;
            for (int i = 0; i < sizeof(struct pt_regs); i++) {
                dst[i] = src[i];
            }

            // 2. 標記正在處理信號
            curr->is_handling_signal = 1;

            // ==========================================
            // 🌟 Advanced Part: 啟用獨立的 Signal Stack
            // ==========================================
            // 1. 動態配置一塊 4KB 的全新 Stack
            curr->signal_stack = (unsigned long)allocate(STACK_SIZE);
            unsigned long new_sp = curr->signal_stack + STACK_SIZE; 
            // 計算 trampoline 的大小 (位元組)
            unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline_start;
            
            // 2. 在全新的 Signal Stack 向下推，挪出空間放跳床
            new_sp -= tramp_size;
            new_sp &= ~0xF; 

            char *user_tramp_space = (char *)new_sp;
            for (int i = 0; i < tramp_size; i++) {
                user_tramp_space[i] = sigreturn_trampoline_start[i];
            }

            // 3. 🌟 偷天換日：將 CPU 的 SP 強制切換到全新的 Stack！
            regs->ra = new_sp;
            regs->sp = new_sp;
            regs->sepc = handler;
            regs->a0 = sig;
        }
        else {
            // 如果沒有註冊 handler，Linux 預設動作通常是殺掉行程
            if (curr->pid <= 1){
                uart_puts("Can't kill idle or kernel shell thread.\n");
                return;
            }
            thread_exit();
        }
    }
}

void kill_zombies() {
    if (!run_queue)
        return;

    struct task_struct* curr = run_queue;
    struct task_struct* prev = run_queue;

    // 先找到 prev 的初始位置 (tail)
    while (prev->next != run_queue) {
        prev = prev->next;
    }

    struct task_struct* start = run_queue;
    do {
        if (curr->state == TASK_ZOMBIE) {
            // 將 ZOMBIE 從 Linked List 中移除
            prev->next = curr->next;
            
            if (curr == run_queue) {
                run_queue = curr->next;
                // 防呆：如果 queue 裡只剩一個自己，且被砍了
                if (run_queue == curr)
                    run_queue = NULL;
            }

            struct task_struct* zombie = curr;
            curr = curr->next; // curr 繼續往下走

            // 🌟 5. 精準回收三大 Stack，徹底杜絕 Memory Leak
            if (zombie->kernel_stack) free((void*)zombie->kernel_stack);
            if (zombie->user_stack) free((void*)zombie->user_stack);
            if (zombie->signal_stack) free((void*)zombie->signal_stack);
            free(zombie);
        }
        else {
            prev = curr;
            curr = curr->next;
        }
    } while (curr != start && run_queue != NULL);
}

void idle() {
    while (1) {
        kill_zombies();
        schedule();
    }
}

void foo() {
    for (int i = 0; i < 5; i++) {
        uart_puts("Process ID: ");
        uart_dec(get_current()->pid);
        uart_puts(" ");
        uart_dec(i);
        uart_puts("\n");
        for (int j = 0; j < 100000000; j++)
            ;
        schedule();
    }
    thread_exit(); 
}


