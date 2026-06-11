#include "vfs.h"
#include "tmpfs.h"
#include "ramfs.h"
#include "device.h"
#include "string.h"
#include "utils.h"
#include "mem_alloc.h"
#include "uart.h"
#include "thread.h"

// Maximum number of file system
#define MAX_FS   16 
// Maximum length of pathname
#define PATH_MAX 255

struct mount* rootfs;
struct filesystem fs_list[MAX_FS];

void init_vfs(){
    // Set tmpfs be root file system and mount on "/"
    rootfs = allocate(sizeof(struct mount));
    struct filesystem tmp_fs = {.name = "tmpfs", .setup_mount = tmpfs_setup_mount};
    int id = register_filesystem(&tmp_fs);
    fs_list[id].setup_mount(&fs_list[id], rootfs);
    
    // Register ramfs and mount on "/ramfs"
    struct filesystem ramfs_fs = {.name = "ramfs", .setup_mount = ramfs_setup_mount};
    register_filesystem(&ramfs_fs);
    vfs_mkdir("/ramfs");
    vfs_mount("/ramfs", "ramfs");
    
    // Get device ID for uart and framebuffer
    int uart_id = register_device("uart");
    int fb_id   = register_device("framebuffer");
    // Create special file for devices and put them under "/dev" 
    vfs_mkdir("/dev");
    vfs_mknod("/dev/uart", uart_id);
    vfs_mknod("/dev/fb", fb_id);
}

int register_filesystem(struct filesystem* fs) {
    // Fine an empty entry to register new file system
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
    if (vfs_lookup(pathname, &vnode) != 0) { // Can't find this vnode
        if (flags & O_CREAT){ // Check flag
            int pos = -1;
            for (int i = 0; i < strlen(pathname); i++){
                if (pathname[i] == '/')
                    pos = i;
            }
            
            char dirname[PATH_MAX] = {0};
            const char* filename;
            
            if (pos == -1) { // relative pathname and only new file name (ex: new_file)
                strcpy(dirname, ".");
                filename = pathname; 
            }
            else if (pos == 0) { // absolute pathname and only new file name (ex: /new_file)
                strcpy(dirname, "/");
                filename = pathname + 1; // +1 for skip '/'
            }
            else {
                if (pos >= PATH_MAX)
                    return -1; // directory name is over limit 
                strncpy(dirname, pathname, pos);
                dirname[pos] = '\0';
                filename = pathname + pos + 1; // +1 for skip '/'
            }
            
            if (vfs_lookup(dirname, &vnode) != 0)
                return -1; // Can't find the parent directory node of new file
            
            // Check parent vnode is FS_DIR
            if (vnode->v_ops->checkType(vnode) != 0)
                return -1;
              
            int create_ret = vnode->v_ops->create(vnode, &vnode, filename);
            if (create_ret != 0)
                return -1;
        }
        else { // non-existed file and no O_CREAT flag
            //uart_puts("File isn't existed and no O_CREAT flag.\n");
            return -1;
        }
    }
    // Initialize file handle
    (*target) = allocate(sizeof(struct file));
    (*target)->flags = flags;
    (*target)->f_count = 1;
    // set other settings under file system
    int open_ret = vnode->f_ops->open(vnode, target);
    if (open_ret != 0) {
        free(*target);
        *target = NULL;
        return open_ret; // If failed, return -1
    }
    return 0;
}

int vfs_close(struct file* file) {
    if (file == NULL){
        uart_puts("The file is NULL!\n");
        return -1;
    }
    file->f_count--;
    if (file->f_count != 0) // If others still use this file, directly return successful value (0) 
        return 0;
    return file->f_ops->close(file); // If no one use this file, free the file handle
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
    if (pathname == NULL)
        return -1;
    int pos = -1;
    int len = strlen(pathname);
    for (int i = 0; i < len; i++) {
        if (pathname[i] == '/') {
            pos = i;
        }
    }
    char dirname[PATH_MAX] = {0};
    const char* newdir_name;

    if (pos == -1) { // relative pathname and only new directory name (ex: new_dir)
        strcpy(dirname, ".");
        newdir_name = pathname;
    } 
    else if (pos == 0) { // absolute pathname and only new directory name (ex: new_dir)
        strcpy(dirname, "/");
        newdir_name = pathname + 1; // +1 for skip '/'
    } 
    else {
        if (pos >= PATH_MAX)
            return -1; // directory name is over limit 
        strncpy(dirname, pathname, pos);
        dirname[pos] = '\0';
        newdir_name = pathname + pos + 1; // +1 for skip '/'
    }

    struct vnode* parent_vnode;
    if (vfs_lookup(dirname, &parent_vnode) != 0) 
        return -1; // Can't find the parent vnode
       
    // Check parent vnode is FS_DIR
    if (parent_vnode->v_ops->checkType(parent_vnode) != 0)
        return -1;
    // Make new directory node
    struct vnode* target_dir; 
    return parent_vnode->v_ops->mkdir(parent_vnode, &target_dir, newdir_name);
}

int vfs_mount(const char* target, const char* filesystem) {
    struct vnode* target_node; // Find the mounted vnode
    if (vfs_lookup(target, &target_node) != 0)
        return -1;
        
    // Check the vnode is FS_DIR
    if (target_node->v_ops->checkType(target_node) != 0)
        return -1;


    // Avoid this vnode has been mounted
    if (target_node->mount != NULL)
        return -1; 

    // Find the file system from register list
    struct filesystem* fs = NULL;
    for (int i = 0; i < MAX_FS; i++) {
        if (fs_list[i].name != NULL && strcmp(fs_list[i].name, filesystem) == 0) {
            fs = &fs_list[i];
            break;
        }
    }
    if (fs == NULL)
        return -1; // This file system hasn't been registered, so can't mount.

    struct mount* mnt = allocate(sizeof(struct mount));
    if (mnt == NULL) {
        uart_puts("Error: Memory allocation for mount is failed.\n");
        return -1;
    }
    mnt->fs = fs;
    // mount file system on its root
    if (fs->setup_mount(fs, mnt) != 0) {
        free(mnt);
        return -1;
    }
    // Set the parent node of mounted node be the one of original node (for lookup cross mounting point)
    mnt->root->parent = target_node->parent;
    // vnode links to mount point
    target_node->mount = mnt;

    return 0;
}

int vfs_lookup(const char* pathname, struct vnode** target) {
    if (pathname == NULL)
        return -1;

    struct task_struct* curr = get_current();
    struct vnode* node;
    int i = 0;
    // Determine relative/absolute pathname
    if (pathname[0] == '/') { // absolute pathname
        // In boot phase, curr->root is NULL, use root file system (rootfs)
        if (curr != NULL && curr->root != NULL) 
            node = curr->root;
        else
            node = rootfs->root;
        // skip consecutive directory separators (multiple '/') 
        while (pathname[i] == '/') {
            i++;
        }
    }
    else { // relative pathname
        // In curr->pwd is NULL, use root file system (rootfs)
        if (curr != NULL && curr->pwd != NULL)
            node = curr->pwd;
        else
            node = rootfs->root; 
    }
    // If pathname only has '/'
    if (pathname[i] == '\0') {
        *target = node;
        return 0;
    }
    
    char component[PATH_MAX];
    // parser pathname
    while (pathname[i] != '\0') {
        int idx = 0;

        // cut the next component
        while (pathname[i] != '/' && pathname[i] != '\0') {
            if (idx >= PATH_MAX - 1)
                return -1; // component name is over limit
            component[idx++] = pathname[i++];
        }
        component[idx] = '\0';

        // skip consecutive directory separators (multiple '/') 
        while (pathname[i] == '/') {
            i++;
        }

        if (idx == 0)
            continue; // skip empty component

        if (strcmp(component, ".") == 0)
            continue; // stay here (same directory)
        else if (strcmp(component, "..") == 0) {
            if (node->parent != NULL)
                node = node->parent; // go to parent node (last layer)
        } 
        else {
            // check whether the file system has this node, and update node for next loop
            if (node->v_ops->lookup(node, &node, component) != 0)
                return -1;
            // If the node is mount point, go to the file system root node
            while (node->mount) {
                node = node->mount->root;
            }
        }
    }
    
    *target = node;
    return 0;
}

int vfs_mknod(const char* pathname, int dev_id) {
    if (pathname == NULL)
        return -1;
    int pos = -1;
    int len = strlen(pathname);
    for (int i = 0; i < len; i++) {
        if (pathname[i] == '/') {
            pos = i;
        }
    }

    char dirname[PATH_MAX] = {0};
    const char* filename;

    if (pos == -1) { // relative pathname and only new file name (ex: new_file)
        strcpy(dirname, "."); 
        filename = pathname;
    }
    else if (pos == 0) { // absolute pathname and only new file name (ex: new_file)
        strcpy(dirname, "/");
        filename = pathname + 1; // +1 for skip '/'
    }
    else {
        if (pos >= PATH_MAX)
            return -1; // directory name is over limit 
        strncpy(dirname, pathname, pos);
        dirname[pos] = '\0';
        filename = pathname + pos + 1;  // +1 for skip '/'
    }

    // Find parent vnode
    struct vnode* parent_vnode;
    if (vfs_lookup(dirname, &parent_vnode) != 0)
        return -1;
    
    // Check parent vnode is FS_DIR
    if (parent_vnode->v_ops->checkType(parent_vnode) != 0)
        return -1;

    // Avoid underlying file system doesn't have mknod()
    if (parent_vnode->v_ops->mknod == NULL)
        return -1; 

    struct vnode* target_node;
    return parent_vnode->v_ops->mknod(parent_vnode, &target_node, filename, dev_id);
}
