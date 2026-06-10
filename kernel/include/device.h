#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

#define MAX_DEVICES 16

struct device_driver {
    const char* name;
    struct file_operations* f_ops;
};

int register_device(const char* name);
int dev_uart_read(struct file* file, void* buf, size_t len);
int dev_uart_write(struct file* file, const void* buf, size_t len);
int dev_fb_read(struct file* file, void* buf, size_t len);
int dev_fb_write(struct file* file, const void* buf, size_t len);
long dev_fb_lseek64(struct file* file, long offset, int whence);
int dev_fb_ioctl(struct file* file, unsigned long request, void* arg);

#endif // DEVFS_H
