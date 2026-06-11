#include "defint.h"
#include "thread.h"
#include "trap.h"
#include "timer.h"
#include "mem_alloc.h"
#include "uart.h"

#define STACK_SIZE 0x1000
extern void ret_from_exception(); // start.S
extern void switch_to(struct task_struct* prev, struct task_struct* next); // start.S
extern char sigreturn_trampoline_start[]; // start.S
extern char sigreturn_trampoline_end[]; // start.S

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
struct task_struct* user_process_create(void (*entry)()){
    struct task_struct* task = allocate(sizeof(struct task_struct));
    if (!task){
        uart_puts("User thread allocation failed.\n");
        return NULL;
    }
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    
    task->kernel_stack = (unsigned long)allocate(STACK_SIZE); 
    task->kernel_sp = task->kernel_stack + STACK_SIZE;
    
    task->user_stack = (unsigned long)allocate(STACK_SIZE);
    task->user_sp = task->user_stack + STACK_SIZE;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    task->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }

    // Simulate back from "sret" condition
    struct pt_regs* regs = (struct pt_regs*)(task->kernel_sp - sizeof(struct pt_regs));
    for (int i = 0; i < sizeof(struct pt_regs) / 8; i++){
        ((unsigned long*)regs)[i] = 0;
    }

    regs->tp = (unsigned long)task;
    regs->sepc = (unsigned long)entry;      // user process entry
    regs->sp = task->user_sp;               // user stack
    
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

    // Return value 0 for child thread
    child_regs->a0 = 0;
    child_regs->tp = (unsigned long)child;
    
    // Calculate parent_sp offset and copy it to child_sp
    unsigned long user_sp_offset = parent->user_sp - regs->sp;
    child_regs->sp = child->user_sp - user_sp_offset;

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


