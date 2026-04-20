static inline unsigned long long get_time() {
    unsigned long long time;
    asm volatile("csrr %0, time" : "=r"(time));
    return time;
}

void timer_init(unsigned long dtb_ptr);
