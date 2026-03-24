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
    static char *last_str = 0; // 記住上次切斷的位置
    
    // 如果傳入 NULL，代表要接續上次的位置繼續切
    if (str == 0) str = last_str;
    if (str == 0) return 0; // 已經切完了

    // 1. 跳過開頭連續的 delimiter (例如 "//a" 會跳過兩個 '/')
    while (*str) {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*str == delim[i]) { is_delim = 1; break; }
        }
        if (!is_delim) break;
        str++;
    }

    if (*str == '\0') {
        last_str = 0;
        return 0;
    }

    // 2. 找到 token 的起點，開始往後找下一個 delimiter
    char *token_start = str;
    while (*str) {
        int is_delim = 0;
        for (int i = 0; delim[i] != '\0'; i++) {
            if (*str == delim[i]) { is_delim = 1; break; }
        }
        
        // 找到 delimiter 了！把它換成 '\0' 截斷字串
        if (is_delim) {
            *str = '\0';
            last_str = str + 1; // 紀錄下次開始的位置
            return token_start;
        }
        str++;
    }
    
    // 這是最後一個 token
    last_str = 0; 
    return token_start;
}

int strncmp(const char *s1, const char *s2, int n) {
    while (n > 0 && *s1 != '\0' && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    
    // 如果 n 扣到 0，代表前 n 個字元都完美比對成功
    if (n == 0) {
        return 0; 
    }
    
    // 如果中間有不相等的字元，回傳它們的 ASCII 差值 (標準 C 語言做法)
    return (*(unsigned char *)s1 - *(unsigned char *)s2);
}
