#include "defint.h"
#include "mem_alloc.h"
#include "trap.h"
#include "thread.h"
#include "timer.h"
#include "cpio.h"
#include "uart.h"
#include "utils.h"
#include "video.h"
#include "vm.h"
#include "vfs.h"

#define align(size, align_val) (((size) + (align_val) - 1) & ~((align_val) - 1))

extern struct task_struct* run_queue; // thread.c
extern unsigned int CPU_FREQ; // timer.c
extern unsigned long pgd[]; // vm.c

// 0: getpid
long sys_getpid() {
    return get_current()->pid;
}

// 1: uart_read
long sys_uart_read(char *buf, long count) {
    long read_bytes = 0;
    for (long i = 0; i < count; i++) {
        buf[i] = uart_getc();
        read_bytes++;
    }
    return read_bytes;
}

// 2: uart_write
long sys_uart_write(const char *buf, long count) {
    long written_bytes = 0;
    for (long i = 0; i < count; i++) {
        uart_putc(buf[i]);
        written_bytes++;
    }
    return written_bytes;
}

// 3: exec
long sys_exec(const char *path, struct pt_regs *regs) {
    unsigned int filesize = 0;
    
    // get the address in CPIO and size of this program
    void* cpio_code_addr = find_program(path, &filesize);
    if (cpio_code_addr == NULL)
        return -1; // Can't find
        
    unsigned long aligned_filesize = align(filesize, PAGE_SIZE); // align file size to 4KB
    struct task_struct *current = get_current();

    unsigned long *new_pgd = (unsigned long *)allocate(PAGE_SIZE);
    memset(new_pgd, 0, PAGE_SIZE);

    // Copy kernel space to higher-half pgd
    for (int i = 256; i < 512; i++) {
        new_pgd[i] = pgd[i]; 
    }
    
    // update process pgd
    if (current->pgd != NULL)
        free_page_tables(current->pgd);
    current->pgd = new_pgd;
        
    current->cpio_addr = (unsigned long)cpio_code_addr;
    current->code_size = filesize;
    
    current->user_stack = 0; 
    current->user_sp = USER_SP_VA; 
    
    current->pending_signals = 0;
    current->is_handling_signal = 0;
    if (current->signal_stack)
        free((void*)current->signal_stack);
    current->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        current->signal_handlers[i] = 0;
    }
    
    // record vmas for code and user stack
    current->vmas[0].vm_start = USER_CODE_VA;
    current->vmas[0].vm_end   = USER_CODE_VA + aligned_filesize;
    current->vmas[0].vm_prot  = PROT_READ | PROT_EXEC;
    current->vmas[0].vm_flags = MAP_ANONYMOUS;
    current->vmas[0].used     = 1;

    current->vmas[1].vm_start = USER_STACK_VA;
    current->vmas[1].vm_end   = USER_SP_VA;
    current->vmas[1].vm_prot  = PROT_READ | PROT_WRITE;
    current->vmas[1].vm_flags = MAP_ANONYMOUS;
    current->vmas[1].used     = 1;

    // clear the info of other regions
    for (int i = 2; i < MAX_VMAS; i++) {
        current->vmas[i].used = 0;
    }
    
    regs->sepc = USER_CODE_VA;
    regs->sp = USER_SP_VA;

    // Update pgd and flush TLB, ready to go to new process
    unsigned long pgd_pa = virt_to_phys((unsigned long)new_pgd);
    asm volatile(
        "csrw satp, %0\n"
        "sfence.vma zero, zero\n"
        :
        : "r"(MAKE_SATP(pgd_pa))
        : "memory"
    );

    return 0;
}

// 4: fork
long sys_fork(struct pt_regs *regs) {
    return fork_process(regs);
}

// 5: waitpid
long sys_waitpid(long pid) {
    return thread_wait(pid);
}

// 6: exit
void sys_exit(int status) {
    thread_exit();
}

// 7: stop
int sys_stop(long pid) {
    if (pid <= 1) {
        uart_puts("Can't stop idle or kernel shell thread.\n");
        return -1;
    }

    struct task_struct *find = get_task_by_pid(pid);
    
    if (find != NULL){
        // Stop itself
        if (find->pid == get_current()->pid)
            thread_exit();
        else
            find->state = TASK_ZOMBIE;
        return 0;
    }
    // Can't find pid
    return -1; 
}

// 8: display
void sys_display(unsigned int *bmp_image, unsigned int width, unsigned int height){
    video_bmp_display(bmp_image, width, height);
}

// 9: usleep
int sys_usleep(unsigned int usec) {
    if (usec < 0)
        return -1; // Failed
    unsigned long long ticks = (unsigned long long)usec * (CPU_FREQ / 1000000);
    unsigned long long start_time = get_time();
    
    // Do schedule() until wake up time
    while ((get_time() - start_time) < ticks) {
        schedule();
    }
    return 0;
}

// 10: signal
long sys_signal(int signum, void *handler) {
    if (signum < 0 || signum >= MAX_SIGNALS)
        return -1; // Failed
    
    struct task_struct *curr = get_current();
    curr->signal_handlers[signum] = (unsigned long)handler;
    return 0;
}

// 11: sigreturn
void sys_sigreturn(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // Recycle the signal stack
    if (curr->signal_stack != 0) {
        free((void *)curr->signal_stack);
        curr->signal_stack = 0;
    }
    // update vmas
    for (int i = 0; i < MAX_VMAS; i++) {
        if (!curr->vmas[i].used && curr->vmas[i].vm_start == USER_SIG_STACK_VA) { 
            curr->vmas[i].used = 0;
            break;
        }
    }
    // Clear the pte about signal_stack
    unsigned long *pte = get_pte(curr->pgd, USER_SIG_STACK_VA);
    if (pte != NULL)
        *pte = 0;

    // Return back original regs
    char *src = (char *)&curr->signal_saved_context;
    char *dst = (char *)regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst[i] = src[i];
    }
    // Clear is_handling bit for next signal
    curr->is_handling_signal = 0;
    uart_puts("This is sigreturn\n");
}

// 12: kill
int sys_kill(int pid, int signum) {
    if (signum < 0 || signum >= MAX_SIGNALS)
        return -1; // Failed
    
    struct task_struct *target = get_task_by_pid(pid);
    if (!target || target->state == TASK_ZOMBIE)
        return -1; // Can't find target task, failed
    // mark specified bit for determining the signal number
    target->pending_signals |= (1ULL << signum);
    return 0;
}

// 13: mmap
void* sys_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct task_struct *curr = get_current();
    unsigned long aligned_len = align(length, PAGE_SIZE); // align size to 4KB
    unsigned long target_addr = (unsigned long)addr;

    // Check flags
    if (!(flags & MAP_ANONYMOUS))
        return (void*)-1;
    
    // Check whether address is NULL. If it is NULL, find the region by kernel
    if (target_addr != 0) {
        target_addr = target_addr & ~(PAGE_SIZE - 1); // align begin address to 4KB
        // Check whether the region be used. If there is used, find another space
        if (is_overlap(curr, target_addr, aligned_len)) 
            target_addr = find_free_vma_region(curr, aligned_len);
    } 
    else 
        target_addr = find_free_vma_region(curr, aligned_len);

    // Can't find suitable region
    if (target_addr == 0)
        return (void*)-1;

    // Record the region in vmas
    int vma_idx = -1;
    for (int i = 0; i < MAX_VMAS; i++) {
        if (!curr->vmas[i].used) {
            curr->vmas[i].vm_start = target_addr;
            curr->vmas[i].vm_end = target_addr + aligned_len;
            curr->vmas[i].vm_prot = prot;
            curr->vmas[i].vm_flags = flags;
            curr->vmas[i].used = 1;
            vma_idx = i;
            break;
        }
    }
    // curr->vmas is full
    if (vma_idx == -1)
        return (void*)-1; 

    // Convert PROT to PTE permission
    unsigned long pte_prot = PROT_USER_BASE; 
    if (prot & PROT_READ)
        pte_prot |= PTE_R;
    if (prot & PROT_WRITE)
        pte_prot |= PTE_W;
    if (prot & PROT_EXEC)
        pte_prot |= PTE_X;

    // MAP_POPULATE: Allocate physical pages immediately 
    if (flags & MAP_POPULATE) {
        for (unsigned long va = target_addr; va < target_addr + aligned_len; va += PAGE_SIZE) {
            void *new_page = allocate(PAGE_SIZE); 
            if (!new_page)
                return (void*)-1;
            memset(new_page, 0, PAGE_SIZE);       
            
            unsigned long page_pa = virt_to_phys((unsigned long)new_page);
            map_pages(curr->pgd, va, PAGE_SIZE, page_pa, pte_prot);
        }
        asm volatile("sfence.vma zero, zero" ::: "memory");
    } 

    return (void*)target_addr; // return virtual address of the begin of this region
}

// 14: open
long sys_open(const char *pathname, int flags) {
    if (pathname == NULL) return -1;
    
    struct task_struct *curr = get_current();
    
    // 尋找 FDT 空位
    int fd = -1;
    for (int i = 0; i < MAX_FS; i++) {
        if (curr->fdt[i] == NULL) {
            fd = i;
            break;
        }
    }
    
    if (fd == -1) {
        return -1; // FDT 滿了 (Too many open files)
    }
    
    struct file *target_file = NULL;
    // 注意：在完整 Multitask 中，vfs_open 需要知道當前行程的 pwd 和 root
    // 我們假設你稍後會修改 vfs_open 或 vfs_lookup 來支援 pwd
    int ret = vfs_open(pathname, flags, &target_file);
    
    if (ret != 0 || target_file == NULL) {
        return -1; // 開啟失敗
    }
    
    // 將 file 指標存入 FDT，並設定參考計數
    curr->fdt[fd] = target_file;
    
    return fd;
}

// 15: close
long sys_close(int fd) {
    struct task_struct *curr = get_current();
    
    // 檢查 fd 是否合法
    if (fd < 0 || fd >= MAX_FS || curr->fdt[fd] == NULL) {
        return -1; // Bad file descriptor
    }
    
    struct file *file_to_close = curr->fdt[fd];
    curr->fdt[fd] = NULL; // 先把 FDT 欄位清空，避免 Race Condition
    
    // 呼叫底層 vfs_close (需處理 f_count)
    return vfs_close(file_to_close);
}

// 16: read
long sys_read(int fd, void *buf, unsigned long count) {
    if (buf == NULL) return -1;
    struct task_struct *curr = get_current();
    
    if (fd < 0 || fd >= MAX_FS || curr->fdt[fd] == NULL) {
        return -1; // Bad file descriptor
    }
    
    // 透過 fd 取得 file struct，並呼叫 vfs_read
    return vfs_read(curr->fdt[fd], buf, count);
}

// 17: write
long sys_write(int fd, const void *buf, unsigned long count) {
    if (buf == NULL) return -1;
    struct task_struct *curr = get_current();
    
    // 你可能會有標準輸出的特例處理
    // 例如：如果作業要求 stdout (fd=1) 直接印到 UART
    /*
    if (fd == 1 || fd == 2) {
        return sys_uart_write((const char*)buf, count);
    }
    */
    // (如果作業後面有規定 stdin/stdout 要掛在 /dev/uart 上，那上面這段特例就不用寫，
    // 會交給 vfs_write 裡面底層的 /dev/uart write 處理)

    if (fd < 0 || fd >= MAX_FS || curr->fdt[fd] == NULL) {
        return -1; 
    }
    
    return vfs_write(curr->fdt[fd], buf, count);
}

// 18: mkdir
long sys_mkdir(const char *pathname, unsigned int mode) {
    if (pathname == NULL) return -1;
    
    // 作業說明表示可以忽略 mode 參數
    // vfs_mkdir 需要能解析相對路徑 (基於 curr->pwd)
    return vfs_mkdir(pathname);
}

// 19: mount
long sys_mount(const char *src, const char *target, const char *filesystem, unsigned long flags, const void *data) {
    if (target == NULL || filesystem == NULL) return -1;
    
    // 忽略 src, flags, data，直接呼叫 vfs_mount
    return vfs_mount(target, filesystem);
}

// 20: chdir
long sys_chdir(const char *path) {
    if (path == NULL) return -1;
    struct task_struct *curr = get_current();
    
    struct vnode *target_dir;
    // vfs_lookup 會根據路徑找到對應的 vnode
    if (vfs_lookup(path, &target_dir) != 0) {
        return -1; // 找不到該目錄
    }
    
    // 檢查目標 vnode 是不是真的是一個目錄
    // 假設你的 vnode 或 internal 有存放 type (如 FS_DIR)
    if (target_dir->v_ops->checkType(target_dir) != 0)
        return -1; 
    
    // 更新當前行程的工作目錄
    curr->pwd = target_dir;
    return 0;
}

// 21: lseek64
long sys_lseek64(int fd, long offset, int whence) {
    struct task_struct *curr = get_current();
    
    // 檢查 fd 範圍與是否被開啟
    if (fd < 0 || fd >= MAX_FS || curr->fdt[fd] == NULL) {
        return -1; 
    }
    
    struct file *file = curr->fdt[fd];
    
    // 檢查這個檔案是否有實作 lseek64 (例如一般目錄可能就沒有)
    if (file->f_ops->lseek64) {
        return file->f_ops->lseek64(file, offset, whence);
    }
    
    return -1; // 不支援此操作
}

// 22: ioctl
long sys_ioctl(int fd, unsigned long request, void* arg) {
    struct task_struct *curr = get_current();
    
    if (fd < 0 || fd >= MAX_FS || curr->fdt[fd] == NULL) {
        return -1; 
    }
    
    struct file *file = curr->fdt[fd];
    
    // 檢查是否有實作 ioctl (只有裝置檔案如 /dev/fb 會有)
    if (file->f_ops->ioctl) {
        return file->f_ops->ioctl(file, request, arg);
    }
    
    return -1; // 一般檔案不支援 ioctl
}

void syscall_handler(struct pt_regs *regs) {
    // a7 stores system call number
    unsigned long syscall_num = regs->a7;
    
    void* ret = (void*)-1;

    switch (syscall_num) {
        case 0: // getpid
            ret = (void*)sys_getpid();
            break;
            
        case 1: // uart_read
            // a0 = *buf, a1 = count
            ret = (void*)sys_uart_read((char*)regs->a0, regs->a1);
            break;
            
        case 2: // uart_write
            // a0 = *buf, a1 = count
            ret = (void*)sys_uart_write((const char*)regs->a0, regs->a1);
            break;
            
        case 3: // exec
            // a0 = *path
            ret = (void*)sys_exec((const char*)regs->a0, regs);
            break;
            
        case 4: // fork
            ret = (void*)sys_fork(regs);
            break;
            
        case 5: // waitpid
            // a0 = pid
            ret = (void*)sys_waitpid(regs->a0);
            break;
            
        case 6: // exit
            // a0 = status (ignored)
            sys_exit(regs->a0);
            break;
            
        case 7: // stop
            // a0 = pid
            ret = (void*)(long)sys_stop(regs->a0);
            break;
            
        case 8: // display
            // a0 = bmp_image, a1 = width, a2 = height
            sys_display((unsigned int*)regs->a0, (unsigned int)regs->a1, (unsigned int)regs->a2);
            break;
            
        case 9: // usleep
            // a0 = usec
            ret = (void*)(long)sys_usleep((unsigned int)regs->a0);
            break;
            
        case 10: // signal
            // a0 = signum, a1 = *handler
            ret = (void*)sys_signal((int)regs->a0, (void *)regs->a1); // ret value ignored
            break;
                        
        case 11: // sigreturn
            sys_sigreturn(regs);
            return;
            
        case 12: // kill
            // a0 = pid, a1 = signum
            ret = (void*)(long)sys_kill((int)regs->a0, (int)regs->a1); 
            break;
            
        case 13: // mmap
            // a0 = addr, a1 = length, a2 = prot, a3 = flags
            ret = (void*)sys_mmap((void *)regs->a0, (unsigned long)regs->a1, (int)regs->a2, (int)regs->a3); 
            break;
            
        case 14: // open
            // a0 = pathname, a1 = flags
            ret = (void*)(long)sys_open((const char*)regs->a0, (int)regs->a1);
            break;

        case 15: // close
            // a0 = fd
            ret = (void*)(long)sys_close((int)regs->a0);
            break;

        case 16: // read
            // a0 = fd, a1 = buf, a2 = count
            ret = (void*)(long)sys_read((int)regs->a0, (void*)regs->a1, (unsigned long)regs->a2);
            break;

        case 17: // write
            // a0 = fd, a1 = buf, a2 = count
            ret = (void*)(long)sys_write((int)regs->a0, (const void*)regs->a1, (unsigned long)regs->a2);
            break;

        case 18: // mkdir
            // a0 = pathname, a1 = mode
            ret = (void*)(long)sys_mkdir((const char*)regs->a0, (unsigned int)regs->a1);
            break;

        case 19: // mount
            // a0 = src, a1 = target, a2 = filesystem, a3 = flags, a4 = data
            ret = (void*)(long)sys_mount((const char*)regs->a0, (const char*)regs->a1, (const char*)regs->a2, (unsigned long)regs->a3, (const void*)regs->a4);
            break;

        case 20: // chdir
            // a0 = path
            ret = (void*)(long)sys_chdir((const char*)regs->a0);
            break;
            
        case 21: // lseek64
            // a0 = fd, a1 = offset, a2 = whence
            ret = (void*)(long)sys_lseek64((int)regs->a0, (long)regs->a1, (int)regs->a2);
            break;

        case 22: // ioctl
            // a0 = fd, a1 = request, a2 = *arg
            ret = (void*)(long)sys_ioctl((int)regs->a0, (unsigned long)regs->a1, (void*)regs->a2);
            break;
            
        default:
            uart_puts("Unknown syscall number: ");
            uart_hex(syscall_num);
            uart_puts("\n");
            break;
    }
    // a0 = return value
    regs->a0 = (unsigned long)ret;
}
