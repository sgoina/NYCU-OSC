#include "utils.h"

void* memset(void* s, int c, int n) {
    unsigned char* p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void* memcpy(void* dst, const void* src, int n) {
    char* d = dst;
    const char* s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}
