#include "deviceTree.h"
#include "mem_alloc.h"
#include "uart.h"
#include "list.h"

static uint64_t mem_start = 0;
static uint64_t mem_end = 0;
static int actual_num_pages = 0; // NEW: 記錄系統實際有幾個可用的 Pages

#define PAGE_SIZE 4096 // 1 page size = 4KB
#define MAX_MEM_PAGES 524288 // NEW: 預留給最多 2GB 記憶體的陣列大小 (524288 * 4KB = 2GB)

#define MAX_ORDER 10 // 2^10 = 1024 pages (4MB)
#define NUM_POOLS 6 // the number of options in Dynamic Memory Allocator
const int pool_sizes[NUM_POOLS] = {16, 32, 64, 128, 256, 512};

// Frame Array node
struct page {
    struct list_head list;
    int order; // -1 is not a head
    int val; // 1 is allocable, 0 is used
    int pool_idx; // NEW: -1 代表屬於 Buddy System，>= 0 代表屬於 Chunk Pool
    int chunk_count; // NEW: 記錄這個 Page 有幾個 chunk 正在被外借
};

// Frame Array (使用編譯期固定的常數，這樣編譯器就不會報錯了！)
struct page mem_map[MAX_MEM_PAGES];

// free list for every order
struct list_head free_area[MAX_ORDER + 1];

// chuck pool for every size
struct list_head chunk_pools[NUM_POOLS];

void init_mem(unsigned long dtb_ptr){
    int mem_offset = fdt_path_offset(dtb_ptr, "/memory"); // find the offset of "/memory" in the device tree
    if (mem_offset != -1){
        int len = 0;
        const void* prop = fdt_getprop(dtb_ptr, mem_offset, "reg", &len); // find the base address and size of memory
        if (prop != NULL){
            const uint32_t* reg = (const uint32_t*)prop;
            mem_start = ((uint64_t)bswap32(reg[0]) << 32) | bswap32(reg[1]);
            
            // 🌟 修正：reg[2], reg[3] 代表的是 size！
            uint64_t mem_size = ((uint64_t)bswap32(reg[2]) << 32) | bswap32(reg[3]);
            mem_end = mem_start + mem_size; 

            // 🌟 執行期計算實際的 page 數量
            actual_num_pages = mem_size / PAGE_SIZE;
            
            // 防呆：如果讀到的記憶體大於 2GB，截斷到我們陣列的極限
            if (actual_num_pages > MAX_MEM_PAGES) {
                actual_num_pages = MAX_MEM_PAGES;
            }

            uart_puts("Memory address: ");
            uart_hex(mem_start);
            uart_puts(", size: ");
            uart_hex(mem_size); // 這裡印出 size 比較直觀
            uart_putc('\n');
        }
    }
    
    // initialize buddy system
    for (int i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&free_area[i]);
    }
    
    // Initialize Chunk Pools
    for (int i = 0; i < NUM_POOLS; i++) {
        INIT_LIST_HEAD(&chunk_pools[i]);
    }

    // Initialize Frame Array (🌟 改用 actual_num_pages)
    for (int i = 0; i < actual_num_pages; i++) {
        mem_map[i].val = 1;
        mem_map[i].order = -1;
        mem_map[i].pool_idx = -1;
        INIT_LIST_HEAD(&mem_map[i].list);
    }

    // cutting into blocks of 2 ^ MAX_ORDER bytes and put them into free list (🌟 改用 actual_num_pages)
    for (int i = 0; i < actual_num_pages; i += (1 << MAX_ORDER)) {
        // 確保最後一塊有足夠的空間可以裝滿一個 MAX_ORDER
        if (i + (1 << MAX_ORDER) <= actual_num_pages) {
            mem_map[i].order = MAX_ORDER;
            list_add_back(&mem_map[i].list, &free_area[MAX_ORDER]);
        }
    }
}

unsigned long page_to_addr(int idx){
    return mem_start + idx * PAGE_SIZE;
}

int end_idx(int idx, int order){
    return idx + (1 << order) - 1;
}

int size_to_order(unsigned int size) {
    int order = 0;
    while ((1 << order) * PAGE_SIZE < size) {
        order++;
    }
    
    return order;
}

struct page* alloc_pages(unsigned int size) {
    int order = size_to_order(size);
    if (order > MAX_ORDER)
        return NULL;

    int current_order = order;
    
    // find the order of an allocable block
    while (current_order <= MAX_ORDER) {
        if (!list_empty(&free_area[current_order]))
            break;
        current_order++;
    }

    if (current_order > MAX_ORDER) {
        uart_puts("There is no memory space for new allocation!\n");
        return NULL;
    }

    struct page *target_page = list_front(&free_area[current_order]);
    int target_idx = target_page - mem_map;
    list_remove(&target_page->list);
    
    uart_puts("[-] Remove page ");
    uart_dec(target_idx);
    uart_puts(" from order ");
    uart_dec(current_order);
    uart_puts(". Range of pages: [");
    uart_dec(target_idx);
    uart_puts(", ");
    uart_dec(end_idx(target_idx, current_order));
    uart_puts("]\n");

    while (current_order > order) {
        current_order--;
        // find the buddy page
        int page_idx = target_page - mem_map;
        int buddy_idx = page_idx ^ (1 << current_order);
        struct page *buddy_page = &mem_map[buddy_idx];

        // put bottom half as buddy into free list
        buddy_page->val = 1;
        buddy_page->order = current_order;
        list_add_back(&buddy_page->list, &free_area[current_order]);
        
        uart_puts("[+] Add page ");
        uart_dec(buddy_idx);
        uart_puts(" to order ");
        uart_dec(current_order);
        uart_puts(". Range of pages: [");
        uart_dec(buddy_idx);
        uart_puts(", ");
        uart_dec(end_idx(buddy_idx, current_order));
        uart_puts("]\n");
    }

    // return block
    target_page->val = 0;
    target_page->order = order;
    
    unsigned int addr = page_to_addr(target_idx);
    uart_puts("[Page] Allocate ");
    uart_hex(addr);
    uart_puts(" at order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(target_idx);
    uart_putc('\n');
    show_mem_alloc();
    return target_page;
}

void free_pages(struct page* p) {
    if (!p || p->val != 0)
        return;

    int order = p->order;
    int page_idx = p - mem_map;
    int original_idx = page_idx;

    while (order < MAX_ORDER) {
        int buddy_idx = page_idx ^ (1 << order);
        struct page *buddy = &mem_map[buddy_idx];

        // check buddy isn't used
        if (!buddy->val || buddy->order != order) {
            break;
        }
        
        uart_puts("[*] Buddy found! buddy idx: ");
        uart_dec(buddy_idx);
        uart_puts(" for page ");
        uart_dec(page_idx);
        uart_puts(" with order ");
        uart_dec(order);
        uart_putc('\n');
        
        // remove buddy page from free list
        list_remove(&buddy->list);
        
        uart_puts("[-] Remove page ");
        uart_dec(buddy_idx);
        uart_puts(" from order ");
        uart_dec(order);
        uart_puts(". Range of pages: [");
        uart_dec(buddy_idx);
        uart_puts(", ");
        uart_dec(end_idx(buddy_idx, order));
        uart_puts("]\n");
        
        // take the min idx
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
            struct page *tmp = p;
            p = buddy;
            buddy = tmp;
        }
        buddy->order = -1; // buddy isn't a head anymore
        order++;
    }

    // put the whole block into free list
    p->val = 1;
    p->order = order;
    list_add_back(&p->list, &free_area[order]);
    
    uart_puts("[+] Add page ");
    uart_dec(page_idx);
    uart_puts(" to order ");
    uart_dec(order);
    uart_puts(". Range of pages: [");
    uart_dec(page_idx);
    uart_puts(", ");
    uart_dec(end_idx(page_idx, order));
    uart_puts("]\n");
    
    uart_puts("[Page] Free ");
    uart_hex(page_to_addr(original_idx));
    uart_puts(" and add back to order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(page_idx);
    uart_putc('\n');
    
    show_mem_alloc();
}

void *kmalloc(unsigned int size) {
    int pool_idx = -1;
    for (int i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            pool_idx = i;
            break;
        }
    }
    if (pool_idx == -1) return NULL; // 保險機制，過大會給 Buddy 處理

    if (list_empty(&chunk_pools[pool_idx])) {
        // 向 Buddy 請求 1 個 Page (PAGE_SIZE bytes)
        struct page *new_page = alloc_pages(PAGE_SIZE);
        if (!new_page) return NULL; 

        new_page->pool_idx = pool_idx; // 標記這個 Page 屬於這個 pool
        
        int chunk_size = pool_sizes[pool_idx];
        unsigned long page_addr = page_to_addr(new_page - mem_map);

        // 切割成小塊，並放入對應的 free list 中
        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            struct list_head *chunk = (struct list_head *)(page_addr + offset);
            list_add_back(chunk, &chunk_pools[pool_idx]);
        }
    }

    // 從 Pool 中拿出第一個可用的 Chunk
    struct list_head *target_chunk = chunk_pools[pool_idx].next;
    list_remove(target_chunk);
    
    // 🌟 NEW: 找到這個 chunk 屬於哪一個 Page，並將借出數量 +1
    unsigned long chunk_addr = (unsigned long)target_chunk;
    unsigned long base_addr = chunk_addr & ~((unsigned long)PAGE_SIZE - 1);
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    mem_map[page_idx].chunk_count++; // 借出一個，計數 +1

    uart_puts("[Chunk] Allocate ");
    uart_hex((unsigned long)target_chunk);
    uart_puts(" at chunk size ");
    uart_dec(pool_sizes[pool_idx]);
    uart_putc('\n');

    return (void *)target_chunk;
}

void kfree(void *ptr) {
    if (!ptr) return;

    unsigned long addr = (unsigned long)ptr;
    // 透過抹除低 12 bits 找到 Page 的起始位址
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1);
    
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];
    int pool_idx = p->pool_idx;

    if (pool_idx == -1) return; // 這不是 kmalloc 分配的，不應呼叫此函式

    uart_puts("[Chunk] Free ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_dec(pool_sizes[pool_idx]);
    uart_putc('\n');
    
    // 🌟 NEW: 歸還一個 Chunk，計數器 -1
    p->chunk_count--;

    // 判斷是否整頁都空了
    if (p->chunk_count == 0) {
        
        int chunk_size = pool_sizes[pool_idx];
        
        // 既然整頁都空了，代表除了現在這個 ptr 以外，
        // 該頁其他的 chunk 早就被 kfree 過，並掛在 chunk_pools[pool_idx] 裡面了。
        // 我們必須把它們從串列上拔下來，不然 Buddy 拿走這塊記憶體後，串列就壞掉了！
        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            unsigned long current_chunk_addr = base_addr + offset;
            
            // 只要不是這次傳進來的 ptr，就代表它已經在 free list 裡面，直接拔除！
            if (current_chunk_addr != addr) {
                struct list_head *chunk_to_remove = (struct list_head *)current_chunk_addr;
                list_remove(chunk_to_remove);
            }
        }

        // 重置 Page 狀態並還給 Buddy System
        p->pool_idx = -1;
        free_pages(p); 
        uart_puts("[Pool] Page completely free! Returning to Buddy.\n");

    } else {
        // 如果還有其他 chunk 沒還回來，那就單純把這個 chunk 掛回 Pool 中
        struct list_head *chunk = (struct list_head *)ptr;
        list_add_back(chunk, &chunk_pools[pool_idx]);
    }
}

void *allocate(unsigned int size) {
    if (size == 0) return NULL;

    // 小於等於 2048 Bytes，交給動態分配器
    if (size <= pool_sizes[NUM_POOLS - 1]) {
        return kmalloc(size);
    } 
    // 大於 2048 Bytes，交給 Buddy System
    else {
        struct page *p = alloc_pages(size);
        if (!p) return NULL;
        return (void *)page_to_addr(p - mem_map);
    }
}

void free(void *ptr) {
    if (!ptr) return;

    unsigned long addr = (unsigned long)ptr;
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1);
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];

    // 透過 pool_idx 判斷交由誰來釋放
    if (p->pool_idx != -1) {
        kfree(ptr);
    } else {
        free_pages(p);
    }
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

void memory_reserve(unsigned long long start, unsigned long long size) {
    uart_puts("begin: ");
    uart_hex(start);
    uart_puts(", end: ");
    uart_hex(start + size);
    uart_puts(", size: ");
    uart_hex(size);
    uart_putc('\n');
    if (size == 0)
        return;

    uint64_t res_start = start;
    uint64_t res_end = start + size;
    
    // Avoid over memory allocation range
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

    // 換算成起始與結束的 Page Frame Number (PFN)
    int start_pfn = (res_start - mem_start) / PAGE_SIZE;
    int end_pfn = (res_end - mem_start + PAGE_SIZE - 1) / PAGE_SIZE; // 向上對齊 (Ceiling)

    // 從最大的區塊開始找，有重疊就切半往下一層丟
    for (int order = MAX_ORDER; order >= 0; order--) {
        
        // 必須先存好 next 節點 (Safe Iteration)
        // 因為如果這個區塊被拔除 (list_remove)，它的 next 指標就會被清空
        struct list_head *curr = free_area[order].next;
        
        while (curr != &free_area[order]) {
            struct list_head *next_node = curr->next;
            
            struct page *p = (struct page *)curr;
            int pfn = p - mem_map;
            int block_start = pfn;
            int block_end = pfn + (1 << order);

            // 狀況 1: 完全沒有重疊 (Outside) -> 不關它的事，看下一個
            if (block_end <= start_pfn || block_start >= end_pfn) {
                curr = next_node;
                continue;
            }

            // 只要有重疊，就先從當前的 free_area 串列中拔除
            list_remove(curr);

            // 狀況 2: 完全被包覆 (Inside) -> 精準命中，保留起來！
            if (block_start >= start_pfn && block_end <= end_pfn) {
                p->val = 0; // 0 代表被佔用 (reserved)
                p->order = -1;
                p->pool_idx = -1;

                uart_puts("[Reserve] Reserve address [");
                uart_hex(page_to_addr(block_start)); uart_puts(", ");
                uart_hex(page_to_addr(block_end));
                uart_puts("). Range of pages: [");
                uart_dec(block_start); uart_puts(", ");
                uart_dec(block_end); uart_puts(")");
                uart_puts(", order = ");
                uart_dec(order);
                uart_putc('\n');

                curr = next_node;
                continue;
            }

            // Partial overlap, order -= 1
            int next_order = order - 1;
            struct page *buddy = &mem_map[pfn ^ (1 << next_order)];

            p->order = next_order;
            buddy->order = next_order;

            // 將切半的兩個區塊加到下一層的 free_area
            // 下一個迴圈 (order - 1) 就會對這兩個小區塊進行更細部的檢查
            list_add_back(&p->list, &free_area[next_order]);
            list_add_back(&buddy->list, &free_area[next_order]);

            curr = next_node;
        }
    }
}

void alloc_test(){
    uart_puts("Testing memory allocation...\n");
    char *ptr1 = (char *)allocate(4000);
    char *ptr2 = (char *)allocate(8000);
    char *ptr3 = (char *)allocate(4000);
    char *ptr4 = (char *)allocate(4000);

    free(ptr1);
    free(ptr2);
    free(ptr3);
    free(ptr4);

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

