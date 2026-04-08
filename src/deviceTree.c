#include "deviceTree.h"
#include "uart.h"
#include "mem_alloc.h"

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

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

// 輔助巨集：將位址向上對齊到 4 的倍數 (FDT 的規範)
#define ALIGN_UP_4(x) (((x) + 3) & ~3)

void fdt_reserve_memory_nodes(unsigned long dtb_ptr) {
    int reserved_offset = fdt_path_offset(dtb_ptr, "/reserved-memory");
    if (reserved_offset == -1) return; // 如果 DTB 裡沒有這個節點，就直接跳過

    uart_puts("--- Parsing /reserved-memory ---\n");

    const void* fdt = (const void*)dtb_ptr;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    const uint8_t* p = (const uint8_t*)fdt + reserved_offset;

    // 確認這個 offset 真的是 FDT_BEGIN_NODE (0x00000001)
    uint32_t token = bswap32(*(const uint32_t*)p);
    if (token != 0x00000001) return; 
    p += 4;

    // 跳過 "/reserved-memory" 的節點名稱
    int name_len = strlen((const char*)p);
    p += ALIGN_UP_4(name_len + 1);

    int depth = 1; // 深度 1 代表我們在 /reserved-memory 內部

    char* node_name;
    int cnt = 0;
    while (1) {
        token = bswap32(*(const uint32_t*)p);
        p += 4;

        if (token == 0x00000001) { 
            // 遇到 FDT_BEGIN_NODE：代表我們進入了它的子節點 (例如 mmode_resv)
            depth++;
            int nlen = strlen((const char*)p);
            if (depth == 2)
                node_name = (char *)p;
            p += ALIGN_UP_4(nlen + 1);
        }
        else if (token == 0x00000002) { 
            // 遇到 FDT_END_NODE：代表離開了一個節點
            depth--;
            if (depth == 0) break; // 深度歸零，代表我們完全離開了 /reserved-memory，結束搜尋
        }
        else if (token == 0x00000003) { 
            // 遇到 FDT_PROP：屬性資料
            uint32_t len = bswap32(*(const uint32_t*)p);
            uint32_t nameoff = bswap32(*(const uint32_t*)(p + 4));
            p += 8;

            // 我們只在乎深度為 2 的節點（也就是 /reserved-memory 的直系子節點）裡的屬性
            if (depth == 2) {
                const char* strings = (const char*)fdt + bswap32(hdr->off_dt_strings);
                const char* prop_name = strings + nameoff;

                // 如果屬性名稱是 "reg"，且長度足夠 64-bit (16 bytes)
                if (strcmp(prop_name, "reg") == 0 && len >= 16) {
                    const uint32_t* reg = (const uint32_t*)p;
                    // 轉序號並組合出 64-bit 的 base 和 size
                    uint64_t base = ((uint64_t)bswap32(reg[0]) << 32) | bswap32(reg[1]);
                    uint64_t size = ((uint64_t)bswap32(reg[2]) << 32) | bswap32(reg[3]);
                    
                    cnt++;
                    uart_dec(cnt);
                    uart_puts(". ");
                    uart_puts(node_name);
                    // 🌟 修改這裡：只印出 Record，不呼叫 memory_reserve
                    uart_puts(" Recorded for early reserve, begin: ");
                    uart_hex(base);
                    uart_puts(", size: ");
                    uart_hex(size);
                    uart_putc('\n');

                    // 🌟 將解析到的區塊塞進 early_reserved 陣列中！
                    add_early_reserve(base, size);
                }
            }
            p += ALIGN_UP_4(len); // 跳過該屬性的資料，繼續往下找
        }
        else if (token == 0x00000009) { 
            // FDT_END
            break;
        }
    }
}
