#include "task.h"
#include "defint.h"
#include "uart.h"
#include "mem_alloc.h"

typedef struct task_node {
    int priority; // smaller => more important task
    task_callback_t callback; // the call function
    void* args;
    struct task_node* next;
} task_node_t;

task_node_t* task_head = NULL;

int current_task_priority = 9999; // global priority

void add_task(task_callback_t cb, void* args, int priority) {
    task_node_t* new_node = (task_node_t*)allocate(sizeof(task_node_t));
    if (!new_node) {
        uart_puts("[Error] add_task: Memory allocation failed!\n");
        return;
    }
    // set new task node
    new_node->callback = cb;
    new_node->priority = priority;
    new_node->args = args;
    new_node->next = NULL;

    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // if new priority < the priority of first node in task list, put new node at the first of task list
    if (task_head == NULL || priority < task_head->priority) {
        new_node->next = task_head;
        task_head = new_node;
    }
    // if new priority >= the priority of first node in task list, find the position and put the new task node there
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

void run_tasks() {
    while (1) {
        // Critical Section
        unsigned long sstatus;
        asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
        // If the current task is more important, no need to run another task and keep doing current task 
        if (task_head == NULL || task_head->priority >= current_task_priority) {
            asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
            break; 
        }  
        task_node_t* task = task_head;
        task_head = task_head->next;
        
        // save priority and set new priority,ready to do context switch
        int prev_priority = current_task_priority; 
        current_task_priority = task->priority;    
        asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
        
        // irq enable for higher interrupt
        asm volatile("csrsi sstatus, 2");
        if (task->callback)
            task->callback(task->args);
        // irq disable
        asm volatile("csrci sstatus, 2");
        
        // Critical Section
        asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
        // return previous priority and ready to do context switch
        current_task_priority = prev_priority;
        asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
        
        free(task);
    }
}
