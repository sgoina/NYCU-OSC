unsigned long page_to_addr(int idx);

int end_idx(int idx, int order);

int size_to_order(unsigned int size);

void init_mem(unsigned long dtb_ptr);

void add_early_reserve(unsigned long long start, unsigned long long size);

void fdt_reserve_memory_nodes(unsigned long dtb_ptr);

void memory_reserve(unsigned long long start, unsigned long long size);

void *allocate(unsigned int size);

void free(void *ptr);

void inc_page_ref(unsigned long pa);

void dec_page_ref(unsigned long pa);

int get_page_ref(unsigned long pa);

void show_mem_alloc();

void alloc_test();

