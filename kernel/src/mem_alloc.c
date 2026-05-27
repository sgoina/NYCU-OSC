#include "deviceTree.h"
#include "mem_alloc.h"
#include "uart.h"
#include "list.h"
#include "vm.h"

extern char _start[]; // kernel start (Virtual Address)
extern char _end[];   // kernel end (Virtual Address)

static uint64_t mem_start = 0; // the start of memory region (Physical Address)
static uint64_t mem_end = 0; // the end of memory region (Physical Address)
static int num_pages = 0; // the number of usable pages

// reserved region (Store Physical Addresses)
struct reserved_region {
    uint64_t start;
    uint64_t end;
};

#define MAX_RESERVED_REGIONS 32 
static struct reserved_region early_reserved[MAX_RESERVED_REGIONS]; //record all reserved regions
static int num_early_reserved = 0; // the number of reserved regions

#define MAX_ORDER 10 // 2^10 = 1024 pages (4MB)
#define NUM_POOLS 6 // the number of options in Dynamic Memory Allocator
const int pool_sizes[NUM_POOLS] = {16, 32, 64, 128, 256, 512};

// Frame Array node
struct page {
    struct list_head list;
    int order; // -1 is not a head, others in 0~10
    int val; // 1 is allocable, 0 is used
    int pool_idx; // -1 for buddy system, 0~5 for different chunk size
    int chunk_count; // Record how many chunk is used in this page
    int ref_count; // 🌟 [新增] COW 實體分頁引用計數器
};

// Frame Array
struct page *mem_map;

// free list for every order
struct list_head free_area[MAX_ORDER + 1];

// chuck pool for every size
struct list_head chunk_pools[NUM_POOLS];

// add a reserved region in array 
void add_early_reserve(uint64_t start, uint64_t size) {
    if (size == 0 || num_early_reserved >= MAX_RESERVED_REGIONS) return;
    early_reserved[num_early_reserved].start = start;
    early_reserved[num_early_reserved].end = start + size;
    num_early_reserved++;
}

// find the safe address for mem_map (Frame Array)
static uint64_t find_safe_base(uint64_t mem_map_size) {
    // bubble sort in reserved region array
    for (int i = 0; i < num_early_reserved - 1; i++) {
        for (int j = 0; j < num_early_reserved - i - 1; j++) {
            if (early_reserved[j].start > early_reserved[j+1].start) {
                struct reserved_region temp = early_reserved[j];
                early_reserved[j] = early_reserved[j+1];
                early_reserved[j+1] = temp;
            }
        }
    }

    uint64_t current_base = mem_start;
    for (int i = 0; i < num_early_reserved; i++) {
        // align the address in 1 page
        uint64_t aligned_base = (current_base + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
        
        if (early_reserved[i].start > aligned_base) {
            uint64_t gap_size = early_reserved[i].start - aligned_base;
            // this gap is enough for mem_map
            if (gap_size >= mem_map_size) {
                return aligned_base;
            }
        }
        // update current_base
        if (early_reserved[i].end > current_base) {
            current_base = early_reserved[i].end;
        }
    }

    // determine whether the last region is enough for mem_map
    uint64_t aligned_base = (current_base + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
    if (mem_end > aligned_base && (mem_end - aligned_base) >= mem_map_size) {
        return aligned_base;
    }
    // can't find a safe region for mem_map
    uart_puts("Error: Cannot find safe gap for mem_map!\n");
    return 0; 
}

// initalize memory allocation
void init_mem(unsigned long dtb_ptr){
    int mem_offset = fdt_path_offset(dtb_ptr, "/memory"); // find the offset of "/memory" in the device tree
    if (mem_offset != -1){
        int len = 0;
        const void* prop = fdt_getprop(dtb_ptr, mem_offset, "reg", &len); // find the base address and size of memory
        if (prop != NULL){
            const uint32_t* reg = (const uint32_t*)prop;
            mem_start = ((uint64_t)bswap32(reg[0]) << 32) | bswap32(reg[1]);
            uint64_t mem_size = ((uint64_t)bswap32(reg[2]) << 32) | bswap32(reg[3]);
            mem_end = mem_start + mem_size;

            num_pages = mem_size / PAGE_SIZE;

            uart_puts("Memory address: ");
            uart_hex(mem_start);
            uart_puts(", size: ");
            uart_hex(mem_size);
            uart_putc('\n');
        }
    }
    
    // 1. Kernel 
    // [FIX] _start 跟 _end 現在是 Virtual Address，必須減掉 PAGE_OFFSET 轉回 Physical Address 記錄
    add_early_reserve((uint64_t)_start - PAGE_OFFSET, (uint64_t)_end - (uint64_t)_start);
    uart_puts("Kernel address: ");
    uart_hex((uint64_t)_start);
    uart_puts(", size: ");
    uart_hex((uint64_t)_end - (uint64_t)_start);
    uart_putc('\n');
    
    // 2. DTB
    // [FIX] dtb_ptr 從 main 傳進來時已是 Virtual Address，轉回 PA 記錄
    add_early_reserve((uint64_t)dtb_ptr - PAGE_OFFSET, bswap32(((const struct fdt_header *)dtb_ptr)->totalsize));
    uart_puts("DTB address: ");
    uart_hex((uint64_t)dtb_ptr);
    uart_puts(", size: ");
    uart_hex(bswap32(((const struct fdt_header *)dtb_ptr)->totalsize));
    uart_putc('\n');
    
    // 3. Initramfs
    int ramfs_offset = fdt_path_offset(dtb_ptr, "/chosen");
    if (ramfs_offset != -1) {
        int len1 = 0, len2 = 0;
        const void* start_prop = fdt_getprop(dtb_ptr, ramfs_offset, "linux,initrd-start", &len1);
        const void* end_prop = fdt_getprop(dtb_ptr, ramfs_offset, "linux,initrd-end", &len2);
        if (start_prop != NULL && end_prop != NULL) {
            uint64_t initrd_start = 0, initrd_end = 0;
            if (len1 == 8)
                initrd_start = bswap64(*(const uint64_t*)start_prop);
            else
                initrd_start = bswap32(*(const uint32_t*)start_prop);
            if (len2 == 8)
                initrd_end   = bswap64(*(const uint64_t*)end_prop);
            else 
                initrd_end   = bswap32(*(const uint32_t*)end_prop);
            
            // DTB 讀出來的 initrd_start 本來就是 PA，所以直接保留即可
            add_early_reserve(initrd_start, initrd_end - initrd_start);
            uart_puts("Initramfs address: ");
            uart_hex(initrd_start);
            uart_puts(", size: ");
            uart_hex(initrd_end);
            uart_putc('\n');
        }
        else
            uart_puts("There is no initramfs needed to reserve\n");
    }
    
    // 4. Device Tree /reserved-memory (從 DTB 讀出來的也是 PA，內部邏輯不變)
    fdt_reserve_memory_nodes(dtb_ptr);
    
    // calculate the size of mem_map and find a safe region to place it
    uint64_t mem_map_size = num_pages * sizeof(struct page);
    uint64_t safe_base = find_safe_base(mem_map_size); // 這裡找到的是 PA
    
    // [FIX] C 語言的指標必須指在 Virtual Address，加上 PAGE_OFFSET 以防 Page Fault
    mem_map = (struct page *)(safe_base + PAGE_OFFSET);

    uart_puts("[Startup Allocator] mem_map placed at: ");
    uart_hex((uint64_t)mem_map); // 印出 VA 方便檢查
    uart_puts(", size: ");
    uart_hex(mem_map_size);
    uart_putc('\n');
    
    // Initialize buddy system
    for (int i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&free_area[i]);
    }
    
    // Initialize Chunk Pools
    for (int i = 0; i < NUM_POOLS; i++) {
        INIT_LIST_HEAD(&chunk_pools[i]);
    }

    // Initialize Frame Array
    for (int i = 0; i < num_pages; i++) {
        mem_map[i].val = 1;
        mem_map[i].order = -1;
        mem_map[i].pool_idx = -1;
        mem_map[i].chunk_count = 0;
        mem_map[i].ref_count = 0;
        INIT_LIST_HEAD(&mem_map[i].list);
    }

    // cutting into blocks of 2 ^ MAX_ORDER bytes and put them into free list
    for (int i = 0; i < num_pages; i += (1 << MAX_ORDER)) {
        // ensure the place is enough for 2 ^ MAX_ORDER bytes
        if (i + (1 << MAX_ORDER) <= num_pages) {
            mem_map[i].order = MAX_ORDER;
            list_add_back(&mem_map[i].list, &free_area[MAX_ORDER]);
        }
    }
    
    uart_puts("--- Start Reserving Memory ---\n");
    uart_puts("Reserve mem_map region: ");
    // memory_reserve 預期接收 PA，所以傳入 safe_base
    memory_reserve(safe_base, mem_map_size);
    
    // save memory for reserved region
    for (int i = 0; i < num_early_reserved; i++) {
        uart_puts("Reserve recorded region: ");
        memory_reserve(early_reserved[i].start, early_reserved[i].end - early_reserved[i].start);
    }
    show_mem_alloc();
}

void fdt_reserve_memory_nodes(unsigned long dtb_ptr) {
    int reserved_offset = fdt_path_offset(dtb_ptr, "/reserved-memory");
    if (reserved_offset == -1)
        return;

    uart_puts("--- Parsing /reserved-memory ---\n");

    const void* fdt = (const void*)dtb_ptr;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    const uint8_t* p = (const uint8_t*)fdt + reserved_offset;
    
    uint32_t token = bswap32(*(const uint32_t*)p);
    if (token != FDT_BEGIN_NODE)
        return; 
    p += 4;

    // skip the name of "/reserved-memory"  
    int name_len = strlen((const char*)p);
    p += ((name_len + 1 + 3) & ~3);

    int depth = 1; // record the depth of checking
    char* node_name; // the name of the region we find
    int cnt = 0; // record the number of the region we find
    while (1) {
        token = bswap32(*(const uint32_t*)p);
        p += 4;
        if (token == FDT_BEGIN_NODE) { 
            depth++;
            int nlen = strlen((const char*)p);
            if (depth == 2)
                node_name = (char *)p;
            p += ((nlen + 1 + 3) & ~3);
        }
        else if (token == FDT_END_NODE) { 
            depth--;
            if (depth == 0)
                return;
        }
        else if (token == FDT_PROP) { 
            uint32_t len = bswap32(*(const uint32_t*)p);
            uint32_t nameoff = bswap32(*(const uint32_t*)(p + 4));
            p += 8;

            const char* strings = (const char*)fdt + bswap32(hdr->off_dt_strings);
            const char* prop_name = strings + nameoff;

            if (strcmp(prop_name, "reg") == 0) {
                const uint32_t* reg = (const uint32_t*)p;
                uint64_t base = ((uint64_t)bswap32(reg[0]) << 32) | bswap32(reg[1]);
                uint64_t size = ((uint64_t)bswap32(reg[2]) << 32) | bswap32(reg[3]);
                
                cnt++;
                uart_dec(cnt);
                uart_puts(". ");
                uart_puts(node_name);
                uart_puts(" begin: ");
                uart_hex(base);
                uart_puts(", size: ");
                uart_hex(size);
                uart_putc('\n');

                add_early_reserve(base, size);
            }
            p += ((len + 3) & ~3);;
        }
        else if (token == FDT_END) { 
            break;
        }
    }
}

// convert page index to address 
// [FIX] 分配出去的記憶體位址應為 Virtual Address (加上 PAGE_OFFSET)
unsigned long page_to_addr(int idx){
    return mem_start + (uint64_t)idx * PAGE_SIZE + PAGE_OFFSET;
}

// get the end page index in the same block
int end_idx(int idx, int order){
    return idx + (1 << order) - 1;
}

// find the enough order for the size
int size_to_order(unsigned int size) {
    int order = 0;
    while ((1 << order) * PAGE_SIZE < size) {
        order++;
    }
    return order;
}

// allocate blocks (size > 4KB)
struct page* alloc_pages(unsigned int size) {
    int order = size_to_order(size);
    if (order > MAX_ORDER){
        uart_puts("The desired size is too big!\n");
        return NULL;
    }

    int current_order = order;
    
    while (current_order <= MAX_ORDER) {
        if (!list_empty(&free_area[current_order]))
            break;
        current_order++;
    }

    if (current_order > MAX_ORDER) {
        uart_puts("There is no memory space for new allocation!\n");
        return NULL;
    }

    struct page *target_page = (struct page *)list_front(&free_area[current_order]);
    list_remove(&target_page->list);
    
    // save redundant area in free list
    while (current_order > order) {
        current_order--;
        int page_idx = target_page - mem_map;
        int buddy_idx = page_idx ^ (1 << current_order);
        struct page *buddy_page = &mem_map[buddy_idx];

        buddy_page->val = 1;
        buddy_page->order = current_order;
        list_add_back(&buddy_page->list, &free_area[current_order]);
    }

    target_page->val = 0;
    target_page->order = order;
    target_page->ref_count = 1;
    
    return target_page;
}

// return block (size >= 4KB)
void free_pages(struct page* p) {
    if (!p){
        uart_puts("The block pointer is NULL.\n");
        return;
    }
    if (p->val != 0){
        uart_puts("This page isn't a head of block or not allocated.\n");
        return;
    }
    
    // 🌟 [新增] COW 核心攔截：遞減計數器
    p->ref_count--;
    if (p->ref_count > 0) {
        return; // 還有其他 Process 在用這塊記憶體，保留它！
    }

    int order = p->order;
    int page_idx = p - mem_map;
    
    while (order < MAX_ORDER) {
        int buddy_idx = page_idx ^ (1 << order);
        struct page *buddy = &mem_map[buddy_idx];

        if (!buddy->val || buddy->order != order)
            break;
        
        list_remove(&buddy->list);
        
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
            struct page *tmp = p;
            p = buddy;
            buddy = tmp;
        }
        buddy->order = -1; 
        order++;
    }

    p->val = 1;
    p->order = order;
    list_add_back(&p->list, &free_area[order]);
}

// Allocate a chunk
void *kmalloc(unsigned int size) {
    int pool_idx = -1;
    for (int i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            pool_idx = i;
            break;
        }
    }
    if (pool_idx == -1)
        return NULL;
    
    if (list_empty(&chunk_pools[pool_idx])) {
        struct page *new_page = alloc_pages(PAGE_SIZE);
        if (!new_page){
            uart_puts("There is no space to manage chunk pool.\n");
            return NULL;
        }
        new_page->pool_idx = pool_idx;
        
        int chunk_size = pool_sizes[pool_idx];
        // page_addr 是 VA，直接對其操作記憶體不會引發 Page Fault
        unsigned long page_addr = page_to_addr(new_page - mem_map);

        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            struct list_head *chunk = (struct list_head *)(page_addr + offset);
            list_add_back(chunk, &chunk_pools[pool_idx]);
        }
    }

    struct page *target_chunk = (struct page *)list_front(&chunk_pools[pool_idx]);
    list_remove(&target_chunk->list);
    
    unsigned long chunk_addr = (unsigned long)target_chunk; // VA
    unsigned long base_addr = chunk_addr & ~((unsigned long)PAGE_SIZE - 1); // VA
    
    // [FIX] base_addr 是 VA，而 mem_start 是 PA。須將 base_addr 減去 PAGE_OFFSET 轉回 PA 才能算出正確的 page_idx
    int page_idx = ((base_addr - PAGE_OFFSET) - mem_start) / PAGE_SIZE;
    mem_map[page_idx].chunk_count++;

    return (void *)target_chunk;
}

// Free a chunk
void kfree(void *ptr) {
    if (!ptr) {
        uart_puts("The chunk pointer is NULL.\n");
        return;
    }

    unsigned long addr = (unsigned long)ptr; // VA
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1); // VA
    
    // [FIX] 扣掉 PAGE_OFFSET 算回真實 PA
    int page_idx = ((base_addr - PAGE_OFFSET) - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];
    int pool_idx = p->pool_idx;

    if (pool_idx == -1) {
        uart_puts("This page isn't for chunk pool.\n");
        return;
    }
    
    p->chunk_count--;

    if (p->chunk_count == 0) {
        int chunk_size = pool_sizes[pool_idx];
        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            unsigned long current_chunk_addr = base_addr + offset;
            if (current_chunk_addr != addr) {
                struct list_head *chunk_to_remove = (struct list_head *)current_chunk_addr;
                list_remove(chunk_to_remove);
            }
        }
        p->pool_idx = -1;
        free_pages(p); 
    }
    else {
        struct list_head *chunk = (struct list_head *)ptr;
        list_add_back(chunk, &chunk_pools[pool_idx]);
    }
}

void *allocate(unsigned int size) {
    if (size == 0){
        uart_puts("Can't allocate a 0 size region\n");
        return NULL;
    }

    if (size <= pool_sizes[NUM_POOLS - 1]) {
        return kmalloc(size);
    } 
    else {
        struct page *p = alloc_pages(size);
        if (!p)
            return NULL;
        return (void *)page_to_addr(p - mem_map); // 回傳 VA 供 Kernel 使用
    }
}

void free(void *ptr) {
    if (!ptr){
        uart_puts("The point is NULL.\n");
        return;
    }
    unsigned long addr = (unsigned long)ptr; // VA
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1); // VA
    
    // [FIX] 計算 Index 時記得將 VA 轉回 PA
    int page_idx = ((base_addr - PAGE_OFFSET) - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];

    if (p->pool_idx != -1)
        kfree(ptr);
    else
        free_pages(p);
}


void memory_reserve(unsigned long long start, unsigned long long size) {
    if (size == 0)
        return;

    uint64_t res_start = start;
    uint64_t res_end = start + size;
    
    if (res_end <= mem_start || res_start >= mem_end){
        uart_puts("This region is not in allocable memory, so not need to reserve.\n");
        return;
    }
    if (res_start < mem_start){
        uart_puts("This base address is not in allocable memory, so reset start address at ");
        uart_hex(mem_start);
        uart_puts(".\n");
        res_start = mem_start;
    }
    if (res_end > mem_end){
        uart_puts("This end address is not in allocable memory, so reset end address at ");
        uart_hex(mem_end);
        uart_puts(".\n");
        res_end = mem_end;
    }
    int start_pfn = (res_start - mem_start) / PAGE_SIZE;
    int end_pfn = (res_end - mem_start + PAGE_SIZE - 1) / PAGE_SIZE;

    for (int order = MAX_ORDER; order >= 0; order--) {
        struct list_head *curr = list_front(&free_area[order]);
        while (curr != &free_area[order]) {
            struct list_head *next_node = curr->next;
            
            struct page *p = (struct page *)curr;
            int pfn = p - mem_map;
            int block_start = pfn;
            int block_end = pfn + (1 << order);

            if (block_end <= start_pfn || block_start >= end_pfn) {
                curr = next_node;
                continue;
            }

            list_remove(curr);

            if (block_start >= start_pfn && block_end <= end_pfn) {
                p->val = 0;
                p->order = -1;
                p->pool_idx = -1;
                curr = next_node;
                continue;
            }

            int next_order = order - 1;
            int buddy_idx = pfn ^ (1 << next_order);
            struct page *buddy = &mem_map[buddy_idx];

            p->order = next_order;
            buddy->order = next_order;

            list_add_back(&p->list, &free_area[next_order]);
            list_add_back(&buddy->list, &free_area[next_order]);
            
            curr = next_node;
        }
    }
}

// increase the count of page reference 
void inc_page_ref(unsigned long pa) {
    // check valid physical address
    if (pa >= mem_start && pa < mem_end) {
        int page_idx = (pa - mem_start) / PAGE_SIZE;
        mem_map[page_idx].ref_count++;
    }
}

// decrease the count of page reference 
void dec_page_ref(unsigned long pa) {
    if (pa >= mem_start && pa < mem_end) {
        int page_idx = (pa - mem_start) / PAGE_SIZE;
        mem_map[page_idx].ref_count--;
    }
}

// get the count of page reference 
int get_page_ref(unsigned long pa) {
    if (pa >= mem_start && pa < mem_end) {
        int page_idx = (pa - mem_start) / PAGE_SIZE;
        return mem_map[page_idx].ref_count;
    }
    return 0;
}

void show_mem_alloc() {
    for (int i = MAX_ORDER; i >= 0; i--){
        uart_puts("free_area[");
        uart_dec(i);
        uart_puts("] ");
        uart_dec(list_size(&free_area[i]));
        uart_putc('\n');
    }
}

void alloc_test(){
    uart_puts("Testing memory allocation...\n");
    char *ptr1 = (char *)allocate(4000);
    char *ptr2 = (char *)allocate(8000);
    char *ptr3 = (char *)allocate(4000);
    char *ptr4 = (char *)allocate(4000);
    char *ptr5 = (char *)allocate(4000);

    free(ptr1);
    free(ptr2);
    free(ptr3);
    free(ptr4);
    free(ptr5);

    /* Test kmalloc */
    uart_puts("Testing dynamic allocator...\n");
    char *kmem_ptr1 = (char *)allocate(16);
    char *kmem_ptr2 = (char *)allocate(32);
    char *kmem_ptr3 = (char *)allocate(64);
    char *kmem_ptr4 = (char *)allocate(128);

    free(kmem_ptr1);
    free(kmem_ptr2);
    free(kmem_ptr3);
    free(kmem_ptr4);
    
    char *kmem_ptr5 = (char *)allocate(16);
    char *kmem_ptr6 = (char *)allocate(32);

    free(kmem_ptr5);
    free(kmem_ptr5);
    free(kmem_ptr6);
    
    // Test allocate new page if the cache is not enough
    void *kmem_ptr[102];
    for (int i=0; i<100; i++) {
        kmem_ptr[i] = (char *)allocate(128);
    }
    for (int i=0; i<100; i++) {
        free(kmem_ptr[i]);
    }
    
    // Test exceeding the maximum size
    char *kmem_ptr7 = (char *)allocate(4194305);
    if (kmem_ptr7 == NULL) {
        uart_puts("Allocation failed as expected for size > 4194304 (4MB)\n");
    }
    else {
        uart_puts("Unexpected allocation success for size > 4194304 (4MB)\n");
        free(kmem_ptr7);
    }
}
