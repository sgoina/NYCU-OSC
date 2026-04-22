#include "task.h"
#include "defint.h"
#include "uart.h"
#include "mem_alloc.h"

// 任務節點結構
typedef struct task_node {
    int priority;
    task_callback_t callback;
    char arg[32];
    struct task_node* next;
} task_node_t;

task_node_t* task_head = NULL;
task_node_t* task_tail = NULL;

int current_task_priority = 9999;

// 將任務加入下半部排隊
void add_task(task_callback_t cb, char* arg, int priority) {
    task_node_t* new_node = (task_node_t*)allocate(sizeof(task_node_t));
    if (!new_node) {
        uart_puts("[Error] add_task: Memory allocation failed!\n");
        return;
    }
    new_node->callback = cb;
    new_node->priority = priority;
    
    int i = 0;
    while(arg && arg[i] != '\0' && i < 31) {
        new_node->arg[i] = arg[i]; i++;
    }
    new_node->arg[i] = '\0';
    new_node->next = NULL;

    // 進入 Critical Section 保護 Queue (中斷可能在任何時候發生)
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    if (task_head == NULL || priority < task_head->priority) {
        new_node->next = task_head;
        task_head = new_node;
    }
    else {
        task_node_t* curr = task_head;
        while (curr->next != NULL && curr->next->priority <= priority) {
            curr = curr->next;
        }
        new_node->next = curr->next;
        curr->next = new_node;
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// 執行所有排隊的任務
void run_tasks() {
    while (1) {
        // 拿取任務時必須保護 Queue
        unsigned long sstatus;
        asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
        
        if (task_head == NULL || task_head->priority >= current_task_priority) {
            asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
            break; 
        }
        
        task_node_t* task = task_head;
        task_head = task_head->next;
        if (task_head == NULL)
            task_tail = NULL;
        
        asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
        
        // ==========================================
        // 準備執行任務，保存上下文 (Context Switch)
        // ==========================================
        // 1. 記住原本被中斷的任務優先權
        int prev_priority = current_task_priority; 
        
        // 2. 把系統當前的優先權「提升」為這個新任務的優先權
        current_task_priority = task->priority;    

        // 3. 打開中斷！這樣在跑這個任務時，如果有更高等級的中斷進來，才能搶佔它
        asm volatile("csrsi sstatus, 2");

        // 🎯 執行真正的 Callback！
        task->callback(task->arg);

        // 4. 執行完畢，關閉中斷保護資料
        asm volatile("csrci sstatus, 2");

        // 5. 任務跑完了，把系統的優先權「降回」原本的狀態
        current_task_priority = prev_priority;
        
        free(task);
    }
}
