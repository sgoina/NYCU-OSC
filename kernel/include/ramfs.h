#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

#define RAMFS_MAX_FILE_NAME 32
#define RAMFS_MAX_DIR_ENTRY 32

struct ramfs_vnode {
    enum fsnode_type type;
    char name[RAMFS_MAX_FILE_NAME];
    struct vnode* entry[RAMFS_MAX_DIR_ENTRY];
    char* data;
    size_t size;
};

int ramfs_setup_mount(struct filesystem* fs, struct mount* mnt);
int ramfs_open(struct vnode* file_node, struct file** target);
int ramfs_close(struct file* file);
int ramfs_read(struct file* file, void* buf, size_t len);
int ramfs_write(struct file* file, const void* buf, size_t len);
int ramfs_lookup(struct vnode* dir_node, struct vnode** target, const char* component_name);
int ramfs_create(struct vnode* dir_node, struct vnode** target, const char* component_name);
int ramfs_mkdir(struct vnode* dir_node, struct vnode** target, const char* component_name);
int ramfs_is_dir_type(struct vnode* target);

#endif // RAMFS_H
