#include "vfs.h"
#include "device.h"
#include "tmpfs.h"
#include "defint.h"
#include "uart.h"
#include "utils.h"
#include "string.h"
#include "mem_alloc.h"
#include "video.h"

// 定義 FB 總大小 (1920 * 1080 * 4 bytes)
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * 4)

// 定義作業要求的 IOCTL 巨集
#define FB_IOCTL_GET_INFO 0

// 作業要求的 struct framebuffer_info
struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp; // byte per pixel
};

struct file_operations dev_uart_ops = { .open = tmpfs_open,   // 沿用標準的 open 分配邏輯
                                        .close = tmpfs_close, // 沿用標準的 close 釋放邏輯
                                        .read = dev_uart_read,
                                        .write = dev_uart_write,
                                        .lseek64 = NULL,
                                        .ioctl = NULL
};

struct file_operations dev_fb_ops = { .open = tmpfs_open,   // 沿用標準的 open 分配邏輯
                                      .close = tmpfs_close, // 沿用標準的 close 釋放邏輯
                                      .read = dev_fb_read,
                                      .write = dev_fb_write,
                                      .lseek64 = dev_fb_lseek64, // 綁定 fb 專屬 lseek
                                      .ioctl = dev_fb_ioctl      // 綁定 fb 專屬 ioctl
};

// 這是全域的裝置註冊表
struct device_driver device_list[MAX_DEVICES];
int dev_count = 0;

// 讓 UART 和 FB 等裝置可以註冊自己，並回傳一個專屬的 Device ID 
int register_device(const char* name) {
    if (dev_count >= MAX_DEVICES)
        return -1;
    
    int dev_id = dev_count++;
    device_list[dev_id].name = name;
    if (strcmp(name, "uart") == 0)
        device_list[dev_id].f_ops = &dev_uart_ops;
    else if (strcmp(name, "framebuffer") == 0)
        device_list[dev_id].f_ops = &dev_fb_ops;
    else
        device_list[dev_id].f_ops = NULL;
    return dev_id; // 回傳 Device ID
}


// 裝置檔案的寫入：直接呼叫你的核心 uart_putc
int dev_uart_write(struct file* file, const void* buf, size_t len) {
    const char* cbuf = (const char*)buf;
    for (size_t i = 0; i < len; i++) {
        uart_putc(cbuf[i]); // 導向你的硬體輸出
    }
    return len;
}

// 裝置檔案的讀取：直接呼叫你的核心 uart_getc
int dev_uart_read(struct file* file, void* buf, size_t len) {
    char* cbuf = (char*)buf;
    for (size_t i = 0; i < len; i++) {
        cbuf[i] = uart_getc(); // 導向你的硬體輸入
    }
    return len;
}

// devfs_fb_read (Framebuffer 通常是 Write Only，但可以防呆寫一下)
int dev_fb_read(struct file* file, void* buf, size_t len) {
    return -1; 
}

// ==========================================
// 1. dev_fb_write
// ==========================================
int dev_fb_write(struct file* file, const void* buf, size_t len) {
    if (file->f_pos >= FB_SIZE)
        return 0;

    size_t write_len = len;
    if (file->f_pos + len > FB_SIZE) {
        write_len = FB_SIZE - file->f_pos;
    }

    // 計算目標實體位址 (虛擬位址)
    void* dst = (void*)(FB_BASE_VA + file->f_pos);
    
    // 複製資料到 Framebuffer 
    memcpy(dst, buf, write_len);
    
    // 【重要】呼叫你提供的 cache flush 
    flush_dcache(dst, write_len);

    // 更新游標 
    file->f_pos += write_len;
    return write_len;
}

// ==========================================
// 2. dev_fb_lseek64 (對應 Syscall 21)
// ==========================================
long dev_fb_lseek64(struct file* file, long offset, int whence) {
    // 作業說明只需實作 SEEK_SET (值為 0)
    if (whence == 0) { // SEEK_SET
        if (offset < 0 || offset > FB_SIZE) {
            return -1; // 超出範圍
        }
        file->f_pos = offset;
        return file->f_pos;
    }
    return -1;
}

// ==========================================
// 3. dev_fb_ioctl (對應 Syscall 22)
// ==========================================
int dev_fb_ioctl(struct file* file, unsigned long request, void* arg) {
    if (request == FB_IOCTL_GET_INFO) {
        struct framebuffer_info* info = (struct framebuffer_info*)arg;
        
        // 這裡直接將 Kernel 知道的硬體資訊塞給 User
        // ⚠️ 注意：因為 sstatus.SUM 已開，Kernel 可直接寫入 User Address (arg)
        info->width = FB_WIDTH;
        info->height = FB_HEIGHT;
        info->bpp = 4; // ARGB8888 佔 4 bytes
        
        return 0;
    }
    return -1; // 不支援的 Request
}
