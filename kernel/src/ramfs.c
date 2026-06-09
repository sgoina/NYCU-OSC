#include "cpio.h"
#include "vfs.h"
#include "ramfs.h"
#include "string.h"
#include "utils.h"
#include "uart.h"
#include "mem_alloc.h"

extern void* cpio_address; // cpio.c

// 綁定操作介面
struct file_operations ramfs_file_ops = { .open = ramfs_open,
                                          .close = ramfs_close,
                                          .read = ramfs_read, 
                                          .write = ramfs_write  // 【強制攔截寫入】
};

struct vnode_operations ramfs_vnode_ops = { .lookup = ramfs_lookup,
                                            .create = ramfs_create, // 【強制攔截建立檔案】
                                            .mkdir = ramfs_mkdir,   // 【強制攔截建立目錄】
                                            .checkType = ramfs_is_dir_type
};

struct vnode* ramfs_create_vnode(enum fsnode_type type) {
    // 分配 vnode 記憶體
    struct vnode* v = (struct vnode*)allocate(sizeof(struct vnode));
    v->mount = NULL;
    v->v_ops = &ramfs_vnode_ops;
    v->f_ops = &ramfs_file_ops;
    
    // 分配 ramfs 專用的內部節點結構
    struct ramfs_vnode* internal = (struct ramfs_vnode*)allocate(sizeof(struct ramfs_vnode));
    internal->type = type;
    memset(internal->name, 0, RAMFS_MAX_FILE_NAME);
    memset(internal->entry, 0, sizeof(internal->entry)); // 將 entry 陣列初始化為 0 (NULL)
    internal->data = NULL;
    internal->size = 0;
    
    // 綁定 internal
    v->internal = internal;
    
    return v;
}

int ramfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    // 1. 建立 ramfs 的 root vnode (這是一個目錄)
    struct vnode* root_node = ramfs_create_vnode(FS_DIR);
    
    // 將 root_node 的操作介面替換成唯讀版本
    root_node->v_ops = &ramfs_vnode_ops;
    root_node->f_ops = &ramfs_file_ops;
    root_node->parent = root_node; // 掛載前，先指向自己
    
    mnt->root = root_node;
    mnt->fs = fs;

    struct ramfs_vnode* root_internal = (struct ramfs_vnode*)root_node->internal;
    strcpy(root_internal->name, "ramfs");
    int entry_idx = 0;

    // 2. 遍歷 CPIO 並建立 VFS 樹狀結構
    char *ptr = (char *)cpio_address;
    
    while (1) {
        struct cpio_t *header = (struct cpio_t *)ptr;
        if (strncmp(header->magic, "070701", 6) != 0) {
            uart_puts("Error: magic number is not match in ramfs setup.\n");
            break;
        }

        int namesize = hextoi(header->namesize, 8);
        int filesize = hextoi(header->filesize, 8);
        const char *filename = ptr + sizeof(struct cpio_t);

        if (strcmp(filename, "TRAILER!!!") == 0) {
            break; // 結束遍歷
        }

        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);
        void* file_data = (void*)(ptr + headsize);

        // 忽略目錄本身 "." (CPIO 通常會包含一個檔名為 "." 的 entry)
        if (strcmp(filename, ".") != 0) {
            
            if (entry_idx >= RAMFS_MAX_DIR_ENTRY) {
                uart_puts("Warning: ramfs root directory is full!\n");
                break;
            }

            // 為這個 CPIO 檔案建立一個 vnode
            struct vnode* file_vnode = ramfs_create_vnode(FS_FILE);
            file_vnode->v_ops = &ramfs_vnode_ops;
            file_vnode->f_ops = &ramfs_file_ops;
            file_vnode->parent = root_node; // 父目錄設定為 ramfs 的根目錄

            struct ramfs_vnode* file_internal = (struct ramfs_vnode*)file_vnode->internal;
            
            // 複製檔名並設定大小與資料指標 (Zero-Copy)
            strncpy(file_internal->name, filename, RAMFS_MAX_FILE_NAME - 1);
            file_internal->size = filesize;
            file_internal->data = file_data; // 直接指向實體記憶體中的 CPIO 位置！

            // 將建立好的檔案 vnode 掛入 ramfs 的 root 目錄中
            root_internal->entry[entry_idx++] = file_vnode;
        }

        ptr += headsize + datasize;
    }

    return 0;
}

int ramfs_open(struct vnode* file_node, struct file** target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = &ramfs_file_ops; 
    (*target)->f_pos = 0;
    return 0;
}

int ramfs_close(struct file* file) {
    free(file);
    return 0;
}

int ramfs_read(struct file* file, void* buf, size_t len) {
    struct ramfs_vnode* inode = (struct ramfs_vnode*)file->vnode->internal;
    
    // 如果沒有資料或 f_pos 已經超過檔案大小，表示無法讀取
    if (inode->data == NULL || file->f_pos >= inode->size) {
        return 0; 
    }
    
    // 計算實際可讀取的長度 (避免讀取超出檔案實際 size)
    size_t readable = inode->size - file->f_pos;
    if (len > readable) {
        len = readable;
    }
    
    // 將資料複製到 buf (這是在讀取 CPIO 在記憶體中的實體資料)
    memcpy(buf, inode->data + file->f_pos, len);
    
    // 更新 file position
    file->f_pos += len;
    
    return len;
}

// 攔截所有寫入與建立的操作
int ramfs_write(struct file* file, const void* buf, size_t len) {
    return -1; // Read-only file system
}

int ramfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1; // Read-only file system
}

int ramfs_lookup(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name) {
    struct ramfs_vnode* dentry = (struct ramfs_vnode*)dir_node->internal;
    
    for (int i = 0; i < RAMFS_MAX_DIR_ENTRY; i++) {
        if (!dentry->entry[i]) {
            continue; // 遇到空的 entry 繼續找
        }
        
        struct ramfs_vnode* inode = (struct ramfs_vnode*)dentry->entry[i]->internal;
        if (!strcmp(inode->name, component_name)) {
            *target = dentry->entry[i];
            return 0;
        }
    }
    return -1; // 找不到
}

int ramfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1; // Read-only file system
}

int ramfs_is_dir_type(struct vnode* target){
    struct ramfs_vnode* internal = (struct ramfs_vnode*)target->internal;
    if (internal->type != FS_DIR)
        return -1;
    return 0;
}
