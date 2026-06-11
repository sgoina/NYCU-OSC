#include "vm.h"

#define FB_BASE_PA  0x7f700000
#define FB_BASE_VA  (FB_BASE_PA + PAGE_OFFSET)
#define FB_WIDTH  1920
#define FB_HEIGHT 1080

void flush_dcache(void* addr, unsigned long len);

void video_bmp_display(unsigned int* bmp_image, int width, int height);
