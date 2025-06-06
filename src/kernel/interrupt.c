#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"


#define PIC_M_CTRL 0x20     // 主片控制端口
#define PIC_M_DATA 0x21     // 主片数据端口
#define PIC_S_CTRL 0xa0     // 从片控制端口
#define PIC_S_DATA 0xa1     // 从片数据端口


#define EFLAGS_IF 0x00000200
#define GET_EFLAGS(EFLAGS_VAR) asm volatile("pushfl; popl %0":"=g"(EFLAGS_VAR))

#define IDT_DESC_CNT 0x81

/*中断门描述符结构体*/
struct gate_desc {
    uint16_t func_offset_low_word;
    uint16_t selector;
    uint8_t dcout;
    uint8_t attribute;
    uint16_t func_offset_high_word;
};

// 静态函数声明
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];      // 中断描述符表

char* intr_name[IDT_DESC_CNT];          // 保存异常名
intr_handler idt_table[IDT_DESC_CNT];
extern intr_handler intr_entry_table[IDT_DESC_CNT]; // 声明引用定义在kernel.S中的中断处理函数入口数组
extern uint32_t syscall_handler(void);

/* 初始化可编程中断控制器8259A */
static void pic_init(void) {
    /* 初始化主片 */
    outb(PIC_M_CTRL, 0x11);     // ICW1：边沿触发，级联8259，需要ICW4
    outb(PIC_M_DATA, 0x20);     // ICW2：起始中断向量号为0x20
                                // IR[0-7]为0x20~0x27
    outb(PIC_M_DATA, 0x04);     // ICW3：IR2接从片
    outb(PIC_M_DATA, 0x01);     // ICW4：8086模式，正常EOI

    /* 初始化从片 */
    outb(PIC_S_CTRL, 0x11);     // ICW1：边沿触发，级联8259，需要ICW4
    outb(PIC_S_DATA, 0x28);     // ICW2：起始中断向量号为0x28
                                // IR[8-15]为0x28~0x2F
    outb(PIC_S_DATA, 0x02);     // ICW3：IR2接从片
    outb(PIC_S_DATA, 0x01);     // ICW4：8086模式，正常EOI
   

    /* 为了测试，把时钟中断和键盘中断都开启 */
    outb(PIC_M_DATA, 0xfc);
    outb(PIC_S_DATA, 0xff);
    outb(PIC_M_DATA, 0xf8);  // IRQ2用于级联从片，必须打开，否则无法响应从片上的中断

    /* 打开从片上的IRQ14，此应交接受硬盘控制器的中断 */
    outb(PIC_S_DATA, 0xbf);

    put_str("    pic_init done\n");
}


/* 创建中断门描述符 */
static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function) {
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcout = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/* 初始化中断描述符表 */
static void idt_desc_init(void) {
    int i, lastindex = IDT_DESC_CNT - 1;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }

    // 单独处理系统调用，因为其对应的中断门的DPL为3，而不是0
    // syscall_handler在kernel.S中定义
    make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);  
    put_str("    idt_desc_init done\n");
}

// 通用中断处理函数，一般用在异常出现时的处理
static void general_intr_handler(uint8_t vec_nr) {
    if (vec_nr == 0x27 || vec_nr == 0x2f) { // IRQ7和IRQ15会产生伪中断，无需处理
        return ;
    }

    // 将光标置为0，从屏幕左上角清出一片打印异常信息的区域，方便阅读
    set_cursor(0);
    int cursor_pos = 0;
    while (cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }

    set_cursor(0);
    put_str("!!!!!!! excetion message begin !!!!!!!!\n");
    set_cursor(88);
    put_str(intr_name[vec_nr]);
    if (vec_nr == 14) {         // 若为Pagefault，将确实的地址打印出来并悬停
        int page_fault_vaddr = 0;
        asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr)); // cr2是存放造成page_fault的地址
        put_str("\npage fault addr is "); put_int(page_fault_vaddr);
    }
    put_str("\n!!!!!!! excetion message end !!!!!!!!\n");
    while (1);
}

// 完成一般中断处理函数注册及异常名注册
static void exception_init(void) {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";   
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* 开中断并返回开中断前的状态 */
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti");        // 开中断，sti指令将IF位置1
        return old_status;
    }
}


/* 关中断，并且返回关中断前的状态 */
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli":::"memory"); // 关中断, cli指令将IF位置0
        return old_status;
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}

/* 在中断处理程序数组第vector_no个元素中注册安装中断处理程序function */
void register_handler(uint8_t vector_no, intr_handler function) {
    idt_table[vector_no] = function;
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

/* 完成有关中断的所有初始化工作 */
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();     // 初始化中断描述符表IDT
    exception_init();    // 异常名初始化并注册通用中断处理函数
    pic_init();          // 初始化8259A

    /* 中断描述符全部放在idt中，现在加载idt的界限和基址到硬件 */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0"::"m"(idt_operand)); 
    put_str("idt_init done\n");
}