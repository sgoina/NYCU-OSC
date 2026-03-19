#include "utils.h"

int strcmp(const char a[], const char b[]){
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? 0 : 1;
}
