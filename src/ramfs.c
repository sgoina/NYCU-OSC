#include "ramfs.h"
#include "defint.h"
#include "deviceTree.h"
#include "uart.h"
#include "string.h"

void *cpio_address = 0;

struct cpio_t {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

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
    int rd_base_offset = fdt_path_offset(dtb_ptr, "/chosen");
    if (rd_base_offset != -1){
        int len = 0;
        uint32_t* rd_start = (uint32_t*)fdt_getprop(dtb_ptr, rd_base_offset, "linux,initrd-start", &len);
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
    int file_cnt = 0;
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
        file_cnt++;
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // jump header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // jump header + name + padding 2
        ptr += next_header_offset;
    }
    uart_puts("Total ");
    uart_dec(file_cnt);
    uart_puts(" files.\n");
    ptr = (char *)cpio_address;
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
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // jump header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // jump header + name + padding 2
        ptr += next_header_offset;
    }
}

void cat_file_content(const char* filename) {
    // TODO: Implement this function
    char *ptr = (char *)cpio_address;
    while (1){
        struct cpio_t *header = (struct cpio_t *) ptr;
        if (strncmp(header->magic, "070701", 6) != 0){
            uart_puts("Error: magic number is not match.\n");
            return;
        }
        int name_sz = hextoi(header->namesize, 8);
        int file_sz = hextoi(header->filesize, 8);
        const char *find_name = ptr + sizeof(struct cpio_t);
        if (strcmp(find_name, "TRAILER!!!") == 0){
            uart_puts(filename);
            uart_puts(": No such file\n");
            break;
        }
        else if (strcmp(find_name, filename) == 0){
            const char *content = ptr + align(sizeof(struct cpio_t) + name_sz, 4);
            uart_puts(content);
            uart_putc('\n');
            return;
        }
        int content_offset = align(sizeof(struct cpio_t) + name_sz, 4); // jump header + name + padding 1
        int next_header_offset = align(content_offset + file_sz, 4); // jump header + name + padding 2
        ptr += next_header_offset;
    }
    return;
}
