#include "vfs.h"
#include "devfs.h"
#include "uart.h"
#include "utils.h"
#include "string.h"
#include "mem_alloc.h"

struct file_operations devfs_uart_ops = { .open = devfs_open,   // 沿用標準的 open 分配邏輯
                                          .close = devfs_close, // 沿用標準的 close 釋放邏輯
                                          .read = devfs_uart_read,
                                          .write = devfs_uart_write
};

struct vnode_operations devfs_vnode_ops = { .lookup = devfs_lookup,
                                            .create = devfs_create,
                                            .mkdir = devfs_mkdir,
                                            .checkType = devfs_is_dir_type
};


// devfs 掛載初始化：在此處手動建立好硬體節點 
int devfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    // 1. 建立 /dev 根目錄節點 (FS_DIR)
    struct vnode* root_node = allocate(sizeof(struct vnode));
    root_node->mount = NULL;
    root_node->v_ops = &devfs_vnode_ops;
    root_node->f_ops = NULL; // 目錄不需要 file_ops
    root_node->parent = root_node;

    struct devfs_vnode* root_internal = allocate(sizeof(struct devfs_vnode));
    root_internal->type = FS_DIR;
    strcpy(root_internal->name, "dev");
    memset(root_internal->entry, 0, sizeof(root_internal->entry));
    root_node->internal = root_internal;

    // 2. 【核心重點】手動建立 uart 裝置節點 (FS_FILE) [cite: 381, 395]
    struct vnode* uart_node = allocate(sizeof(struct vnode));
    uart_node->mount = NULL;
    uart_node->v_ops = &devfs_vnode_ops;
    uart_node->f_ops = &devfs_uart_ops; // 掛上剛剛寫好的 UART 操作介面 [cite: 368]
    uart_node->parent = root_node;

    struct devfs_vnode* uart_internal = allocate(sizeof(struct devfs_vnode));
    uart_internal->type = FS_FILE;
    strcpy(uart_internal->name, "uart");
    memset(uart_internal->entry, 0, sizeof(uart_internal->entry));
    uart_node->internal = uart_internal;

    // 3. 把 uart 塞進 /dev 的目錄陣列中
    root_internal->entry[0] = uart_node;

    /* =========================================================
       💡 未來加強：當你要實作 Advanced 2 的 Framebuffer 時，
       只需要在這裡 malloc 一個 fb_node，並讓它的 f_ops 指向你的 fbfs 介面，
       最後塞進 root_internal->entry[1] 即可！VFS 架構完全不用重寫！
       ========================================================= */

    mnt->root = root_node;
    mnt->fs = fs;
    return 0;
}

int devfs_open(struct vnode* file_node, struct file** target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = file_node->f_ops;
    (*target)->f_pos = 0;
    return 0;
}

int devfs_close(struct file* file) {
    free(file);
    return 0;
}

// 裝置檔案的寫入：直接呼叫你的核心 uart_putc
int devfs_uart_write(struct file* file, const void* buf, size_t len) {
    const char* cbuf = (const char*)buf;
    for (size_t i = 0; i < len; i++) {
        uart_putc(cbuf[i]); // 導向你的硬體輸出 [cite: 396]
    }
    return len;
}

// 裝置檔案的讀取：直接呼叫你的核心 uart_getc
int devfs_uart_read(struct file* file, void* buf, size_t len) {
    char* cbuf = (char*)buf;
    for (size_t i = 0; i < len; i++) {
        cbuf[i] = uart_getc(); // 導向你的硬體輸入 [cite: 396]
    }
    return len;
}

// devfs 專屬的 lookup：在硬編碼的裝置陣列中比對名稱 [cite: 195]
int devfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    struct devfs_vnode* dentry = (struct devfs_vnode*)dir_node->internal;
    
    // 如果這個節點本身是一般檔案(裝置)，無法再往下查找
    if (dentry->type != FS_DIR) return -1;

    for (int i = 0; i < DEVFS_MAX_DIR_ENTRY; i++) {
        if (!dentry->entry[i]) continue;
        
        struct devfs_vnode* inode = (struct devfs_vnode*)dentry->entry[i]->internal;
        if (strcmp(inode->name, component_name) == 0) {
            *target = dentry->entry[i];
            return 0; // 成功找到裝置檔案 [cite: 196]
        }
    }
    return -1; // 找不到該裝置
}

// 唯讀與安全攔截防護（不允許在 /dev 底下隨意 mkdir 或 create 一般檔案）
int devfs_create(struct vnode* dir, struct vnode** t, const char* name) {
    return -1;
}

int devfs_mkdir(struct vnode* dir, struct vnode** t, const char* name) {
    return -1;
}

int devfs_is_dir_type(struct vnode* target){
    struct devfs_vnode* internal = (struct devfs_vnode*)target->internal;
    if (internal->type != FS_DIR)
        return -1;
    return 0;
}
