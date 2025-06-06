#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H
void panic_spin(char* filename, int line, const char* func, const char* condition);

/*************************** __VA_ARGS__ *******************************
 * __VA_ARGS__ 是预处理器所支持的专用标识符。
 * 代表所有与省略号相对应的参数。
 * "..."表示定义的宏其参数可变。*/
/*
    __FILE__, __LINE__, __func__也都是编译器预定义的宏
*/
#define PANIC(...) panic_spin (__FILE__, __LINE__, __func__, __VA_ARGS__)
/***********************************************************************/

#ifdef NDEBUG
    #define ASSERT(CONDITION) ((void)0)
#else
    #define ASSERT(CONDITION) \
        if (CONDITION) {}     \
        else {                \
            PANIC(#CONDITION);\
        }                      
#endif /*__NDEBUG*/
#endif /*__KERNEL_DEBUG_H*/