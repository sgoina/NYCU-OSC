#include "mem_alloc.h"
#include "uart.h"
#include "defint.h"
#include "list.h"

#define MEM_START 0x10000000UL
#define MEM_END   0x20000000UL
#define PAGE_SIZE 4096 // 1 page size = 4KB
#define NUM_PAGES ((MEM_END - MEM_START) / PAGE_SIZE) // total 65536 pages

#define MAX_ORDER 10 // 2^10 = 1024 pages (4MB)

// Frame Array node
struct page {
    struct list_head list;
    int order; // -1 is not a head
    int val; // 1 is allocable, 0 is used
};

// Frame Array
struct page mem_map[NUM_PAGES];

// free list for every order
struct list_head free_area[MAX_ORDER + 1];

void buddy_init() {
    // Initialize every free list
    for (int i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&free_area[i]);
    }

    // Initialize Frame Array
    for (int i = 0; i < NUM_PAGES; i++) {
        mem_map[i].val = 1;
        mem_map[i].order = -1;
        INIT_LIST_HEAD(&mem_map[i].list);
    }

    // cutting into blocks of 2 ^ MAX_ORDER bytes and put them into free list
    for (int i = 0; i < NUM_PAGES; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        list_add_back(&mem_map[i].list, &free_area[MAX_ORDER]);
    }
}

unsigned long page_to_addr(int idx){
    return MEM_START + idx * PAGE_SIZE;
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
    unsigned int next_addr = page_to_addr(target_idx + (1 << order));
    uart_puts("[Page] Allocate ");
    uart_hex(addr);
    uart_puts(" at order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(target_idx);
    uart_puts(". Next address at order ");
    uart_dec(order);
    uart_puts(": ");
    uart_hex(next_addr);
    uart_putc('\n');
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
    
    unsigned int next_addr = page_to_addr(page_idx + (1 << order));
    uart_puts("[Page] Free ");
    uart_hex(page_to_addr(original_idx));
    uart_puts(" and add back to order ");
    uart_dec(order);
    uart_puts(", page ");
    uart_dec(page_idx);
    uart_puts(". Next address at order ");
    uart_dec(order);
    uart_puts(": ");
    uart_hex(next_addr);
    uart_putc('\n');
}

void alloc_test(){
    uart_puts("Testing memory allocation...\n");
    buddy_init();
    char *ptr1 = (char *)alloc_pages(4000);
    char *ptr2 = (char *)alloc_pages(8000);
    char *ptr3 = (char *)alloc_pages(4000);
    char *ptr4 = (char *)alloc_pages(4000);
    
    free_pages((struct page *)ptr1);
    free_pages((struct page *)ptr2);
    free_pages((struct page *)ptr3);
    free_pages((struct page *)ptr4);
}
