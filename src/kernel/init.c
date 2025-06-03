#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "syscall_init.h"
#include "ide.h"
#include "fs.h"

/* 负责初始化所有模块 */
void init_all() {
    put_str("init_all\n");
    idt_init();         // 初始化中断
    timer_init();       // 初始化PIT
    mem_init();         // 内存初始化
    thread_init();      // 初始化主线程
    console_init();     // 初始化显示终端
    keyboard_init();    // 初始化键盘
    tss_init();         // 初始化任务状态段
    syscall_init();     // 初始化系统调用
    ide_init();         // 初始化ide
    filesys_init();     // 初始化文件系统
}