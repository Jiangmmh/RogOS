#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"
typedef void* intr_handler;
void idt_init(void);  // 只对外暴露idt_init函数

/* 定义中断的两种状态：
    INTR_OFF=0, 表示关中断
    INTR_ON =1, 表示开中断
*/
enum intr_status {
    INTR_OFF,
    INTR_ON
};

enum intr_status intr_get_status(void);
enum intr_status intr_set_status(enum intr_status);
enum intr_status intr_enable();
enum intr_status intr_disable(void);

void register_handler(uint8_t vector_no, intr_handler function);
#endif
