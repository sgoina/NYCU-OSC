#include "defint.h"
#include "thread.h"
#include "trap.h"
#include "mem_alloc.h"
#include "uart.h"

#define STACK_SIZE 0x1000
extern void switch_to(struct task_struct* prev, struct task_struct* next);
extern void ret_from_exception(); 

struct task_struct* run_queue = 0;
int nr_threads = 0;

static void enqueue(struct task_struct** queue, struct task_struct* task) {
    task->state = TASK_RUNNING; // 初始化狀態
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
}

struct task_struct* get_current() {
    register struct task_struct* current asm("tp");
    return current;
}


void schedule() {
    // TODO: Implement this function
    struct task_struct* prev = get_current();
    struct task_struct* next = prev->next;

    // 跳過已經變成 ZOMBIE 的任務
    while (next->state == TASK_ZOMBIE && next != prev) {
        next = next->next;
    }

    if (prev != next) {
        switch_to(prev, next);
    }
}

struct task_struct* thread_create(void (*threadfn)()) {
    struct task_struct* task = allocate(sizeof(struct task_struct));    
    task->pid = nr_threads++;
    task->stack = (unsigned long)allocate(STACK_SIZE);
    task->thread.ra = (unsigned long)threadfn;
    task->thread.sp = task->stack + STACK_SIZE;
    enqueue(&run_queue, task);
    return task;
}

struct task_struct* user_process_create(void (*entry)()){
    struct task_struct* task = (struct task_struct*)allocate(sizeof(struct task_struct));
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;

    // kernel stack & user stack
    task->stack = (unsigned long)allocate(STACK_SIZE); // kernel stack
    task->kernel_sp = task->stack + STACK_SIZE;
    task->user_sp = (unsigned long)allocate(STACK_SIZE) + STACK_SIZE;

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
    if (!child) return -1;

    // 2. 繼承狀態與分配新 PID (直接使用 thread.c 內部的 nr_threads)
    child->pid = nr_threads++;
    child->state = TASK_RUNNING;

    // 3. 配置獨立的 Kernel Stack 與 User Stack
    child->stack = (unsigned long)allocate(STACK_SIZE); 
    child->kernel_sp = child->stack + STACK_SIZE;
    
    unsigned long child_user_stack_base = (unsigned long)allocate(STACK_SIZE);
    child->user_sp = child_user_stack_base + STACK_SIZE;

    // 4. 複製 User Stack 內容 (4KB)
    char *src_user_stack = (char *)(parent->user_sp - STACK_SIZE);
    char *dst_user_stack = (char *)(child->user_sp - STACK_SIZE);
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

void thread_exit() {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE; // 標記為殭屍狀態，等待被回收
    schedule();                   // 交出 CPU，永遠不會再返回這裡
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

            // 釋放 Stack 空間與 task_struct 記憶體
            if (zombie->stack) {
                free((void*)zombie->stack);
            }
            uart_puts("kill pid = ");
            uart_dec(zombie->pid);
            uart_putc('\n');
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
        uart_hex(get_current()->pid);
        uart_puts(" ");
        uart_hex(i);
        uart_puts("\n");
        for (int j = 0; j < 100000000; j++)
            ;
        schedule();
    }
    thread_exit(); 
}


