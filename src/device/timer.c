#include "timer.h"
#include "io.h"
#include "print.h"
#include "thread.h"
#include "debug.h"
#include "interrupt.h"


#define IRQ0_FREQUENCY 	100
#define INPUT_FREQUENCY        1193180
#define COUNTER0_VALUE		INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT		0X40
#define COUNTER0_NO 		0
#define COUNTER_MODE		2
#define READ_WRITE_LATCH	3
#define PIT_COUNTROL_PORT	0x43
#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

uint32_t ticks;     // 内核自中断开启依赖总共的嘀嗒数

void frequency_set(uint8_t counter_port, uint8_t counter_no, uint8_t rwl, uint8_t counter_mode, uint16_t counter_value)
{
    outb(PIT_COUNTROL_PORT,(uint8_t) (counter_no << 6 | rwl << 4 | counter_mode << 1)); // 向寄存器端口0x43写入控制字
    outb(counter_port,(uint8_t)counter_value);          // 先写入counter_value的低8位
    outb(counter_port,(uint8_t)counter_value >> 8);     // 再写入counter_value的高8位
} 

/* 时钟中断函数 */
static void intr_timer_handler(void) {
    struct task_struct* cur_thread = running_thread();
    ASSERT(cur_thread->stack_magic == 0x19870916);  // 检查是否溢出

    cur_thread->elapsed_ticks++;    // 记录此线程占用的 cpu 时间
    ticks++;                        // 从内核第一次处理时间中断后开始至今的滴哒数,内核态和用户态总共的嘀哒数

    if (cur_thread->ticks == 0) {   // 时间片用完，调度新进程上cpu
        schedule();
    } else {
        cur_thread->ticks--;
    }
}

/* 设置时钟频率 */
void timer_init(void)
{
    put_str("timer_init start!\n");
    frequency_set(COUNTER0_PORT,COUNTER0_NO,READ_WRITE_LATCH,COUNTER_MODE,COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);     // 将时钟中断处理程序绑定到idt_table中
    put_str("timer_init done!\n");
}

/* 以ticks为单位的sleep，任何时间形式的sleep都会转换为此ticks形式 */
static void ticks_to_sleep(uint32_t sleep_ticks) {
    uint32_t start_tick = ticks;
    while (ticks - start_tick < sleep_ticks) {
        thread_yield();
    }
}

/* 以毫秒为单位的sleep */
void mtime_sleep(uint32_t m_seconds) {
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);
    ticks_to_sleep(sleep_ticks);
}
