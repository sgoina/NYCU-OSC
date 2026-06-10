#include "vfs.h"
#include "device.h"
#include "tmpfs.h"
#include "defint.h"
#include "uart.h"
#include "utils.h"
#include "string.h"
#include "mem_alloc.h"
#include "video.h"

// 1920 * 1080 * 4 bytes
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * 4)
// IOCTL Marco
#define FB_IOCTL_GET_INFO 0

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp; // byte per pixel
};

struct file_operations dev_uart_ops = { .open = tmpfs_open,
                                        .close = tmpfs_close,
                                        .read = dev_uart_read,
                                        .write = dev_uart_write,
                                        .lseek64 = NULL,
                                        .ioctl = NULL
};

struct file_operations dev_fb_ops = { .open = tmpfs_open,
                                      .close = tmpfs_close,
                                      .read = dev_fb_read,
                                      .write = dev_fb_write,
                                      .lseek64 = dev_fb_lseek64,
                                      .ioctl = dev_fb_ioctl
};

struct device_driver device_list[MAX_DEVICES];
int dev_count = 0;

// Register device and return a device ID
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
    return dev_id;
}

int dev_uart_read(struct file* file, void* buf, size_t len) {
    char* cbuf = (char*)buf;
    for (size_t i = 0; i < len; i++) {
        cbuf[i] = uart_getc();
    }
    return len;
}

int dev_uart_write(struct file* file, const void* buf, size_t len) {
    const char* cbuf = (const char*)buf;
    for (size_t i = 0; i < len; i++) {
        uart_putc(cbuf[i]);
    }
    return len;
}

// Framebuffer is Write Only
int dev_fb_read(struct file* file, void* buf, size_t len) {
    return -1; 
}


int dev_fb_write(struct file* file, const void* buf, size_t len) {
    if (file->f_pos >= FB_SIZE)
        return 0;

    size_t write_len = len;
    if (file->f_pos + len > FB_SIZE)
        write_len = FB_SIZE - file->f_pos;

    // calculate target address from base address of framebuffer
    void* dst = (void*)(FB_BASE_VA + file->f_pos);
    
    // copy data to framebuffer
    memcpy(dst, buf, write_len); 
    flush_dcache(dst, write_len);
    // update file position
    file->f_pos += write_len;
    return write_len;
}

long dev_fb_lseek64(struct file* file, long offset, int whence) {
    if (whence == 0) { // SEEK_SET
        if (offset < 0 || offset > FB_SIZE)
            return -1; // over range
        file->f_pos = offset;
        return file->f_pos;
    }
    return -1;
}

int dev_fb_ioctl(struct file* file, unsigned long request, void* arg) {
    if (request == FB_IOCTL_GET_INFO) { // return framebuffer info
        struct framebuffer_info* info = (struct framebuffer_info*)arg;
        info->width = FB_WIDTH;
        info->height = FB_HEIGHT;
        info->bpp = 4;
        return 0;
    }
    return -1;
}
