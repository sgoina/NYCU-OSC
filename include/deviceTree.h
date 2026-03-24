int fdt_path_offset(unsigned long dtb_ptr, const char* path);

const void* fdt_getprop(unsigned long dtb_ptr, int nodeoffset, const char* name, int* lenp);
