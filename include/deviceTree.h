#include "defint.h"
#include "string.h"

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

int fdt_path_offset(unsigned long dtb_ptr, const char* path);

const void* fdt_getprop(unsigned long dtb_ptr, int nodeoffset, const char* name, int* lenp);

void fdt_reserve_memory_nodes(unsigned long dtb_ptr);
