void buddy_init();

struct page* alloc_pages(unsigned int order);

void free_pages(struct page* p);

void alloc_test();
