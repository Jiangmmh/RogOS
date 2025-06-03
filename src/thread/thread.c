#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "print.h"
#include "process.h"
#include "sync.h"
#include "file.h"

#define PG_SIZE 4096
struct task_struct* idle_thread;        // idel线程
struct task_struct* main_thread;        // 主线程的PCB
struct list thread_ready_list;          // 就绪队列，调度器从中选出一个执行
struct list thread_all_list;            // 所有任务队列
static struct list_elem* thread_tag;    // 用于保存队列中的线程结点

struct lock pid_lock;

extern void switch_to(struct task_struct* cur, struct task_struct* next);


/* 系统空闲时运行的线程 */
static void idle(void* arg UNUSED) {
    while (1) {
        thread_block(TASK_BLOCKED);
        asm volatile ("sti; hlt":::"memory");
    }
}

/* 获取当前线程的PCB指针 */
struct task_struct* running_thread() {
    uint32_t esp;
    asm ("mov %%esp, %0":"=g"(esp));
    // esp指向PCB所在页的高地址端，取esp高20位，即PCB所在页的起始地址
    return (struct task_struct*)(esp & 0xfffff000);
}

/* 由kernel_thread去执行function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    /* 执行function前要开中断，避免后面的时钟中断被屏蔽，无法调度其他线程 */
    intr_enable(INTR_ON);
    function(func_arg);
}

/* 分配pid */
static pid_t allocate_pid(void) {
    static pid_t next_pid = 0;      // 静态局部变量
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

/* 初始化线程栈thread_stack,将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
    /* 先预留中断使用栈intr_stack的空间，可见thread.h中定义的结构 */
    pthread->self_kstack -= sizeof(struct intr_stack);

    /* 再留出线程栈空间thread_stack，见thread.h中的定义 */
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;  // 在这段空间上对thread_stack进行初始化
    kthread_stack->eip = kernel_thread;     // 线程创建时指向kernel_thread，调用传入的函数
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    if (pthread == main_thread) {   // 将main函数封装为一个线程，直接将其设置为TASK_RUNNING
        pthread->status = TASK_RUNNING;
    } else {
        pthread->status = TASK_READY;
    }

    /* 内核栈是线程自己在内核态下使用的，放在PCB所在页的最高端 */
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);  // 设置线程的内核栈
    pthread->priority = prio;
    pthread->ticks = prio;      // 时间片就是线程的优先级！！
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->cwd_inode_nr = 0;
    pthread->parent_pid = -1;
    pthread->stack_magic = 0x19870916;  // 魔数，用于越界检查

    /* 准备好三个标准输入/输出 */ 
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;

    /* 其余的文件描述符全部置为-1 */ 
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
}

/* 创建一个优先级为prio的线程，线程名为name，线程所执行的函数是function(func_arg)*/
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
    /* PCB都位于内核空间，包括用户进程的pcb也是在内核空间 */
    struct task_struct* thread = get_kernel_pages(1);    // 每个PCB都占1页空间4KB

    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    // 确保之前不在就绪队列中，加入就绪队列
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    // 确保之前不在全部队列中，加入全部队列
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    return thread;
}

/* 将kernel中的main函数完善为主线程 */
static void make_main_thread(void) {
    put_str("make_main_thread start\n");
    /* 因为 main 线程早已运行,
    * 咱们在 loader.S 中进入内核时的 mov esp,0xc009f000,
    * 就是为其预留 pcb 的,因此 pcb 地址为 0xc009e000,
    * 不需要通过 get_kernel_pages 另分配一页*/
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
    put_str("make_main_thread done\n");
}


/* 实现任务调度 */
void schedule() {
    ASSERT(intr_get_status() == INTR_OFF);  // 必须关中断，保证原子性

    struct task_struct* cur = running_thread();
    /* 在取出线程运行时使用的是pop，因此上一个正在运行的线程已不在就绪队列中 */
    if (cur->status == TASK_RUNNING) {
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);     // 加到队尾去
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    } else {
        /* 若此线程需要某时间发生后才继续上cpu运行，不需要将其加入队列，因为当前线程不在就绪队列中 */
    }

    /* 若就绪队列中没有可运行的任务，唤醒idle线程 */
    if (list_empty(&thread_ready_list)) {
        thread_unblock(idle_thread);    // idle线程，啥也不干
    }

    ASSERT(!list_empty(&thread_ready_list));    // 就绪队列非空
    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);  // 取出就绪队列队首线程的tag
    struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    process_activate(next); // 激活任务页表

    switch_to(cur, next);   // 执行完线程切换后，还要返回kernel.S，继续执行中断返回的指令
}

extern void init(void);
/* 初始化线程环境 */
void thread_init(void)
{
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    /* 先创建第一个用户进程:init */
    process_execute(init, "init");         // 放在第一个初始化,这是第一个进程,init进程的pid为1
    /* 将当前main函数创建为线程 */
    make_main_thread();
    /* 创建idle线程 */
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}

/* 当前线程将自己阻塞，标志其状态为stat */
void thread_block(enum task_status stat) {
    /* stat取值为TASK_BLOCKED、TASK_WAITING、TASK_HANGING*/
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
    enum intr_status old_status = intr_disable();
    struct task_struct* cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();     // 在其中将当前线程从就绪队列中剔除
    intr_set_status(old_status);
}

/* 将线程pthread接触阻塞 */
void thread_unblock(struct task_struct* pthread) {
    enum intr_status old_status = intr_get_status();
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    if (pthread->status != TASK_READY) {
        // ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {    // 保险起见，再判断一下
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }   
        list_push(&thread_ready_list, &pthread->general_tag);   // 将刚刚unblock的线程加入就绪队列队首
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

/* 主动让出cpu，换其他线程运行 */
void thread_yield(void) {
    struct task_struct *cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

// 为fork出来的子进程分配pid
pid_t fork_pid(void) {
    return allocate_pid();
}

/* 以填充空格的方式输出buf */
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format) {
   memset(buf, 0, buf_len);
   uint8_t out_pad_0idx = 0;
   switch(format) {
      case 's':
	 out_pad_0idx = sprintf(buf, "%s", ptr);
	 break;
      case 'd':
	 out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
      case 'x':
	 out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
   }
   while(out_pad_0idx < buf_len) { // 以空格填充
      buf[out_pad_0idx] = ' ';
      out_pad_0idx++;
   }
   sys_write(stdout_no, buf, buf_len - 1);
}

/* 用于在list_traversal函数中的回调函数,用于针对线程队列的处理 */
static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
   struct task_struct* pthread = elem2entry(struct task_struct, all_list_tag, pelem);
   char out_pad[16] = {0};

   pad_print(out_pad, 16, &pthread->pid, 'd');

   if (pthread->parent_pid == -1) {
      pad_print(out_pad, 16, "NULL", 's');
   } else { 
      pad_print(out_pad, 16, &pthread->parent_pid, 'd');
   }

   switch (pthread->status) {
      case 0:
	 pad_print(out_pad, 16, "RUNNING", 's');
	 break;
      case 1:
	 pad_print(out_pad, 16, "READY", 's');
	 break;
      case 2:
	 pad_print(out_pad, 16, "BLOCKED", 's');
	 break;
      case 3:
	 pad_print(out_pad, 16, "WAITING", 's');
	 break;
      case 4:
	 pad_print(out_pad, 16, "HANGING", 's');
	 break;
      case 5:
	 pad_print(out_pad, 16, "DIED", 's');
   }
   pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

   memset(out_pad, 0, 16);
   ASSERT(strlen(pthread->name) < 17);
   memcpy(out_pad, pthread->name, strlen(pthread->name));
   strcat(out_pad, "\n");
   sys_write(stdout_no, out_pad, strlen(out_pad));
   return false;	// 此处返回false是为了迎合主调函数list_traversal,只有回调函数返回false时才会继续调用此函数
}

 /* 打印任务列表 */
void sys_ps(void) {
   char* ps_title = "PID            PPID           STAT           TICKS          COMMAND\n";
   sys_write(stdout_no, ps_title, strlen(ps_title));
   list_traversal(&thread_all_list, elem2thread_info, 0);
}
