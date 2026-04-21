#include "deviceTree.h"
#include "uart.h"
#include "timer.h"
#include "sbi.h"

unsigned int CPU_FREQ = 0;

void timer_init(unsigned long dtb_ptr) {
    int cpus_offset = fdt_path_offset(dtb_ptr, "/cpus");
    int len;
    const void* prop = fdt_getprop(dtb_ptr, cpus_offset, "timebase-frequency", &len);
    if (prop != NULL)
        CPU_FREQ = bswap32(*(const unsigned int*)prop);    
    else 
        return;
    
    // 1. 設定第一次的目標時間：現在時間 + 2秒的 ticks
    unsigned long long current_time = get_time();
    sbi_set_timer(current_time + CPU_FREQ * 2);

    // 2. 開啟 Supervisor Timer Interrupt Enable (sie.STIE, bit 5)
    unsigned long sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1 << 5); 
    asm volatile("csrw sie, %0" : : "r"(sie));
}
