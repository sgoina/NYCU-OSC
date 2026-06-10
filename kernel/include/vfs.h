#ifndef VFS_H
#define VFS_H
#include "defint.h"

#define O_CREAT 00000100

enum fsnode_type { FS_DIR, FS_FILE, FS_DEVICE};

struct vnode {
    struct mount* mount;
    struct vnode_operations* v_ops;
    struct file_operations* f_ops;
    void* internal;
    struct vnode* parent;
};

// file handle
struct file {
    struct vnode* vnode;
    size_t f_pos;  // RW position of this file handle
    struct file_operations* f_ops;
    int flags;
    int f_count; // 【新增】有多少個行程正在使用這個 file
};

struct mount {
    struct vnode* root;
    struct filesystem* fs;
};

struct filesystem {
    const char* name;
    int (*setup_mount)(struct filesystem* fs, struct mount* mount);
};

struct file_operations {
    int (*open)(struct vnode* file_node, struct file** target);
    int (*close)(struct file* file);
    int (*read)(struct file* file, void* buf, size_t len);
    int (*write)(struct file* file, const void* buf, size_t len);
    long (*lseek64) (struct file* file, long offset, int whence);
    int (*ioctl) (struct file* file, unsigned long request, void* arg);
};

struct vnode_operations {
    int (*lookup)(struct vnode* dir_node, struct vnode** target, const char* component_name);
    int (*create)(struct vnode* dir_node, struct vnode** target, const char* component_name);
    int (*mkdir)(struct vnode* dir_node, struct vnode** target, const char* component_name);
    int (*mknod)(struct vnode* dir_node, struct vnode** target, const char* component_name, int dev_id);
    int (*checkType)(struct vnode* target);
};

void init_vfs();
int register_filesystem(struct filesystem* fs);
int vfs_open(const char* pathname, int flags, struct file** target);
int vfs_close(struct file* file);
int vfs_read(struct file* file, void* buf, size_t len);
int vfs_write(struct file* file, const void* buf, size_t len);
int vfs_mkdir(const char* pathname);
int vfs_mount(const char* target, const char* filesystem);
int vfs_lookup(const char* pathname, struct vnode** target);
int vfs_mknod(const char* pathname, int dev_id);


#endif // VFS_H

