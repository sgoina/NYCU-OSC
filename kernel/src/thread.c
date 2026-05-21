#include "defint.h"
#include "utils.h"
#include "thread.h"
#include "trap.h"
#include "timer.h"
#include "mem_alloc.h"
#include "uart.h"
#include "utils.h"
#include "vm.h"

#define STACK_SIZE 0x1000

#define align(size, align_val) (((size) + (align_val) - 1) & ~((align_val) - 1))

extern void ret_from_exception(); // start.S
extern void switch_to(struct task_struct* prev, struct task_struct* next); // start.S
extern char sigreturn_trampoline_start[]; // start.S
extern char sigreturn_trampoline_end[]; // start.S
extern unsigned long pgd[]; // vm.c

struct task_struct* run_queue = 0;
int nr_threads = 0; // new pid number

static void enqueue(struct task_struct** queue, struct task_struct* task) {
    task->state = TASK_RUNNING;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // If the new task is the first task in run_queue
    if (*queue == 0) {
        *queue = task;
        task->next = task;
    }
    else {
        struct task_struct* tail = *queue;
        // Find the last element
        while (tail->next != *queue) {
            tail = tail->next;
        }
        tail->next = task;
        task->next = *queue;
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

struct task_struct* get_current() {
    // register: put the variable in register instead of RAM
    register struct task_struct* current asm("tp");
    return current;
}

// Find the task_struct by pid
struct task_struct* get_task_by_pid(int pid) {
    if (!run_queue)
        return NULL;
    
    struct task_struct* curr = run_queue;
    do {
        if (curr->pid == pid) {
            return curr;
        }
        curr = curr->next;
    } while (curr != run_queue);
    // Can't find
    return NULL;
}

void schedule() {
    struct task_struct* prev = get_current();
    struct task_struct* next = prev->next;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // skip zombie thread
    while (next->state == TASK_ZOMBIE && next != prev) {
        next = next->next;
    }
    // jump to next thread
    if (prev != next) {
        // --- 👇 [新增] Virtual Memory 切換邏輯 ---
        // 1. 如果 next 是 User Process (擁有自己的 PGD)
        if (next->pgd != NULL) {
            // 將虛擬位址轉換為實體位址
            unsigned long pgd_pa = (unsigned long)next->pgd - PAGE_OFFSET;
            
            // 寫入 satp 並刷新 TLB
            asm volatile(
                "sfence.vma zero, zero\n"
                "csrw satp, %0\n"
                "sfence.vma zero, zero\n"
                :
                : "r"(MAKE_SATP(pgd_pa))
                : "memory"
            );
        } 
        // 2. 如果 next 是 Kernel Thread (沒有自己的 PGD，共用 Kernel PGD)
        else {
            unsigned long pgd_pa = (unsigned long)pgd - PAGE_OFFSET;
            
            // 寫入 satp 並刷新 TLB
            asm volatile(
                "sfence.vma zero, zero\n"
                "csrw satp, %0\n"
                "sfence.vma zero, zero\n"
                :
                : "r"(MAKE_SATP(pgd_pa))
                : "memory"
            );
        }
        // --- 👆 [新增] Virtual Memory 切換邏輯結束 ---
    
        switch_to(prev, next);
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// for creating new kernel thread
void kernel_thread_wrapper() {
    // enable interrupt because of disable in schedule()
    asm volatile("csrsi sstatus, 2");

    struct task_struct* curr = get_current();

    if (curr->entry_point)
        curr->entry_point();
}

// Create kernel thread
struct task_struct* thread_create(void (*threadfn)()) {
    if (threadfn == NULL){
        uart_puts("Can't create a NULL thread.\n");
        return NULL;
    }
    struct task_struct* task = allocate(sizeof(struct task_struct));   
    if (!task){
        uart_puts("Thread thread allocation failed.\n");
        return NULL;
    }
    // Need enalbe interrupt because schedule() will disable before switch_to()
    task->thread.ra = (unsigned long)kernel_thread_wrapper;
    // The function of kernel thread
    task->entry_point = threadfn;
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    task->kernel_stack = (unsigned long)allocate(STACK_SIZE);
    task->kernel_sp = task->kernel_stack + STACK_SIZE;
    task->user_stack = 0; // because this thread is kernel thread
    task->thread.sp = task->kernel_sp;
    task->pgd = NULL; // for kernel thread
    
    // initialize signal-related info
    task->signal_stack = 0;
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    enqueue(&run_queue, task);
    return task;
}

// Create user thread
struct task_struct* user_process_create(unsigned long filesize, void* program_pa) {
    struct task_struct* task = allocate(sizeof(struct task_struct));
    if (!task){
        uart_puts("User thread allocation failed.\n");
        return NULL;
    }
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    
    task->kernel_stack = (unsigned long)allocate(STACK_SIZE); 
    task->kernel_sp = task->kernel_stack + STACK_SIZE;
    
    // ❌ 刪除這兩行重複的分配
    //task->user_stack = (unsigned long)allocate(STACK_SIZE);
    //task->user_sp = task->user_stack + STACK_SIZE;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    task->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    /// --- 👇 Virtual Memory 核心新增邏輯 ---

    // 1. 分配專屬的 PGD (注意 allocate 回傳的是 Kernel VA)
    task->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(task->pgd, 0, PAGE_SIZE);
    
    // 🌟 關鍵修正：將 Kernel 的記憶體宇宙（上半部）複製給 User Process
    // 在 Sv39 中，index 256 到 511 負責處理高位址空間
    for (int i = 256; i < 512; i++) {
        task->pgd[i] = pgd[i];
    }

    // 2. 分配實體 Stack
    task->user_stack = (unsigned long)allocate(STACK_SIZE);
    task->user_sp = USER_STACK_VA; // 虛擬位址固定！

    // 3. 映射 Code 與 Stack 到專屬 PGD
    // 必須將 allocate 回傳的 VA 轉為 PA 才能存入 Page Table
    unsigned long stack_pa = (unsigned long)task->user_stack - PAGE_OFFSET;
    
    // ✅ 關鍵修正：確認 program_pa 是否為虛擬位址，若是，必須扣除 PAGE_OFFSET
    // 假設從 find_program 拿到的是 Kernel VA:
    unsigned long code_pa = (unsigned long)program_pa;
    if (code_pa >= PAGE_OFFSET) {
        code_pa -= PAGE_OFFSET;
    }
    
    map_pages(task->pgd, USER_CODE_VA, align(filesize, PAGE_SIZE), code_pa, PROT_USER_RX);
    map_pages(task->pgd, USER_STACK_VA - STACK_SIZE, STACK_SIZE, stack_pa, PROT_USER_RW);

    // --- 👆 Virtual Memory 核心新增邏輯結束 ---

    // Simulate back from "sret" condition
    struct pt_regs* regs = (struct pt_regs*)(task->kernel_sp - sizeof(struct pt_regs));
    for (int i = 0; i < sizeof(struct pt_regs) / 8; i++){
        ((unsigned long*)regs)[i] = 0;
    }

    regs->tp = (unsigned long)task;
    
    // 💡 關鍵改變：sepc 和 sp 綁死在固定的虛擬位址！
    regs->sepc = USER_CODE_VA;             
    regs->sp = task->user_sp;
    //regs->sepc = (unsigned long)entry;      // user process entry
    //regs->sp = task->user_sp;               // user stack
    
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    // setting U-Mode and enable interrupt
    sstatus &= ~(1 << 8); // SPP (Supervisor mode Previous Privilege mode): 1=Supervisor, 0=User
    sstatus |= (1 << 5);  // SPIE (Supervisor Previous Interrupt Enable)
    regs->sstatus = sstatus;

    task->thread.ra = (unsigned long)ret_from_exception; // Simulate completing exception and ready to go to U-Mode
    task->thread.sp = (unsigned long)regs; // for ret_from_exception

    enqueue(&run_queue, task);
    return task;
}

// Fork a thread
long fork_process(struct pt_regs *regs) {
    struct task_struct *parent = get_current();
    struct task_struct *child = allocate(sizeof(struct task_struct));
    if (!child) {
        uart_puts("Fork thread allocation failed.\n");
        return -1;
    }

    child->pid = nr_threads++;
    child->state = TASK_RUNNING;
    
    child->kernel_stack = (unsigned long)allocate(STACK_SIZE); 
    child->kernel_sp = child->kernel_stack + STACK_SIZE;
    
    child->user_stack = (unsigned long)allocate(STACK_SIZE);
    child->user_sp = child->user_stack + STACK_SIZE;
    
    // Copy user stack of parent thread
    char *src_user_stack = (char *)parent->user_stack;
    char *dst_user_stack = (char *)child->user_stack;
    for (int i = 0; i < STACK_SIZE; i++) {
        dst_user_stack[i] = src_user_stack[i];
    }
    
    child->pending_signals = 0;
    child->is_handling_signal = 0;
    child->signal_stack = 0;
    // signal handler is inherited from parent
    for (int i = 0; i < MAX_SIGNALS; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }

    // Simulate back from "sret" condition
    unsigned long regs_offset = parent->kernel_sp - (unsigned long)regs; // avoid there is something in parent->kernel stack, so need to calculate offset
    struct pt_regs *child_regs = (struct pt_regs *)(child->kernel_sp - regs_offset);
    // Copy regs from parent thread
    char *src_regs = (char *)regs;
    char *dst_regs = (char *)child_regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst_regs[i] = src_regs[i];
    }
    
    // 1. 為 Child 分配全新的 PGD
    child->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(child->pgd, 0, PAGE_SIZE);

    // 2. 複製 Stack 實體記憶體 (你原本寫的，保留！)
    child->user_stack = (unsigned long)allocate(STACK_SIZE);
    memcpy((void*)child->user_stack, (void*)parent->user_stack, STACK_SIZE);

    // 3. 【新增】複製 Code 實體記憶體！
    // 💡 提示：為了讓 fork 知道要複製多大的 Code，你可能需要在 task_struct 裡新增 code_size 和 code_frame_addr 欄位。
    child->code_frame = allocate(parent->code_size);
    memcpy((void*)child->code_frame, (void*)parent->code_frame, parent->code_size);

    // 4. 【新增】將複製好的 Code 和 Stack 映射到 Child 的 PGD 中
    unsigned long child_code_pa = (unsigned long)child->code_frame - PAGE_OFFSET;
    unsigned long child_stack_pa = (unsigned long)child->user_stack - PAGE_OFFSET;
    
    map_pages(child->pgd, USER_CODE_VA, parent->code_size, child_code_pa, PROT_USER_RX);
    map_pages(child->pgd, USER_STACK_VA - STACK_SIZE, STACK_SIZE, child_stack_pa, PROT_USER_RW);

    // Return value 0 for child thread
    child_regs->a0 = 0;
    child_regs->tp = (unsigned long)child;
    
    // Calculate parent_sp offset and copy it to child_sp
    //unsigned long user_sp_offset = parent->user_sp - regs->sp;
    //child_regs->sp = child->user_sp - user_sp_offset;
    // ✅ 現在的寫法：直接複製！因為大家的虛擬位址都一樣
    child_regs->sp = regs->sp;

    child->thread.ra = (unsigned long)ret_from_exception; // Simulate completing exception and ready to go to U-Mode
    child->thread.sp = (unsigned long)child_regs; // for ret_from_exception

    enqueue(&run_queue, child);
    // Return value child->pid for parent thread
    return child->pid;
}

// Waiting another thread
long thread_wait(long pid) {
    while (1) {
        struct task_struct *wait = get_task_by_pid(pid);

        if (!wait || wait->state == TASK_ZOMBIE)
            return pid;
        schedule();
    }
    return -1; // Impossible to be here
}

// Become zombie thread
void thread_exit() {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE;
    schedule();
}

// Handle signal
void thread_handle_signals(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // Determine exist, being handling signal, pending signal
    if (!curr || curr->is_handling_signal || curr->pending_signals == 0)
        return;

    // Find which signal number
    int signum = -1;
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (curr->pending_signals & (1ULL << i)) {
            signum = i;
            break;
        }
    }
    
    if (signum != -1) {
        // Get the specific handler
        unsigned long handler = curr->signal_handlers[signum];
        
        // Clear pending bit
        curr->pending_signals &= ~(1ULL << signum);

        if (handler != 0) {
            // backup regs
            char *src = (char *)regs;
            char *dst = (char *)&curr->signal_saved_context;
            for (int i = 0; i < sizeof(struct pt_regs); i++) {
                dst[i] = src[i];
            }

            // Mark is_handling bit
            curr->is_handling_signal = 1;

            // Initialize signal stack
            curr->signal_stack = (unsigned long)allocate(STACK_SIZE);
            unsigned long new_sp = curr->signal_stack + STACK_SIZE; 
            // Put trampoline code into signal stack
            unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline_start;
            
            new_sp -= tramp_size;
            new_sp &= ~0xF; // align to 16 bytes

            char *user_tramp_space = (char *)new_sp;
            for (int i = 0; i < tramp_size; i++) {
                user_tramp_space[i] = sigreturn_trampoline_start[i];
            }

            regs->ra = new_sp; // set return address to trampoline function 
            regs->sp = new_sp; // set stack pointer to signal stack pointer
            regs->sepc = handler; // Go to signal handler function
        }
        else {
            // There is no signal handler, ready to kill the thread
            if (curr->pid <= 1){
                uart_puts("Can't kill idle or kernel shell thread.\n");
                return;
            }
            thread_exit();
        }
    }
}

// Kill all zombie threads
void kill_zombies() {
    if (!run_queue)
        return;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    struct task_struct* curr = run_queue->next;
    struct task_struct* prev = run_queue;
    struct task_struct* start = run_queue;
    
    while (curr != start && run_queue != NULL) {
        if (curr->state == TASK_ZOMBIE) {
            prev->next = curr->next;

            struct task_struct* zombie = curr;
            curr = curr->next;

            if (zombie->kernel_stack)
                free((void*)zombie->kernel_stack);
            if (zombie->user_stack)
                free((void*)zombie->user_stack);
            if (zombie->signal_stack)
                free((void*)zombie->signal_stack);
            free(zombie);
        }
        else {
            prev = curr;
            curr = curr->next;
        }
    }
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
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


