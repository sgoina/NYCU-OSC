void init_mem(unsigned long dtb_ptr);

void *allocate(unsigned int size);

void free(void *ptr);

void show_mem_alloc();

void alloc_test();

void memory_reserve(unsigned long long start, unsigned long long size);
