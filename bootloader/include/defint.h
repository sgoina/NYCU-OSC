typedef unsigned char      uint8_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long long uintptr_t;
typedef unsigned long long size_t;
#define NULL ((void *)0)

static inline uint32_t bswap32(uint32_t x) {
    return (((x & 0x000000FF) << 24) |
            ((x & 0x0000FF00) <<  8) |
            ((x & 0x00FF0000) >>  8) |
            ((x & 0xFF000000) >> 24));
}

static inline uint64_t bswap64(uint64_t x) {
    return (((x & 0x00000000000000FFULL) << 56) |
            ((x & 0x000000000000FF00ULL) << 40) |
            ((x & 0x0000000000FF0000ULL) << 24) |
            ((x & 0x00000000FF000000ULL) <<  8) |
            ((x & 0x000000FF00000000ULL) >>  8) |
            ((x & 0x0000FF0000000000ULL) >> 24) |
            ((x & 0x00FF000000000000ULL) >> 40) |
            ((x & 0xFF00000000000000ULL) >> 56));
}
