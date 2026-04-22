#include "deviceTree.h"
#include "uart.h"
#include "timer.h"
#include "sbi.h"
#include "mem_alloc.h"
#include "task.h"

unsigned int CPU_FREQ = 0;

typedef struct timer_node {
    unsigned long expire_time; // 絕對觸發時間 (硬體 Ticks)
    timer_callback_t callback; // 時間到時要執行的函式
    char arg[32];              // 傳給 callback 的字串參數
    struct timer_node* next;   // 指向下一個最近的計時器
} timer_node_t;

// 宣告一個全域指標，永遠指向「最快要到期的那一個」
timer_node_t* timer_list_head = NULL;

void timer_init(unsigned long dtb_ptr) {
    int cpus_offset = fdt_path_offset(dtb_ptr, "/cpus");
    int len;
    const void* prop = fdt_getprop(dtb_ptr, cpus_offset, "timebase-frequency", &len);
    if (prop != NULL)
        CPU_FREQ = bswap32(*(const unsigned int*)prop);    
    else 
        return;

    // 2. 開啟 Supervisor Timer Interrupt Enable (sie.STIE, bit 5)
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 5); 
    asm volatile("csrw sie, %0" : : "r"(sie));
}

void add_timer(timer_callback_t cb, char* arg, unsigned long duration_sec) {
    // 1. 分配記憶體給新的 Timer 節點 (用你剛寫好的 allocate)
    timer_node_t* new_node = (timer_node_t*)allocate(sizeof(timer_node_t));
    new_node->callback = cb;
    
    // 複製參數字串
    int i = 0;
    while(arg && arg[i] != '\0' && i < 31) { 
        new_node->arg[i] = arg[i]; 
        i++; 
    }
    new_node->arg[i] = '\0';
    
    // 2. 計算「絕對觸發時間」
    new_node->expire_time = get_time() + (duration_sec * CPU_FREQ);
    new_node->next = NULL;

    // 3. 進入 Critical Section：關閉中斷保護 Linked List
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));

    // 4. 依序插入 (時間越近的排越前面)
    if (timer_list_head == NULL || new_node->expire_time < timer_list_head->expire_time) {
        // 情況 A：它是目前最快要到期的！插在頭部！
        new_node->next = timer_list_head;
        timer_list_head = new_node;
        
        // ⭐ 關鍵：更新硬體鬧鐘，提早叫醒 CPU
        // sbi_set_timer 是 OpenSBI 提供的 API
        sbi_set_timer(new_node->expire_time); 
    }
    else {
        // 情況 B：尋找合適的插入點
        timer_node_t* current = timer_list_head;
        while (current->next != NULL && current->next->expire_time <= new_node->expire_time) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }

    // 離開 Critical Section：恢復中斷
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

void handle_timer_interrupt() {
    unsigned long current_time = get_time();

    // 1. 檢查並執行「所有」已經到期的 Timer
    // (用 while 是預防有兩個 Timer 的到期時間非常接近，一次中斷可以處理完)
    while (timer_list_head != NULL && timer_list_head->expire_time <= current_time) {
        // 從頭部拔除節點
        timer_node_t* expired_node = timer_list_head;
        timer_list_head = timer_list_head->next; 

        // 執行使用者設定的 Callback
        add_task(expired_node->callback, expired_node->arg, 5);

        free(expired_node); 
    }

    // 2. 處理完後，重新設定下一次的硬體鬧鐘
    if (timer_list_head != NULL) {
        // 還有任務在排隊，設定為下一個人的時間
        sbi_set_timer(timer_list_head->expire_time);
    }
    else {
        // 佇列空了，把硬體計時器設到極遠的未來 (-1ULL 代表 64-bit 的最大值)
        // 這樣直到有新的 add_timer 加入前，都不會再觸發無謂的 Timer 中斷
        sbi_set_timer(-1ULL); 
    }
}

// 定義原本那個「每 2 秒印一次」的函式
void print_boot_time(char* arg) {
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

void timeout_callback(char* arg) {
    uart_puts(arg);
    uart_putc('\n');
}
