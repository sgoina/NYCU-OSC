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
        // --- 👇 [新增] Virtual Memory 切換邏輯 ---
        // 將虛擬位址轉換為實體位址
        unsigned long pgd_pa = (unsigned long)next->pgd - PAGE_OFFSET;
        
        // 寫入 satp 並刷新 TLB
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
    task->entry_point = threadfn;
    task->pid = nr_threads++;
    task->state = TASK_RUNNING;
    task->kernel_stack = (unsigned long)allocate(PAGE_SIZE);
    task->kernel_sp = task->kernel_stack + PAGE_SIZE;
    task->user_stack = 0; // because this thread is kernel thread
    task->thread.sp = task->kernel_sp;
    task->pgd = pgd; // for kernel thread
    
    // 👇 新增：Kernel Thread 沒有專屬的 User Code 空間
    task->cpio_addr = 0;
    task->code_size = 0;
    
    // initialize signal-related info
    task->signal_stack = 0;
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    for (int i = 0; i < MAX_VMAS; i++) {
        task->vmas[i].used = 0;
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
    
    task->kernel_stack = (unsigned long)allocate(PAGE_SIZE); 
    task->kernel_sp = task->kernel_stack + PAGE_SIZE;
    
    // ❌ 刪除這兩行重複的分配
    //task->user_stack = (unsigned long)allocate(PAGE_SIZE);
    //task->user_sp = task->user_stack + PAGE_SIZE;
    
    task->pending_signals = 0;
    task->is_handling_signal = 0;
    task->signal_stack = 0;
    for(int i = 0; i < MAX_SIGNALS; i++){
        task->signal_handlers[i] = 0;
    }
    
    /// --- 👇 Virtual Memory 核心新增邏輯 (Demand Paging 版本) ---

    // 1. 僅分配專屬的 PGD
    task->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(task->pgd, 0, PAGE_SIZE);
    
    // 複製 Kernel 的記憶體宇宙（上半部）
    for (int i = 256; i < 512; i++) {
        task->pgd[i] = pgd[i];
    }

    // 🌟 2. 移除實體 Code 與 Stack 的 allocate() 與 map_pages()！
    // 我們不在此處分配實體記憶體，改由紀錄 CPIO 的原始位址與大小
    // 💡 提示：請確保你的 task_struct 有 cpio_addr 與 code_size 欄位
    unsigned long aligned_filesize = align(filesize, PAGE_SIZE);
    task->cpio_addr = (unsigned long)program_va; 
    task->code_size = filesize; // 記錄原始檔案大小，以便 Page Fault 讀取

    // 3. 註冊 VMA 帳本 (告訴 OS 這兩塊虛擬宇宙是合法的預約空間)
    
    // 註冊 Code 區塊 (VMA 0)
    task->vmas[0].vm_start = USER_CODE_VA;
    task->vmas[0].vm_end   = USER_CODE_VA + aligned_filesize;
    task->vmas[0].vm_prot  = PROT_READ | PROT_EXEC | PROT_WRITE;
    task->vmas[0].used     = 1;

    // 註冊 Stack 區塊 (VMA 1)
    unsigned long stack_vma_size = 20 * PAGE_SIZE; 
    unsigned long stack_top = (USER_SP_VA + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); 
    
    task->vmas[1].vm_start = stack_top - stack_vma_size;
    task->vmas[1].vm_end   = stack_top;
    task->vmas[1].vm_prot  = PROT_READ | PROT_WRITE;
    task->vmas[1].used     = 1;

    // 其餘 VMA 清空
    for (int i = 2; i < MAX_VMAS; i++) {
        task->vmas[i].used = 0;
    }
    
    // --- 👆 Virtual Memory 核心邏輯結束 ---
    
    // Simulate back from "sret" condition
    struct pt_regs* regs = (struct pt_regs*)(task->kernel_sp - sizeof(struct pt_regs));
    for (int i = 0; i < sizeof(struct pt_regs) / 8; i++){
        ((unsigned long*)regs)[i] = 0;
    }

    regs->tp = (unsigned long)task;
    
    // 💡 關鍵改變：sepc 和 sp 綁死在固定的虛擬位址！
    regs->sepc = USER_CODE_VA;             
    regs->sp = USER_SP_VA;
    //regs->sepc = (unsigned long)entry;      // user process entry
    //regs->sp = task->user_sp;               // user stack
    
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
    
    child->kernel_stack = (unsigned long)allocate(PAGE_SIZE); 
    child->kernel_sp = child->kernel_stack + PAGE_SIZE;
    
    child->pending_signals = 0;
    child->is_handling_signal = 0;
    child->signal_stack = 0;
    // signal handler is inherited from parent
    for (int i = 0; i < MAX_SIGNALS; i++) {
        child->signal_handlers[i] = parent->signal_handlers[i];
    }

    // Simulate back from "sret" condition
    unsigned long regs_offset = parent->kernel_sp - (unsigned long)regs; // avoid there is something in parent->kernel stack, so need to calculate offset
    struct pt_regs *child_regs = (struct pt_regs *)(child->kernel_sp - regs_offset);
    // Copy regs from parent thread
    char *src_regs = (char *)regs;
    char *dst_regs = (char *)child_regs;
    for (int i = 0; i < sizeof(struct pt_regs); i++) {
        dst_regs[i] = src_regs[i];
    }
    
    // --- 👇 Virtual Memory 宇宙複製開始 👇 ---

    // 3. 為 Child 分配全新的 PGD
    child->pgd = (unsigned long*)allocate(PAGE_SIZE);
    memset(child->pgd, 0, PAGE_SIZE);

    // 🌟 關鍵修正：複製 Kernel 的高位址映射 (絕對不能漏！)
    for (int i = 256; i < 512; i++) {
        child->pgd[i] = pgd[i];
    }

    // 🌟 統一天下：使用 VMA 進行 Page-by-Page 的拷貝
    // 這段迴圈會自動幫你處理 Code (vmas[0]), Stack (vmas[1]), 以及所有的 mmap！
    for (int i = 0; i < MAX_VMAS; i++) {
        child->vmas[i] = parent->vmas[i]; // 複製帳本

        if (child->vmas[i].used) {
            // 🌟🌟🌟 實現你的想法：Code 區塊不複製實體記憶體！🌟🌟🌟
            // 因為程式碼是唯讀的，我們讓 Child 保持 Page Table 空白。
            // 等 Child 執行到這裡時，會觸發 Instruction Page Fault，
            // 透過你寫好的 do_trap，從 child->cpio_addr 重新載入，完美達成 Demand Paging！
            if (child->vmas[i].vm_start == USER_CODE_VA) {
                continue; // 直接跳過實體記憶體拷貝！
            }

            unsigned long start = child->vmas[i].vm_start;
            unsigned long end = child->vmas[i].vm_end;
            
            // 每次前進 4KB (一頁)，負責拷貝 Stack 或是 Mmap 出來的可寫入記憶體
            for (unsigned long va = start; va < end; va += PAGE_SIZE) {
                
                // 1. 偷看 Parent 的 Page Table
                unsigned long *pte = get_pte(parent->pgd, va);
                
                // 如果 PTE 存在且有效 (代表 Parent 真的有這塊實體記憶體)
                if (pte != NULL && (*pte & PTE_V)) {
                    
                    // 2. 幫 Child 配置一塊新的實體記憶體
                    void *child_page = allocate(PAGE_SIZE);
                    if (!child_page) return -1; // OOM

                    // 3. 安全拷貝資料：
                    // 為了避免 Kernel 去讀 User VA (va) 觸發 SUM 保護機制，
                    // 我們直接從 PTE 算出 Parent 的實體位址，並轉為 Kernel 可以讀的位址。
                    unsigned long parent_pa = (*pte >> 10) << 12;
                    void *parent_kernel_va = (void *)(parent_pa + PAGE_OFFSET);
                    
                    memcpy(child_page, parent_kernel_va, PAGE_SIZE);

                    // 4. 將權限轉為 PTE 格式並映射到 Child 的 Page Table
                    unsigned long child_pa = (unsigned long)child_page - PAGE_OFFSET;
                    
                    unsigned long pte_prot = PROT_USER_BASE; 
                    if (child->vmas[i].vm_prot & PROT_READ)  pte_prot |= PTE_R;
                    if (child->vmas[i].vm_prot & PROT_WRITE) pte_prot |= PTE_W;
                    if (child->vmas[i].vm_prot & PROT_EXEC)  pte_prot |= PTE_X;
                    
                    map_pages(child->pgd, va, PAGE_SIZE, child_pa, pte_prot);
                }
                // 💡 如果 pte 為 NULL，代表 Parent 還沒觸發過 Demand Paging
                // 我們就什麼都不做，完美跳過這個未爆彈！
            }
        }
    }

    child->code_frame = 0;
    child->user_stack = 0;
    // 讓 Child 知道自己如果踩空了 Code 區塊，該去哪裡討救兵
    child->cpio_addr = parent->cpio_addr;
    child->code_size = parent->code_size;

    // Return value 0 for child thread
    child_regs->a0 = 0;
    child_regs->tp = (unsigned long)child;
    
    // Calculate parent_sp offset and copy it to child_sp
    //unsigned long user_sp_offset = parent->user_sp - regs->sp;
    //child_regs->sp = child->user_sp - user_sp_offset;
    // ✅ 現在的寫法：直接複製！因為大家的虛擬位址都一樣
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
            // 🌟 關鍵修正 1：將分配到的實體記憶體映射到 User 虛擬位址
            unsigned long sig_stack_pa = curr->signal_stack - PAGE_OFFSET;
            // 注意：必須加上 PTE_X，因為我們要讓 User 執行放在 Stack 裡的 Trampoline Code！
            map_pages(curr->pgd, USER_SIG_STACK_VA, PAGE_SIZE, sig_stack_pa, PROT_USER_RWX);
            
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
            free(zombie);
        }
        else {
            prev = curr;
            curr = curr->next;
        }
    }
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
}

// 檢查是否與現有的 VMA 重疊
int is_overlap(struct task_struct *curr, unsigned long start, unsigned long len) {
    unsigned long end = start + len;
    for (int i = 0; i < MAX_VMAS; i++) {
        if (curr->vmas[i].used) {
            // 如果 [start, end) 和 [vm_start, vm_end) 有交集，就是重疊
            if (!(end <= curr->vmas[i].vm_start || start >= curr->vmas[i].vm_end)) {
                return 1; 
            }
        }
    }
    return 0;
}

unsigned long find_free_vma_region(struct task_struct *curr, unsigned long len) {
    unsigned long search_addr = MMAP_BASE;
    while (search_addr < USER_SP_VA) { // Can't over user space
        if (!is_overlap(curr, search_addr, len)) {
            return search_addr;
        }
        search_addr += PAGE_SIZE; // 往下找一個 Page
    }
    return 0; // 找不到空間
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


