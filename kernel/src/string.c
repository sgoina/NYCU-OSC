#include "string.h"

int strcmp(const char *a, const char *b){
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0') ? 0 : 1;
}

int strlen(const char *a){
    int length = 0;
    while (*a != '\0'){
        length++;
        a++;
    }
    return length;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while (1){
        *d = *src;
        src++;
        if (*d == '\0')
            break;
        d++;
    }
    return dest;
}

char *strtok(char *str, const char *delim) {
    static char *last_str = 0; // the position in last time 
    
    // if str is 0, means wanting to cut origin string
    if (str == 0)
        str = last_str;
    // the string is end
    if (str == 0)
        return 0; 

    // skip continuous delimiter in the beginning
    while (*str) {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*str == delim[i]){
                is_delim = 1; 
                break;
            }
        }
        if (!is_delim)
            break;
        str++;
    }
    // the string is end
    if (*str == '\0') {
        last_str = 0;
        return 0;
    }

    // find the next delimiter and return the token
    char *token_start = str;
    while (*str) {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*str == delim[i]) { is_delim = 1; break; }
        }
        
        // find the delimiter and cut the string
        if (is_delim) {
            *str = '\0';
            last_str = str + 1; // record the position for next strtok()
            return token_start;
        }
        str++;
    }
    
    // the string is end, can't not use strtok() on the origin string 
    last_str = 0; 
    return token_start;
}

int strncmp(const char *s1, const char *s2, int n) {
    while (n > 0 && *s1 != '\0' && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    // perfect match in n chars
    if (n == 0)
        return 0; 
    // not match
    return (*(unsigned char *)s1 - *(unsigned char *)s2);
}
