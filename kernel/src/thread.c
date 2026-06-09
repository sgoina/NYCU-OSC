#include "defint.h"
#include "utils.h"
#include "thread.h"
#include "trap.h"
#include "timer.h"
#include "mem_alloc.h"
#include "uart.h"
#include "utils.h"
#include "vm.h"

#define align(size, align_val) (((size) + (align_val) - 1) & ~((align_val) - 1))

extern void ret_from_exception(); // start.S
extern void switch_to(struct task_struct* prev, struct task_struct* next); // start.S
extern char sigreturn_trampoline_start[]; // start.S
extern char sigreturn_trampoline_end[]; // start.S
extern unsigned long pgd[]; // vm.c
extern struct mount* rootfs; // vfs.c

struct task_struct* run_queue = 0;
int nr_threads = 0; // new pid number

static void enqueue(struct task_struct** queue, struct task_struct* task) {
    task->state = TASK_RUNNING;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // If the new task is the first task in run_queue
    if (*queue == 0) {
        *queue = task;
        task->next = task;
    }
    else {
        struct task_struct* tail = *queue;
        // Find the last element
        while (tail->next != *queue) {
            tail = tail->next;
        }
        tail->next = task;
        task->next = *queue;
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

struct task_struct* get_current() {
    // register: put the variable in register instead of RAM
    register struct task_struct* current asm("tp");
    return current;
}

// Find the task_struct by pid
struct task_struct* get_task_by_pid(int pid) {
    if (!run_queue)
        return NULL;
    
    struct task_struct* curr = run_queue;
    do {
        if (curr->pid == pid) {
            return curr;
        }
        curr = curr->next;
    } while (curr != run_queue);
    // Can't find
    return NULL;
}

void schedule() {
    struct task_struct* prev = get_current();
    struct task_struct* next = prev->next;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    // skip zombie thread
    while (next->state == TASK_ZOMBIE && next != prev) {
        next = next->next;
    }
    // jump to next thread
    if (prev != next) {
        unsigned long pgd_pa = virt_to_phys((unsigned long)next->pgd);
        
        // Update satp and flush TLB
        asm volatile(
            "csrw satp, %0\n"
            "sfence.vma zero, zero\n"
            :
            : "r"(MAKE_SATP(pgd_pa))
            : "memory"
        );
    
        switch_to(prev, next);
    }
    
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// for creating new kernel thread
void kernel_thread_wrapper() {
    // enable interrupt because of disable in schedule()
    asm volatile("csrsi sstatus, 2");

    struct task_struct* curr = get_current();

    if (curr->entry_point)
        curr->entry_point();
}

// Create kernel thread
struct task_struct* thread_create(void (*threadfn)()) {
    if (threadfn == NULL){
        uart_puts("Can't create a NULL thread.\n");
        return NULL;
    }
    struct task_struct* task = allocate(sizeof(struct task_struct));   
    if (!task){
        uart_puts("Thread thread allocation failed.\n");
        return NULL;
    }
    // Need enalbe interrupt because schedule() will disable before switch_to()
    task->thread.ra = (unsigned long)kernel_thread_wrapper;
    // The function of kernel thread
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    task->next = NULL;
    task->entry_point = threadfn;
    task->kernel_stack = (unsigned long)allocate(PAGE_SIZE);
    task->kernel_sp = task->kernel_stack + PAGE_SIZE;
    task->user_stack = 0; // because this thread is kernel thread
    task->user_sp = 0;
    // initialize signal-related info
    task->signal_stack = 0;
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    task->thread.sp = task->kernel_sp;
    task->pgd = pgd; // satp in kernel thread is same
    task->cpio_addr = 0; // Kenerl image program isn't in CPIO
    task->code_size = 0;
    // initialize vmas
    for (int i = 0; i < MAX_VMAS; i++) {
        task->vmas[i].used = 0;
    }
    // initialize VFS state
    for (int i = 0; i < MAX_FS; i++) {
        task->fdt[i] = NULL; // 清空所有 file descriptor
    }
    
    // 如果全域的 rootfs 已經準備好，就預設指向根目錄；否則先設為 NULL
    extern struct mount* rootfs; // 確保編譯器認得全域變數 rootfs
    if (rootfs != NULL) {
        task->pwd = rootfs->root;
        task->root = rootfs->root;
    }
    else {
        task->pwd = NULL;
        task->root = NULL;
    }
    
    enqueue(&run_queue, task);
    return task;
}

// Create user thread
struct task_struct* user_process_create(unsigned long filesize, void* program_va) {
    struct task_struct* task = allocate(sizeof(struct task_struct));
    if (!task){
        uart_puts("User thread allocation failed.\n");
        return NULL;
    }
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    task->next = NULL;
    task->entry_point = NULL;
    
    task->kernel_stack = (unsigned long)allocate(PAGE_SIZE); 
    task->kernel_sp = task->kernel_stack + PAGE_SIZE;
    task->user_stack = 0;
    task->user_sp = USER_SP_VA;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    task->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    task->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(task->pgd, 0, PAGE_SIZE);
    
    // Copy kernel space to higher-half pgd
    for (int i = 256; i < 512; i++) {
        task->pgd[i] = pgd[i];
    }

    unsigned long aligned_filesize = align(filesize, PAGE_SIZE);
    task->cpio_addr = (unsigned long)program_va; 
    task->code_size = filesize;

    // record vmas for code and user stack
    task->vmas[0].vm_start = USER_CODE_VA;
    task->vmas[0].vm_end   = USER_CODE_VA + aligned_filesize;
    task->vmas[0].vm_prot  = PROT_READ | PROT_EXEC | PROT_WRITE;
    task->vmas[0].vm_flags  = MAP_ANONYMOUS;
    task->vmas[0].used     = 1;
    
    task->vmas[1].vm_start = USER_STACK_VA;
    task->vmas[1].vm_end   = USER_SP_VA;
    task->vmas[1].vm_prot  = PROT_READ | PROT_WRITE;
    task->vmas[1].vm_flags  = MAP_ANONYMOUS;
    task->vmas[1].used     = 1;

    // clear the info of other regions
    for (int i = 2; i < MAX_VMAS; i++) {
        task->vmas[i].used = 0;
    }
    // initialize VFS state
    for (int i = 0; i < MAX_FS; i++) {
        task->fdt[i] = NULL; // 確保沒有繼承到垃圾指標
    }
    task->pwd = rootfs->root;  // User process 的預設起點是 "/"
    task->root = rootfs->root; 
    
    // ==========================================
    // 【Advanced 1】預先為 User Space 開啟標準輸出入
    // ==========================================
    struct file* uart_file = NULL;
    
    // 因為是在 Kernel 模式下準備 task，我們可以直接呼叫 vfs_open 開啟裝置檔案
    int ret = vfs_open("/dev/uart", 0, &uart_file);
    if (ret == 0 && uart_file != NULL) {
        // 設定 f_count 為 3，避免 user 呼叫一次 close(0) 就把整個 uart_file 釋放掉
        uart_file->f_count = 3; 
        
        task->fdt[0] = uart_file; // stdin
        task->fdt[1] = uart_file; // stdout
        task->fdt[2] = uart_file; // stderr
    }
    else
        uart_puts("Warning: Failed to map standard I/O to /dev/uart.\n");
    // ==========================================
    
    // Simulate back from "sret" condition
    struct pt_regs* regs = (struct pt_regs*)(task->kernel_sp - sizeof(struct pt_regs));
    for (int i = 0; i < sizeof(struct pt_regs) / 8; i++){
        ((unsigned long*)regs)[i] = 0;
    }

    regs->tp = (unsigned long)task;
    regs->sepc = USER_CODE_VA;             
    regs->sp = USER_SP_VA;
    
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    // setting U-Mode and enable interrupt
    sstatus &= ~(1 << 8); // SPP (Supervisor mode Previous Privilege mode): 1=Supervisor, 0=User
    sstatus |= (1 << 5);  // SPIE (Supervisor Previous Interrupt Enable)
    regs->sstatus = sstatus;

    task->thread.ra = (unsigned long)ret_from_exception; // Simulate completing exception and ready to go to U-Mode
    task->thread.sp = (unsigned long)regs; // for ret_from_exception

    enqueue(&run_queue, task);
    return task;
}

// Fork a thread
long fork_process(struct pt_regs *regs) {
    struct task_struct *parent = get_current();
    struct task_struct *child = allocate(sizeof(struct task_struct));
    if (!child) {
        uart_puts("Fork thread allocation failed.\n");
        return -1;
    }

    child->pid = nr_threads++;
    child->state = TASK_RUNNING;
    child->next = NULL;
    child->entry_point = NULL;
    
    child->kernel_stack = (unsigned long)allocate(PAGE_SIZE); 
    child->kernel_sp = child->kernel_stack + PAGE_SIZE;
    child->user_stack = 0;
    child->user_sp = USER_SP_VA;
    
    child->pending_signals = 0;
    child->is_handling_signal = 0;
    child->signal_stack = 0;
    // signal handler is inherited from parent
    for (int i = 0; i < MAX_SIGNALS; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }
    
    // ==========================================
    // 【新增】VFS 狀態與檔案描述符表 (FDT) 繼承
    // ==========================================
    child->pwd = parent->pwd;   // 繼承當前工作目錄
    child->root = parent->root; // 繼承根目錄

    for (int i = 0; i < MAX_FS; i++) {
        child->fdt[i] = parent->fdt[i]; // 複製打開的檔案指標
        
        // 【重要】如果你有實作 file reference count (檔案參照計數)
        // 這裡必須將 f_count 加 1，因為現在多了一個行程在使用這個檔案！
        if (child->fdt[i] != NULL) {
            child->fdt[i]->f_count++; 
        }
    }
    // ==========================================

    // Simulate back from "sret" condition
    unsigned long regs_offset = parent->kernel_sp - (unsigned long)regs; // avoid there is something in parent->kernel stack, so need to calculate offset
    struct pt_regs *child_regs = (struct pt_regs *)(child->kernel_sp - regs_offset);
    // Copy regs from parent thread
    char *src_regs = (char *)regs;
    char *dst_regs = (char *)child_regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst_regs[i] = src_regs[i];
    }

    child->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(child->pgd, 0, PAGE_SIZE);

    // Copy kernel space to higher-half pgd
    for (int i = 256; i < 512; i++) {
        child->pgd[i] = pgd[i];
    }

    // Copy vmas
    for (int i = 0; i < MAX_VMAS; i++) {
        child->vmas[i] = parent->vmas[i]; 

        if (child->vmas[i].used) {
            unsigned long start = child->vmas[i].vm_start;
            unsigned long end = child->vmas[i].vm_end;
            
            for (unsigned long va = start; va < end; va += PAGE_SIZE) {
                
                // Get the pte and check if it is valid
                unsigned long *pte = get_pte(parent->pgd, va);
                if (pte != NULL && (*pte & PTE_V)) {
                    unsigned long parent_pa = (*pte >> 10) << 12;
                  
                    inc_page_ref(parent_pa); // increase reference count

                    // PROT_WRITE => PTE_COW not PTE_W
                    unsigned long shared_pte_prot = PROT_USER_BASE; 
                    if (child->vmas[i].vm_prot & PROT_READ)
                        shared_pte_prot |= PTE_R;
                    if (child->vmas[i].vm_prot & PROT_EXEC)
                        shared_pte_prot |= PTE_X;
                    if (child->vmas[i].vm_prot & PROT_WRITE)
                        shared_pte_prot |= PTE_COW;
  
                    // virtual address mapping to physical address of shared frame 
                    map_pages(child->pgd, va, PAGE_SIZE, parent_pa, shared_pte_prot);

                    // Update parent pte because of COW flag
                    *pte = MAKE_PTE(parent_pa, shared_pte_prot);
                }
            }
        }
    }

    asm volatile("sfence.vma zero, zero" ::: "memory");
    
    child->cpio_addr = parent->cpio_addr;
    child->code_size = parent->code_size;

    // Return value 0 for child thread
    child_regs->a0 = 0;
    child_regs->tp = (unsigned long)child;
    
    // copy parent's user stack pointer
    child_regs->sp = regs->sp;

    child->thread.ra = (unsigned long)ret_from_exception; // Simulate completing exception and ready to go to U-Mode
    child->thread.sp = (unsigned long)child_regs; // for ret_from_exception

    enqueue(&run_queue, child);
    // Return value child->pid for parent thread
    return child->pid;
}

// Waiting another thread
long thread_wait(long pid) {
    while (1) {
        struct task_struct *wait = get_task_by_pid(pid);

        if (!wait || wait->state == TASK_ZOMBIE)
            return pid;
        schedule();
    }
    return -1; // Impossible to be here
}

// Become zombie thread
void thread_exit() {
    struct task_struct* current = get_current();
    current->state = TASK_ZOMBIE;
    schedule();
}

// Handle signal
void thread_handle_signals(struct pt_regs *regs) {
    struct task_struct *curr = get_current();
    
    // Determine exist, being handling signal, pending signal
    if (!curr || curr->is_handling_signal || curr->pending_signals == 0)
        return;

    // Find which signal number
    int signum = -1;
    for (int i = 0; i < MAX_SIGNALS; i++) {
        if (curr->pending_signals & (1ULL << i)) {
            signum = i;
            break;
        }
    }
    
    if (signum != -1) {
        // Get the specific handler
        unsigned long handler = curr->signal_handlers[signum];
        
        // Clear pending bit
        curr->pending_signals &= ~(1ULL << signum);

        if (handler != 0) {
            // backup regs
            char *src = (char *)regs;
            char *dst = (char *)&curr->signal_saved_context;
            for (int i = 0; i < sizeof(struct pt_regs); i++) {
                dst[i] = src[i];
            }

            // Mark is_handling bit
            curr->is_handling_signal = 1;

            // Initialize signal stack
            curr->signal_stack = (unsigned long)allocate(PAGE_SIZE);
            unsigned long sig_stack_pa = virt_to_phys(curr->signal_stack);
            // Mapping to 0x0000003000000000UL for Signal stack
            map_pages(curr->pgd, USER_SIG_STACK_VA, PAGE_SIZE, sig_stack_pa, PROT_USER_RWX);
            
            int sig_vma_idx = -1;
            for (int i = 0; i < MAX_VMAS; i++) {
                if (!curr->vmas[i].used) {
                    curr->vmas[i].vm_start = USER_SIG_STACK_VA;
                    curr->vmas[i].vm_end   = USER_SIG_STACK_VA + PAGE_SIZE;
                    curr->vmas[i].vm_prot  = PROT_READ | PROT_WRITE | PROT_EXEC; 
                    curr->vmas[i].vm_flags = MAP_ANONYMOUS; 
                    curr->vmas[i].used     = 1;
                    sig_vma_idx = i;
                    break;
                }
            }
            if (sig_vma_idx == -1)
                uart_puts("Warning: VMA is full, cannot record signal stack!\n");
            
            unsigned long kernel_sp = curr->signal_stack + PAGE_SIZE; 
            unsigned long user_sp = USER_SIG_STACK_VA + PAGE_SIZE; 
            
            // Put trampoline code into signal stack
            unsigned long tramp_size = sigreturn_trampoline_end - sigreturn_trampoline_start;            
            kernel_sp -= tramp_size;
            kernel_sp &= ~0xF; // align to 16 bytes
            user_sp -= tramp_size;
            user_sp &= ~0xF;
            char *user_tramp_space = (char *)kernel_sp;
            for (int i = 0; i < tramp_size; i++) {
                user_tramp_space[i] = sigreturn_trampoline_start[i];
            }

            regs->ra = user_sp; // set return address to trampoline function 
            regs->sp = user_sp; // set stack pointer to signal stack pointer
            regs->sepc = handler; // Go to signal handler function
        }
        else {
            // There is no signal handler, ready to kill the thread
            if (curr->pid <= 1){
                uart_puts("Can't kill idle or kernel shell thread.\n");
                return;
            }
            thread_exit();
        }
    }
}

// Kill all zombie threads
void kill_zombies() {
    if (!run_queue)
        return;
    
    // Critical Section
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus));
    
    struct task_struct* curr = run_queue->next;
    struct task_struct* prev = run_queue;
    struct task_struct* start = run_queue;
    
    while (curr != start && run_queue != NULL) {
        if (curr->state == TASK_ZOMBIE) {
            prev->next = curr->next;

            struct task_struct* zombie = curr;
            curr = curr->next;

            if (zombie->kernel_stack)
                free((void*)zombie->kernel_stack);
            if (zombie->user_stack)
                free((void*)zombie->user_stack);
            if (zombie->signal_stack)
                free((void*)zombie->signal_stack);
            if (zombie->pgd)
                free_page_tables(zombie->pgd);
            free(zombie);
        }
        else {
            prev = curr;
            curr = curr->next;
        }
    }
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// Check whether the region isn't overlap with the other of curr->vmas
int is_overlap(struct task_struct *curr, unsigned long start, unsigned long len) {
    unsigned long end = start + len;
    for (int i = 0; i < MAX_VMAS; i++) {
        if (curr->vmas[i].used) {
            if (!(end <= curr->vmas[i].vm_start || start >= curr->vmas[i].vm_end)) 
                return 1; // Overlapped!!
        }
    }
    return 0;
}

// Find a suitable region and return its begin address
unsigned long find_free_vma_region(struct task_struct *curr, unsigned long len) {
    unsigned long search_addr = MMAP_BASE;
    // Can't over user space
    while (search_addr < USER_SP_VA) {
        if (!is_overlap(curr, search_addr, len)) 
            return search_addr;
        search_addr += PAGE_SIZE;
    }
    return 0; // Can't find the suitable region
}

void idle() {
    while (1) {
        kill_zombies();
        schedule();
    }
}

void foo() {
    for (int i = 0; i < 5; i++) {
        uart_puts("Process ID: ");
        uart_dec(get_current()->pid);
        uart_puts(" ");
        uart_dec(i);
        uart_puts("\n");
        for (int j = 0; j < 100000000; j++)
            ;
        schedule();
    }
    thread_exit(); 
}


