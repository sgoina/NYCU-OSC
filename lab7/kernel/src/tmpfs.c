#include "tmpfs.h"
#include "vfs.h"
#include "device.h"
#include "string.h"
#include "utils.h"
#include "uart.h"
#include "mem_alloc.h"

extern struct device_driver device_list[MAX_DEVICES]; // device.c

struct file_operations tmpfs_file_ops = { .open = tmpfs_open,
                                          .close = tmpfs_close,
                                          .read = tmpfs_read,
                                          .write = tmpfs_write,
                                          .lseek64 = NULL,
                                          .ioctl = NULL
};

struct vnode_operations tmpfs_vnode_ops = {.lookup = tmpfs_lookup,
                                           .create = tmpfs_create,
                                           .mkdir = tmpfs_mkdir,
                                           .mknod = tmpfs_mknod,
                                           .checkType = tmpfs_is_dir_type
};

// create a vnode for tmpfs
struct vnode* tmpfs_create_vnode(enum fsnode_type type) {
    // Initialize vfs node
    struct vnode* v = (struct vnode*)allocate(sizeof(struct vnode));
    if (v == NULL)
        return NULL;
    v->mount = NULL;
    v->v_ops = &tmpfs_vnode_ops;
    v->f_ops = &tmpfs_file_ops;
    
    // Initialize tmpfs node
    struct tmpfs_vnode* internal = (struct tmpfs_vnode*)allocate(sizeof(struct tmpfs_vnode));
    if (internal == NULL){
        free(v);
        return NULL;
    }
    internal->type = type;
    memset(internal->name, 0, TMPFS_MAX_FILE_NAME);
    memset(internal->entry, 0, sizeof(internal->entry));
    internal->data = NULL;
    internal->size = 0;
    internal->dev_id = -1;
    
    // set vnode->internal node to new tmpfs node
    v->internal = internal;
    
    return v;
}

int tmpfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    mnt->root = tmpfs_create_vnode(FS_DIR);
    if (mnt->root == NULL)
        return -1;
    mnt->root->parent = mnt->root; // root's parent points to itself
    mnt->fs = fs;
    return 0;
}

int tmpfs_open(struct vnode* file_node, struct file** target) {
    struct tmpfs_vnode* internal = (struct tmpfs_vnode*)file_node->internal;
    if (internal->type == FS_DIR) { // The node is FS_DIR, can't be file
        uart_puts("Error: Is a directory.\n");
        return -1; 
    }
    (*target)->vnode = file_node;
    (*target)->f_pos = 0;
    // If the file is special file (device)
    if (internal->type == FS_DEVICE) {
        int dev_id = internal->dev_id;
        if (dev_id >= 0 && dev_id < MAX_DEVICES && device_list[dev_id].f_ops != NULL)
            (*target)->f_ops = device_list[dev_id].f_ops; // set file operations for device node
        else
            return -1;
    } 
    else
        (*target)->f_ops = &tmpfs_file_ops; // set file operations for normal tmpfs file node 
    return 0;
}

int tmpfs_close(struct file* file) {
    free(file);
    return 0;
}

int tmpfs_read(struct file* file, void* buf, size_t len) {
    if (file == NULL || buf == NULL)
        return -1;
    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;
    // Check node is FS_FILE
    if (inode->type != FS_FILE)
        return -1; 
    // Reach to EOF (End Of File), can't read anymore
    if (inode->data == NULL || file->f_pos >= inode->size)
        return 0; 
    
    // Avoid over file size, recalculate length
    size_t readable = inode->size - file->f_pos;
    if (len > readable)
        len = readable;
    
    memcpy(buf, inode->data + file->f_pos, len);
    // update file position
    file->f_pos += len;
    return len;
}

int tmpfs_write(struct file* file, const void* buf, size_t len) {
    if (file == NULL || buf == NULL)
        return -1;
    struct tmpfs_vnode* inode = (struct tmpfs_vnode*)file->vnode->internal;
    // Check node is FS_FILE
    if (inode->type != FS_FILE)
        return -1;
    // If file position is over TMPFS_MAX_FILE_SIZE
    if (file->f_pos >= TMPFS_MAX_FILE_SIZE) 
        return -1;
    // If it is first writing, allocate memory space
    if (inode->data == NULL) {
        inode->data = (char*)allocate(TMPFS_MAX_FILE_SIZE);
        if (inode->data == NULL)
            return -1; // Allocation failed
        memset(inode->data, 0, TMPFS_MAX_FILE_SIZE);
    }
    
    // Avoid over TMPFS_MAX_FILE_SIZE, recalculate length
    if (file->f_pos + len > TMPFS_MAX_FILE_SIZE)
        len = TMPFS_MAX_FILE_SIZE - file->f_pos; 
    
    memcpy(inode->data + file->f_pos, buf, len);
    // update file position
    file->f_pos += len;
    
    // update file size
    if (file->f_pos > inode->size)
        inode->size = file->f_pos;
    return len;
}

int tmpfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    if (!dir_node || !target || !component_name) 
        return -1;
    struct tmpfs_vnode* dentry = (struct tmpfs_vnode*)dir_node->internal;
    // Check the node is FS_DIR 
    if (dentry->type != FS_DIR)
        return -1;
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (!dentry->entry[i])
            continue;
        struct tmpfs_vnode* inode = (struct tmpfs_vnode*)dentry->entry[i]->internal;
        if (!strcmp(inode->name, component_name)) {
            *target = dentry->entry[i];
            return 0;
        }
    }
    return -1;
}

int tmpfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    // Avoid other nodes has same component_name
    struct vnode* check_node;
    if (tmpfs_lookup(dir_node, &check_node, component_name) == 0) {
        uart_puts("The file is existed. No should to create a new one in tmpfs.\n");
        return -1; 
    }
    
    struct tmpfs_vnode* dir_internal = (struct tmpfs_vnode*)dir_node->internal;
    // Check the node is FS_DIR 
    if (dir_internal->type != FS_DIR)
        return -1;
    int free_idx = -1; // Find empty entry for new node
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_internal->entry[i] == NULL) {
            free_idx = i;
            break;
        }
    }
    // Directory entries is fulled
    if (free_idx == -1) {
        uart_puts("Directory entries is fulled.\n");
        return -1;
    }
    // Generate a FS_FILE vnode
    struct vnode* new_vnode = tmpfs_create_vnode(FS_FILE);
    if (new_vnode == NULL)
        return -1;
    struct tmpfs_vnode* new_internal = (struct tmpfs_vnode*)new_vnode->internal;
    strncpy(new_internal->name, component_name, TMPFS_MAX_FILE_NAME - 1); // copy the file name for new tmpfs node
    dir_internal->entry[free_idx] = new_vnode; // set new tmpfs node in parent's entry
    new_vnode->parent = dir_node;
    
    if (target != NULL)
        *target = new_vnode;
    return 0;
}

int tmpfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    // Avoid other nodes has same component_name
    struct vnode* check_node;
    if (tmpfs_lookup(dir_node, &check_node, component_name) == 0){
        uart_puts("The directory is existed. No should to create a new one in tmpfs.\n");
        return -1;
    }

    struct tmpfs_vnode* dir_internal = (struct tmpfs_vnode*)dir_node->internal;
    // Check the node is FS_DIR 
    if (dir_internal->type != FS_DIR)
        return -1;
    int free_idx = -1; // Find empty entry for new node
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_internal->entry[i] == NULL) {
            free_idx = i;
            break;
        }
    }
    // Directory entries is fulled
    if (free_idx == -1) {
        uart_puts("Directory entries is fulled.\n");
        return -1;
    }
    // Generate a FS_DIR vnode
    struct vnode* new_vnode = tmpfs_create_vnode(FS_DIR);
    if (new_vnode == NULL)
        return -1;
    struct tmpfs_vnode* new_internal = (struct tmpfs_vnode*)new_vnode->internal;
    strncpy(new_internal->name, component_name, TMPFS_MAX_FILE_NAME - 1); // copy the directory name for new tmpfs node
    dir_internal->entry[free_idx] = new_vnode; // set new tmpfs node in parent's entry
    new_vnode->parent = dir_node;
    
    if (target != NULL)
        *target = new_vnode;
    return 0;
}

int tmpfs_mknod(struct vnode* dir_node, struct vnode** target, const char* component_name, int dev_id) {
    // Avoid other nodes has same component_name
    struct vnode* check_node;
    if (tmpfs_lookup(dir_node, &check_node, component_name) == 0) {
        uart_puts("Device name is already existed.\n");
        return -1; 
    }
    
    struct tmpfs_vnode* dir_internal = (struct tmpfs_vnode*)dir_node->internal;
    // Check the node is FS_DIR 
    if (dir_internal->type != FS_DIR)
        return -1;
    int free_idx = -1;  // Find empty entry for new node
    for (int i = 0; i < TMPFS_MAX_DIR_ENTRY; i++) {
        if (dir_internal->entry[i] == NULL) {
            free_idx = i;
            break;
        }
    }
    // Directory entries is fulled
    if (free_idx == -1) {
        uart_puts("Directory entries is fulled.\n");
        return -1;
    }

    // Generate a FS_DEVICE vnode
    struct vnode* new_node = tmpfs_create_vnode(FS_DEVICE); 
    if (new_node == NULL)
        return -1;
    struct tmpfs_vnode* new_internal = (struct tmpfs_vnode*)new_node->internal;
    strncpy(new_internal->name, component_name, TMPFS_MAX_FILE_NAME - 1);
    new_internal->dev_id = dev_id;
    new_node->parent = dir_node;
    dir_internal->entry[free_idx] = new_node;
    
    if (target != NULL)
        *target = new_node;
    return 0;
}

int tmpfs_is_dir_type(struct vnode* target){
    struct tmpfs_vnode* internal = (struct tmpfs_vnode*)target->internal;
    if (internal->type != FS_DIR)
        return -1;
    return 0;
}
