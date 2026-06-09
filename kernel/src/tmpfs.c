#include "tmpfs.h"
#include "vfs.h"
#include "string.h"
#include "utils.h"
#include "uart.h"
#include "mem_alloc.h"

struct file_operations tmpfs_file_ops = {.open = tmpfs_open,
                                         .close = tmpfs_close,
                                         .read = tmpfs_read,
                                         .write = tmpfs_write};

struct vnode_operations tmpfs_vnode_ops = {.lookup = tmpfs_lookup,
                                           .create = tmpfs_create,
                                           .mkdir = tmpfs_mkdir,
                                           .checkType = tmpfs_is_dir_type};

struct vnode* tmpfs_create_vnode(enum fsnode_type type) {
    // TODO: Implement this function
    // 分配 vnode 記憶體
    struct vnode* v = (struct vnode*)allocate(sizeof(struct vnode));
    v->mount = NULL;
    v->v_ops = &tmpfs_vnode_ops;
    v->f_ops = &tmpfs_file_ops;
    
    // 分配 tmpfs 專用的內部節點結構
    struct tmpfs_vnode* internal = (struct tmpfs_vnode*)allocate(sizeof(struct tmpfs_vnode));
    internal->type = type;
    memset(internal->name, 0, TMPFS_MAX_FILE_NAME);
    memset(internal->entry, 0, sizeof(internal->entry)); // 將 entry 陣列初始化為 0 (NULL)
    internal->data = NULL;
    internal->size = 0;
    
    // 綁定 internal
    v->internal = internal;
    
    return v;
}

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    mnt->root = tmpfs_create_vnode(FS_DIR);
    mnt->root->parent = mnt->root; // 根目錄的 parent 指向自己
    mnt->fs = fs;
    return 0;
}

int tmpfs_open(struct vnode* file_node, struct file** target) {
    (*target)->vnode = file_node;
    (*target)->f_ops = &tmpfs_file_ops;
    (*target)->f_pos = 0;
    return 0;
}

int tmpfs_close(struct file* file) {
    free(file);
    return 0;
}

int tmpfs_read(struct file* file, void* buf, size_t len) {
    // TODO: Implement this function
    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;
    
    // 如果沒有資料或 f_pos 已經超過檔案大小，表示無法讀取
    if (inode->data == NULL || file->f_pos >= inode->size) {
        return 0; 
    }
    
    // 計算實際可讀取的長度 (避免讀取超出檔案實際 size)
    size_t readable = inode->size - file->f_pos;
    if (len > readable) {
        len = readable;
    }
    
    // 將資料複製到 buf
    memcpy(buf, inode->data + file->f_pos, len);
    
    // 更新 file position
    file->f_pos += len;
    
    return len;
}

int tmpfs_write(struct file* file, const void* buf, size_t len) {
    // TODO: Implement this function
    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;
    
    // 如果是第一次寫入，為檔案分配資料空間
    if (inode->data == NULL) {
        inode->data = (char*)allocate(TMPFS_MAX_FILE_SIZE);
        memset(inode->data, 0, TMPFS_MAX_FILE_SIZE);
    }
    
    // 【新增】如果已經寫滿或超過限制，直接回傳 0 (無法再寫入)
    if (file->f_pos >= TMPFS_MAX_FILE_SIZE) {
        return 0;
    }
    
    // 檢查是否超出 tmpfs 單一檔案的最大容量限制
    if (file->f_pos + len > TMPFS_MAX_FILE_SIZE) {
        len = TMPFS_MAX_FILE_SIZE - file->f_pos; 
    }
    
    // 寫入資料
    memcpy(inode->data + file->f_pos, buf, len);
    
    // 更新 file position
    file->f_pos += len;
    
    // 若目前的 f_pos 超過了原本記錄的檔案大小，更新檔案大小
    if (file->f_pos > inode->size) {
        inode->size = file->f_pos;
    }
    
    return len;
}

int tmpfs_lookup(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name) {
    struct tmpfs_vnode* dentry = dir_node->internal;
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (!dentry->entry[i])
            continue;
        struct tmpfs_vnode* inode = dentry->entry[i]->internal;
        if (!strcmp(inode->name, component_name)) {
            *target = dentry->entry[i];
            return 0;
        }
    }
    return -1;
}

int tmpfs_create(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name) {
    // TODO: Implement this function
    // 【新增防護】1. 檢查是否已經有同名檔案
    struct vnode* check_node;
    if (tmpfs_lookup(dir_node, &check_node, component_name) == 0) {
        // 如果 lookup 回傳 0，代表檔案已存在，必須 fail
        uart_puts("The file is existed. No should to create a new one in tmpfs.\n");
        return -1; 
    }
    
    struct tmpfs_vnode* dir_internal = (struct tmpfs_vnode*)dir_node->internal;
    
    // 尋找目錄 entry 中第一個空的位置
    int free_idx = -1;
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_internal->entry[i] == NULL) {
            free_idx = i;
            break;
        }
    }
    
    // 如果目錄滿了，回傳錯誤
    if (free_idx == -1) {
        uart_puts("Directory entries is fulled.\n");
        return -1;
    }

    // 建立一個新的檔案節點 (FS_FILE)
    struct vnode* new_vnode = tmpfs_create_vnode(FS_FILE);
    new_vnode->parent = dir_node;
    struct tmpfs_vnode* new_internal = (struct tmpfs_vnode*)new_vnode->internal;
    
    // 複製檔名
    strncpy(new_internal->name, component_name, TMPFS_MAX_FILE_NAME - 1);
    
    // 註冊至 parent directory 的 entry 中
    dir_internal->entry[free_idx] = new_vnode;
    
    // 將新建的 vnode 透過 target 回傳給 caller
    *target = new_vnode;
    
    return 0;
}

int tmpfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    // 1. 檢查是否已經有同名檔案或目錄存在
    struct vnode* check_node;
    if (tmpfs_lookup(dir_node, &check_node, component_name) == 0) {
        return -1; // 名稱已被佔用
    }

    struct tmpfs_vnode* dir_internal = (struct tmpfs_vnode*)dir_node->internal;
    
    // 2. 尋找父目錄 entry 中第一個空的位置
    int free_idx = -1;
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_internal->entry[i] == NULL) {
            free_idx = i;
            break;
        }
    }
    
    if (free_idx == -1) return -1; // 目錄滿了

    // 3. 建立一個新的「目錄」節點 (FS_DIR)
    struct vnode* new_vnode = tmpfs_create_vnode(FS_DIR);
    new_vnode->parent = dir_node;
    struct tmpfs_vnode* new_internal = (struct tmpfs_vnode*)new_vnode->internal;
    
    strncpy(new_internal->name, component_name, TMPFS_MAX_FILE_NAME - 1);
    // 如果你有實作確保字串結尾的習慣，可以在這裡強制加上 '\0'
    
    dir_internal->entry[free_idx] = new_vnode;
    
    if (target != NULL) {
        *target = new_vnode;
    }
    
    return 0;
}

int tmpfs_is_dir_type(struct vnode* target){
    struct tmpfs_vnode* internal = (struct tmpfs_vnode*)target->internal;
    if (internal->type != FS_DIR)
        return -1;
    return 0;
}
