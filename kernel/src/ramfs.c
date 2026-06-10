#include "cpio.h"
#include "vfs.h"
#include "ramfs.h"
#include "string.h"
#include "utils.h"
#include "uart.h"
#include "mem_alloc.h"

extern void* cpio_address; // cpio.c

struct file_operations ramfs_file_ops = { .open = ramfs_open,
                                          .close = ramfs_close,
                                          .read = ramfs_read, 
                                          .write = ramfs_write,
                                          .lseek64 = NULL,
                                          .ioctl = NULL
};

struct vnode_operations ramfs_vnode_ops = { .lookup = ramfs_lookup,
                                            .create = ramfs_create,
                                            .mkdir = ramfs_mkdir,
                                            .mknod = NULL,
                                            .checkType = ramfs_is_dir_type
};

// create a vnode for ramfs
struct vnode* ramfs_create_vnode(enum fsnode_type type) {
    // Initialize vfs node
    struct vnode* v = (struct vnode*)allocate(sizeof(struct vnode));
    v->mount = NULL;
    v->v_ops = &ramfs_vnode_ops;
    v->f_ops = &ramfs_file_ops;
    
    // Initialize ramfs node
    struct ramfs_vnode* internal = (struct ramfs_vnode*)allocate(sizeof(struct ramfs_vnode));
    internal->type = type;
    memset(internal->name, 0, RAMFS_MAX_FILE_NAME);
    memset(internal->entry, 0, sizeof(internal->entry)); 
    internal->data = NULL;
    internal->size = 0;
    
    // set vnode->internal node to new ramfs node
    v->internal = internal;
    
    return v;
}

int ramfs_setup_mount(struct filesystem* fs, struct mount* mnt) {
    // Build a ramfs root node (FS_DIR)
    struct vnode* root_node = ramfs_create_vnode(FS_DIR);
    // Initialize root node
    root_node->v_ops = &ramfs_vnode_ops;
    root_node->f_ops = &ramfs_file_ops;
    root_node->parent = root_node; // Before finish mounting, its parent points to itself
    // setting mounting point
    mnt->root = root_node;
    mnt->fs = fs;

    struct ramfs_vnode* root_internal = (struct ramfs_vnode*)root_node->internal;
    strcpy(root_internal->name, "ramfs");
    int entry_idx = 0;

    // Traveling CPIO for build vfs tree
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

        if (strcmp(filename, "TRAILER!!!") == 0)
            break; // CPIO end

        int headsize = align(sizeof(struct cpio_t) + namesize, 4); // skip header + name + padding 1 to get content (file_data)
        int datasize = align(filesize, 4);
        void* file_data = (void*)(ptr + headsize);

        // Ignore "." file in CPIO
        if (strcmp(filename, ".") != 0) {
            if (entry_idx >= RAMFS_MAX_DIR_ENTRY) {
                uart_puts("Warning: ramfs root directory is full!\n");
                break;
            }
            // build a vnode for this file
            struct vnode* file_vnode = ramfs_create_vnode(FS_FILE);
            file_vnode->v_ops = &ramfs_vnode_ops;
            file_vnode->f_ops = &ramfs_file_ops;
            file_vnode->parent = root_node; // set parent to ramfs root node
            
            struct ramfs_vnode* file_internal = (struct ramfs_vnode*)file_vnode->internal;
            strncpy(file_internal->name, filename, RAMFS_MAX_FILE_NAME - 1);
            file_internal->size = filesize;
            file_internal->data = file_data; // points to the memory address of the file in CPIO 
            // put the new file node into the entries of root node
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
    if (file == NULL || buf == NULL)
        return -1;
    struct ramfs_vnode* inode = (struct ramfs_vnode*)file->vnode->internal;
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

int ramfs_write(struct file* file, const void* buf, size_t len) {
    return -1; // Read-only file system
}

int ramfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    return -1; // Read-only file system
}

int ramfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name) {
    if (!dir_node || !target || !component_name) 
        return -1;
    struct ramfs_vnode* dentry = (struct ramfs_vnode*)dir_node->internal;
    // Check the node is FS_DIR 
    if (dentry->type != FS_DIR)
        return -1;
    for (int i = 0; i < RAMFS_MAX_DIR_ENTRY; i++) {
        if (!dentry->entry[i])
            continue;
        struct ramfs_vnode* inode = (struct ramfs_vnode*)dentry->entry[i]->internal;
        if (!strcmp(inode->name, component_name)) {
            *target = dentry->entry[i];
            return 0;
        }
    }
    return -1;
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
