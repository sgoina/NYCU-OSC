typedef void (*timer_callback_t)(void* args);

static inline unsigned long long get_time() {
    unsigned long long time;
    asm volatile("rdtime %0" : "=r"(time)); // read the current time (absolute)
    return time;
}

void timer_init(unsigned long dtb_ptr);

void add_timer(timer_callback_t cb, void* args, unsigned long duration_ticks);

void handle_timer_interrupt();

void print_boot_time(void* arg);

void timeout_callback(void* arg);
