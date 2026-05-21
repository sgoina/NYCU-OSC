#include "ramfs.h"
#include "deviceTree.h"
#include "uart.h"
#include "trap.h"
#include "mem_alloc.h"
#include "vm.h"

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
            unsigned long cpio_pa = 0;
            if (len == 4) {
                cpio_pa = bswap32(*rd_start);
            }
            else if (len == 8) {
                uint64_t high = bswap32(rd_start[0]);
                uint64_t low = bswap32(rd_start[1]);
                cpio_pa = (high << 32) | low;
            }
            // 算好實體位址後，加上 PAGE_OFFSET 轉為虛擬位址，再存入指標
            cpio_address = (void *)(cpio_pa + PAGE_OFFSET);
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
            // the beginning and size of the code
            void* original_code = (void*)(ptr + headsize);
            int total_size = filesize + STACK_SIZE;
            total_size = align(total_size, 16); 
            // Allocate memory space for the file
            void* user_memory = allocate(total_size); 
            if (!user_memory) {
                uart_puts("[EXEC]: Memory allocation failed!\n");
                return -1;
            }
            // copy file from CPIO to mem_map
            char *src = (char *)original_code;
            char *dst = (char *)user_memory;
            for (int i = 0; i < filesize; i++) {
                dst[i] = src[i];
            }

            void* entry_point = user_memory; // the beginning of the process
            unsigned long user_sp = (unsigned long)user_memory + total_size; // User stack of the process

            unsigned long sstatus;
            // csrr: Read CSR
            asm volatile("csrr %0, sstatus" : "=r"(sstatus));
            // setting U-Mode and enable interrupt
            sstatus &= ~(1 << 8); // SPP (Supervisor mode Previous Privilege mode): 1=Supervisor, 0=User
            sstatus |= (1 << 5);  // SPIE (Supervisor Previous Interrupt Enable)
            // csrw: Write CSR
            asm volatile("csrw sstatus, %0" : : "r"(sstatus));
            // write sepc to the beginning of the process
            asm volatile("csrw sepc, %0" : : "r"(entry_point));
            // Go to U-Mode
            asm volatile(
                "csrw sscratch, sp\n\t" // backup kernel stack, sscratch (Supervisor Scratch Register)
                "mv sp, %0\n\t"         // switch to User stack
                "sret\n\t"              // return from S-Mode and go back to User process
                : 
                : "r"(user_sp)
            );
            return 0; 
        }
        ptr += headsize + datasize;
    }
    uart_puts("Can't find the file in CPIO to execute!\n");
    return -1;
}

// find the program entry
void* find_program(const char *filename, unsigned int *filesize_out) {
    char *ptr = (char *)cpio_address;
    while (strncmp(ptr + sizeof(struct cpio_t), "TRAILER!!!", 10)) {
        struct cpio_t* hdr = (struct cpio_t*)ptr;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);
        if (!strncmp(ptr + sizeof(struct cpio_t), filename, namesize)) {
            void* program_data_addr = (void*)(ptr + headsize); // 這是 CPIO 內容在 Kernel 中的虛擬位址
            // 透過指標回傳 filesize
            if (filesize_out != NULL)
                *filesize_out = filesize;
            return program_data_addr; 
        }
        ptr += headsize + datasize;
    }
    uart_puts("Can't find the file in CPIO to execute!\n");
    return NULL; 
}
