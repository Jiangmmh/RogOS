#include "string.h"
#include "global.h"
// #include "debug.h"
#include "assert.h"

/* 将dst_起始的size个字节置为value */
void memset(void* dst_, uint8_t value, uint32_t size) {
    assert(dst_ != NULL);
    uint8_t* dst = (uint8_t*)dst_;
    while (size-- > 0) 
        *dst++ = value;
}

/* 将src_起始的size个字节复制到dst_ */
void memcpy(void* dst_, const void* src_, uint32_t size) {
    assert(dst_ != NULL && src_ != NULL);
    uint8_t* dst = dst_;
    const uint8_t* src = src_;
    while (size-- > 0) {
        *dst++ = *src++;
    }
}

/* 连续比较以地址a_和地址b_开头的size个字节，若相等返回0，若a_>b_返回1，否则返回-1 */
int memcmp(const void* a_, const void* b_, uint32_t size) {
    assert(a_ != NULL && b_ != NULL);
    const char* a = a_;
    const char* b = b_;
    while (size-- > 0) {
        if (*a != *b) {
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

/* 将字符串从src_复制到dst_ */
char* strcpy(char* dst_, const char* src_) {
    assert(dst_ != NULL & src_ != NULL);
    char* ret = dst_;
    while ((*dst_++ = *src_++));
    return ret;
}

/* 返回字符串长度 */
uint32_t strlen(const char* str) {
    assert(str != NULL);
    const char* p = str;
    while (*p++);           
    return p - str - 1;     // 碰到'\0'，仍向后移动一位，因此需要-1
}

/* 比较两个字符串，若a中的字符串大于b中的字符串返回1，相等时返回0，否则返回-1 */
int8_t strcmp(const char* a, const char* b) {
    assert(a != NULL && b != NULL);
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a < *b ? -1 : *a > *b;  // 这个条件语句很精彩
}

/* 从左到右查找字符串str中首次出现字符ch的地址 */
char* strchr(const char* str, const uint8_t ch) {
    assert(str != NULL);
    while (*str != 0) {
        if (*str == ch) {
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

/* 从右到左查找字符串str中首次出现字符ch的地址 */
char* strrchr(const char* str, const uint8_t ch) {
    assert(str != NULL);
    const char* last_char = NULL;
    while (*str != 0) {
        if (*str == ch) {
            last_char = (char*)str;
        }
        str++;
    }
    return (char*)last_char;
}

/* 将字符串src_拼接到dst_后，返回拼接的串地址 */
char* strcat(char* dst_, const char* src_) {
    assert(dst_ != NULL && src_ != NULL);
    char* str = dst_;
    while (*str++);
    --str;
    while ((*str++ = *src_++));  // 这里应该是保证dst_有足够的空间存放拼接后的串
    return dst_;
}

/* 在字符串str中查找字符ch出现的次数 */
uint32_t strchrs(const char* str, uint8_t ch) {
    assert(str != NULL);
    uint32_t ch_cnt = 0;
    const char* p = str;
    while (*p != 0) {
        if (*p == ch) {
            ch_cnt++;
        }
        p++;
    }
    return ch_cnt;
}