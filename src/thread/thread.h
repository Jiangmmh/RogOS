#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC 8
#define TASK_NAME_LEN 16

extern struct list thread_ready_list, thread_all_list;
/* 自定义通用函数类型，它将在很多线程函数中作为形参类型 */
typedef void thread_func(void*);
typedef int16_t pid_t;

/* 进程或线程的状态 */
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

/******************** 中断栈 intr_stack *****************
 * 此结构用于中断发生时保护程序的上下文环境：
 * 进程或线程被外部中断或软中断打断时,会按照此结构压入上下文
 * 寄存器,intr_exit 中的出栈操作是此结构的逆操作
 * 此栈在线程自己的内核栈中位置固定,所在页的最顶端
********************************************************/
struct intr_stack {
    uint32_t vec_no;    // 中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // ？
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* 下面这些由cpu从低特权级进入高特权级时压入 */
    uint32_t err_code;  // err_code被压入在eip之后
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;
    void* esp;
    uint32_t ss;
};

/*********** 线程栈 thread_stack ***********
 * 线程自己的栈,用于存储线程中待执行的函数
 * 此结构在线程自己的内核栈中位置不固定,
 * 仅用在 switch_to 时保存线程环境。
 * 实际位置取决于实际运行情况。
 * *****************************************/
struct thread_stack {
    uint32_t ebp;       // 根据SysV_ABI，ebp、ebx、edi、esi、esp由被调用的函数保存
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;
    /* 线程第一次执行时,eip 指向待调用的函数 kernel_thread 其他时候,eip 是指向 switch_to 的返回地址*/
    void (*eip)(thread_func* func, void* func_arg);

    /* 以下仅供第一次被调度上cpu时使用，用ret跳转后栈中的元素 */
    void (*unused_retaddr);     // 作为占位符，充当返回地址
    thread_func* function;      // 由kernel_thread所调用的函数名
    void* func_arg;             // 由kernel_thread所调用的函数所需的参数
};

/* 进程或线程的pcb，程序控制块 */
struct task_struct {
    uint32_t* self_kstack;      // 内核线程自己的内核栈
    pid_t parent_pid;
    pid_t pid;
    enum task_status status;    // 进程状态
    char name[16];
    uint8_t priority;           // 优先级
    uint8_t ticks;              // 时间片

    uint32_t elapsed_ticks;     // 从上cpu起总共执行了多少嘀嗒数

    int32_t fd_table[MAX_FILES_OPEN_PER_PROC];  // 文件描述符数组

    struct list_elem general_tag;   // 用于线程在一般队列中的结点
    struct list_elem all_list_tag;  // 线用于线程队列thread_all_list中的结点

    uint32_t* pgdir;            // 进程自己页表的虚拟地址
    struct virtual_addr userprog_vaddr; // 用户进程的虚拟地址
    struct mem_block_desc u_block_desc[DESC_CNT];

    uint32_t cwd_inode_nr;      // 进程所在工作目录的inode编号
    

    uint32_t stack_magic;       // 栈的边界标记，用于检测栈的溢出
};

struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread();
void thread_init(void);
void schedule();
void thread_unblock(struct task_struct* pthread);
void thread_block(enum task_status stat);
void init_thread(struct task_struct* pthread, char* name, int prio);
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void thread_yield(void);
// 为fork出来的子进程分配pid
pid_t fork_pid(void);
 /* 打印任务列表 */
void sys_ps(void);
#endif