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
            // 1. 取得程式的進入點 (Header 之後緊接著就是檔案內容)
            void* entry_point = (void*)(ptr + headsize);
            
            // 2. 配置一頁記憶體作為 User Stack
            void* user_stack = allocate(STACK_SIZE);
            // Stack 由高位址往低位址生長，所以指標要指到記憶體區塊的頂部
            unsigned long user_sp = (unsigned long)user_stack + STACK_SIZE;

            // 3. 設定 sstatus
            unsigned long sstatus;
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            // 清除 SPP (bit 8) 將 Previous Privilege 設為 0 (User Mode)
            // 設定 SPIE (bit 5) 以允許在 User Mode 時發生中斷
            sstatus &= ~(1 << 8); 
            sstatus |= (1 << 5);  
            asm volatile("csrw sstatus, %0" : : "r"(sstatus));
            
            // 4. 將進入點寫入 sepc
            asm volatile("csrw sepc, %0" : : "r"(entry_point));

            // 5. 切換 Stack 並進入 User Mode
            // 【關鍵點】我們必須把目前的 Kernel SP 存入 sscratch。
            // 這樣將來 User Program 觸發 Exception (Trap) 回到 start.S 時，
            // csrrw sp, sscratch, sp 才能順利拿到 Kernel Stack 來儲存 Context。
            asm volatile(
                "csrw sscratch, sp\n\t" // 將目前的 Kernel SP 備份到 sscratch
                "mv sp, %0\n\t"         // 將 CPU 的 SP 切換成剛分配好的 User SP
                "sret\n\t"              // 執行 sret，硬體會跳轉到 sepc 並降級為 U-mode
                : 
                : "r"(user_sp)
            );
            return 0; 
        }
        ptr += headsize + datasize;
    }
    return -1;
}
