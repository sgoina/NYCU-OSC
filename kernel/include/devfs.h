#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

#define DEVFS_MAX_FILE_NAME 15
#define DEVFS_MAX_DIR_ENTRY 16
#define DEVFS_MAX_FILE_SIZE 4096

// 定義 devfs 專屬的內部節點結構
struct devfs_vnode {
    enum fsnode_type type;
    char name[DEVFS_MAX_FILE_NAME];
    struct vnode* entry[DEVFS_MAX_DIR_ENTRY]; // 用於 /dev 目錄存放裝置節點
};

int devfs_setup_mount(struct filesystem* fs, struct mount* mnt);
int devfs_open(struct vnode* file_node, struct file** target);
int devfs_close(struct file* file);
int devfs_uart_read(struct file* file, void* buf, size_t len);
int devfs_uart_write(struct file* file, const void* buf, size_t len);
int devfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name);
int devfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name);
int devfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name);
int devfs_is_dir_type(struct vnode* target);

#endif // DEVFS_H
