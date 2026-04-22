#include "ramfs.h"
#include "deviceTree.h"
#include "uart.h"
#include "trap.h"
#include "mem_alloc.h"

extern void handle_exception(void);

#define STACK_SIZE  0x1000
static void *cpio_address = 0;

/**
 * @brief Convert a hexadecimal string to integer
 *
 * @param s hexadecimal string
 * @param n length of the string
 * @return integer value
 */
static int hextoi(const char* s, int n) {
    int r = 0;
    while (n-- > 0) {
        r = r << 4;
        if (*s >= 'A')
            r += *s++ - 'A' + 10;
        else if (*s >= 0)
            r += *s++ - '0';
    }
    return r;
}

/**
 * @brief Align a number to the nearest multiple of a given number
 *
 * @param n number
 * @param byte alignment
 * @return aligned number
 */
static int align(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
}

int initrd_addr(unsigned long dtb_ptr){
    int rd_base_offset = fdt_path_offset(dtb_ptr, "/chosen"); // find the base address of "chosen" in device tree.
    if (rd_base_offset != -1){
        int len = 0;
        uint32_t* rd_start = (uint32_t*)fdt_getprop(dtb_ptr, rd_base_offset, "linux,initrd-start", &len); // use offset to find starting address of cpio
        if(rd_start){
            if (len == 4) {
                cpio_address = (void*)(unsigned long)bswap32(*rd_start);
            } else if (len == 8) {
                uint64_t high = bswap32(rd_start[0]);
                uint64_t low = bswap32(rd_start[1]);
                cpio_address = (void*)(unsigned long)((high << 32) | low);
            }
        }
    }
    else
        return -1;
    return 0;
}

void ls_filenames(){
    char *ptr = (char *)cpio_address;
    int file_cnt = 0; // the counting number of files
    // count the total of files
    while (1){
        // check header->magic
        struct cpio_t *header = (struct cpio_t *) ptr;
        if (strncmp(header->magic, "070701", 6) != 0){
            uart_puts("Error: magic number is not match.\n");
            break;
        }
        int name_sz = hextoi(header->namesize, 8);
        int file_sz = hextoi(header->filesize, 8);
        const char *filename = ptr+ sizeof(struct cpio_t); // skip header and get the file name
        if (strcmp(filename, "TRAILER!!!") == 0)
            break;
        file_cnt++;
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // skip header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // skip content + padding 2
        ptr += next_header_offset;
    }
    uart_puts("Total ");
    uart_dec(file_cnt);
    uart_puts(" files.\n");
    ptr = (char *)cpio_address;
    // show all files
    while (1){
        struct cpio_t *header = (struct cpio_t *) ptr;
        if (strncmp(header->magic, "070701", 6) != 0){
            uart_puts("Error: magic number is not match.\n");
            break;
        }
        int name_sz = hextoi(header->namesize, 8);
        int file_sz = hextoi(header->filesize, 8);
        const char *filename = ptr+ sizeof(struct cpio_t);
        if (strcmp(filename, "TRAILER!!!") == 0)
            break;
        uart_dec(file_sz);
        uart_putc(' ');
        uart_puts(filename);
        uart_putc('\n');
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // skip header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // skip content + padding 2
        ptr += next_header_offset;
    }
}

void cat_file_content(const char* filename) {
    char *ptr = (char *)cpio_address;
    // find the file and show its content
    while (1){
        struct cpio_t *header = (struct cpio_t *) ptr;
        // check header->magic
        if (strncmp(header->magic, "070701", 6) != 0){
            uart_puts("Error: magic number is not match.\n");
            return;
        }
        int name_sz = hextoi(header->namesize, 8);
        int file_sz = hextoi(header->filesize, 8);
        const char *find_name = ptr + sizeof(struct cpio_t);
        // Can't find the file
        if (strcmp(find_name, "TRAILER!!!") == 0){
            uart_puts(filename);
            uart_puts(": No such file\n");
            break;
        }
        // find the file and show the content
        else if (strcmp(find_name, filename) == 0){
            const char *content = ptr + align(sizeof(struct cpio_t) + name_sz, 4);
            uart_puts(content);
            uart_putc('\n');
            return;
        }
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // skip header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // skip content + padding 2
        ptr += next_header_offset;
    }
    return;
}

int exec(const char *filename){
    char *ptr = (char *)cpio_address;
    while (strncmp(ptr + sizeof(struct cpio_t), "TRAILER!!!", 10)) {
        struct cpio_t* hdr = (struct cpio_t*)ptr;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);
        if (!strncmp(ptr + sizeof(struct cpio_t), filename, namesize)){
            // 1. 取得原始程式碼在 CPIO 中的位置與大小
            void* original_code = (void*)(ptr + headsize);
            
            // 2. 計算總需要的記憶體大小 (Code + Stack)
            int total_size = filesize + STACK_SIZE;
            
            // 【重要】將總大小對齊到 16 bytes，確保未來的 User SP 也是 16-byte 對齊
            total_size = (total_size + 15) & ~15; 

            // 3. 一次配置一塊「大」記憶體
            void* user_memory = allocate(total_size); 
            if (!user_memory) {
                uart_puts("Memory allocation failed!\n");
                return -1;
            }

            // 4. 將程式碼從 CPIO 複製到記憶體的「底部」 (低位址)
            char *src = (char *)original_code;
            char *dst = (char *)user_memory;
            for (int i = 0; i < filesize; i++) {
                dst[i] = src[i];
            }

            // 5. 設定進入點 (Entry Point) 為剛配置的記憶體起始位址
            void* entry_point = user_memory;
            
            // 6. 設定 User Stack
            // Stack 由高位址往低位址生長，所以指標要指到這塊大記憶體的「最頂部」
            unsigned long user_sp = (unsigned long)user_memory + total_size;

            // 7. 設定 sstatus 準備進入 User Mode
            unsigned long sstatus;
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            sstatus &= ~(1 << 8); // 清除 SPP，設定 Previous Privilege 為 0 (User Mode)
            sstatus |= (1 << 5);  // 設定 SPIE，允許 U-mode 發生中斷
            asm volatile("csrw sstatus, %0" : : "r"(sstatus));
            
            // 8. 將進入點寫入 sepc
            asm volatile("csrw sepc, %0" : : "r"(entry_point));

            // 9. 備份 Kernel SP、切換 User SP 並進入 User Mode
            asm volatile(
                "csrw sscratch, sp\n\t" // 備份 Kernel SP
                "mv sp, %0\n\t"         // 切換成剛算好的 User SP
                "sret\n\t"              // 降級並跳轉
                : 
                : "r"(user_sp)
            );
            return 0; 
        }
        ptr += headsize + datasize;
    }
    return -1;
}
