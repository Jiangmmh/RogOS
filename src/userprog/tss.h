#ifndef __USERPROG_TSS_H
#define __USERPROG_TSS_H

#include "stdint.h"
#include "thread.h"

void tss_init();
struct gdt_desc make_gdt_desc(uint32_t* desc_addr,uint32_t limit,uint8_t attr_low,uint8_t attr_high);
void update_tss_esp(struct task_struct* pthread);

#endif