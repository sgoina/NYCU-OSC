void init_mem(unsigned long dtb_ptr);

void add_early_reserve(unsigned long long start, unsigned long long size);

void fdt_reserve_memory_nodes(unsigned long dtb_ptr);

void memory_reserve(unsigned long long start, unsigned long long size);

void *allocate(unsigned int size);

void free(void *ptr);

void show_mem_alloc();

void alloc_test();

