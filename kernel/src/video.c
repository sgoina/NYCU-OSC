#include "string.h"

#define FB_BASE   0x7f700000
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define CACHE_BLOCK_SIZE 64

// 宣告你的 memcpy (如果是放在 User Program，請確保你有自己實作或 linked)
void *memcpy(void *dest, const void *src, unsigned long n) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

// 🌟 修正 1：換成助教提供的硬派機器碼，確保編譯器絕對看得懂！
#define cbo_flush(start)                \
    ({                                  \
        asm volatile("mv a0, %0\n\t"    \
                     ".word 0x0025200F" \
                     :                  \
                     : "r"(start)       \
                     : "memory", "a0"); \
    })

// 刷 Cache 邏輯 (完全保留)
static void flush_dcache(void* addr, unsigned long len) {
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1);
    __sync_synchronize();
    for (unsigned long line = start; line < (unsigned long)addr + len;
         line += CACHE_BLOCK_SIZE) {
        cbo_flush(line);
        __sync_synchronize();
    }
}

// 🌟 修正 2：刪除了原本所有的 QEMU FW_CFG 結構與 video_init() 函數
// 直接保留畫圖邏輯！
void video_bmp_display(unsigned int* bmp_image, int width, int height) {
    unsigned int* fb = (unsigned int*)FB_BASE;
    int start_x = (FB_WIDTH - width) / 2;
    int start_y = (FB_HEIGHT - height) / 2;
    for (int y = 0; y < height; y++) {
        void* dst = fb + (start_y + y) * FB_WIDTH + start_x;
        memcpy(dst, bmp_image + y * width, width * sizeof(unsigned int));
        flush_dcache(dst, width * sizeof(unsigned int));
    }
}
