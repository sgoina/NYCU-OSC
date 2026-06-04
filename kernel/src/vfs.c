#include "vfs.h"
#include "tmpfs.h"
#include "string.h"
#include "utils.h"
#include "mem_alloc.h"
#include "uart.h"

#define MAX_FS   16
#define MAX_FD   16
#define PATH_MAX 255

struct mount* rootfs;
struct filesystem fs_list[MAX_FS];

int register_filesystem(struct filesystem* fs) {
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name == NULL) {
            fs_list[i].name = fs->name;
            fs_list[i].setup_mount = fs->setup_mount;
            return i;
        }
    }
    uart_puts("Failed to register filesystem.\n");
    return -1;
}

int vfs_open(const char* pathname, int flags, struct file** target) {
    struct vnode* vnode;
    if (vfs_lookup(pathname, &vnode) != 0) {
        if (flags & O_CREAT){
            int pos = -1;
            for (int i = 0; i < strlen(pathname); i++){
                if (pathname[i] == '/')
                    pos = i;
            }
            
            char dirname[PATH_MAX] = {0};
            const char* filename;
            
            if (pos == -1) {
                // 路徑中沒有 '/'，代表要在當前目錄(或根目錄)建立
                filename = pathname; 
            }
            else {
                strncpy(dirname, pathname, pos);
                filename = pathname + pos + 1;
            }
            
            if (vfs_lookup(dirname, &vnode) != 0)
                return -1;
            vnode->v_ops->create(vnode, &vnode, filename);
        }
        else {
            uart_puts("File isn't existed and no O_CREAT flag.\n");
            return -1; // 檔案不存在，且沒有 O_CREAT 標籤，直接回傳錯誤
        }
    }
    (*target) = allocate(sizeof(struct file));
    (*target)->flags = flags;
    vnode->f_ops->open(vnode, target);
    return 0;
}

int vfs_close(struct file* file) {
    if (file == NULL){
        uart_puts("The file is NULL!\n");
        return -1;
    }
    return file->f_ops->close(file);
}

int vfs_read(struct file* file, void* buf, size_t len) {
    if (file == NULL){
        uart_puts("The file is NULL!\n");
        return -1;
    }
    return file->f_ops->read(file, buf, len);
}

int vfs_write(struct file* file, const void* buf, size_t len) {
    if (file == NULL){
        uart_puts("The file is NULL!\n");
        return -1;
    }
    return file->f_ops->write(file, buf, len);
}

int vfs_lookup(const char* pathname, struct vnode** target) {
    if (strlen(pathname) == 0 || strcmp(pathname, "/") == 0) {
        *target = rootfs->root;
        return 0;
    }

    struct vnode* node = rootfs->root;
    char component[PATH_MAX] = {0};
    int idx = 0;

    for (int i = 1; i < strlen(pathname); i++) {
        if (pathname[i] == '/') {
            component[idx] = '\0';
            if (node->v_ops->lookup(node, &node, component) != 0)
                return -1;
            while (node->mount)
                node = node->mount->root;
            idx = 0;
        } else {
            component[idx++] = pathname[i];
        }
    }
    component[idx] = '\0';

    if (node->v_ops->lookup(node, &node, component) != 0)
        return -1;

    while (node->mount)
        node = node->mount->root;

    *target = node;
    return 0;
}
