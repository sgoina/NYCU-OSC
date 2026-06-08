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
            return -1;
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

int vfs_mkdir(const char* pathname) {
    // 1. 找出最後一個 '/' 的位置，用來切割字串
    int pos = -1;
    for (int i = 0; i < strlen(pathname); i++) {
        if (pathname[i] == '/') {
            pos = i;
        }
    }

    char dirname[PATH_MAX] = {0};
    const char* newdir_name;

    if (pos == -1) {
        // 沒有 '/'，表示在當前目錄(根目錄)建立
        newdir_name = pathname;
        // 這裡為了簡化，你可以直接把 dirname 設定為 "/"
        strcpy(dirname, "/");
    }
    else if (pos == 0) {
        // 路徑長相為 "/newdir"
        strcpy(dirname, "/");
        newdir_name = pathname + 1;
    }
    else {
        // 路徑長相為 "/dir1/dir2/newdir"
        strncpy(dirname, pathname, pos);
        newdir_name = pathname + pos + 1;
    }

    // 2. 尋找父目錄的 vnode
    struct vnode* parent_vnode;
    if (vfs_lookup(dirname, &parent_vnode) != 0) {
        return -1; // 父目錄不存在
    }
       
    // check parent vnode is FS_DIR 
    if (parent_vnode->v_ops->checkType(parent_vnode) != 0)
        return -1;

    // 3. 在父目錄下建立新目錄
    struct vnode* target_dir;
    return parent_vnode->v_ops->mkdir(parent_vnode, &target_dir, newdir_name);
}

int vfs_mount(const char* target, const char* filesystem) {
    // 1. 尋找掛載點的 vnode (例如 "/mnt")
    struct vnode* target_node;
    if (vfs_lookup(target, &target_node) != 0) {
        return -1; // 找不到掛載目標目錄
    }

    // 防護：確保該目錄還沒有被掛載過其他檔案系統
    if (target_node->mount != NULL) {
        return -1; 
    }

    // 2. 從已註冊的檔案系統列表中尋找對應的 filesystem (例如 "tmpfs")
    struct filesystem* fs = NULL;
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name != NULL && strcmp(fs_list[i].name, filesystem) == 0) {
            fs = &fs_list[i];
            break;
        }
    }
    
    if (fs == NULL) {
        return -1; // 尚未註冊該檔案系統，無法掛載
    }

    struct mount* mnt = allocate(sizeof(struct mount));
    if (mnt == NULL) {
        uart_puts("Error: Memory allocation for mount is failed.\n");
        return -1;
    }
    mnt->fs = fs;

    // 4. 呼叫檔案系統的 setup_mount
    // 這會通知 tmpfs (或其他檔案系統) 初始化它自己的狀態，
    // 並把它專屬的 root vnode 綁定到 mnt->root 上。
    if (fs->setup_mount(fs, mnt) != 0) {
        free(mnt);
        return -1;
    }

    // 5. 【最關鍵的一步】建立橋樑
    // 將原來目標目錄的 mount 指標，指向我們新建立的 mount 結構
    target_node->mount = mnt;

    return 0;
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
            while (node->mount) // If node is mount point, go to the mounted file system’s root vnode
                node = node->mount->root;
            idx = 0;
        }
        else {
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
