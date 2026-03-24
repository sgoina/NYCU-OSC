#include "deviceTree.h"
#include "defint.h"
#include "string.h"

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009

#define MAX_DEPTH 16

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

static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}


int fdt_path_offset(unsigned long dtb_ptr, const char* path){
    const void* fdt = (const void*)dtb_ptr;
    const struct fdt_header* header = (const struct fdt_header*)fdt;
    if (bswap32(header->magic) != 0xd00dfeed)
      return -1;
      
    int cnt_elements = 0; // the count number of elements
    char *path_element[MAX_DEPTH];
    char path_copy[256]; // assume not over 256 bytes
    strcpy(path_copy, path);
    
    char *element = strtok(path_copy, "/");
    path_element[cnt_elements++] = "";
    
    while (element != 0 && cnt_elements < MAX_DEPTH) {
        path_element[cnt_elements++] = element;
        element = strtok(0, "/");
    }

    // --- 2. 遍歷 FDT 結構 ---
    const uint8_t* p = (const uint8_t*)fdt + bswap32(header->off_dt_struct);
    
    int current_depth = 0; // 記錄目前身處的樹狀深度
    int match_depth = 0;   // 記錄目前成功匹配了幾層路徑

    while (1) {
        uint32_t offset = p - (const uint8_t*)fdt;
        uint32_t token = bswap32(*(const uint32_t*)p);
        p += 4; // 跳過 Token 欄位

        if (token == FDT_BEGIN_NODE) {
            const char* node_name = (const char*)p;
            int name_len = strlen(path_element[match_depth]);
            
            // 這裡必須在名稱比對前就推進指標，因為跳過這個節點的名稱，指標才能指到下一個 Token 或屬性
            p = (const uint8_t*)align_up(p + strlen(node_name) + 1, 4);

            // 比對節點名稱 (例如：path_element 是 "serial"，而 node_name 可能是 "serial@10000000")
            // 使用 strncmp 來允許忽略 @ 後面的位址
            if (current_depth == match_depth && strncmp(path_element[match_depth], node_name, name_len) == 0) {
                // 如果連 @ 前面的名稱都比對不符，可能是路徑找錯了
                // 如果加上檢查：目標長度相同，或是遇到 @ 符號，會更嚴謹
                if (node_name[name_len] == '\0' || node_name[name_len] == '@') {
                    match_depth++;
                }
            }
            
            // 如果所有路徑元素都匹配成功，回傳這個節點的起始偏移量
            if (match_depth == cnt_elements) {
                return offset;
            }
            
            current_depth++;
        }
        else if (token == FDT_END_NODE) {
            current_depth--;
            // 如果我們退出了當前匹配的層級，需要將 match_depth 也退回
            if (current_depth < match_depth) {
                match_depth = current_depth;
            }
        }
        else if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            // 跳過 len (4 bytes) 和 nameoff (4 bytes)，並對齊資料區段
            p += 8;
            p = (const uint8_t*)align_up(p + len, 4);
        }
        else if (token == FDT_END) {
            break; // 樹結構結束
        }
    }
    return -1; // 找不到對應路徑
}

const void* fdt_getprop(unsigned long dtb_ptr, int nodeoffset, const char* name, int* lenp) {
    const void* fdt = (const void*)dtb_ptr;
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    
    // 加上簡單的防呆，確認是指向一個有效的 FDT
    if (bswap32(hdr->magic) != 0xd00dfeed) {
        return NULL;
    }

    const uint8_t* p = (const uint8_t*)fdt + nodeoffset;
    
    // 確認這個 offset 真的指向一個節點的開頭
    uint32_t token = bswap32(*(const uint32_t*)p);
    if (token != FDT_BEGIN_NODE) {
        return NULL;
    }
    p += 4; // 跳過 FDT_BEGIN_NODE token

    const char* strings = (const char*)fdt + bswap32(hdr->off_dt_strings);
    
    // 跳過節點名稱，並加上明確的指標轉型
    int name_len = strlen((const char*)p);
    p = (const uint8_t*)align_up(p + name_len + 1, 4);

    int depth = 0;

    while (1) {
        token = bswap32(*(const uint32_t*)p);
        p += 4;

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            uint32_t nameoff = bswap32(*(const uint32_t*)(p + 4));
            p += 8;

            const char* prop_name = strings + nameoff;
            
            // 找到對應屬性
            if (depth == 0 && strcmp(prop_name, name) == 0) {
                if (lenp) {
                    *lenp = len;
                }
                return (const void*)p; // 回傳指向資料的指標
            }
            
            // 沒找到，跳過資料區塊，加上明確指標轉型
            p = (const uint8_t*)align_up(p + len, 4);
        }
        else if (token == FDT_BEGIN_NODE) {
            if (depth == 0) return NULL; // 遇到子節點，代表屬性已經沒了
            depth++;
            int nlen = strlen((const char*)p);
            p = (const uint8_t*)align_up(p + nlen + 1, 4); // 明確指標轉型
        }
        else if (token == FDT_END_NODE) {
            if (depth == 0) return NULL; // 當前節點結束
            depth--;
        }
        else if (token == FDT_NOP) {
            // FDT_NOP 不帶任何資料，直接繼續下一輪迴圈
            continue;
        }
        else if (token == FDT_END) {
            break; // 樹結構結束
        }
    }
    
    return NULL;
}
