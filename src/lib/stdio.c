#include "stdio.h"
#include "stdint.h"
#include "syscall.h"
#include "global.h"
#include "string.h"

#define va_start(ap, v) (ap = (va_list)&v)  // 使ap指向第一个固定参数v
#define va_arg(ap, t)   (*((t*)(ap += 4)))  // 使ap指向下一个参数并返回其值
#define va_end(ap)      (ap = NULL)         // 清楚ap

/* 将整型转换为字符串 int to ascii*/
static void itoa(uint32_t value, char** buf_ptr_addr, uint8_t base) {
    uint32_t m = value % base;
    uint32_t q = value / base;
    if (q) {    // 商不为0，继续转换
        itoa(q, buf_ptr_addr, base);
    }

    if (m < 10) {
        *((*buf_ptr_addr)++) = m + '0';
    } else {
        *((*buf_ptr_addr)++) = m - 10 + 'A';
    }
}

/* 将参数ap按照格式format输出到字符串str，并返回str的长度 */
uint32_t vsprintf(char* str, const char* format, va_list ap) {
    char* buf_ptr = str;
    const char* index_ptr = format;
    char index_char = *index_ptr;
    int32_t arg_int;
    char* arg_str;
    while (index_char) {
        if (index_char != '%') {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }
        index_char = *(++index_ptr);    // 得到%后面的字符
        switch(index_char) {
            case 's':
                arg_str = va_arg(ap, char*);
                strcpy(buf_ptr, arg_str);
                buf_ptr += strlen(arg_str);
                index_char = *(++index_ptr);
                break;
            case 'd':
                arg_int = va_arg(ap, int);
                if (arg_int < 0) {
                    arg_int = 0 - arg_int;
                    *buf_ptr++ = '-';
                }
                itoa(arg_int, &buf_ptr, 10);
                index_char = *(++index_ptr);
                break;
            case 'x':
                arg_int = va_arg(ap, int);      // va_arg就是让ap指向栈中的下一个参数
                itoa(arg_int, &buf_ptr, 16);    // 这里为什么用二级指针没想明白
                index_char = *(++index_ptr);
                break;
            case 'c':
                (*buf_ptr++) = va_arg(ap, char);
                index_char = *(++index_ptr);
                break;
        }
    }
    return strlen(str);
}

/* 格式化输出字符串format */
uint32_t printf(const char* format, ...) {
   va_list args;
   va_start(args, format);	       // 使args指向format
   char buf[1024] = {0};	       // 用于存储拼接后的字符串
   vsprintf(buf, format, args);
   va_end(args);
   return write(1, buf, strlen(buf)); 
}

/* 同printf不同的地方就是字符串不是写到终端,而是写到buf中 */
uint32_t sprintf(char* buf, const char* format, ...) {
   va_list args;
   uint32_t retval;
   va_start(args, format);
   retval = vsprintf(buf, format, args);
   va_end(args);
   return retval;
}