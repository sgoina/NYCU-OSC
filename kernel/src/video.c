#include "utils.h"
#include "video.h"
#include "vm.h"

#define FB_BASE_PA  0x7f700000
#define FB_BASE_VA  (FB_BASE_PA + PAGE_OFFSET)
#define FB_WIDTH  1920
#define FB_HEIGHT 1080
#define CACHE_BLOCK_SIZE 64

// Ensure the display hardware reads the latest data from DRAM rather than stale cache.
// ".word 0x0025200F" = flush a0 to display
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
    unsigned long start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1); //align
    __sync_synchronize(); //Memory Barrier
    for (unsigned long line = start; line < (unsigned long)addr + len;line += CACHE_BLOCK_SIZE) {
        cbo_flush(line);
        __sync_synchronize(); //Memory Barrier
    }
}

// show image on framebuffer
void video_bmp_display(unsigned int* bmp_image, int width, int height) {
    unsigned int* fb = (unsigned int*)FB_BASE_VA;
    // Center alignment
    int start_x = (FB_WIDTH - width) / 2;
    int start_y = (FB_HEIGHT - height) / 2;
    // copy row by row 
    for (int y = 0; y < height; y++) {
        void* dst = fb + (start_y + y) * FB_WIDTH + start_x;
        memcpy(dst, bmp_image + y * width, width * sizeof(unsigned int));
        flush_dcache(dst, width * sizeof(unsigned int));
    }
}
