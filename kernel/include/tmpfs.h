#ifndef TMPFS_H
#define TMPFS_H

#include "vfs.h"

struct file_operations {
    int (*open)(struct vnode* file_node, struct file** target);
    int (*close)(struct file* file);
    int (*read)(struct file* file, void* buf, size_t len);
    int (*write)(struct file* file, const void* buf, size_t len);
};

struct vnode_operations {
    int (*lookup)(struct vnode* dir_node,
                  struct vnode** target,
                  const char* component_name);
    int (*create)(struct vnode* dir_node,
                  struct vnode** target,
                  const char* component_name);
};


int tmpfs_setup_mount(struct filesystem* fs, struct mount* mnt);
int tmpfs_open(struct vnode* file_node, struct file** target);
int tmpfs_close(struct file* file);
int tmpfs_read(struct file* file, void* buf, size_t len);
int tmpfs_write(struct file* file, const void* buf, size_t len);
int tmpfs_lookup(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name);
int tmpfs_create(struct vnode* dir_node,
                 struct vnode** target,
                 const char* component_name);
                 
#endif // TMPFS_H
