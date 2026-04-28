#include "deviceTree.h"
#include "mem_alloc.h"
#include "uart.h"
#include "list.h"

extern char _start[]; // kernel start
extern char _end[]; // kernel end

static uint64_t mem_start = 0; // the start of memory region
static uint64_t mem_end = 0; // the end of memory region
static int num_pages = 0; // the number of usable pages

// reserved region
struct reserved_region {
    uint64_t start;
    uint64_t end;
};

#define MAX_RESERVED_REGIONS 32 
static struct reserved_region early_reserved[MAX_RESERVED_REGIONS]; //record all reserved regions
static int num_early_reserved = 0; // the number of reserved regions

#define PAGE_SIZE 4096 // 1 page size = 4KB
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
    add_early_reserve((uint64_t)_start, (uint64_t)_end - (uint64_t)_start);
    uart_puts("Kernel address: ");
    uart_hex((uint64_t)_start);
    uart_puts(", size: ");
    uart_hex((uint64_t)_end - (uint64_t)_start);
    uart_putc('\n');
    // 2. DTB
    add_early_reserve((uint64_t)dtb_ptr, bswap32(((const struct fdt_header *)dtb_ptr)->totalsize));
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
    // 4. Device Tree /reserved-memory
    fdt_reserve_memory_nodes(dtb_ptr);
    
    // calculate the size of mem_map and find a safe region to place it
    uint64_t mem_map_size = num_pages * sizeof(struct page);
    uint64_t safe_base = find_safe_base(mem_map_size);
    mem_map = (struct page *)safe_base;

    uart_puts("[Startup Allocator] mem_map placed at: ");
    uart_hex(safe_base);
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
            // finish finding, exit this function
            if (depth == 0)
                return;
        }
        else if (token == FDT_PROP) { 
            uint32_t len = bswap32(*(const uint32_t*)p);
            uint32_t nameoff = bswap32(*(const uint32_t*)(p + 4));
            p += 8;

            const char* strings = (const char*)fdt + bswap32(hdr->off_dt_strings);
            const char* prop_name = strings + nameoff;

            // if prop == "reg", then get its base and size
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
            // skip the prop
            p += ((len + 3) & ~3);;
        }
        else if (token == FDT_END) { 
            break;
        }
    }
}

// convert page index to address
unsigned long page_to_addr(int idx){
    return mem_start + (uint64_t)idx * PAGE_SIZE;
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

    struct page *target_page = (struct page *)list_front(&free_area[current_order]);
    //int target_idx = target_page - mem_map;
    list_remove(&target_page->list);
    
    /*uart_puts("[-] Remove page ");
    uart_dec(target_idx);
    uart_puts(" from order ");
    uart_dec(current_order);
    uart_puts(". Range of pages: [");
    uart_dec(target_idx);
    uart_puts(", ");
    uart_dec(end_idx(target_idx, current_order));
    uart_puts("]\n");*/
  
    // save redundant area in free list
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
        
        /*uart_puts("[+] Add page ");
        uart_dec(buddy_idx);
        uart_puts(" to order ");
        uart_dec(current_order);
        uart_puts(". Range of pages: [");
        uart_dec(buddy_idx);
        uart_puts(", ");
        uart_dec(end_idx(buddy_idx, current_order));
        uart_puts("]\n");*/
    }

    // return block
    target_page->val = 0;
    target_page->order = order;
    
    /*unsigned int addr = page_to_addr(target_idx);
    uart_puts("[Page] Allocate ");
    uart_hex(addr);
    uart_puts(" at order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(target_idx);
    uart_putc('\n');*/
    //show_mem_alloc();
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

    int order = p->order;
    int page_idx = p - mem_map;
    //int original_idx = page_idx;
    
    // find its buddy and merge them to put in free list of the next order
    while (order < MAX_ORDER) {
        int buddy_idx = page_idx ^ (1 << order);
        struct page *buddy = &mem_map[buddy_idx];

        // buddy is used or not in free list of this order
        if (!buddy->val || buddy->order != order)
            break;
        
        /*uart_puts("[*] Buddy found! buddy idx: ");
        uart_dec(buddy_idx);
        uart_puts(" for page ");
        uart_dec(page_idx);
        uart_puts(" with order ");
        uart_dec(order);
        uart_putc('\n');*/
        
        // remove buddy page from free list
        list_remove(&buddy->list);
        
        /*uart_puts("[-] Remove page ");
        uart_dec(buddy_idx);
        uart_puts(" from order ");
        uart_dec(order);
        uart_puts(". Range of pages: [");
        uart_dec(buddy_idx);
        uart_puts(", ");
        uart_dec(end_idx(buddy_idx, order));
        uart_puts("]\n");*/
        
        // take the min idx
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
            struct page *tmp = p;
            p = buddy;
            buddy = tmp;
        }
        buddy->order = -1; // buddy isn't a head of block anymore
        order++;
    }

    // put the whole block into free list
    p->val = 1;
    p->order = order;
    list_add_back(&p->list, &free_area[order]);
    
    /*uart_puts("[+] Add page ");
    uart_dec(page_idx);
    uart_puts(" to order ");
    uart_dec(order);
    uart_puts(". Range of pages: [");
    uart_dec(page_idx);
    uart_puts(", ");
    uart_dec(end_idx(page_idx, order));
    uart_puts("]\n");*/
    
    /*uart_puts("[Page] Free ");
    uart_hex(page_to_addr(original_idx));
    uart_puts(" and add back to order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(page_idx);
    uart_putc('\n');*/
    
    //show_mem_alloc();
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
    // this size is too big for a chunk
    if (pool_idx == -1)
        return NULL;
    
    // There is no chunk of this size, so it needs a page (4KB) to manage chunk pool
    if (list_empty(&chunk_pools[pool_idx])) {
        struct page *new_page = alloc_pages(PAGE_SIZE);
        if (!new_page){
            uart_puts("There is no space to manage chunk pool.\n");
            return NULL;
        }
        // This page is for a chunk pool of this chunk size
        new_page->pool_idx = pool_idx;
        
        int chunk_size = pool_sizes[pool_idx];
        unsigned long page_addr = page_to_addr(new_page - mem_map);

        // cut the page to some pieces and put them into chunk pool 
        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            struct list_head *chunk = (struct list_head *)(page_addr + offset);
            list_add_back(chunk, &chunk_pools[pool_idx]);
        }
    }

    struct page *target_chunk = (struct page *)list_front(&chunk_pools[pool_idx]);
    list_remove(&target_chunk->list);
    
    // Find the page base address of this chunk, and update the number of used chunks
    unsigned long chunk_addr = (unsigned long)target_chunk;
    unsigned long base_addr = chunk_addr & ~((unsigned long)PAGE_SIZE - 1);
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    mem_map[page_idx].chunk_count++;

    /*uart_puts("[Chunk] Allocate ");
    uart_hex((unsigned long)target_chunk);
    uart_puts(" at chunk size ");
    uart_dec(pool_sizes[pool_idx]);
    uart_putc('\n');*/

    return (void *)target_chunk;
}

// Free a chunk
void kfree(void *ptr) {
    if (!ptr) {
        uart_puts("The chunk pointer is NULL.\n");
        return;
    }

    // Find the page base address of this chunk
    unsigned long addr = (unsigned long)ptr;
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1);
    // get the pool index to find the chunk size
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];
    int pool_idx = p->pool_idx;

    if (pool_idx == -1) {
        uart_puts("This page isn't for chunk pool.\n");
        return;
    }

    /*uart_puts("[Chunk] Free ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_dec(pool_sizes[pool_idx]);
    uart_putc('\n');*/
    
    p->chunk_count--;

    // if all chunks in the same page are return, returning the page to free list
    if (p->chunk_count == 0) {
        int chunk_size = pool_sizes[pool_idx];
        // remove all chunks in the same page from chunk pool, except the current freed chunk
        for (int offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
            unsigned long current_chunk_addr = base_addr + offset;
            if (current_chunk_addr != addr) {
                struct list_head *chunk_to_remove = (struct list_head *)current_chunk_addr;
                list_remove(chunk_to_remove);
            }
        }
        p->pool_idx = -1;
        free_pages(p); 
        //uart_puts("[Pool] Page completely free! Returning to Buddy.\n");
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

    // size <= 512, allocate a chunk
    if (size <= pool_sizes[NUM_POOLS - 1]) {
        return kmalloc(size);
    } 
    // size > 512, allocate a block
    else {
        struct page *p = alloc_pages(size);
        if (!p)
            return NULL;
        return (void *)page_to_addr(p - mem_map); // return frame address
    }
}

void free(void *ptr) {
    if (!ptr){
        uart_puts("The point is NULL.\n");
        return;
    }
    unsigned long addr = (unsigned long)ptr;
    unsigned long base_addr = addr & ~((unsigned long)PAGE_SIZE - 1); // for chunk
    int page_idx = (base_addr - mem_start) / PAGE_SIZE;
    struct page *p = &mem_map[page_idx];

    // Decide to free a block or chunk
    if (p->pool_idx != -1)
        kfree(ptr);
    else
        free_pages(p);
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
    /*uart_puts("begin: ");
    uart_hex(start);
    uart_puts(", end: ");
    uart_hex(start + size);
    uart_puts(", size: ");
    uart_hex(size);
    uart_putc('\n');*/
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
    // calculate the page index
    int start_pfn = (res_start - mem_start) / PAGE_SIZE;
    int end_pfn = (res_end - mem_start + PAGE_SIZE - 1) / PAGE_SIZE;

    for (int order = MAX_ORDER; order >= 0; order--) {
        struct list_head *curr = list_front(&free_area[order]);
        // In the loop, curr isn't a head node of list
        while (curr != &free_area[order]) {
            struct list_head *next_node = curr->next;
            
            struct page *p = (struct page *)curr;
            int pfn = p - mem_map;
            int block_start = pfn;
            int block_end = pfn + (1 << order);

            // No overlap
            if (block_end <= start_pfn || block_start >= end_pfn) {
                curr = next_node;
                continue;
            }

            // if overlap, remove from free list of this order
            list_remove(curr);
            /*uart_puts("[-] Remove page ");
            uart_dec(pfn);
            uart_puts(" from order ");
            uart_dec(order);
            uart_puts(". Range of pages: [");
            uart_dec(pfn);
            uart_puts(", ");
            uart_dec(end_idx(pfn, order));
            uart_puts("]\n");*/

            // full overlap
            if (block_start >= start_pfn && block_end <= end_pfn) {
                p->val = 0;
                p->order = -1;
                p->pool_idx = -1;

                /*uart_puts("[Reserve] Reserve address [");
                uart_hex(page_to_addr(block_start));
                uart_puts(", ");
                uart_hex(page_to_addr(block_end));
                uart_puts("). Range of pages: [");
                uart_dec(block_start);
                uart_puts(", ");
                uart_dec(block_end);
                uart_puts(")");
                uart_puts(", order = ");
                uart_dec(order);
                uart_putc('\n');*/

                curr = next_node;
                continue;
            }

            // Partial overlap, order -= 1
            int next_order = order - 1;
            int buddy_idx = pfn ^ (1 << next_order);
            struct page *buddy = &mem_map[buddy_idx];

            p->order = next_order;
            buddy->order = next_order;

            // This order is too big, so halve the size and put them into free list of next order
            list_add_back(&p->list, &free_area[next_order]);
            list_add_back(&buddy->list, &free_area[next_order]);
            
            /*uart_puts("[+] Add page ");
            uart_dec(pfn);
            uart_puts(" to order ");
            uart_dec(next_order);
            uart_puts(". Range of pages: [");
            uart_dec(pfn);
            uart_puts(", ");
            uart_dec(end_idx(pfn, next_order));
            uart_puts("]\n");
            uart_puts("[+] Add page ");
            uart_dec(buddy_idx);
            uart_puts(" to order ");
            uart_dec(next_order);
            uart_puts(". Range of pages: [");
            uart_dec(buddy_idx);
            uart_puts(", ");
            uart_dec(end_idx(buddy_idx, next_order));
            uart_puts("]\n");*/

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
