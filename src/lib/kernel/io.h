/******************机器模式*******************
 *  b -- 输出寄存器 QImode 名称,即寄存器中的最低 8 位:[a-d]l
 *  w -- 输出寄存器 HImode 名称,即寄存器中 2 个字节的部分,如[a-d]x
 *  HImode
 *      "Half-Integer"模式,表示一个两字节的整数
 *  QImode
 *      "Quarter-Integer"模式,表示一个一字节的整数
******************************************************/


#ifndef __LIB_KERNEL_IO_H
#define __LIB_KERNEL_IO_H
#include "stdint.h"

/* 向端口port写入一个字节 */
static inline void outb(uint16_t port, uint8_t data) {  // static表示该函数仅在本文件中有效，对外不可见
    /*
        对端口指定N表示0~255，d表示用dx存储端口号
        %b0表示al，%w1表示dx
    */
    asm volatile("outb %b0, %w1"::"a"(data), "Nd"(port));
}

/* 将addr处起始的word_cnt个字写入端口port*/
static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt) {
    // outsw 是把 ds:esi 处的 16 位的内容写入 port 端口
    asm volatile("cld; rep outsw":"+S"(addr), "+c"(word_cnt): "d"(port));
}



/* 将从端口port读入的一个字节返回 */
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile("inb %w1, %b0":"=a"(data):"Nd"(port));
    return data;
}

/* 将从端口port读入的word_cnt个字写入addr */
static inline void insw(uint16_t port, void* addr, uint32_t word_cnt) {
    // insw 是将从端口 port 处读入的 16 位内容写入 es:edi 指向的内存
    asm volatile("cld; rep insw":"+D"(addr), "+c"(word_cnt):"d"(port):"memory");
}

#endif
