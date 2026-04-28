#include "defint.h"
#include "thread.h"
#include "mem_alloc.h"
#include "uart.h"

#define STACK_SIZE 0x1000
extern void switch_to(struct task_struct* prev, struct task_struct* next);

int nr_threads = 0;
struct task_struct* run_queue = 0;

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


