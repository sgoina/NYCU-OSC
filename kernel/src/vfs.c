#include "vfs.h"
#include "tmpfs.h"
#include "ramfs.h"
#include "string.h"
#include "utils.h"
#include "mem_alloc.h"
#include "uart.h"
#include "thread.h"

#define MAX_FS   16
#define PATH_MAX 255

struct mount* rootfs;
struct filesystem fs_list[MAX_FS];

void init_vfs(){
    rootfs = allocate(sizeof(struct mount));
    struct filesystem tmp_fs = {.name = "tmpfs", .setup_mount = tmpfs_setup_mount};
    int id = register_filesystem(&tmp_fs);
    fs_list[id].setup_mount(&fs_list[id], rootfs);
    // 註冊 ramfs
    struct filesystem ramfs_fs = {.name = "ramfs", .setup_mount = ramfs_setup_mount};
    register_filesystem(&ramfs_fs);
    // 在 rootfs (通常是 tmpfs) 建立 /ramfs 資料夾
    vfs_mkdir("/ramfs");
    // 掛載 ramfs 到 /ramfs
    vfs_mount("/ramfs", "ramfs");
}

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
                // 路徑中沒有 '/'，代表要在當前目錄建立
                strcpy(dirname, "."); // 【修正】必須指定為當前目錄
                filename = pathname; 
            }
            else if (pos == 0) {
                // 【補上這段】確保 "/filename" 這種路徑不會出錯
                strcpy(dirname, "/");
                filename = pathname + 1;
            }
            else {
                strncpy(dirname, pathname, pos);
                dirname[pos] = '\0'; // 確保字串結尾
                filename = pathname + pos + 1;
            }
            
            if (vfs_lookup(dirname, &vnode) != 0)
                return -1;
                
            // 【關鍵修復】接住 create 的回傳值！
            int create_ret = vnode->v_ops->create(vnode, &vnode, filename);
            if (create_ret != 0) {
                return -1; // 底層拒絕建立 (例如 ramfs 是唯讀的)
            }
        }
        else {
            //uart_puts("File isn't existed and no O_CREAT flag.\n");
            return -1;
        }
    }
    (*target) = allocate(sizeof(struct file));
    (*target)->flags = flags;
    (*target)->f_count = 1;
    vnode->f_ops->open(vnode, target);
    return 0;
}

int vfs_close(struct file* file) {
    if (file == NULL){
        uart_puts("The file is NULL!\n");
        return -1;
    }
    file->f_count--;
    if (file->f_count != 0)
        return 0;
    return file->f_ops->close(file); // 只有當最後一個行程關閉它時，才真正 free 掉
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
    if (pathname == NULL) return -1;

    // 1. 找出最後一個 '/' 的位置
    int pos = -1;
    int len = strlen(pathname);
    for (int i = 0; i < len; i++) {
        if (pathname[i] == '/') {
            pos = i;
        }
    }

    char dirname[PATH_MAX] = {0};
    const char* newdir_name;

    // 2. 切割字串：支援絕對與相對路徑
    if (pos == -1) {
        // 情境 A: 只有檔名沒有斜線 (例: "newdir")
        // 父目錄就是當前工作目錄 "."
        strcpy(dirname, ".");
        newdir_name = pathname;
    } 
    else if (pos == 0) {
        // 情境 B: 直接在根目錄底下 (例: "/newdir")
        strcpy(dirname, "/");
        newdir_name = pathname + 1;
    } 
    else {
        // 情境 C: 深層路徑 (例: "dir1/dir2/newdir" 或 "/dir1/newdir")
        strncpy(dirname, pathname, pos);
        dirname[pos] = '\0'; // 【關鍵修正】確保字串有正確的結尾符號
        newdir_name = pathname + pos + 1;
    }

    // 3. 尋找父目錄的 vnode
    struct vnode* parent_vnode;
    if (vfs_lookup(dirname, &parent_vnode) != 0) {
        return -1; // 父目錄不存在
    }
       
    // 4. 檢查 parent vnode 是否真的是目錄 (FS_DIR)
    // 注意：確保你的 v_ops 真的有 checkType 這個自定義函式指標喔！
    if (parent_vnode->v_ops->checkType(parent_vnode) != 0) {
        return -1;
    }

    // 5. 在父目錄下建立新目錄
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
    
    // 掛載完成後，將新檔案系統 root 的 parent 指向掛載點的 parent
    mnt->root->parent = target_node->parent;

    // 5. 【最關鍵的一步】建立橋樑
    // 將原來目標目錄的 mount 指標，指向我們新建立的 mount 結構
    target_node->mount = mnt;

    return 0;
}

int vfs_lookup(const char* pathname, struct vnode** target) {
    if (pathname == NULL) return -1;

    struct task_struct* curr = get_current();
    struct vnode* node;
    int i = 0;

    // 1. 判斷絕對路徑 vs 相對路徑
    if (pathname[0] == '/') {
        // 如果當前行程的 root 是 NULL (如開機階段)，退回使用全域 rootfs
        if (curr != NULL && curr->root != NULL) 
            node = curr->root;
        else
            node = rootfs->root; // 絕對路徑：從 root 開始
        // 略過開頭所有的連續斜線 (正規化)
        while (pathname[i] == '/') i++;
    }
    else {
        // 如果當前行程的 pwd 是 NULL，退回使用全域 rootfs
        if (curr != NULL && curr->pwd != NULL)
            node = curr->pwd;  // 相對路徑：從當前工作目錄開始
        else
            node = rootfs->root; 
    }

    // 若路徑只有 "/"
    if (pathname[i] == '\0') {
        *target = node;
        return 0;
    }

    char component[PATH_MAX];

    // 2. 逐一解析路徑節點
    while (pathname[i] != '\0') {
        int idx = 0;

        // 切割出下一個 component，直到遇到 '/' 或字串結束
        while (pathname[i] != '/' && pathname[i] != '\0') {
            component[idx++] = pathname[i++];
        }
        component[idx] = '\0';

        // 略過連續的斜線 (處理 "dir1///dir2" 的情況)
        while (pathname[i] == '/') i++;

        if (idx == 0) continue; // 防禦性略過空字串

        // 3. 處理 "." 與 ".."
        if (strcmp(component, ".") == 0) {
            continue; // 留在此層目錄，不做事
        } else if (strcmp(component, "..") == 0) {
            // 回到上一層目錄
            if (node->parent != NULL) {
                node = node->parent;
            }
        } else {
            // 一般檔案或目錄的 lookup
            if (node->v_ops->lookup(node, &node, component) != 0) {
                return -1; // 找不到該節點
            }

            // 若該節點是個掛載點，自動跳轉到掛載的檔案系統 root
            while (node->mount) {
                node = node->mount->root;
            }
        }
    }

    *target = node;
    return 0;
}
