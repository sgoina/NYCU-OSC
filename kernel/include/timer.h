typedef void (*timer_callback_t)(char* arg); // 定義 Callback 函式的格式

static inline unsigned long long get_time() {
    unsigned long long time;
    asm volatile("csrr %0, time" : "=r"(time));
    return time;
}

void timer_init(unsigned long dtb_ptr);

void add_timer(timer_callback_t cb, char* arg, unsigned long duration_sec);

void handle_timer_interrupt();

void print_boot_time(char* arg);

void timeout_callback(char* arg);
