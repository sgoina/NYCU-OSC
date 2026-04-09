#include "deviceTree.h"
#include "uart.h"
#include "mem_alloc.h"

#define MAX_DEPTH 16

static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}


int fdt_path_offset(unsigned long dtb_ptr, const char* path){
    const void* fdt = (const void*)dtb_ptr;
    // check header->magic
    const struct fdt_header* header = (const struct fdt_header*)fdt;
    if (bswap32(header->magic) != 0xd00dfeed)
      return -1;
      
    int cnt_elements = 0; // the count number of elements
    char *path_element[MAX_DEPTH]; // assume not over 16 elements in the path
    char path_copy[256]; // assume not over 256 bytes
    strcpy(path_copy, path);
    
    // cut elements in the path
    char *element = strtok(path_copy, "/");
    path_element[cnt_elements++] = "";
    while (element != 0 && cnt_elements < MAX_DEPTH) {
        path_element[cnt_elements++] = element;
        element = strtok(0, "/");
    }

    // find all elements in the flattened  device tree
    const uint8_t* p = (const uint8_t*)fdt + bswap32(header->off_dt_struct); // go to structure block
    int current_depth = 0;
    int match_depth = 0;
    while (1) {
        uint32_t offset = p - (const uint8_t*)fdt;
        uint32_t token = bswap32(*(const uint32_t*)p);
        p += 4; // skip a token

        if (token == FDT_BEGIN_NODE) {
            const char* node_name = (const char*)p;
            int name_len = strlen(path_element[match_depth]);
            p = (const uint8_t*)align_up(p + strlen(node_name) + 1, 4); // skip the name of the node

            // compare node name and element name without @ and address, EX: "memory@80000000"
            if (current_depth == match_depth && strncmp(path_element[match_depth], node_name, name_len) == 0) {
                if (node_name[name_len] == '\0' || node_name[name_len] == '@') {
                    match_depth++;
                }
            }
            // if find all elements, then return offset
            if (match_depth == cnt_elements) {
                return offset;
            }
            current_depth++;
        }
        else if (token == FDT_END_NODE) {
            // decrease current_depth and match_depth if necessary
            current_depth--;
            if (current_depth < match_depth) {
                match_depth = current_depth;
            }
        }
        else if (token == FDT_PROP) {
            /* 
            struct { both are big-endian
                uint32_t len;
                uint32_t nameoff;
            }
            */
            uint32_t len = bswap32(*(const uint32_t*)p);
            p += 8;
            p = (const uint8_t*)align_up(p + len, 4); // skip the property struct and the length of info 
        }
        else if (token == FDT_END) {
            break; // the end of device tree
        }
    }
    return -1; // can't find the address of the path
}

const void* fdt_getprop(unsigned long dtb_ptr, int nodeoffset, const char* name, int* lenp) {
    const void* fdt = (const void*)dtb_ptr;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    // check header->magic
    if (bswap32(hdr->magic) != 0xd00dfeed) {
        return NULL;
    }

    const uint8_t* p = (const uint8_t*)fdt + nodeoffset;
    
    // check the offset is a FDT_BEGIN_NODE
    uint32_t token = bswap32(*(const uint32_t*)p);
    if (token != FDT_BEGIN_NODE) {
        return NULL;
    }
    p += 4; // skip FDT_BEGIN_NODE

    const char* strings = (const char*)fdt + bswap32(hdr->off_dt_strings); // the address of string block
    int name_len = strlen((const char*)p);
    p = (const uint8_t*)align_up(p + name_len + 1, 4); // skip the node name

    int depth = 0; // insure not going deeper node

    while (1) {
        token = bswap32(*(const uint32_t*)p);
        p += 4; // skip token

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            uint32_t nameoff = bswap32(*(const uint32_t*)(p + 4));
            p += 8;
            const char* prop_name = strings + nameoff;
            if (depth == 0 && strcmp(prop_name, name) == 0) {
                if (lenp) {
                    *lenp = len;
                }
                return (const void*)p; // return the property
            }
            
            p = (const uint8_t*)align_up(p + len, 4); // if not find, skip the property info
        }
        else if (token == FDT_BEGIN_NODE) {
            if (depth == 0)
                return NULL; // The origin node is end, return NULL
            depth++; // In child node
            int nlen = strlen((const char*)p);
            p = (const uint8_t*)align_up(p + nlen + 1, 4); // skip node name
        }
        else if (token == FDT_END_NODE) {
            if (depth == 0)
                return NULL; // The node is end, return NULL
            depth--; // leave child node
        }
        else if (token == FDT_END) {
            break; // The end of device tree
        }
    }
    return NULL; // Can't find the property
}
