#include "deviceTree.h"
#include "uart.h"
#include "timer.h"
#include "sbi.h"
#include "mem_alloc.h"
#include "task.h"
#include "thread.h"

unsigned long CPU_FREQ = 0;
#define SCHED_TICK (CPU_FREQ / 32)
extern struct task_struct* run_queue;

typedef struct timer_node {
    unsigned long expire_time; // trigger time
    timer_callback_t callback; // the calling function
    void* args;       // the args for calling function
    struct timer_node* next; 
} timer_node_t;

timer_node_t* timer_list_head = NULL;

void set_next_timer() {
    /*unsigned long next_time = get_time() + SCHED_TICK;
    uart_puts("This is heartbeat ");
    uart_hex(next_time);
    uart_putc('\n');*/
    add_timer(set_next_timer, NULL, SCHED_TICK);
    schedule();
}

void timer_init(unsigned long dtb_ptr) {
    // Get CPU frequency rate from device tree
    int cpus_offset = fdt_path_offset(dtb_ptr, "/cpus");
    int len;
    const void* prop = fdt_getprop(dtb_ptr, cpus_offset, "timebase-frequency", &len);
    if (prop != NULL)
        CPU_FREQ = bswap32(*(const unsigned int*)prop);    
    else 
        return;
    
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 5); // 開啟 STIE (Supervisor Timer Interrupt Enable)
    asm volatile("csrw sie, %0" : : "r"(sie));
    // 🌟 最初的推力：設定開機後的第一次鬧鐘
    add_timer(set_next_timer, NULL, SCHED_TICK);
}

void add_timer(timer_callback_t cb, void* args, unsigned long duration_ticks) {
    // allocate memory space for new timer node
    timer_node_t* new_node = (timer_node_t*)allocate(sizeof(timer_node_t));
    
    // Set the timer node
    new_node->callback = cb;
    new_node->args = args;
    new_node->expire_time = get_time() + duration_ticks;
    new_node->next = NULL;

    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));

    // if timeset < the time of first node in timer list, put new node at the first of timer list
    if (timer_list_head == NULL || new_node->expire_time < timer_list_head->expire_time) {
        new_node->next = timer_list_head;
        timer_list_head = new_node;
        sbi_set_timer(new_node->expire_time); 
    }
    // if timeset >= the time of first node in timer list, find the position and put the new timer node there
    else {
        timer_node_t* current = timer_list_head;
        while (current->next != NULL && current->next->expire_time <= new_node->expire_time) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

void handle_timer_interrupt() {
    unsigned long current_time = get_time();
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    

    // Check all the time in timer list
    while (timer_list_head != NULL && timer_list_head->expire_time <= current_time) {
        timer_node_t* expired_node = timer_list_head;
        timer_list_head = timer_list_head->next; 

        // execute the function
        add_task(expired_node->callback, expired_node->args, TASK_PRIORITY_TIMER);

        free(expired_node); 
    }
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));

    // Reset the timer for next timer node
    if (timer_list_head != NULL) 
        sbi_set_timer(timer_list_head->expire_time);
    // If there is no timer node, set a infinity number for timer not to trigger interrupt 
    else 
        sbi_set_timer(-1ULL); 
}

// boot timer
void print_boot_time(void* arg) {
    static unsigned long long start_ticks = 0;
    unsigned long long current_ticks = get_time();
    if (!start_ticks) 
        start_ticks = current_ticks;
    unsigned int seconds = (unsigned int)((current_ticks - start_ticks) / CPU_FREQ);
    uart_puts("boot time: ");
    uart_dec(seconds);
    uart_putc('\n');
    add_timer(print_boot_time, NULL, 2);
}

// command "settimeout"
void timeout_callback(void* arg) {
    uart_puts((char *)arg);
    uart_putc('\n');
    free(arg);
}
