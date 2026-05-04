#include "deviceTree.h"
#include "uart.h"
#include "timer.h"
#include "sbi.h"
#include "mem_alloc.h"
#include "task.h"
#include "thread.h"

unsigned int CPU_FREQ = 0;

typedef struct timer_node {
    unsigned long expire_time; // trigger time
    timer_callback_t callback; // the calling function
    void* args;       // the args for calling function
    struct timer_node* next; 
} timer_node_t;

timer_node_t* timer_list_head = NULL;

void timer_init(unsigned long dtb_ptr) {
    // Get CPU frequency rate from device tree
    int cpus_offset = fdt_path_offset(dtb_ptr, "/cpus");
    int len;
    const void* prop = fdt_getprop(dtb_ptr, cpus_offset, "timebase-frequency", &len);
    if (prop != NULL)
        CPU_FREQ = bswap32(*(const unsigned int*)prop);    
    else 
        return;

    // The 6th bit of sie => STIE (Supervisor Timer Interrupts Enable)
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 5); 
    asm volatile("csrw sie, %0" : : "r"(sie));
}

void add_timer(timer_callback_t cb, void* args, unsigned long duration_sec) {
    // allocate memory space for new timer node
    timer_node_t* new_node = (timer_node_t*)allocate(sizeof(timer_node_t));
    
    // Set the timer node
    new_node->callback = cb;
    new_node->args = args;
    new_node->expire_time = get_time() + (duration_sec * CPU_FREQ);
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

#define SCHED_TICK (CPU_FREQ / 32)

void handle_timer_interrupt() {
    unsigned long current_time = get_time();

    // 1. 處理你原本超棒的 Timer List (軟體計時器)
    while (timer_list_head != NULL && timer_list_head->expire_time <= current_time) {
        timer_node_t* expired_node = timer_list_head;
        timer_list_head = timer_list_head->next; 

        // 執行 Callback
        add_task(expired_node->callback, expired_node->args, TASK_PRIORITY_TIMER);
        free(expired_node); 
    }

    // 2. 🌟 關鍵修改：強制設定下一次中斷為 1/32 秒後 🌟
    // 這裡我們不再依賴 timer_list_head 來設定硬體 timer，
    // 而是強制讓系統擁有固定的「心跳」，保證排程器一定會被喚醒！
    unsigned long next_tick = get_time() + SCHED_TICK;
    
    // (可選進階寫法) 如果 Timer List 裡面有比 1/32 秒更早到期的任務，就提早醒來
    if (timer_list_head != NULL && timer_list_head->expire_time < next_tick) {
        sbi_set_timer(timer_list_head->expire_time);
    } else {
        sbi_set_timer(next_tick);
    }

    // 3. 🌟 作業的核心要求：強制作業切換 🌟
    // 只要時間一到，管你 User Program 跑得多開心，直接拔掉它的 CPU 控制權！
    schedule();
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
