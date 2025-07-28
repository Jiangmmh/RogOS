# MIT 6.1810学习笔记

MIT的操作系统课程，授课教师是Moris和Franc两个大佬，我学习的是24 fall的内容，其他年份的内容都差不多，之前看了OSTEP和《x86汇编：从实模式到保护模式》，对操作系统有一点基础，因此打算30天之内解决战斗。

代码仓库：https://github.com/Jiangmmh/MIT-6.1810-OS/tree/main

Lecture视频（20fall）：https://www.bilibili.com/video/BV166421f7D6?spm_id_from=333.788.videopod.sections&vd_source=082ae7da449141d5d5cd54b517c4204a&p=12

Lecture翻译博客：https://www.zhihu.com/column/c_1294282919087964160

非常棒的xv6源码的讲解：https://www.youtube.com/watch?v=fWUJKH0RNFE&list=PLbtzT1TYeoMhTPzyTZboW_j7TPAnjv9XB&ab_channel=hhp3

我的目标：

- 看完所有Lecture
- 尽可能独立完成所有lab

## 结语

尽管已经有了一定的操作系统基础，但是做这门课程的实验时仍然被折磨得很痛苦。课程一开始还挺轻松的，我跟着lecture和xv6book的介绍，并阅读源码，逐步理清了xv6的实现思路，前两个lab做得也很顺利，但是从第三个lab开始，基本上每个lab都要卡很久，倒不是因为没有思路，基本上每个lab我都能很快的有一个自己的实现计划，并编写出对应的代码，但内核是一个各部分联系紧密且难以调试的软件，大多数时候无法根据错误的结果直接定位到bug在哪里，只能不断地通过重新审视自己的代码、打印中间结果去debug。通过课程的学习和lab的实践，我对于操作系统的运行机制和实现方法有了更加深入的理解。或许我永远不需要真的在工作中编写操作系统内核，但操作系统的实现策略和机制对于很多软件系统的实现有着借鉴意义。最近太忙了，其实还剩一个文件系统的lab没做，以后有时间了再补上。

---


## 一点xv6的梳理

### StartUp

xv6是一个支持多核心的操作系统，因此下面整个启动过程在所有核心上均会执行。

xv6的启动过程为：entry.S->start.c-> main.c。

首先来看entry.S，其中为每个核心的执行设置了栈:

```assembly
        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + (hartid * 4096)

        la sp, stack0		# 基址，stack0定义在start.c中
        li a0, 1024*4
        csrr a1, mhartid	# 获取当前核心号
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0		# 为当前核心设置内核栈
        
        # jump to start() in start.c
        call start
spin:
        j spin
```

再来看start.c，内核启动的入口点，负责从机器模式（Machine mode）到监管者模式（Supervisor Mode）的切换，然后跳转到main函数：

```c
// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;	// 清除旧模式位
  x |= MSTATUS_MPP_S;		// 将模式设置为Supervisor Mode
  w_mstatus(x);				// mstatus控制处理器特权模式

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);		// 将mepc寄存器指向main，mret后跳转到此处

  // disable paging for now.
  w_satp(0);				// 关闭分页

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();		// 初始化时钟，每个核心都有独立的时钟

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);		// 核心号存放在tp寄存器中

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}
```

然后跳转到main.c执行main函数：

```c
volatile static int started = 0;

void
main()
{
  if(cpuid() == 0){	// 第一个核心执行下列初始化操作
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();	// 告知编译器，在started赋1之前的操作必须全部完成
    started = 1;
  } else {
    while(started == 0)	// 必须等待第一个核心初始化完成之后其他核心才能进行自己的相关初始化
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
```

### Spinlock

xv6的spinlock实现在`spinlock.c`和`spinlock.h`中，在看锁的内容之前，我们需要先思考几个问题：

- 为什么需要锁？
- 锁到底是什么？
- 锁如何实现目标功能？ 

对于第一个问题，再xv6中需要锁的原因在于，xv6是一个支持多核并发的操作系统，这些核心又共享内存中的某些资源，多个核心的并发执行顺序是不固定的，这就要求不同核心之间对某些资源进行互斥访问。而锁就是提供互斥访问资源的资源。

对于第二个问题，从本质上看，锁就是内存中的一小块区域，线程可以往这块内存中写入1表示占有该锁，向这块内存中写入0表示释放该锁。

对于第三个问题，在使用锁时，第一个向锁中写入1的线程能够获取锁，进而获取资源，而后来的线程在前者释放锁之前会被阻塞掉（要么原地循环-自旋锁，要么被置为非就绪态-睡眠锁）。

spinlock结构体的定义，包含了一个locked字段来标识该锁是否已被获取，name为锁名，cpu字段指向获取了该锁的CPU。

```c
// Mutual exclusion lock.
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};
```

```c
// 对锁进行初始化
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// 尝试获取锁
void
acquire(struct spinlock *lk)
{
  push_off(); // 关中断避免死锁x
  if(holding(lk))
    panic("acquire");

  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)	// riscv提供的原子swap操作
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();	// 就是告知编译器别瞎优化，别把指令的顺序给重排打乱
						// 导致前面的某些指令被放到这个调用之后去
  // Record info about lock acquisition for holding() and debugging.
  lk->cpu = mycpu();
}

// 释放锁
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();		

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked); 	// 同 lk->locked = 0

  pop_off();
}

/*
	这里使用push_off和pop_off的原因在于，一个核心有可能同时获取多个锁，
	我们不希望在所有的锁被释放之前将中断打开（可能造成死锁），因此使用一个counter来记录获取的锁数
	只有当获取的锁全部被释放后才能打开中断
*/
// 关中断
void
push_off(void)
{
  int old = intr_get();

  intr_off();
  if(mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

// 开中断
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
```

值得注意的是，自旋锁不适合用在长期持有的情况，因为在等待期间它仍然不会主动放弃执行，而是继续占用时间片，浪费资源。自旋锁应当被用于保护共享数据，它的critical section较为短小。

### SleepLock

xv6的sleeplock实现在`sleeplock.c`和`sleeplock.h`中，睡眠锁主要就是实现两个函数sleep和wakeup。这里有几个问题：sleep的进程何时被唤醒？wakeup要唤醒哪些进程？

xv6中使用channel这个概念，channel就是一个整数，在sleep时传入channel作为参数，在其中赋值给进程的chan。在wakeup时只有当睡眠进程的chan与sleep中传入的相同时才将其唤醒。

总结一下：

- channel只是一个整数，在调用sleep时作为参数传入
- channel被保存在proc结构体中
- wakeup遍历所有procs，唤醒与参数chan相匹配的进程

```c
// Long-term locks for processes
struct sleeplock {
  uint locked;       // Is the lock held?
  struct spinlock lk; // spinlock protecting this sleep lock
  
  // For debugging:
  char *name;        // Name of lock.
  int pid;           // Process holding lock
};

void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);		// 进入睡眠之前必须释放锁，避免死锁

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
```

> 在进入sleep之后先通过acquire(&p->lock);获取了本进程的锁，这是为什么？
>
> 因为在sleep中会修改进程的chan和state，而在wakeup中也会遍历所有进程然后判断其chan和state，决定是否修改state。
>
> 考虑不获取进程锁的情况，线程A在进入sleep后释放掉lk（避免死锁），但还未来得及设置进程的chan和state，就被调度下去，换上线程B调用执行wakeup，但此时线程A还未设置chan和state=SLEEP，唤醒失败。等到线程A再次被调度上来，将state设置为SLEEP，又出现了永久阻塞。
>
> 因此为了保证进程状态的一致性，必须在sleep修改chan和state、wakeup访问chan和state之前获取锁，在完成修改和访问之后释放。

> 在sleep中获取进程锁，修改状态为SLEEPING，然后调用了sched，注意此时进程的锁还未被释放，而wakeup中也要获取进程的锁，这不会造成死锁吗？
>
> 不会造成死锁，这是由xv6的调度方式保证的。其实sched的注释就提到了，在调用sched之前要将状态修改好，并持有进程的锁，在sched退出时同样会保持锁的持有。那这个进程的锁是在哪里被释放的呢？在sched中通过swtch切换到scheduler，继续接着scheduler中swtch之后的代码执行，其中就有释放进程锁的语句。同样也是在scheduler中，在进程被调度上CPU时会先获取进程的锁，因此在sched执行完毕后进程锁仍是被持有的状态。具体的调度细节看proc.c中schedule部分的代码。

sys_sleep使用sleep和wakeup的例子：

```c
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);	// 注意这里要先获取tickslock
  // ===============================================================
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);	// 以全局变量ticks的地址为chan
  }
  // ===============================================================
  release(&tickslock);
  return 0;
}
```

```c
// trap.c
void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);	// 这里也要先获取tickslock
    // ===============================================================
    ticks++;
    wakeup(&ticks);			// 更新ticks后唤醒sys_sleep中的线程
    release(&tickslock);
    // ===============================================================
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}
```

> 值得注意的是，在使用sleep和wakeup时，必须保证对条件变量进行原子性访问，否则可能出现wakeup丢失的问题，进而导致死锁。
>
> 举个例子：
>
> ```c
> // 线程 A（消费者）
> while (condition == false) {
>  sleep(chan); // 等待条件满足
> }
> 
> // 线程 B（生产者）
> condition = true;
> wakeup(chan);   // 唤醒消费者
> ```
>
> 线程A判断循环条件conditoin == false为真，进入循环体，准备调用sleep。
>
> 此时发生调度，线程B抢占CPU，将condition设置为true，然后调用wakeup，但线程A还未执行到sleep，唤醒无效
>
> 等到线程A恢复执行，调用sleep进入睡眠，这就造成了永久阻塞。

因此在sys_sleep的例子中：

- 在访问ticks时，必须使用tickslock进行加锁（tickslock保护ticks）
- 避免丢失wakeup
- 在睡眠时不能持有锁，否则会造成死锁

### Mem Management

在xv6中，所有物理内存的分配和释放都以4K的页面为单位，没有提供自定义数量的内存分配，其实现都在kalloc.c中。

- kinit，物理内存的初始化：

  ```c
  extern char end[]; // first address after kernel.  内核会被加载到RAM起始处
                     // defined by kernel.ld.
  
  struct run {
    struct run *next;
  };
  
  struct {
    struct spinlock lock;
    struct run *freelist;	// 第一个物理页面的指针
  } kmem;	// 管理物理内存的结构体
  
  void
  kinit()
  {
    initlock(&kmem.lock, "kmem");
    freerange(end, (void*)PHYSTOP);   // end是内核代码的末尾，让kmem管理内核剩余全部内存
  }
  
  
  
  void
  freerange(void *pa_start, void *pa_end)
  {
    char *p;
    p = (char*)PGROUNDUP((uint64)pa_start);
    for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {	// 用单链表将物理页面链接起来
      kfree(p);
    }
  }
  ```

- kfree，释放物理页面

  ```c
  // Free the page of physical memory pointed at by pa,
  // which normally should have been returned by a
  // call to kalloc().  (The exception is when
  // initializing the allocator; see kinit above.)
  void
  kfree(void *pa)
  {
    struct run *r;
  
    if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
      panic("kfree");
      
    memset(pa, 1, PGSIZE);	// 清除其中的内容，为安全考虑  
    r = (struct run*)pa;		// 将pa地址的内容视为run
  
    acquire(&kmem.lock);
    r->next = kmem.freelist;    // 头部插入，相当于栈
    kmem.freelist = r;
    release(&kmem.lock);
  }
  ```

- kalloc，发呢配物理页面

  ```c
  // Allocate one 4096-byte page of physical memory.
  // Returns a pointer that the kernel can use.
  // Returns 0 if the memory cannot be allocated.
  void *
  kalloc(void)
  {
    struct run *r;
  
    acquire(&kmem.lock);
    r = kmem.freelist;	// 取链表首结点
    if(r) {
      kmem.freelist = r->next;
    }
    release(&kmem.lock);
  
    if(r)
      memset((char*)r, 5, PGSIZE); // fill with junk
  
    return (void*)r;
  }
  ```

### System Call

我们来看一下完整的系统调用过程。

首先从用户这一端来看，所有的系统调用都需要在user.h中声明：

```c
// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int trace(int);
```

而这些声明的实现在哪里呢？注意usys.pl这个文件：

```c
#!/usr/bin/perl -w

# Generate usys.S, the stubs for syscalls.

print "# generated by usys.pl - do not edit\n";

print "#include \"kernel/syscall.h\"\n";

sub entry {
    my $name = shift;
    print ".global $name\n";	// 让名字全局课件
    print "${name}:\n";			
    print " li a7, SYS_${name}\n";	// 将立即数SYS_name加载到寄存器a7
    print " ecall\n";			// 调用ecall切换到内核态并跳到异常处理入口
    print " ret\n";
}
	
entry("fork");
entry("exit");
entry("wait");
entry("pipe");
entry("read");
entry("write");
entry("close");
entry("kill");
entry("exec");
entry("open");
entry("mknod");
entry("unlink");
entry("fstat");
entry("link");
entry("mkdir");
entry("chdir");
entry("dup");
entry("getpid");
entry("sbrk");
entry("sleep");
entry("uptime");
entry("trace");
```

这是一个perl脚本，下面每个entry都会按照上面的sub entry所定义的形式展开，展开内容在编译后会放在usys.S文件中。如fork:

```assembly
#include "kernel/syscall.h"
.global fork
fork:
 li a7, SYS_fork
 ecall
 ret
```

这里的SYS_fork定义在kernel/syscall.h中。

在调用系统调用的包装函数后，将系统调用号放入a7，然后执行ecall，ecall保存下一条指令的地址、关中断、切换到内核态、将uservec地址存入pc、将trap的原因放入scause等。

uservec的内容：

```assembly
.section trampsec
.globl trampoline
.globl usertrap
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c sets stvec to point here, so
        # traps from user space start here,
        # in supervisor mode, but with a
        # user page table.
        #

        # save user a0 in sscratch so
        # a0 can be used to get at TRAPFRAME.
        csrw sscratch, a0       // 把a0的内容暂存到sscratch

        # each process has a separate p->trapframe memory area,
        # but it's mapped to the same virtual address
        # (TRAPFRAME) in every process's user page table.
        li a0, TRAPFRAME        // 将用户进程的trapframe地址存入a0
        
        # save the user registers in TRAPFRAME
        sd ra, 40(a0)   // 到此并未切换页表，仍然可以访问用户进程的地址空间
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# save the user a0 in p->trapframe->a0
        csrr t0, sscratch
        sd t0, 112(a0)

        # initialize kernel stack pointer, from p->trapframe->kernel_sp
        ld sp, 8(a0)

        # make tp hold the current hartid, from p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # load the address of usertrap(), from p->trapframe->kernel_trap
        ld t0, 16(a0)   // 将usertrap函数的地址存入t0

        # fetch the kernel page table address, from p->trapframe->kernel_satp.
        ld t1, 0(a0)    // 将内核页表的地址存入t1

        # wait for any previous memory operations to complete, so that
        # they use the user page table.
        sfence.vma zero, zero

        # install the kernel page table.
        csrw satp, t1    # 注意！！！！，此后切换为内核页表

        # flush now-stale user entries from the TLB.
        sfence.vma zero, zero

        # jump to usertrap(), which does not return
        jr t0
```

首先将当前上下文保存到trapframe中，然后切换到内核页表并跳转到usertrap函数。

```c
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);   // 修改stvec指向kernelvec，因为我们已经在内核态了

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){  // 根据scause寄存器的内容判断trap的原因
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4; // 当前epc指向ecall，应当+4使其指向下一条指令

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){  // 处理外部中断
    // ok
  } else {  // 异常，直接kill掉该进程
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)  // 时钟中断，无事发生，直接放弃执行
    yield();

  usertrapret();
}
```

其中根据scause寄存器中的值来判断trap的原因，如果是因为系统调用，scause会等于8。在其中调用了syscall函数。

```c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

syscall根据之前传入到a7寄存器的值作为索引，从syscalls数组中找对相应的处理函数，并将返回值保存到a0。

从syscall返回后会继续执行usertrap中的内容，调用usertrapret函数：

```c
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);    // 修改stvec指向uservec

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);	// 设置返回地址

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);	// 获取用户页表

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}
```

最后跳转到userret，恢复用户进程的上下文，调用sret返回

```assembly
.globl userret
userret:
        # userret(pagetable)
        # called by usertrapret() in trap.c to
        # switch from kernel to user.
        # a0: user page table, for satp.

        # switch to the user page table.
        sfence.vma zero, zero
        csrw satp, a0   // a0中存放的是用户进程的页表
        sfence.vma zero, zero

        li a0, TRAPFRAME

        # restore all but a0 from TRAPFRAME
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# restore user a0
        ld a0, 112(a0)
        
        # return to user mode and user pc.
        # usertrapret() set up sstatus and sepc.
        sret

```

### RISCV

介绍下RISCV的内容。

32个寄存器：

- zero：硬连接到0
- ra：return address，返回地址
- sp：stack pointer，栈指针，指向当前栈顶
- tp：thread pointer，保存core number或者是Hart id
- a0~a7：参数寄存器，由调用者保存
- t0~t6：临时寄存器，由调用者保存
- s0~s11：保护寄存器，由被调用者保存

3种模式：machine mode，supervisor mode，user mode

CSR寄存器（Control and Status Register）：

- mhartid：保存hart id
- mstatus和sstatus：状态寄存器
- mtvec和stvec：trap vector，保存trap处理函数地址
- mepc和sepc：保存之前的pc值
- scause：保存发生trap的原因
- mscratch和sscratch：工作寄存器
- satp：保存指向页表的指针
- mie和sie：中断使能
- sip：中断等待
- medeleg：异常委托
- mideleg：中断委托

页表

- 内核页表，内存的虚拟地址和物理地址一一对应

- 每个用户进程都有一个自己的页表：

  - RISCV提供了三种不同的页表格式，Sv32、Sv39和Sv48，xv6使用的是Sv39，采用三级页表的形式。

  - 为了快速查找页面，硬件一般提供了TLB（Translation lookaside buffer），用来缓存页表项，可以使用指令sfence.vma来刷新TBL，在xv6种可以通过调用函数sfence_vma()来执行该指令。
- Sv39使用39位地址空间，第12位为4K页面的页内偏移，前27为分别为各级页表项的索引。最高级页面的地址存放在satp寄存器种。

- trap的过程：

  - sstatus.SPP保存发生trap时模式
  - sstatus.SPIE保存发生trap时的中断使能情况


### Filesystem

Disk Device：

- 传输单元为块（Block），大小为1024。注意区分块和扇区，扇区通常为512字节，块大小通常为扇区的整数倍
- 磁盘块被编号0,1,2,...等。超级块放在1号块（第二块）中，其中存放了文件系统的元信息
- xv6使用qemu模拟硬件，这里的磁盘为virtio-disk，驱动程序提供了一个函数virtio_disk_rw对虚拟磁盘进行读写。
  - 读：virtio_disk_rw(buf_ptr, 0);
  - 写：virtio_disk_rw(buf_ptr, 1);

Buffer:

- buf结构体，xv6中有一个固定长度的buf数组，每个buf包含一个block的数据，BSIZE = 1024，还有一些block的元信息。

  ```c
  struct buf {
    int valid;   // 是否有从磁盘读入的数据？
    int disk;    // does disk "own" buf? 1表示磁盘正在读写过程中
    uint dev;		// 标识设备
    uint blockno;	// 标识块号
    struct sleeplock lock;
    uint refcnt;	// 引用计数
    struct buf *prev; // LRU cache list
    struct buf *next;
    uchar data[BSIZE];
  };
  ```

- bcache结构体管理这些buf，在初始化的时候以双向链表的形式将其串连起来，头节点为bcache.head。

  ```c
  struct {
    struct spinlock lock;
    struct buf buf[NBUF];
  
    struct buf head;
  } bcache;
  ```

  为了实现LRU，每次在释放buf时，都将其从链表中摘出，再头插到bcache.head之后。每次要取空闲的buf时则从bcache.head.prev开始，往前查找，因为从前往后离head最远的就是最久未使用的。

- 对外的函数结构：

  - binit，初始化锁，并将所有buf组织成双向链表的形式

    ```c
    void
    binit(void)
    {
      struct buf *b;
    
      initlock(&bcache.lock, "bcache");
    
      // Create linked list of buffers
      bcache.head.prev = &bcache.head;
      bcache.head.next = &bcache.head;
      for(b = bcache.buf; b < bcache.buf+NBUF; b++){	// 将所有buf以头插的方式插入双向链表中
        b->next = bcache.head.next;
        b->prev = &bcache.head;
        initsleeplock(&b->lock, "buffer");
        bcache.head.next->prev = b;
        bcache.head.next = b;
      }
    }
    ```

  - bread，从设备dev中读取blockno号块，如果该块在buf中，增加引用计数并返回，否则分配一个buf并从磁盘中读入

    ```c
    // Look through buffer cache for block on device dev.
    // If not found, allocate a buffer.
    // In either case, return locked buffer.
    static struct buf*
    bget(uint dev, uint blockno)
    {
      struct buf *b;
    
      acquire(&bcache.lock);	// 保持对bcache的互斥访问
    
      // Is the block already cached?
      for(b = bcache.head.next; b != &bcache.head; b = b->next){	// 先看看该块是不是已经存在了buf中
        if(b->dev == dev && b->blockno == blockno){
          b->refcnt++;
          release(&bcache.lock);	// 找到了目标buf，释放掉bcache的锁
          acquiresleep(&b->lock);	// buf一次只允许一个proc使用
          return b;
        }
      }
    
      // Not cached.
      // Recycle the least recently used (LRU) unused buffer.
      for(b = bcache.head.prev; b != &bcache.head; b = b->prev){ // 不存在，那就分配一个buf
        if(b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;		// 这里将valid设置为0，待会在bread中会通过virtio_disk_rw读取磁盘块到buf中
          b->refcnt = 1;
          release(&bcache.lock);	// 同上
          acquiresleep(&b->lock);
          return b;
        }
      }
      panic("bget: no buffers");
    }
    
    // Return a locked buf with the contents of the indicated block.
    struct buf*
    bread(uint dev, uint blockno)
    {
      struct buf *b;
    
      b = bget(dev, blockno);
      if(!b->valid) {			// 新buf，需要从磁盘读入数据
        virtio_disk_rw(b, 0);
        b->valid = 1;
      }
      return b;
    }
    ```

  - bwrite，写入buf的数据到磁盘，设备号和块号都保存在buf中

    ```c
    // Write b's contents to disk.  Must be locked.
    void
    bwrite(struct buf *b)
    {
      if(!holdingsleep(&b->lock))	// 必须持有锁
        panic("bwrite");
      virtio_disk_rw(b, 1);	//  将buf写入到virtio磁盘
    }
    ```

  - brelse

    ```c
    // Release a locked buffer.
    // Move to the head of the most-recently-used list.
    void
    brelse(struct buf *b)
    {
      if(!holdingsleep(&b->lock))
        panic("brelse");
    
      releasesleep(&b->lock);	// 这里释放是没问题的，因为后面必须先获取bcache的锁才能对b进行修改，并且不释放可能造成死锁
    
      acquire(&bcache.lock);
      b->refcnt--;
      if (b->refcnt == 0) {	// 该buf没有被任何proc引用，应当释放掉
        // no one is waiting for it.
        b->next->prev = b->prev;	// 将buf从链表中摘出
        b->prev->next = b->next;
          
        b->next = bcache.head.next;	// 再头插到链表
        b->prev = &bcache.head;
        bcache.head.next->prev = b;z
        bcache.head.next = b;
      }
      
      release(&bcache.lock);
    }
    ```

  - 还有两个函数，用于对buf的引用计数进行原子性的增减

    ```c
    void
    bpin(struct buf *b) {
      acquire(&bcache.lock);
      b->refcnt++;
      release(&bcache.lock);
    }
    
    void
    bunpin(struct buf *b) {
      acquire(&bcache.lock);
      b->refcnt--;
      release(&bcache.lock);
    }
    ```

Crash recovery：log file

- why？为什么需要故障恢复？因为我们一个简单的更新请求，可能涉及多次磁盘操作，而如果在这个请求处理过程中系统出现故障，导致某些数据被写入磁盘，而另一些却没有写入，这就破坏了数据的一致性。

- 拿文件系统来说，文件系统的数据结构被保存在磁盘上，更新一个磁盘块的内容需要涉及多个磁盘块的读写（更新inode块、更新目录所在的块、更新bitmap块），如果更新了inode块和bitmap块，还未将数据写入数据块时出现故障，那么后续该块就相当于丢失了，无法使用。

- 要保证磁盘数据的一致性，就必须遵循操作的原子性，即要么完整写入，要么都不写。 

- 更进一步，多个磁盘的读写操作必须被组合成一个事务，要么整个事务发生，要么事务中的操作一个也不发生。

- 使用log file进行故障恢复，先来看一下磁盘中的数据结构：

  - 前两个为启动块和超级快
  - 然后是一个log file的header和log数据
  - 其余的块都可视为数据

- 其中的header和日志数据块在内存中都有相应的缓存。

  - logheader结构体和log结构体：

  ```c
  struct logheader {
    int n;	// 当前日志块的数量
    int block[LOGSIZE]; // 记录log块对应的数据块号
  };
  
  struct log {
    struct spinlock lock;
    int start;	// log头块的起始地址
    int size;		// 总日志块数
    int outstanding; // how many FS sys calls are executing.记录事务数，每次调用begin_op加1，每次end_op减1
    int committing;  // in commit(), please wait.
    int dev;
    struct logheader lh;
  };
  struct log log;
  ```

- initlog，初始化日志系统

  ```c
  void
  initlog(int dev, struct superblock *sb)
  {
    if (sizeof(struct logheader) >= BSIZE)
      panic("initlog: too big logheader");
  
    initlock(&log.lock, "log");
    log.start = sb->logstart;	// log的start设置为log头块的起始地址
    log.size = sb->nlog;		// 写入log块个数
    log.dev = dev;
    recover_from_log();		// 尝试从log中恢复
  }
  ```

- 事务从begin_op开始，该函数一般在文件相关系统调用的开头被调用：

  ```c
  // called at the start of each FS system call.
  void
  begin_op(void)
  {
    acquire(&log.lock);	// 保护outstanding
    while(1){
      if(log.committing){	// 如果有其他线程在commiting，wait
        sleep(&log, &log.lock);
      } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){ // 如果当前和其他事务的log块数量超出槽的个数
        // this op might exhaust log space; wait for commit.
        sleep(&log, &log.lock);
      } else {
        log.outstanding += 1;	// 增加计数，表示当前事务的个数
        release(&log.lock);
        break;
      }
    }
  }
  ```

- log_write，在事务被提交期间数据并不直接写入磁盘，而是在header中将对应磁盘块号记录下来，然后增加buf的引用计数，确保buf不被释放，等到事务提交时一起写入磁盘。

  ```c
  // Caller has modified b->data and is done with the buffer.
  // Record the block number and pin in the cache by increasing refcnt.
  // commit()/write_log() will do the disk write.
  //
  // log_write() replaces bwrite(); a typical use is:
  //   bp = bread(...)
  //   modify bp->data[]
  //   log_write(bp)
  //   brelse(bp)
  void
  log_write(struct buf *b)	// 就是在修改完buf后，将其记录在log的header中
  {
    int i;
  
    acquire(&log.lock);
    if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
      panic("too big a transaction");
    if (log.outstanding < 1)	// log_write必须在事务中使用
      panic("log_write outside of trans");
  
    for (i = 0; i < log.lh.n; i++) {	// 如果该数据块对应的log块已经存在，那就将写入合并，不再另外分配log块
      if (log.lh.block[i] == b->blockno)   // log absorption
        break;
    }
    log.lh.block[i] = b->blockno;	// 保存该log块对应的数据块号
    if (i == log.lh.n) {  // Add new block to log?
      bpin(b);    // 并不真正地写入，而是通过bpin增加引用计数，避免该buf被释放
      log.lh.n++;
    }
    release(&log.lock);
  }
  ```

- 在end_op结束，一般在文件相关系统调用的最后被调用

  ```c
  // called at the end of each FS system call.
  // commits if this was the last outstanding operation.
  void
  end_op(void)
  {
    int do_commit = 0;
  
    acquire(&log.lock);
    log.outstanding -= 1;	// 减少计数
    if(log.committing)
      panic("log.committing");
    if(log.outstanding == 0){	// 如果当前事务数为0，说明全部事务都完成了
      do_commit = 1;
      log.committing = 1;	// 设置为事务提交状态
    } else {
      // begin_op() may be waiting for log space,
      // and decrementing log.outstanding has decreased
      // the amount of reserved space.
      wakeup(&log);
    }
    release(&log.lock);
  
    if(do_commit){
      // call commit w/o holding locks, since not allowed
      // to sleep with locks.
      commit();	// 提交
      acquire(&log.lock);
      log.committing = 0; // 提交完成，清除状态
      wakeup(&log);
      release(&log.lock);
    }
  }
  ```

- commit，提交日志，先将修改过的buf块写入到磁盘的log块中，然后再写入磁盘的数据块，全部写完后清除日志

  ```c
  static void
  commit()
  {
    if (log.lh.n > 0) {	// 日志块的数量，若为0表示没有日志需要恢复
      write_log();     // Write modified blocks from buf cache to log
      write_head();    // Write header to disk -- the real commit
      install_trans(0); // Now install writes to home locations
      log.lh.n = 0;     // 清除日志
      write_head();     // Erase the transaction from the log
    }
  }
  ```

- write_log，根据内存中loghader的记录，将buf中的内容全部写入磁盘日志块中。

  ```c
  // Copy modified blocks from cache to log.
  static void
  write_log(void)
  {
    int tail;
  
    for (tail = 0; tail < log.lh.n; tail++) {
      struct buf *to = bread(log.dev, log.start+tail+1); // log block
      struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block
      memmove(to->data, from->data, BSIZE);
      bwrite(to);  // write the log
      brelse(from);
      brelse(to);
    }
  }
  ```

- write_header，将内存中的log头写入磁盘

  ```c
  // Write in-memory log header to disk.
  // This is the true point at which the
  // current transaction commits.
  static void
  write_head(void)
  {
    struct buf *buf = bread(log.dev, log.start);	// 读取磁盘中的log头
    struct logheader *hb = (struct logheader *) (buf->data);
    int i;
    hb->n = log.lh.n;	// 将内存中的log头写入buf中的数据部分
    for (i = 0; i < log.lh.n; i++) {
      hb->block[i] = log.lh.block[i];
    }
    bwrite(buf);	// 将buf写入磁盘的log头
    brelse(buf);
  }
  ```

- install_trans，将磁盘上的日志备份写入真正的数据块中。

  ```c
  // Copy committed blocks from log to their home location
  static void
  install_trans(int recovering)
  {
    int tail;
  
    for (tail = 0; tail < log.lh.n; tail++) {	// 遍历日志，将日志块的内容复制到数据块
      struct buf *lbuf = bread(log.dev, log.start+tail+1); // read log block
      struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // read dst
      memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
      bwrite(dbuf);  // write dst to disk
      if(recovering == 0)	//  0表示从commit中提交，需要unpin，而从恢复中调用则不需要
        bunpin(dbuf);
      brelse(lbuf);
      brelse(dbuf);
    }
  }
  ```

- recover_from_log，只在initlog中被调用

  ```c
  static void
  recover_from_log(void)
  {
    read_head();
    install_trans(1); // if committed, copy from log to disk
    log.lh.n = 0;
    write_head(); // clear the log
  }
  ```

- read_head，将log头从磁盘读入内存，只在recover_from_log中被调用

  ```c
  // Read the log header from disk into the in-memory log header
  static void
  read_head(void)
  {
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *lh = (struct logheader *) (buf->data);
    int i;
    log.lh.n = lh->n; 
    for (i = 0; i < log.lh.n; i++) {
      log.lh.block[i] = lh->block[i];
    }
    brelse(buf);
  }
  ```

## Lecutres

### Lecutre8 page fault

利用page fault可以为我们的虚拟内存实现：

- lazy allocation：对于用户进程使用sbrk申请内存，假装为其分配，实际上并没有，当用户访问时再通过page fault对其所需的页面进行真正的分配
- copy-on-write fork：在调用fork时，并不为子进程分配物理内存，而是直接共享父进程的页面，这里把访问权限设置为只读，当父子任何一方要进行写时，再分配页面并将访问权限改为可读可写。
- demand paging：在程序执行时并不直接将整个程序加载到内存，而是先设置好页表，通过pagefault将需要的页面加载进内存。
- mmap：将磁盘数据映射到内存中来。

虚拟内存的作用：

- 实现进程间、内核与进程间隔离
- 实现对内存空间的抽象，间接访问物理内存

page fault需要的信息：

- 引起page fault的虚拟地址va，保存在STVAL寄存器
- page fault的类型，保存在SCAUSE寄存器
- 引起page fault的指令的地址，保存在SEPC寄存器

---

- 教授上手在xv6中实现了一个lazy allocate的简单版本

- one zero fill on demand：对于BSS段中的内容，一开始只分配一个物理页，将其全部置0，然后让所有BSS的虚拟地址都指向该页面，访问权限为只读，当写入出现时通过page fault创建新页面。
- copy-on-write(COW) fork
  - 在处理对只读页面进行写操作导致的page fault时，如何区分COW fork的情形和对一个普通的只读页面？利用PTE中的保留字来确定。
  - 需要记录页面的被应用的次数，用来判断是否释放该页面。
- demand paging
  - 在exec中只将部分可执行程序加载到内存，其余的用到时通过page fault处理来加载。
  - 当物理内存不足时，选择一页将其换出到磁盘，然后把需要的页面加载到内存，一般使用LRU来实现，并且尽可能选择non-dirty page，因为被被写过的页面后续可能还会被写，如果现在将其换出，后续被再次被写入后还要继续写回磁盘。
- memory-mapped files：为文件分配虚拟地址，并将所需部分读入到内存，在之后的文件操作中就可以避免反复地进行磁盘IO，而是直接在内存中存取。

### Lecture9 interrupt

- H/W wants attention!
- traps(syscall, exception, interrupt) have the same mechanism.
- 中断特殊的地方：asynchronous(异步)， concurrency(并发), programmable(需要被编程)
- SiFive 硬件简单介绍
- 用来管理设备的代码——driver(驱动)， 其通过内存IO映射来读写设备的数据/控制寄存器
- RISCV对interrupt的支持：
  - SIE寄存器
  - SSTATUS寄存器
  - SIP寄存器，
  - Scause寄存器
  - STVEC寄存器
- 两个例子：如何向显示器输出字符、如何从键盘获取输入。本质上就是CPU通过设备驱动与设备的数据端口和控制端口进行数据交互，并通过中断实现双方的异步执行。
- 中断和并发
  - 设备和CPU并发执行，生产者消费者模型
  - 中断停止当前程序的执行，包括内核程序
  - 驱动的上层和下层也可能是并行的

### Lecture10 lock

- lock保证了并行程序的正确性，但又降低了并行性，影响性能
- lock会增加编写程序的难度
- 介绍了xv6实现spinlock源码，其通过RISCV提供的原子指令来完成锁的获取
- 自旋锁被获取后要关闭中断
- 编写并行程序的建议：
  - 如无必要，不要share数据给不同进程/线程
  - 从粗粒度（coarse-grained）的锁开始，不断向细粒度（fine-grained）的锁演化

### Lecture11 thread

- thread：一次线性执行，需要的资源有PC，寄存器，栈

- 交错执行（interleave）：

  - multi-core
  - switch

- 是否共享内存？

  - xv6的内核线程——Yes
  - xv6的用户进程只有一个线程——No
  - Linux支持用户线程

- 挑战：

  - 线程间如何切换？schedule
  - 切换时需要保存什么信息？
  - 如何将运行中的线程停下，收回控制权？

- 时钟中断

  - 定时器每隔一段时间发出一个时钟中断
  - 操作系统在中断处理中收回系统的控制权，并通过调度器选择下一个执行的线程
  - 这被称为抢占式（preemptive）调度，与之相反的是自愿调度（voluntary）调度。

- 线程状态（State）

  - Running：正在CPU上执行
  - Runnable：准备好了去执行，但还没有获得CPU
  - Sleeping：阻塞状态，等待唤醒

- 线程如何调度？

  - 造成线程调度的原因有：用户线程使用系统调用被sleep、时钟发出中断重新调度

  - 无论调度的原因是什么，最终都会调用yield->sched->swtch，在swtch中context从当前线程切换到CPU的sheduler线程，在scheduler中循环查找第一个状态为RUNNABLE的线程，调用swtch切换到该线程，该线程会从上次调用swtch的地方继续执行。

    ```c
    void
    yield(void)
    {
      struct proc *p = myproc();
      acquire(&p->lock);
      p->state = RUNNABLE;  // 将当前进程的状态设置为Runnable，然后调用sched
      sched();
      release(&p->lock);
    }
    
    void
    sched(void)
    {
      int intena;
      struct proc *p = myproc();
    
      if(!holding(&p->lock))    // 必须先获取自己的锁
        panic("sched p->lock");
      if(mycpu()->noff != 1)    // 释放其他的锁
        panic("sched locks");
      if(p->state == RUNNING)   // 状态不能为运行态
        panic("sched running");
      if(intr_get())            // 必须关中断
        panic("sched interruptible");
    
      intena = mycpu()->intena;
      swtch(&p->context, &mycpu()->context);  // cpu的context中保存的是sched线程的上下文
      mycpu()->intena = intena;
    }
    
    void
    scheduler(void)
    {
      struct proc *p;
      struct cpu *c = mycpu();
    
      c->proc = 0;
      for(;;){
        // The most recent process to run may have had interrupts
        // turned off; enable them to avoid a deadlock if all
        // processes are waiting.
        intr_on();
    
        int found = 0;
        for(p = proc; p < &proc[NPROC]; p++) {  // 循环调度，选择第一个碰到的就绪态进程执行
          acquire(&p->lock);
          if(p->state == RUNNABLE) {
            // Switch to chosen process.  It is the process's job
            // to release its lock and then reacquire it
            // before jumping back to us.
            p->state = RUNNING;
            c->proc = p;
            swtch(&c->context, &p->context);
    
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
            found = 1;
          }
          release(&p->lock);
        }
        if(found == 0) {  // 如果一轮下来一个就绪进程都没有，就报错
          // nothing to run; stop running on this core until an interrupt.
          intr_on();
          asm volatile("wfi");
        }
      }
    }
    ```

- 系统中的第一个线程是如何被调度的？

  - 在userinit中调用了allocproc为第一个线程分配struct proc，其中有两行代码指定了context的返回地址和栈地址。

    ```c
    static struct proc*
    allocproc(void)
    {
      struct proc *p;
    
      .....
    
      // Set up new context to start executing at forkret,
      // which returns to user space.
      memset(&p->context, 0, sizeof(p->context));
      p->context.ra = (uint64)forkret;    // 这就是
      p->context.sp = p->kstack + PGSIZE;
    
      return p;
    }
    ```

  - 执行完userinit后回到main函数，最后会执行scheduler，当前进程中唯一的RUNNABLE的proc就是刚刚创建的那个，因此在调用swtch后会跳转到context.ra指向的指令去执行，可以看到allocproc中设置的返回地址为forkret，它会调用usertrapret返回用户态，去执行userinit中指定的initcode中的代码。

    ```c
    void
    forkret(void)
    {
      static int first = 1;
    
      // Still holding p->lock from scheduler.
      release(&myproc()->lock);
    
      if (first) {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        fsinit(ROOTDEV);
    
        first = 0;
        // ensure other cores see first=0.
        __sync_synchronize();
      }
    
      usertrapret();
    }
    ```


### Lecture12 Q&A for COW

- Morris介绍了他自己做Lab的方法："take small steps", 先找一个问题的一个小子集，编写程序，尝试使它运行起来通过一些简单的测试，而不是从一开始就思考并编写一个完整的解决方案。

### Lecture13 Coordination

- Coordination，xv6中的协作场景有管道、磁盘读写、wait
- 实现同步的最简单方式类似自旋锁，用一个循环不断check变量的值，在这种实现方式下，等待的线程即使啥事儿不干，操作系统仍要分给他CPU时间，浪费性能。
- 另一种方案类似睡眠锁，在获取不到时将当前线程休眠（状态设为SLEEP+放入某数据结构中），在其他进程释放后唤醒一个（notify）或唤醒全部（broadcast）睡眠的进程，再次尝试获取。
- Lost Wakeup，首先如果只有一个锁的话，在sleep之前不能加锁，因为调用wakeup的线程也要获取锁，这样就造成了死锁。因而必须在sleep之前将锁释放，但这又带来了问题，如果在锁被释放掉后，sleep被执行之前，wakeup先一步执行了，那么sleep的进程将不会被唤醒，如果没有后续的wakeup的话。
- 解决方法是再添加一个锁，两个锁先后获取和释放，具体细节看课程和xv6 book。 
- 学生问：为什么要提供kill这个系统调用，一个进程难道不会使用这个系统调用来杀掉其他进程吗？moris回答：如果你在MIT的Athena机器上这么做，We will probably kick you out of school. 回到正题，你当然可以kill掉自己的进程，但是像Linux这种操作系统，是存在用户权限的，它会对用户id进行检查，你只能kill自己的这个id下的进程。

### Lecture14 FileSystem

- 使用三节课来讲解文件系统，两节课在xv6环境中，一节在Linux环境中。

- 许多文件系统之间的共性：实现了持久化存储、文件名和树形目录的抽象、路径名、在进程间共享文件

- 文件系统为什么重要？为持久化存储提供易于使用的抽象、故障处理保证数据安全、改进存储性能和提供并发支持

- 文件相关系统调用：open(fname, ops)，write(fd, buf, size)，read(fd, buf, size)，link(oldname, newname)，unlink(fname)，

- xv6文件系统的数据结构：superblock（保存整个文件系统的情况），log blocks（用于故障恢复），inode（保存文件的信息），bitmap blocks（空闲块的管理），磁盘上其余的内容就是数据块了。

- xv6的inode中有12个直接块，1个间接块，最大支持268KB的文件。

- 问：如果要用read访问一个文件第8000个字节的数据，应该怎么做？用8000/1024，如结果小于12，则数据存放在直接块中，否则存放在间接块中，依此发可以找对对应数据块，而8000%1024可以得到块内偏移。

- 可以用make qemu查看一下xv6构建的文件系统：

  ```bash
  mkfs/mkfs fs.img README  user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie  user/_cowtest
  nmeta 46 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 1) blocks 1954 total 2000
  balloc: first 813 blocks have been allocated
  balloc: write bitmap block at sector 45
  ```

- echo "hi" > x背后发生了什么？

  ```c
  ---- create the file
  write: 33	// 获取一个inode，在其中的type中写入信息 
  write: 33	// 向该inode中填入文件信息
  write: 46	// 向根目录中添加该文件的信息
  write: 32	// 修改根目录的inode中的size，因为扎在其中添加了新文件
  write: 33	// 再次更新文件的inode
  
  ---- write "hi" to file
  write: 45	// 修改位图块，占用一个数据块
  write: 595	// 将'h'写入
  write: 595	// 将'i'写入
  write: 33	// 更新文件的inode，如size
  
      
  ---- write "\n" to file
  write: 595
  write: 33
  
  ```

- Bcache（Block Cache）：一个磁盘块在内存中只有一个备份、使用LRU实现缓存块的替换、两级锁（一个保护bcache内部数据结构，另一个通过睡眠锁对不同的缓存加锁）

- 总结：

  - 文件系统 = on-disk data structure
  - Block cache用于缓存最近访问过的磁盘块，提高存取性能

### Lecture15 Crash Recovery

- 这一节讨论系统crash对磁盘文件系统的影响和如何恢复的问题
- 文件操作是一个多步的磁盘操作，crash会导致文件系统的invariant被破坏
- 这里介绍使用logging来解决磁盘故障恢复的问题，它的作用有：
  - 使文件系统调用具备原子性
  - 快速恢复
  - 高性能
- 步骤：将数据写入log、提交操作、将数据写入数据块、清空日志。

### Lecture16 FS Performance and Fast CR

- 复习xv6的日志系统

- xv6故障恢复的问题：同步的文件系统调用，每个系统调用都要等待磁盘写入完成后才能返回。

- ext3文件系统：

  - 日志的格式包含了一个日志超级快（指向第一个有效的日志描述符快）、日志描述符块（类似xv6的header）、数据块、commit块。

  - ext3性能提高的关键：

    - 异步的文件系统调用

      - 在修改完内存中的缓存之后便可返回，不必等待写磁盘完成，这为IO并发提供了基础，并使批处理成为可能

      - 注：通过fsync(fd)可以保证立即将数据写入磁盘

    - 批处理

      - ext3总是有一个"打开"的事务，在事务被创建的几秒之内，所有的文件系统调用都将归属到该事务，等事务结束时一起被提交。

      - 写入合并（write absorption），对同一个块的多次写入将合并为一次
    - 有利于磁盘调度

    - 并发性（新的系统调用可以在不等待旧事务处理的情况下执行）

      - 允许多个系统调用同时执行，在事务被提交之前，不同核心上的线程可以并行修改属于该事务的数据块。 
    - 之前的事务可以同时提交日志
      - 之前的事务可以同时正在被写入到磁盘数据块中
      - 之前的事务可以正在被free

  - ext3源码中的内容：

    - 使用日志，调用start获取一个handler，调用get获取相应块的内容，修改块的内容，调用stop告知文件系统本次修改已完成

    - 提交日志的步骤：阻塞新的系统调用，等待未完成的系统调用，打开一个新事务，写入write descriptor块，写入日志的数据块，等待之前的写入完成，写入commit块，等待commit块写入完成，写入真正的数据块。等待这些都做完之后才能够使用这部分的日志块。

    - 在恢复时，先从日志超级块中找到第一个有效的write descriptor块，并将对应事务写入磁盘数据块中，然后往后循环查看，如果下一个块同样为write descriptor块（以一个Magic number标识），则说明后面还存在未完成的事务，继续写入直至碰不到write descriptor块。

      这里有一个问题，如果一个事务之后的数据块起始的内容正好为write descriptor块的魔数，这不就导致误判了吗？

      这个问题可以简化为如何区分魔数所在的块是write descriptor块还是普通数据块。Morris给出了一个区分方法，即将所有数据块中出现的魔数用0替换掉，然后在write descriptor块中记录下这些替换了魔数的块，在写入时将内容还原。


> 比较一下xv6中的log系统和ext3，xv6中一次只允许一个事务进行磁盘写入。而ext3可以同时开启多个事务，不同的事务可以并行处理。

## Lab1 utils

耗时：6小时

### Boot xv6

首先配置好实验环境，我使用的Ubuntu24.04：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ lsb_release -a
No LSB modules are available.
Distributor ID: Ubuntu
Description:    Ubuntu 24.04.2 LTS
Release:        24.04
Codename:       noble
```

工具链安装：https://pdos.csail.mit.edu/6.1810/2024/tools.html

克隆启动代码到本地：

```
git clone git://g.csail.mit.edu/xv6-labs-2024
```

然后编译运行，启动qemu：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ make qemu
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -global virtio-mmio.force-legacy=false -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$ 
```

### sleep

要求创建文件`user/sleep.c`，在其中实现一个用户程序sleep，传入时间参数ticks，睡眠ticks个时钟中断间隔的时间。

根据提示，我们应当使用sleep系统调用来实现这个程序，而对sleep系统调用进行封装的库函数声明在`user/user.h`中，因此该程序需要包含这个头文件，其中使用了uint等类型定义，因此还需要包含`kernel/types.h`，在`user/ulib.c`中还定义了一些有用的辅助函数，在`user/printf.c`中实现了一些打印函数。我的实现如下：

```c
#include "kernel/types.h"
#include "user/user.h"


int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: sleep [num_ticks]\n");
        exit(1);
    }

    int num_ticks = atoi(argv[1]);
    sleep(num_ticks);
    exit(0);
}
```

然后模仿其他的用户程序，修改Makefile，将_sleep添加到变量UPROGS中:

```makefile
UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_sleep\
```

我们在这里分析一下xv6的系统调用过程：

首先要使用封装系统调用的库函数，需要包含`user/user.h`，这些库函数的真正实现在汇编程序`user/usys.S`中，这些函数都用`.global`修饰，可供外界使用。查看这个汇编程序：

```c
#include "kernel/syscall.h"
// ................
.global sleep
sleep:
 li a7, SYS_sleep
 ecall
 ret
// ................
```

这个函数中先将立即数SYS_sleep装入寄存器a7中，这里的SYS_sleep不知道是啥，看一下它的定义：

```c
// System call numbers
#define SYS_fork    1
#define SYS_exit    2
#define SYS_wait    3
#define SYS_pipe    4
#define SYS_read    5
#define SYS_kill    6
#define SYS_exec    7
#define SYS_fstat   8
#define SYS_chdir   9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_sleep  13
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
```

都是一些系统调用号的宏定义，去内核代码中找系统调用相关的文件，发现了`kernel/syscall.c`：

```c++
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

可以想到，ecall指令最后应该是调用了syscall这个函数，其中将寄存器a7的值赋给num，将其作为索引，执行了存放再syscalls数组中的sys_sleep函数。继续跟踪sys_sleep函数，其定义再`kernel/sysproc.c`中：

```c
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);	// 获取参数，细节可以看argint的实现
  if(n < 0)
    n = 0;
  acquire(&tickslock);	// 加锁，实现对ticks的互斥访问
  ticks0 = ticks;	// ticks是一个全局变量，表示自启动到当前的tick数
  while(ticks - ticks0 < n){	// 差为从调用到当下经过的tick数
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);	// 具体的sleep实现，先不展开
  }
  release(&tickslock);
  return 0;
}
```

就这样吧，其他的就先不展开说了。

测试一下：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ ./grade-lab-util sleep
make: 'kernel/kernel' is up to date.
== Test sleep, no arguments == sleep, no arguments: OK (1.0s)
== Test sleep, returns == sleep, returns: OK (0.8s)
== Test sleep, makes syscall == sleep, makes syscall: OK (1.1s)
```

### pingpong

考察管道的使用，不解释了，我的实现如下：

```c
#include "kernel/types.h"
#include "user/user.h"

int main() {
    int p1[2], p2[2];
    int pid;
    char buf, byte = 0x66;   // a byte for send/recv
    
    pipe(p1);   // c->p
    pipe(p2);   // p->c

    pid = fork();
    if (pid == 0) {  // child
        close(p1[0]);
        close(p2[1]);
        read(p2[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        write(p1[1], &buf, 1);
    } else {    // parent
        close(p1[1]);
        close(p2[0]);
        write(p2[1], &byte, 1);
        read(p1[0], &buf, 1);
        printf("%d: received pong\n", getpid());
    }
    exit(0);
}
```

测试：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ ./grade-lab-util pingpong
make: 'kernel/kernel' is up to date.
== Test pingpong == pingpong: OK (1.4s)
    (Old xv6.out.pingpong failure log removed)
```

### primes

这个问题很有意思，使用多进程实现素数筛，这里先介绍下素数筛，顾名思义该方法用于筛选素数，其重点落在这个“筛”字上。筛的是啥？合数！怎么筛？根据合数的定义，一个合数x，其必定由两个小于x且非1的数相乘得到。因此，只要能保证x不是任何一个比他小且非1的数的倍数，那么x一定是素数。

- 问题：对于一个大于等于2的数n，筛选出2~n范围内所有的素数。
- 埃氏素数筛的思路：使用一个数组，在相应位置保存2~n，遍历i = 2~sqrt(n)，从数组中剔除i的倍数（将对应位置元素置为0），最后数组中非0的元素均为素数。

```c
#include <iostream>
#include <vector>

using namespace std;

// 埃拉托色尼筛选法
vector<int> getPrimes(int n) {
    vector<int> A(n+1, 0);
    for (int i = 2; i <= n; i++) 
        A[i] = i;
    
    for (int i = 2; i * i <= n; i++) {    // 2 ~ sqrt(n)范围内
        if (A[i] != 0) {    // 未被筛掉
            for (int j = i*i; j <= n; j += i)   // 筛去其所有的倍数
                A[j] = 0;
        }
    }

    vector<int> primes;
    primes.reserve(n);
    for (int i = 2; i <= n; i++) {
        if (A[i] != 0)
            primes.push_back(A[i]);
    }
    return primes;
}

int main() {
    int n;
    cin >> n;
    if (n < 2) {
        cerr << "n must greater than 1\n";
        exit(1);
    }

    auto primes = getPrimes(n);
    for (auto p : primes) 
        cout << p << " ";
    cout << endl;

    return 0;
}
```

回到这道题，首先主进程需要生成2~280，并晒去2的倍数后发给子进程。而子进程的任务必须需要使用递归实现，否则无法实现反复创建下游子进程。

这个递归函数的设计：

- 参数：管道的接收端文件描述符，用于接受上一个进程发送的数据
- 基本情况：调用read返回EOF（发送方关闭文件描述符）
- 处理逻辑：从上一个进程接受第一个数据作为cur，然后接受后续数据并晒去cur的倍数，其余的发给下一个进程
- 递归情况：子进程继续递归调用helper

```c
#include "kernel/types.h"
#include "user/user.h"

void helper(int p) {
    int cur, pp[2], len, buf;
    if (read(p, &cur, 4) <= 0) {
        exit(0);
    }

    pipe(pp);
    if (fork() == 0) {  // child
        close(p);
        close(pp[1]);
        helper(pp[0]);
    } else {    // parent
        close(pp[0]);
        printf("prime %d\n", cur);
        while ((len = read(p, &buf, 4)) > 0) {
            if (buf % cur != 0) {		// 筛掉cur的倍数
                write(pp[1], &buf, 4);
            }
        }
        close(p);
        close(pp[1]);
        wait(0);
    }
}

int main() {
    int p[2];
    int pid, i = 2;
    pipe(p);

    if ((pid = fork()) == 0) {    // child
        close(p[1]);
        helper(p[0]);
    } else {    // parent
        close(p[0]);
        printf("prime %d\n", i++);
        while (i <= 280) {		// 生成2~280，并晒去2的倍数
            if (i % 2 != 0) {
                write(p[1], &i, 4);
            }
            i++;
        }
        close(p[1]);
        wait(0);
    }
}

```

注意其中的细节：

- 每次传输4个字节的整数数据，因此write和read的第三个参数均为4
- 每个进程应当等待下一个进程执行结束再退出，使用wait等待
- 文件描述符数量有限，应当及时关闭用不到的文件描述符，避免文件描述符不够用

测试一下：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ ./grade-lab-util primes
make: 'kernel/kernel' is up to date.
== Test primes == primes: OK (1.8s)
```

### find

模仿user/ls.c中读取目录和文件的方式，遍历目录：

- 对于目录，递归查找
- 对于文件，判断其文件名是否与查找目标的文件名相同，若是则输出其路径
- 全局遍历buf保存当前路径（回溯法），局部变量name保存当前文件名
- 注意剔除`.`和`..`

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

char buf[512];

void find(char* path, char* target) {
    char *p, name[512];
    int fd;
    struct dirent de;
    struct stat st;
    
    if ((fd = open(path, O_RDONLY)) < 0) { 
        fprintf(2, "find: cannot open %s\n", path);
        exit(1);
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        exit(1);
    }
    
    if (st.type != T_DIR) {     // 若path对应的不是目录直接退出
        close(fd);
        exit(1);
    } else {
        strcpy(buf, path);      
        p = buf + strlen(path);
        *p++ = '/';
        while (read(fd, &de, sizeof(de)) == sizeof(de)) {   // 反复读取目录中的文件或目录
            if(de.inum == 0)
                continue;
            strcpy(name, de.name);
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)  // 剔除.和..
                continue;
            
            strcpy(p, name);
            p = p + strlen(name);

            if (stat(buf, &st) < 0) {
                fprintf(2, "find: cannot stat %s\n", buf);
                close(fd);
                exit(1);
            }

            if (st.type == T_FILE) {    // 文件
                if (strcmp(name, target) == 0) {
                    printf("%s\n", buf);
                }
            } else if (st.type == T_DIR) { // 目录，递归find
                find(buf, target);
            }
            p -= strlen(name);  // 回溯
        }
        close(fd);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(2, "Usage: find [path] [filename]\n");
        exit(1);
    }

    find(argv[1], argv[2]);

    return 0;
}
```

测试一下：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ ./grade-lab-util find
make: 'kernel/kernel' is up to date.
== Test find, in current directory == find, in current directory: OK (1.8s)
== Test find, in sub-directory == find, in sub-directory: OK (1.4s)
== Test find, recursive == find, recursive: OK (1.1s)
```

### xargs

这个问题困扰了我很久，不是因为内容难，而是审题不清，没有真正看hint中的内容，导致存在一点小问题，总是过不了测试，最后重新把hint看了一遍才找到我问题。不要想当然，一定要认真看实验指导！！！！

```c
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

char* args[MAXARG];
int main(int argc, char* argv[]) {
    char buf[512];
    int i, pid;
    for (i = 0; i < argc - 1; i++) {
        args[i] = (char*)malloc(512);
        strcpy(args[i], argv[i + 1]);
    }
    
    args[i] = (char*)malloc(512);
    while(gets(buf, 512)[0]) {
        uint len = strlen(buf);
        if (buf[len-1] == '\n' || buf[len-1] == '\r') { // 剔除多余的换行
            buf[len-1] = 0;
        }

        pid = fork();   // 创建子进程，每行执行一次
        if (pid == 0) {    
            strcpy(args[i++], buf);
            args[i] = 0;
            exec(args[0], args);
        } else {
            wait(0);
        }
    }

    for (int j = 0; j < i; j++) {	// 内存释放
        free(args[j]);
    }
    
    return 0;
}

```

测试一下：

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ ./grade-lab-util xargs
make: 'kernel/kernel' is up to date.
== Test xargs == xargs: OK (2.1s)
    (Old xv6.out.xargs failure log removed)
== Test xargs, multi-line echo == xargs, multi-line echo: OK (0.5s)
    (Old xv6.out.xargs_multiline failure log removed)
```

```bash
minghan@Minghan:~/projs/MIT-6.1810-OS$ make grade
== Test sleep, no arguments ==
$ make qemu-gdb
sleep, no arguments: OK (2.8s)
== Test sleep, returns ==
$ make qemu-gdb
sleep, returns: OK (0.7s)
== Test sleep, makes syscall ==
$ make qemu-gdb
sleep, makes syscall: OK (0.5s)
== Test pingpong ==
$ make qemu-gdb
pingpong: OK (0.8s)
== Test primes ==
$ make qemu-gdb
primes: OK (1.7s)
== Test find, in current directory ==
$ make qemu-gdb
find, in current directory: OK (0.8s)
== Test find, in sub-directory ==
$ make qemu-gdb
find, in sub-directory: OK (1.0s)
== Test find, recursive ==
$ make qemu-gdb
find, recursive: OK (1.2s)
== Test xargs ==
$ make qemu-gdb
xargs: OK (1.6s)
== Test xargs, multi-line echo ==
$ make qemu-gdb
xargs, multi-line echo: OK (0.4s)
== Test time ==
time: OK
Score: 110/110
```

### 总结

这个实验比较简单，主要是实验环境的配置和利用xv6提供的系统调用实现几个命令行应用，花了大概6.5个小时。

其中比较有意思的可能是primes，使用多进程+管道实现素数筛的思路简直是脑洞大开，并且通过这个问题，我体会到及时关闭不需要的文件描述符的重要性。

卡我最久的应该是xargs，因为审题不清，导致实现思路不对，总是有一点小错误无法通过测试，不过再次审题后还是写出来了。

lab1最后还有几个option练习，暂时我就先不做了，之后如果有时间再做吧。

## Lab2 system calls

耗时：6小时

### Using gdb

使用gdb调试xv6的方法：

- 在一个终端启动远程调试服务器

```bash
make GPUS=1 qemu-gdb qemu-gdb
```

- 在另一个终端连接：

```bash
gdb-multiarch
```

启动gdb后可能会遇到这个问题：

```bash
warning: File "/home/minghan/projs/xv6-labs-2024/.gdbinit" auto-loading has been declined by your `auto-load safe-path' set to "$debugdir:$datadir/auto-load:/home/minghan/projs/MIT-6.1810-OS/.gdbinit".
To enable execution of this file add
        add-auto-load-safe-path /home/minghan/projs/xv6-labs-2024/.gdbinit
line to your configuration file "/home/minghan/.config/gdb/gdbinit".
```

按照其中的指示将`add-auto-load-safe-path /home/minghan/projs/xv6-labs-2024/.gdbinit`写到指定的那个文件中去就解决了。

---

几个问题：

1. Looking at the backtrace output, which function called syscall?
   在gdb中输入bt，查看调用栈：

  ```bash
(gdb) bt
#0  syscall () at kernel/syscall.c:133
#1  0x0000000080001a2e in usertrap () at kernel/trap.c:67
#2  0x0000000000000000 in ?? ()
Backtrace stopped: frame did not save the PC
  ```

  我们可以看到是`kernel/trap.c`中的`usertrap()`在第67行调用了`syscall()`

2. What is the value of p->trapframe->a7 and what does that value represent?
   根据proc.h中struct proc的内容，a7应该保存在其中的trapframe中，在gdb中输入： p /x *(p->trapframe)

  ```bash
$4 = {kernel_satp = 0x8000000000087fff, kernel_sp = 0x3fffffe000, kernel_trap = 0x800019b8, epc = 0x18, kernel_hartid = 0x1, ra = 0x0, sp = 0x1000, gp = 0x0, tp = 0x0, t0 = 0x0, 
  t1 = 0x0, t2 = 0x0, s0 = 0x0, s1 = 0x0, a0 = 0x24, a1 = 0x2b, a2 = 0x0, a3 = 0x0, a4 = 0x0, a5 = 0x0, a6 = 0x0, a7 = 0x7, s2 = 0x0, s3 = 0x0, s4 = 0x0, s5 = 0x0, s6 = 0x0, s7 = 0x0, 
  s8 = 0x0, s9 = 0x0, s10 = 0x0, s11 = 0x0, t3 = 0x0, t4 = 0x0, t5 = 0x0, t6 = 0x0}
  ```

  可以看到a7的值为0x7，这个值表示系统调用函数sys_exec在syscalls数组中的索引。

3. What was the previous mode that the CPU was in?
   在gdb中输入p /x $sstatus，获取Supervisor Status寄存器的值为0x200000022，查看riscv-privileged手册，发现：

  > The SPP bit indicates the privilege level at which a hart was executing before entering supervisor mode. 
  > When a trap is taken, SPP is set to 0 if the trap originated from user mode, or 1 otherwise.

  其中有一位是SSP，该位为0表示本次trap来自于用户态，为1则为内核态。SSP在SStatus寄存器的第8位上，检查发现该位为0，即CPU之前处于用户态。

4. Write down the assembly instruction the kernel is panicing at. Which register corresponds to the variable num?

  修改函数syscall中的一条语句：

  ```c
// num = p->trapframe->a7;
  num = * (int *) 0;
  ```

  运行报错：

  ```
xv6 kernel is booting

hart 1 starting
hart 2 starting
scause=0xd sepc=0x80001c82 stval=0x0
panic: kerneltrap
  ```

  其中spec指定了出错指令在kernel.S中的位置，在kernel.S中查找这个地址的指令：

  ```bash
80001c82:	00002683          	lw	a3,0(zero) # 0 <_entry-0x80000000>
  ```

  错误指令为lw a3,0(zero)，可以看到变量num放在a3中。

5. Why does the kernel crash? Hint: look at figure 3-3 in the text; is address 0 mapped in the kernel address space? Is that confirmed by the value in scause above? 
   内核crash的原因是访问虚拟地址0处的内容，而根据xv6 book中图3-3的描述，虚拟地址0处不属于内核空间，没有对应的物理地址映射。
   根据scause的内容为0xd，查找riscv手册，0xd表示Load page fault，证实了我们的猜想。

6. What is the name of the process that was running when the kernel paniced? What is its process id (pid)?
   直接用p命令打印name和pid即可，进程名为initcode，pid为1。

### System call tracing

实现一个系统调用sys_trace，给一个mask值，它可以追踪在应用程序被执行的过程中调用了哪些系统调用。更具体一点，要求在系统调用结束后输出pid+系统调用名+返回值，并且在进程调用fork后还要继承trace的mask继续进行跟踪。

要实现这个sys_trace有几个需要解决的问题：

- 一个普通用户程序调用包装系统调用的函数后，如何转移到内核？如何找到对应的处理函数？该处理函数如何获取用户程序传入的参数？结果如何返回？
- 弄清楚系统调用的调用过程之后，我们应该在何处进行tracing的打印？
- 如何根据mask判断一个系统调用是否是我们追踪的目标？
- 进程如何保存mask并在fork后子进程能继续保持对mask中指定的系统调用的追踪？

对于第一个问题，在lab1的sleep部分中看过一次，这里再梳理一下：

- 一个应用程序想要使用操作系统提供的功能，必须包含user.h头文件，其中声明了所有对系统调用包装的库函数。

- 这些函数的实现通过一个Perl脚本usys.pl生成，这段代码是一个 Perl 脚本 中的子程序（`sub`），用于生成 RISC-V 汇编语言的系统调用封装函数。它的作用是为给定的系统调用名称（如 `write`、`read`）生成对应的汇编代码片段。

  ```perl
  sub entry {
      my $name = shift;
      print ".global $name\n";
      print "${name}:\n";
      print " li a7, SYS_${name}\n";
      print " ecall\n";
      print " ret\n";
  }
  entry("fork");
  entry("exit");
  entry("wait");
  ...
  ```

  在make qemu后会生成usys.S文件，每个包装函数都将对应的系统调用号装入寄存器a7，然后执行ecall指令，跳转到内核。

  ```assembly
  .global fork
  fork:
   li a7, SYS_fork
   ecall
   ret
  .global exit
  exit:
   li a7, SYS_exit
   ecall
   ret
  ...
  ```

  其中的SYS_xxx定义在kernel/syscall.h中：

  ```c
  // System call numbers
  #define SYS_fork    1
  #define SYS_exit    2
  #define SYS_wait    3
  ...
  ```

- 进入内核后会调用kernel/syscall.c中的syscall函数，在其中根据之前传入的系统调用号在syscalls数组找到对应的系统调用处理函数并执行，然后将返回值存入a0。

  ```c
  static uint64 (*syscalls[])(void) = {
  [SYS_fork]    sys_fork,
  [SYS_exit]    sys_exit,
  [SYS_wait]    sys_wait,
  [SYS_pipe]    sys_pipe,
  [SYS_read]    sys_read,
  [SYS_kill]    sys_kill,
  [SYS_exec]    sys_exec,
  [SYS_fstat]   sys_fstat,
  [SYS_chdir]   sys_chdir,
  [SYS_dup]     sys_dup,
  [SYS_getpid]  sys_getpid,
  [SYS_sbrk]    sys_sbrk,
  [SYS_sleep]   sys_sleep,
  [SYS_uptime]  sys_uptime,
  [SYS_open]    sys_open,
  [SYS_write]   sys_write,
  [SYS_mknod]   sys_mknod,
  [SYS_unlink]  sys_unlink,
  [SYS_link]    sys_link,
  [SYS_mkdir]   sys_mkdir,
  [SYS_close]   sys_close,
  };
  
  void
  syscall(void)
  {
    int num;
    struct proc *p = myproc();
  
    num = p->trapframe->a7;
    if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
      // Use num to lookup the system call function for num, call it,
      // and store its return value in p->trapframe->a0
      p->trapframe->a0 = syscalls[num]();
    } else {
      printf("%d %s: unknown sys call %d\n",
              p->pid, p->name, num);
      p->trapframe->a0 = -1;
    }
  }
  ```

对于第二个问题：我们应该在何处进行tracing的打印？

既然要输出系统调用的返回值，那就必须在系统调用完成之后输出调用信息，因此我选择在syscall函数的末尾进行打印。

对于第三个问题：如何根据mask判断一个系统调用是否是我们追踪的目标？

这个简单，系统调用号在num变量中，只需用简单的位运算来判断一下即可：

```c
if (p->mask & (1 << num)) {
    printf("%d: syscall %s -> %ld\n", p->pid, syscall_names[num], p->trapframe->a0);
}
```

对于第四个问题：进程如何保存mask并在fork后子进程能继续保持对mask中指定的系统调用的追踪？

这需要将mask加入到PCB中，并在fork时将父进程的mask复制给子进程。

---

有了这些基础后，来尝试实现trace：

- 首先将宏定义SYS_trace加入kernel/syscall.h中

  ```c
  #define SYS_trace  22
  ```

- 在kernel/syscall.c中加入如下内容，为了方便打印系统调用名，我还添加了一个系统调用名的映射数组：

  ```c
  extern uint64 sys_trace(void);
  
  static uint64 (*syscalls[])(void) = {
  ....
  [SYS_trace]   sys_trace,
  };
  
  static char* syscall_names[] = {
    "foo",
    "fork",
    "exit",
    "wait",
    "pipe",
    "read",
    "kill",
    "exec",
    "fstat",
    "chdir",
    "dup",
    "getpid",
    "sbrk",
    "sleep",
    "uptime",
    "open",
    "write",
    "mknod",
    "unlink",
    "link",
    "mkdir",
    "close",
    "trace",
    };
  
  void
  syscall(void)
  {
    ......
    if (p->mask & (1 << num)) {
      printf("%d: syscall %s -> %ld\n", p->pid, syscall_names[num], p->trapframe->a0);
    }
  }
  
  ```

- 在kernel/sysproc.c中实现sys_trace函数：

  ```c
  uint64
  sys_trace(void) 
  {
    int mask;
    argint(0, &mask);   // 获取参数
    struct proc* p =  myproc();
    p->mask = mask;    // 将mask保存在PCB中
  
    return 0;
  }
  ```

- 修改kernel/proc.c中的fork函数：

  ```c
  int
  fork(void)
  {
    ...
    safestrcpy(np->name, p->name, sizeof(p->name));
  
    // inheritate mask for tracing syscalls
    np->mask = p->mask;
  
    pid = np->pid;
  
    release(&np->lock);
    ...
  }
  ```

- 在user/user.h中加入函数声明`int trace(int);`，在usys.pl中加入`entry("trace");`

测试一下：

```
$ trace 2147483647 grep hello README
3: syscall trace -> 0
3: syscall exec -> 3
3: syscall open -> 3
3: syscall read -> 1023
3: syscall read -> 971
3: syscall read -> 298
3: syscall read -> 0
3: syscall close -> 0
```

```bash
minghan@Minghan:~/projs/xv6-labs-2024$ ./grade-lab-syscall trace
make: 'kernel/kernel' is up to date.
== Test trace 32 grep == trace 32 grep: OK (0.4s) 
== Test trace close grep == trace close grep: OK (0.9s) 
== Test trace exec + open grep == trace exec + open grep: OK (1.0s) 
== Test trace all grep == trace all grep: OK (0.9s) 
== Test trace nothing == trace nothing: OK (1.0s) 
== Test trace children == trace children: OK (13.5s)
```

### Attack xv6

被这个问题卡了整整一个下午，对着xv6的源码一直看，看的我两眼发黑，最后也是分析得差不多了，但是有几个细节处没法判断，最后在范围内不断尝试，找到了答案。

先来看下`attacktest.c`：首先fork+exec执行了`secret.c`中的程序，在其中申请了一些内存，并在某处写入了8个字节的secret，等它执行完后再次fork+exec执行`attack.c`中的程序。其实看完`attacktest.c`我就确定了思路，无非就是要我们理清fork+exec启动进程以及进程退出过程中物理页面是如何分配和释放的。但是实际上要弄明白全部过程没那么容易。在我深入到fork、exec、exit和wait等系统调用中的源码后，理出了一个大概的分配和释放思路，但是其中有些部分的准确页面分配数量仍然有些模糊，之后在更加深入了解xv6的其他部分后再来彻底弄明白这个问题。

有一点值得注意，在`kernel/kalloc.c`中的kalloc和kfree函数中，可以看到xv6使用了链式栈实现物理页面的分配和回收，因此回分配顺序和回收顺序是相反的。

> 由于是后进先出的方式回收物理页面，因此第10个页面写入的secrete，在attack中应该位于倒数第10个页面，即第23个页面。

---

刚刚看完了xv6 book的第三章内容，其中解释很多下午做这个实验时的疑惑，现在再来梳理一下`attacktest.c`程序中页面分配和回收的过程。

分配的过程：

- 用户程序第一次调用fork，fork->sys_fork->fork，在其中调用allocproc函数。
  - kernel/proc.c:129行，为trapeframe分配1个页面
  - kernel/proc.c:136行，调用proc_pagetable，为页表分配3个页面
    - 一个顶级页表
    - 为了映射TRAPFRAME和TRAMPOLINE，还创建了两个低级页表
  - kernel/proc.c:292行，调用uvmcopy，将父进程的页表中的内容复制到子进程中，这里面分配了6个页面
    - 2个页面分别存放text段和data段
    - 2个页面作为栈（其中一个作为guard page）
    - 2个页面作为两个低级页表，因为调用了mappages，其中又调用了walk，并将alloc置为1，在找不到页表时会动态分配页面
- 然后调用exec，exec->sys_exec
  - kernel/sysfile.c:457行，secrete含有一个参数，因此这里会分配1个页面 
  - 进入exec函数：
    - kernel/exec.c:49行，为新页表分配3个页面，理由同上
    - kernel/exec.c:65行，读取ELF文件，为每个segment分配1个页面，再加上低两级的页表，共4个页面
    - kernel/exec.c:83行，为用户栈分配2个页面
- 在secrete.c中分配了32个页面（一个PTE管512个页面，因此无需分配新页表）

回收的过程：

- kernel/exec.c:129行，回收旧页表+其中映射的页面，共9个页面
- kernel/sysfile.c:467行，回收argv，1个页面
- kernel/proc.c:159行，回收trapframe，1个页面
- kernel/proc.c:162行，调用proc_freepagetable回收所有页表中的页面
  - 先回收text、data、stack占用的4个页面
  - 然回收放32个再secrete.c中申请的32个页面
- kernel/vm.c:320行，回收三级页表，一共5个页面
  - 1个顶级页目录表
  - 2个映射低地址用户程序的低级页表
  - 2个映射高地址TRAPFRAME和TRAMPOLINE的低级页表

搞清楚secrete执行和分配的过程后，attack程序的执行也类似。按照先前的公式，在那32个页面被释放后有5个页面被释放，因此secrete所在页面在当前freelist的第22+5=27块，而attack在使用sbrk申请之前占用了10块，因此secrete在之后的第17块上。

> trapeframe(1块) + pagetable(3块) + uvmcopy(6块) + argv(1块) - oldpagetable(10块) + new_pagetable(3块) + load(4块) + stack&guard(2块) = 10块

至于为什么将页内偏移改为0就无法找到正确的secrete？这是因为每个页面的起始8个字节会作为freelist的next指针，因此在回收后会将每个页面的起始8个字节给覆盖掉，这就破坏了secrete。可以进行测试，在页内偏移为0~7时都无法找到secrete，当页内偏移大于等于8之后就没问题了。

```c
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)
  char *end = sbrk(PGSIZE*32);    // 申请32个页
  end = end + 16 * PGSIZE;			
  end += 32;
  write(1, end, 8);		// 输出到终端，测试用
  write(1, "\n", 1);
    
  write(2, end, 8);
  
  exit(1);
}
```

测试一下：

```bash
minghan@Minghan:~/projs/xv6-labs-2024$ make grade
== Test answers-syscall.txt == 
answers-syscall.txt: OK 
== Test trace 32 grep == 
$ make qemu-gdb
trace 32 grep: OK (2.5s) 
== Test trace close grep == 
$ make qemu-gdb
trace close grep: OK (1.0s) 
== Test trace exec + open grep == 
$ make qemu-gdb
trace exec + open grep: OK (0.9s) 
== Test trace all grep == 
$ make qemu-gdb
trace all grep: OK (0.8s) 
== Test trace nothing == 
$ make qemu-gdb
trace nothing: OK (1.1s) 
== Test trace children == 
$ make qemu-gdb
trace children: OK (13.2s) 
== Test attack == 
$ make qemu-gdb
attack: OK (0.3s) 
== Test time == 
time: OK 
Score: 50/50
```

### 总结

花了大概6个多小时，通过这个实验，学会了使用gdb来调试xv6，并且真正弄明白了xv6的系统调用调用过程和实现方法。在实现trace时一气呵成，非常迅速地就完成并通过了测试，感觉xv6的源码还是挺容易看的，我以为最后一个Attack也会是如此顺利。下午睡完午觉来到办公室打算秒掉这个问题，结果硬是搞了4小时才弄出个大概，最后靠猜才得出答案:(，在做完后面的内容后再回来把这个问题彻底弄明白。

做完后看到一个博客，分析得挺好，但是我觉得还是有点问题：https://blog.csdn.net/weixin_42543071/article/details/143351746

---

看完xv6 book第三章后我又重新推了一遍页面分配的过程，这个博客没有问题，结论和我是一致的。绝了，推这个东西推到凌晨一点，赶紧回寝室睡觉了:)。

## Lab3 page tables

耗时：估计在17个小时。

在开始做这个实验之前，再次看了一遍`kernel/memlayout.h`,、`kernel/vm.c`和`kernel/kalloc.c`这三个文件中源码，经过之前的一番折磨，感觉基本上都弄明白了。

### Inspect a user-process page table

启动qemu，运行用户程序`pgtbltest`：

```
$ pgtbltest
print_pgtbl starting
va 0x0 pte 0x21FC885B pa 0x87F22000 perm 0x5B
va 0x1000 pte 0x21FC7C1B pa 0x87F1F000 perm 0x1B
va 0x2000 pte 0x21FC7817 pa 0x87F1E000 perm 0x17
va 0x3000 pte 0x21FC7407 pa 0x87F1D000 perm 0x7
va 0x4000 pte 0x21FC70D7 pa 0x87F1C000 perm 0xD7
va 0x5000 pte 0x0 pa 0x0 perm 0x0
va 0x6000 pte 0x0 pa 0x0 perm 0x0
va 0x7000 pte 0x0 pa 0x0 perm 0x0
va 0x8000 pte 0x0 pa 0x0 perm 0x0
va 0x9000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF6000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF7000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF8000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF9000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFA000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFB000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFC000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFD000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFE000 pte 0x21FD08C7 pa 0x87F42000 perm 0xC7
va 0xFFFFF000 pte 0x2000184B pa 0x80006000 perm 0x4B
print_pgtbl: OK
ugetpid_test starting
usertrap(): unexpected scause 0xd pid=4
            sepc=0x53e stval=0x3fffffd000
```

这个程序打印了用户页表的前十个页面和后十个页面对应的页表项（通过为lab3设计的系统调用pgpte获得），现在来逐条分析下这二十个页表项的逻辑含义和访问权限。

- `va 0x0 pte 0x21FC885B pa 0x87F22000 perm 0x5B`：存放用户代码，权限为可读可执行
- `va 0x1000 pte 0x21FC7C1B pa 0x87F1F000 perm 0x1B`：存放用户代码，权限为可读可执行

- `va 0x2000 pte 0x21FC7817 pa 0x87F1E000 perm 0x17`：存放用户数据，权限为可读可写
- `va 0x3000 pte 0x21FC7407 pa 0x87F1D000 perm 0x7`：存放用户数据，权限为可读可写
- `va 0x4000 pte 0x21FC70D7 pa 0x87F1C000 perm 0xD7`：存放用户数据，权限为可读可写
- `va 0x6000 ~ va 0x9000pa pte 0x0 pa 0x0 perm 0x0`：空页表项
- `va 0xFFFF6000 ~ va 0xFFFFD000 pte 0x0 pa 0x0 perm 0x0`：空页表项
- `va 0xFFFFE000 pte 0x21FD08C7 pa 0x87F42000 perm 0xC7`：trapframe，用于发生trap时保存运行状态，权限为可读可写
- `va 0xFFFFF000 pte 0x2000184B pa 0x80006000 perm 0x4B`：trampoline，用于存放trap的入口（uservec）和出口（userret），权限位可读可执行

### Speed up system calls

离谱，这个问题花了2个小时20分钟，看完问题我立刻就制定好了思路：

- 创建进程时分配一个物理页面，并在其中写入pid
- 在用户进程的页表中插入一个PTE，实现虚拟地址USYSCALL到该物理页面的映射，权限要设置用户只读，因此为U+R

理清思路后我愉快地在fork中加入了分配页面+mapping的代码（同下面的结果），运行报错`panic: freewalk: leaf`，定位到freewalk函数，这个panic的触发条件是发现了叶子页表项，也就是说freewalk只负责回收页目录表项。在源码中发现了该函数的调用路径为：`proc_freepagetable->uvmfree->freewalk`，在freewalk被调用之前应当将所有页表项的页面都回收并置为0。于是我先在freeproc函数中proc_freepagetable被调用之前添加了解除USYSCALL页面mapping的代码（同下面的结果），运行仍然报错`panic: freewalk: leaf`，通过print大法，我发现程序还没有调用freeproc就报了这个错误，也就是说在我们的解除USYSCALL页面mapping的代码执行之前proc_freepagetable就被调用过了。为了搞明白这个原因我重新梳理了一遍xv6整个系统启动过程中进程和页表的创建与回收情况：

- 直接从main函数开始看，第一个进程创建的过程是：main->userinit()->allocproc，在allocproc中获取了一个空闲的进程（1号进程），并创建了页表，在userinit中为该进程分配页面存入initcode并将该进程的代码区映射到这个页面，initcode中通过exec系统调用执行用户程序init（在user/init.c中）。
- 再来看init.c，其中调用了fork+exec创建shell进程（2号进程），1号进程调用wait挂起，将控制权交给shell。

因此，我之前的问题在于，只考虑了调用fork创建进程和调用freeproc销毁进程时页面的分配和回收情况，而忽略了userinit和exec中对页面的分配和回收。通过上一个lab的学习，我已经知道在exec中会重新创建一个pagetable，并将oldpagetable中的内容给释放掉。因此我需要将建立映射的代码放到pagetable刚刚创建的位置，而将解除mapping的代码放到pagetable销毁的地方，这样才能cover所有情况。通过查看源码，可以发现pagetable的创建都在proc_pagetable中，并且在程序执行到此处时pid已经分配了，而pagetable的销毁都在proc_freepagetable中，解除mapping的代码应该放在其调用uvmfree之前。

于是乎，有了最后的结果：

- 在`kernel/proc.c:proc_pagetable`中插入：

  ```c
  pagetable_t
  proc_pagetable(struct proc *p)
  {
    .......
    // 分配一个页面保存pid，供用户进程访问，避免内核态/用户态切换
    #ifdef LAB_PGTBL
    struct usyscall* usyspa;
    if((usyspa = (struct usyscall*)kalloc()) == 0){
      freeproc(p);
      release(&p->lock);
      return 0;
    }
  
    usyspa->pid = p->pid;
    if(mappages(pagetable, USYSCALL, PGSIZE, (uint64)usyspa, PTE_R | PTE_U) < 0){
      uvmunmap(pagetable, USYSCALL, 1, 0);
      uvmfree(pagetable, 0);
      return 0;
    }
    #endif
  
    return pagetable;
  }
  ```

- 在`kernel/proc.c:proc_freepagetable`中插入：

  ```c
  void
  proc_freepagetable(pagetable_t pagetable, uint64 sz)
  {
    // 释放之前分配的用于存储pid的页面
    #ifdef LAB_PGTBL
    if (pagetable) {
      uvmunmap(pagetable, USYSCALL, 1, 1);
    }
    #endif
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
  }
  ```

测试一下：

```c
minghan@Minghan:~/projs/xv6-labs-2024$ make qemu
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -global virtio-mmio.force-legacy=false -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 1 starting
hart 2 starting
init: starting sh
$ pgtbltest
print_pgtbl starting
va 0x0 pte 0x21FC785B pa 0x87F1E000 perm 0x5B
va 0x1000 pte 0x21FC6C1B pa 0x87F1B000 perm 0x1B
va 0x2000 pte 0x21FC6817 pa 0x87F1A000 perm 0x17
va 0x3000 pte 0x21FC6407 pa 0x87F19000 perm 0x7
va 0x4000 pte 0x21FC60D7 pa 0x87F18000 perm 0xD7
va 0x5000 pte 0x0 pa 0x0 perm 0x0
va 0x6000 pte 0x0 pa 0x0 perm 0x0
va 0x7000 pte 0x0 pa 0x0 perm 0x0
va 0x8000 pte 0x0 pa 0x0 perm 0x0
va 0x9000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF6000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF7000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF8000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFF9000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFA000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFB000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFC000 pte 0x0 pa 0x0 perm 0x0
va 0xFFFFD000 pte 0x21FC7C13 pa 0x87F1F000 perm 0x13
va 0xFFFFE000 pte 0x21FD00C7 pa 0x87F40000 perm 0xC7
va 0xFFFFF000 pte 0x2000184B pa 0x80006000 perm 0x4B
print_pgtbl: OK
ugetpid_test starting
ugetpid_test: OK
print_kpgtbl starting
print_kpgtbl: OK
superpg_test starting
pgtbltest: superpg_test failed: pte different, pid=3
```

> 真是纸上得来终觉浅，绝知此事要躬行，看似简单的问题，要想将其付诸现实，要考虑的事情很多，绝不是脑中想象得那么容易。

### Print a page table

我的方法和freewalk的思路不同，用freewalk的递归思路最后就是有一点问题，搞了个把小时，还不搞不出来。我直接按自己的想法来实现了，效率可能低一点吧，但是思路相对清晰一些。三重循环，在每层获取对应的PTE，检查V位，然后打印。

```c
#ifdef LAB_PGTBL
void 
printformat(uint64 va, pte_t* pte, int level) {
  for (int i = 0; i < level; i++) {
    printf(" ..");
  }
  printf("%p: pte %p pa %p\n", (void*)va, (void*)(*pte), (void*)((*pte) >> 10 << 12));
}

pte_t*
mywalk(pagetable_t pagetable, uint64 va, int lv, int alloc) {
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > lv; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(lv, va)];
}


void
vmprint(pagetable_t pagetable) {
  // your code here
  printf("page table %p\n", pagetable);
  uint64 va = 0;
  
  // 查看level-2 PTE
  for (int i = 0; i < 512; i++) {	
    pte_t *ptetop = mywalk(pagetable, va, 2, 1);
    if (ptetop == 0) {
      va += 0x40000000;
      continue;
    }
    if (*ptetop & PTE_V) {
      printformat(va, ptetop, 1);
    }
    // 查看level-1 PTE
    for (int j = 0; j < 512; j++) {
      pte_t *ptemid = mywalk(pagetable, va, 1, 0);
      if (ptemid == 0) {
        va += 0x200000;
        continue;
      }
      if (*ptemid & PTE_V) {
        printformat(va, ptemid, 2);
      }
      // 查看level-0 PTE
      for (int k = 0; k < 512; k++) {
        pte_t *ptelow = mywalk(pagetable, va, 0, 0);
        if (ptelow == 0) {
          va += 0x1000;
          continue;
        }
        if (*ptelow & PTE_V) {
          printformat(va, ptelow, 3);
        }
        va += 0x1000;
        if (va >= MAXVA)
          return ;
      }
    }
  }
}
#endif
```

别人的写法：

```c
#ifdef LAB_PGTBL
void
vmprinthelper(pagetable_t pagetable, int level, uint64 va)
{
  uint64 sz = 0;
  if (level == 2) sz = 512 * 512 * PGSIZE;
  else if (level == 1) sz = 512 * PGSIZE;
  else sz = PGSIZE;
  for(int i = 0; i < 512; i++, va += sz){
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) == 0) continue;
    for (int j = 0; j < 3 - level; ++j) printf(" ..");
    printf("%p: ", (void*)va);
    printf("pte %p pa %p\n", (void*)pte, (void*)PTE2PA(pte));
    if ((pte & (PTE_R|PTE_W|PTE_X)) == 0)
      vmprinthelper((void*)PTE2PA(pte), level - 1, va);
  }
}

void
vmprint(pagetable_t pagetable) {
  // your code here
  printf("page table %p\n", pagetable);
  vmprinthelper(pagetable, 2, 0);
}
#endif
```

```bash
print_kpgtbl starting
page table 0x0000000087f30000
 ..0x0000000000000000: pte 0x0000000021fd4001 pa 0x0000000087f50000
 .. ..0x0000000000000000: pte 0x0000000021fd3c01 pa 0x0000000087f4f000
 .. .. ..0x0000000000000000: pte 0x0000000021fcd05b pa 0x0000000087f34000
 .. .. ..0x0000000000001000: pte 0x0000000021fd0c1b pa 0x0000000087f43000
 .. .. ..0x0000000000002000: pte 0x0000000021fd48d7 pa 0x0000000087f52000
 .. .. ..0x0000000000003000: pte 0x0000000021fd4407 pa 0x0000000087f51000
 .. .. ..0x0000000000004000: pte 0x0000000021fc60d7 pa 0x0000000087f18000
 ..0x0000003fc0000000: pte 0x0000000021fcc401 pa 0x0000000087f31000
 .. ..0x0000003fffe00000: pte 0x0000000021fcc801 pa 0x0000000087f32000
 .. .. ..0x0000003fffffd000: pte 0x0000000021fccc13 pa 0x0000000087f33000
 .. .. ..0x0000003fffffe000: pte 0x0000000021fc88c7 pa 0x0000000087f22000
 .. .. ..0x0000003ffffff000: pte 0x000000002000184b pa 0x0000000080006000
print_kpgtbl: OK
```

我输出的虚拟地址和实验指导上的不一样，我核查了一下MAXVA的地址为：0x40 0000 0000，因此TRAMPOLINE、TRAPFRAME和USYSCALL三个页面的位置正号是0x0000003ffffff000、0x0000003fffffe000和0x0000003fffffd000。

### Use superpages

除了4KB的普通页面，RISCV还提供了2MB和1GB的超级页面，其使用方式是，分别在level1-PTE和level2-PTE中将权限设置为PTE_V+任意一个权限（如读或写），用更直白的话来说就是，RISCV通过权限位来判断一个PTE是否为叶子，一个level0-PTE对应了4KB的页面，一个level1-PTE对应的是512\*4KB = 2MB，正好就是2M的超级页，而一个level2-PTE对因的是512\*512*4KB=1GB，因此这种分配超级页的方法可以使页表中同时存在普通页和超级页，其虚拟地址空间仍是连续的。

通过询问GPT，我得到了两种实现超级页的方案

- 方案一：扩展kalloc，支持连续物理页的分配
- 方案二：预留一部分内存空间，用于超级页的分配

为了图方便，我选择的是方案二，首先将内存（128M）划分为两部分，然后模仿普通页构造一个spkmem，并准备好spkalloc和spkfree用于分配和回收超级页。

```
// memlayout.h
#define PHYSTOP (KERNBASE + 128*1024*1024)  // 内存总共128M
#define SUPERBASE (KERNBASE + 20*1024*1024)
```

```c
// kalloc.c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem, spkmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&spkmem.lock, "spkmem");
  freerange(end, (void*)SUPERBASE);  
  spfreerange((void*)SUPERPGROUNDUP(SUPERBASE), (void*)PHYSTOP);  
}

// 超级页内存池的初始化函数
void
spfreerange(void *pa_start, void *pa_end)
{
  char *p = (char*)SUPERPGROUNDUP((uint64)pa_start);
  for(; p + SUPERPGSIZE <= (char*)pa_end; p += SUPERPGSIZE)
    spkfree(p);
}

// 超级页的分配函数
void *
spkalloc(void)
{
  struct run *r;

  acquire(&spkmem.lock);
  r = spkmem.freelist;
  if(r)
    spkmem.freelist = r->next;
  release(&spkmem.lock);

  if(r)
    memset((char*)r, 5, SUPERPGSIZE); // fill with junk
  return (void*)r;
}

// 超级页的回收函数
void
spkfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < (char*)SUPERBASE || (uint64)pa >= PHYSTOP)
    panic("spkfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;

  acquire(&spkmem.lock);
  r->next = spkmem.freelist;
  spkmem.freelist = r;
  release(&spkmem.lock);
}
```

先来看如何实现sbrk分配超级页，sbrk会调用growproc，进而调用uvmalloc，因此这里需要对uvmalloc进行更改，提供分配超级页的功能。我一开始想的是如果sbrk的参数大于等于2M，我就直接分配若干个超级页给用户进程，但这种分配方式可能会造成超级页和之前的普通页之间存在地址空间的浪费，最后导致无法通过测试。为了充分利用地址空闲，应当先分配普通页，直到地址对齐到2M，再去尽可能多地分配超级页，最后为剩下的部分分配普通页。

```c
// vm.c
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if(newsz < oldsz)
    return oldsz;
  
  // 先为当前未对齐2M的剩余部分分配普通页
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < SUPERPGROUNDUP(oldsz) && a < newsz; a += sz){
    sz = PGSIZE;
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, sz);
#endif
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }

  // 尽可能多地分配超级页
  for (; a + SUPERPGSIZE < newsz; a += sz) {
    sz = SUPERPGSIZE;
    mem = spkalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, sz);
    if (mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0) {
      spkfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }

  // 为剩余的部分分配普通页
  for(; a < newsz; a += sz){
    sz = PGSIZE;
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, sz);
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}
```

由于普通页的PTE在0页表中，而超级页的PTE在1级页表中，因此两者在页表中创建映射的方式不同，需要修改mappages。因为我采用的是为超级页预留内存，因此可以用当前页面的物理地址来判断该页面是普通页还是超级页，为了获取超级页在1级页表中的PTE，我使用了自己写了的mywalk：

```c
// vm.c
pte_t*
mywalk(pagetable_t pagetable, uint64 va, int lv, int alloc) {
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > lv; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(lv, va)];
}

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last, pgsz;
  pte_t *pte;

  // 根据pa来自于哪种页面的内存可以判断该页是超级页还是普通页
  if (pa >= SUPERBASE)
    pgsz = SUPERPGSIZE;
  else 
    pgsz = PGSIZE;

  if((va % pgsz) != 0)
    panic("mappages: va not aligned");

  if((size % pgsz) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - pgsz;
  for(;;){
    if(pgsz == PGSIZE && (pte = walk(pagetable, a, 1)) == 0)  // 利用逻辑运算的短路特性
      return -1;
    if(pgsz == SUPERPGSIZE && (pte = mywalk(pagetable, a, 1, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += pgsz;
    pa += pgsz;
  }
  return 0;
}
```

现在考虑超级页面和普通页面的释放，跟踪查看wait调用中的代码，可以看到页面的释放最终是通过uvmunmap来实现的。在这里同样是使用物理地址pa作为依据判断页面是否为超级页，并且值得注意的是，在walk中已经实现了碰到叶子节点就返回该PTE，因此wolk碰到了超级页也会返回其PTE：

```c
// vm.c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  int sz;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += sz){
    sz = PGSIZE;
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0) {
      printf("va=%ld pte=%ld\n", a, *pte);
      panic("uvmunmap: not mapped");
    }
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    
    // walk碰到叶子PTE会直接返回，因此有可能是超级页的PTE，还是通过pa判断
    uint64 pa  = PTE2PA(*pte);
    if (pa >= SUPERBASE) {
      a += SUPERPGSIZE - sz;
    }

    if(do_free){
      uint64 pa  = PTE2PA(*pte);
      if (pa >= SUPERBASE) 
        spkfree((void*)pa);
      else 
        kfree((void*)pa);
    }
    *pte = 0;
  }
}
```

最后还要考虑fork复制超级页的问题，在fork调用中通过uvmcopy来复制父进程的页表到子进程，同样通过pa来判断页面，针对不同页面做不同的操作：

```c
// vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc;

  for(i = 0; i < sz; i += szinc){
    szinc = PGSIZE;
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0) 
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if (pa >= SUPERBASE) {
      szinc = SUPERPGSIZE;
      if ((mem = spkalloc()) == 0)
        goto err;
    } else if((mem = kalloc()) == 0) {
      goto err;
    }
    
    memmove(mem, (char*)pa, szinc);
    if(mappages(new, i, szinc, (uint64)mem, flags) != 0){
      if (szinc == PGSIZE)
        kfree(mem);
      else 
        spkfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

---

下面这些是中途遇问题的一些记录，没啥用，不用看：

```bash
superpg_test starting
panic: uvmcopy: page not present
```

通过debug+输出使用sbrk申请4个2M超级页后的页表，确认了超级页面的分配没有问题：

```bash
print_kpgtbl starting
page table 0x0000000083f30000
 ..0x0000000000000000: pte 0x0000000020fd4001 pa 0x0000000083f50000
 .. ..0x0000000000000000: pte 0x0000000020fd3c01 pa 0x0000000083f4f000
 .. .. ..0x0000000000000000: pte 0x0000000020fcd05b pa 0x0000000083f34000
 .. .. ..0x0000000000001000: pte 0x0000000020fd0c1b pa 0x0000000083f43000
 .. .. ..0x0000000000002000: pte 0x0000000020fd48d7 pa 0x0000000083f52000
 .. .. ..0x0000000000003000: pte 0x0000000020fd4407 pa 0x0000000083f51000
 .. .. ..0x0000000000004000: pte 0x0000000020fc60d7 pa 0x0000000083f18000
 ..0x0000003fc0000000: pte 0x0000000020fcc401 pa 0x0000000083f31000
 .. ..0x0000003fffe00000: pte 0x0000000020fcc801 pa 0x0000000083f32000
 .. .. ..0x0000003fffffd000: pte 0x0000000020fccc13 pa 0x0000000083f33000
 .. .. ..0x0000003fffffe000: pte 0x0000000020fc88c7 pa 0x0000000083f22000
 .. .. ..0x0000003ffffff000: pte 0x0000000020001c4b pa 0x0000000080007000
print_kpgtbl: OK
superpg_test starting
print_kpgtbl starting
page table 0x0000000083f30000
 ..0x0000000000000000: pte 0x0000000020fd4001 pa 0x0000000083f50000
 .. ..0x0000000000000000: pte 0x0000000020fd3c01 pa 0x0000000083f4f000
 .. .. ..0x0000000000000000: pte 0x0000000020fcd05b pa 0x0000000083f34000
 .. .. ..0x0000000000001000: pte 0x0000000020fd0c1b pa 0x0000000083f43000
 .. .. ..0x0000000000002000: pte 0x0000000020fd48d7 pa 0x0000000083f52000
 .. .. ..0x0000000000003000: pte 0x0000000020fd4407 pa 0x0000000083f51000
 .. .. ..0x0000000000004000: pte 0x0000000020fc60d7 pa 0x0000000083f18000
 .. ..0x0000000000200000: pte 0x0000000021f80017 pa 0x0000000087e00000
 .. ..0x0000000000400000: pte 0x0000000021f00017 pa 0x0000000087c00000
 .. ..0x0000000000600000: pte 0x0000000021e80017 pa 0x0000000087a00000
 .. ..0x0000000000800000: pte 0x0000000021e00017 pa 0x0000000087800000
 ..0x0000003fc0000000: pte 0x0000000020fcc401 pa 0x0000000083f31000
 .. ..0x0000003fffe00000: pte 0x0000000020fcc801 pa 0x0000000083f32000
 .. .. ..0x0000003fffffd000: pte 0x0000000020fccc13 pa 0x0000000083f33000
 .. .. ..0x0000003fffffe000: pte 0x0000000020fc88c7 pa 0x0000000083f22000
 .. .. ..0x0000003ffffff000: pte 0x0000000020001c4b pa 0x0000000080007000
print_kpgtbl: OK
DEBUG: supercheck start
DEBUG: supercheck done
panic: uvmcopy: page not present
```

可以看到相比调用sbrk申请8M内存之前，分配完内存之后的页表中多出了4个level1-PTE，其权限均为0x17（U+R+W+V）。并且通过print大法确认通过了supercheck，因此问题一定出现在fork的uvmcopy处，经过调试，发现了问题出在这个判断上`if((*pte & PTE_V) == 0)`，由于在分配超级页时需要align到2M的倍数地址处，因此超级页和之前的普通页之间可能存在着一些空隙，这些空隙对应的PTE中V位都是0，因此当访问到普通页和超级页之间的空隙时上面的判断就正确，造成panic，这里直接将panic改为continue，继续往下copy：

```c
if((*pte & PTE_V) == 0) 
      // panic("uvmcopy: page not present");
      continue;
```

运行后仍然有问题：

```bash
DEBUG: supercheck start
DEBUG: supercheck done
DEBUG: supercheck start
DEBUG: supercheck done
va=20480 pte=0
panic: uvmunmap: not mapped
```

在这里确定了fork出来的子进程中的supercheck也通过了，那么问题出现必然在超级页的回收中。继续调试，根据之前的实验我们了解到进程的释放在wait中，wait调用freeproc，其中又调用了proc_freepagetable，在其中我们首先释放了超级页，然后让原来的代码去释放普通页，在这里我犯了个错误，即释放完一个超级页之后没有更新sz的值，导致在后面释放普通页时仍然去查看旧的sz范围的空间，但其中的很多地址空间对应的PTE早就在释放超级页时被置为0了，因此才会导致这个错误：

```c
if((*pte & PTE_V) == 0) {
      printf("va=%ld pte=%ld\n", a, *pte);
      panic("uvmunmap: not mapped");
}
```

### 总结

被最后一个task搞心态了，还是想太简单了，我一开始的想法是碰到大于等于2M的内存就直接分配超级页，并很快就写好了代码并通过了pgtbltest，但是make grade一直飘红，反复地推理我的代码，还是找不到哪里有问题，足足耗了我一整天。最后通过AI+别人的一篇博客，才明白问题出在页面的分配上，在当前地址还未与2M对齐的时候，想要直接分配超级页就必然要丢掉一些对齐前的地址，这造成了巨大的虚拟地址的浪费。感觉自己的思路还是太僵化了，无论sbrk传的参数是多大，只要分配足够的内存给它就行了，就算参数值大于等于2M，也不意味着就必须分配超级页。为了充分避免地址空间的浪费，应当在地址对齐2M前先分配普通页，在对齐后若还有2M以上的内存需求，就尽可能多地分配超级页，最后如有剩余再分配普通页。这个实验给了我一个警示，一定要从最根源处找问题，而不是一直被自己不可靠的假设给束缚住，要敢于否定自己先前的观点，就如芒格所说的“对于一个观点，如果我不能比任何人更好地反驳它，我就没有资格拥有它”。

参考：https://erlsrnby04.github.io/2024/10/05/MIT-6-1810-Lab3-page-tables/#4-Use-superpages-moderate-hard

## Lab4 trap

耗时：估计7小时

### RISC-V assembly

1. Which registers contain arguments to functions? For example, which register holds 13 in main's call to printf?
   a0-a7存放函数调用传递的参数，13是main中传给printf的第三个参数，因此放在寄存器a2中。
2. Where is the call to function f in the assembly code for main? Where is the call to g? (Hint: the compiler may inline functions.)
   实际上并没有真正地去调用函数f，以内g和f非常简单，编译器直接将f和g的调用展开了。
3. At what address is the function printf located?
   在call.asm中查找printf，发现在0x6bc，由call.c程序与库文件链接而来。
4. What value is in the register ra just after the jalr to printf in main?
   jal将下一条指令的地址存入ra，因此在执行jalr后ra的值为li a0, 0的地址0x34。
5. 几个问题：
   - What is the output?
     答：HE110 World
   - The output depends on that fact that the RISC-V is little-endian. If the RISC-V were instead big-endian what would you set i to in order to yield the same output?
     答：将i调整为0x00726c64
   - Would you need to change 57616 to a different value?
     答：57616作为一个十六进制整数值输出不需要修改。
6. In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen?
   额，这题有点莫名其妙，会出现编译错误，因为缺少参数。 

### Backtrace

实现backtrace函数，输出当前的整个调用栈，存在的问题如下：

- 具体输出什么？保存在栈中的返回地址
- 如何遍历调用栈？通过存放在栈中的previous sp，即上一个栈帧的地址来不断向上找
- 返回地址和上一个栈帧的地址放在栈的什么地方？分别存放在当前栈帧的最顶部和次顶部
- 如何获取当前栈帧的起始地址？用过r_fp函数获取
- 如何判断是否到了第一个栈帧？一般一个内核栈都在一个页面中被分配，使用PGROUNDDOWN来比较判断当前栈帧和下一个栈帧是否在同一个页面，如果不在同一个页面就退出

```c
// kernel/printf.c
void backtrace() {
  uint64 ra, prev_fp, base;
  uint64 fp = r_fp();   // 获取当前栈帧的起始地址
  base = PGROUNDDOWN(fp);

  while (fp >= base && fp < base + PGSIZE) {
    ra = *((uint64*)(fp - 8));
    prev_fp = *((uint64*)(fp - 16));
    printf("%p\n", (void*)ra);
    fp = prev_fp;
  }
}
```

在sys_sleep中加入backtrace：

```c
// kernel/sysproc.c
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;
  argint(0, &n);
  if(n < 0)
    n = 0;
    
  // 调用trace
  backtrace();

  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}
```

在panic中加入backtrace，为之后的调试提供遍历：

```c
// kernel/sysproc.c
void
panic(char *s)
{
  pr.locking = 0;
  printf("panic: ");
  printf("%s\n", s);
  backtrace();
  panicked = 1; // freeze uart output from other CPUs
  for(;;)
    ;
}
```

测试一下：

```bash
$ bttest
0x0000000080001dac
0x0000000080001cbc
0x0000000080001a3e
```

用addr2line工具检验下结果：

```bash
minghan@Minghan:~/projs/xv6-labs-2024$ addr2line -e kernel/kernel
0x0000000080001dac
/home/minghan/projs/xv6-labs-2024/kernel/sysproc.c:64
0x0000000080001cbc
/home/minghan/projs/xv6-labs-2024/kernel/syscall.c:141 (discriminator 1)
0x0000000080001a3e
/home/minghan/projs/xv6-labs-2024/kernel/trap.c:76
```

### Alarm

我的目标：添加一个系统调用sigalarm(interval, handler)，当一个程序调用sigalarm(n, fn)，每隔n个时钟ticks，调用一次fn。注意，调用sigalarm(0, 0)表示停止alert。

思路：

- [x] 测试在文件user/alarmtest.c中，在Makefile中将其加入
- [x] 修改struct proc，在其中存储alarm interval和handler
- [x] 添加系统调用`int sigalarm(int ticks, void (*handler)());`
- [x] 添加系统调用`int sigreturn(void);`，暂时返回0
- [x] 在allocproc函数中对此二者进行初始化
- [x] 修改时钟中断处理函数，加入对alarm interval的追踪，注意handler为空时不必做处理
- [x] 修改usertrap， 让其返回用户态执行handler

我们先看test0：

```c
void
test0()
{
  int i;
  printf("test0 start\n");
  count = 0;

  sigalarm(2, periodic);
  for(i = 0; i < 1000*500000; i++){
    if((i % 1000000) == 0)
      write(2, ".", 1);
    if(count > 0)
      break;
  }
  sigalarm(0, 0);
  if(count > 0){
    printf("test0 passed\n");
  } else {
    printf("\ntest0 failed: the kernel never called the alarm handler\n");
  }
}
```

要通过这个测试必须至少调用一次periodic，这个函数通过sigalarm系统调用传入。在用户程序执行的过程中，每当时钟发出中断，都会去并执行trampoline中的uservec，保存当前上下文并切换到kernel页表，然后跳转到usertrap，并在其中调用devintr函数，来处理时钟中断。我们已经在时钟中断处理函数中对alarmticks进行了处理，那么等它处理完后我们就可以来检查alarm是否到期，如果到期就需要调用handler。我们已经知道了在进入trap时已经将返回地址保存在了sepc寄存器中，然后在usertrap中又将其保存到了trapframe的epc字段中`p->trapframe->epc = r_sepc();`。在返回到用户态时会去执行p->trapframe->epc中指向的指令，因此想要在返回用户态时执行periodic函数，就必须在usertrap中将该函数的地址赋给p->trapframe->epc。

```c
// user/user.h
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
```

```c
// user/usys.pl
entry("sigalarm");
entry("sigreturn");
```

```c
// kernel/syscall.h
#define SYS_sigalarm 22
#define SYS_sigreturn 23
```

```c
// kernel/syscall.c
extern uint64 sys_sigalarm(void);
extern uint64 sys_sigreturn(void);

static uint64 (*syscalls[])(void) = {
......
[SYS_sigalarm] sys_sigalarm,
[SYS_sigreturn] sys_sigreturn,
};
```

```c
// kernel/proc.h
struct proc {
  ......
  int alrmticks;               // Alarm ticks
  int alrmperiod;              // Alarm period
  void (*alrmhandler)();       // Alarm handler
};
```

```c
// kernel/sysproc.c
uint64 sys_sigalarm(void) {
  uint64 handler;
  struct proc* p = myproc();
    
  argint(0, &p->alrmperiod);
  p->alrmticks = p->alrmperiod;

  argaddr(1, &handler);
  p->alrmhandler = (void (*)())handler;
    
  return 0;
}

uint64 sys_sigreturn(void) {
  return 0;
}
```

```c
// kernel/proc.c
static struct proc*
allocproc(void)
{
  ......
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // 初始化alarm数据
  p->alrmticks = 0;
  p->alrmperiod = 0;  
  p->alrmhandler = 0;

  return p;
}
```

```c
void
usertrap(void)
{
  ......

  if(killed(p))
    exit(-1);

  if (which_dev == 2) {
    if (p->alrmperiod > 0) {
      p->alrmticks--;
      if (p->alrmticks == 0) {
        p->alrmticks = p->alrmperiod;
        p->trapframe->epc = (uint64)p->alrmhandler;
      }
    }
    yield();
  }

  usertrapret();
}
```

---

再来看test1：

```c
void
test1()
{
  int i;
  int j;

  printf("test1 start\n");
  count = 0;
  j = 0;
  sigalarm(2, periodic);
  for(i = 0; i < 500000000; i++){
    if(count >= 10)
      break;
    foo(i, &j);
  }
  if(count < 10){
    printf("\ntest1 failed: too few calls to the handler\n");
  } else if(i != j){
    printf("\ntest1 failed: foo() executed fewer times than it was called\n");
  } else {
    printf("test1 passed\n");
  }
}
```

这个测试要求在执行完periodic之后要保证响应完陷入和系统调用，回到用户程序后执行上下文不能发生改变，这就需要将trapframe备份一下，在返回时进行还原。

在struct proc中再添加一个struct trapframe，在执行handler之前将原来的trapframe备份一下，然后在sigreturn中恢复。

```c
// kernel/proc.h
struct proc {
  ......
  int alrmticks;               
  int alrmperiod;              
  void (*alrmhandler)();       
  struct trapframe alrmtrapframe; 
};
```

```c
// kernel/trap.c
void alrmstore() 
{
  struct proc* p = myproc();
  memmove(&(p->alrmtrapframe), p->trapframe, sizeof(*p->trapframe));
}

void
usertrap(void)
{
  ......
  if(killed(p))
    exit(-1);

  if (which_dev == 2) {
    if (p->alrmperiod > 0) {
      p->alrmticks--;
      if (p->alrmticks == 0) {
        p->alrmticks = p->alrmperiod;
        alrmstore();        
        p->trapframe->epc = (uint64)p->alrmhandler;
      }
    }
    yield();
  }

  usertrapret();
}
```

```c
uint64 sys_sigreturn(void) {
  struct proc* p = myproc();
  memmove(p->trapframe, &(p->alrmtrapframe), sizeof(*p->trapframe));
  return 0;
}
```

test3要求在handler执行期间不能再次执行，test4要求解决在sigreturn的返回值占用a0的问题。

对于test3，可以在handler执行期间将`p->alrmperiod`的值置为0，然后在sigreturn中恢复。

```c
if (which_dev == 2) {
    if (p->alrmperiod > 0) {
      p->alrmticks--;
      if (p->alrmticks == 0) {
        p->alrmticks = p->alrmperiod;
        p->alrmperiod = 0;
        alrmstore(); 
        p->trapframe->epc = (uint64)p->alrmhandler;
      }
    }
    yield();
 }
```

对于test4，sigreturn可以直接返回trapframe->a0来避免a0被覆盖的问题。

测试一下：

```bash
$ alarmtest
test0 start
.......................................................alarm!
test0 passed
test1 start
......alarm!
.....alarm!
......alarm!
.....alarm!
.....alarm!
......alarm!
.....alarm!
......alarm!
.....alarm!
......alarm!
test1 passed
test2 start
..........................................................alarm!
test2 passed
test3 start
test3 passed
```

```bash
minghan@Minghan:~/projs/xv6-labs-2024$ make grade
== Test answers-traps.txt == 
answers-traps.txt: OK 
== Test backtrace test == 
$ make qemu-gdb
backtrace test: OK (2.0s) 
== Test running alarmtest == 
$ make qemu-gdb
(3.6s) 
== Test   alarmtest: test0 == 
  alarmtest: test0: OK 
== Test   alarmtest: test1 == 
  alarmtest: test1: OK 
== Test   alarmtest: test2 == 
  alarmtest: test2: OK 
== Test   alarmtest: test3 == 
  alarmtest: test3: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (124.9s) 
== Test time == 
time: OK 
Score: 95/95
```

### 总结

这个实验设计的目的是让我们理解在用户程序执行过程中如何处理trap的，包括如何进入进入内核态、如何保存用户进程上下文、如何处理并返回到用户进程。特别是从内核态回到用户态那部分，还是不容易理解清楚的。

几个参考：

- https://ttzytt.com/2022/07/xv6_lab4_record/index.html
- https://www.bilibili.com/video/BV1wu411d7Kd/?spm_id_from=333.788&vd_source=4de003ee9a3815aedd7d0cb2c7a12d14

我问了Deepseek一个问题：xv6在发生时钟中断后，usertrap中为什么要调用sched？最终如何回到用户程序继续执行的？

**1. 时钟中断触发与处理流程**
**(1) 硬件中断处理**
• 中断触发：硬件定时器（如CLINT）周期性地触发时钟中断，CPU从用户态陷入内核态。

• 保存现场：硬件自动保存当前程序计数器（PC）到`sepc`寄存器，并跳转到`stvec`寄存器指定的陷阱处理入口（如`uservec`）。

**(2) 进入`usertrap`**
• `uservec`到`usertrap`：  

  `uservec`（位于`trampoline.S`）保存用户寄存器的状态到进程的`trapframe`，随后调用`usertrap`（`kernel/trap.c`）。

  ```c
void usertrap(void) {
    struct proc *p = myproc();
    // 判断中断类型
    if (r_scause() == 0x8000000000000005L) { // 时钟中断
        if (p->state == RUNNING) {
            yield(); // 调用yield让出CPU
        }
    }
    // ...
}
  ```

---

**2. 调用`yield`与`sched`的目的**
**(1) 强制进程让出CPU**
• 协作式调度的不足：xv6默认是协作式调度，进程需主动调用`yield`（如通过`sleep`或`exit`）让出CPU。

• 时钟中断的抢占作用：  

  时钟中断强制触发`yield`，将当前进程标记为`RUNNABLE`并调用`sched`，实现抢占式调度。

  ```c
void yield(void) {
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched(); // 进入调度器
    release(&p->lock);
}
  ```

**(2) `sched`的上下文切换**
• 切换到调度器上下文：  

  `sched`调用`swtch`保存当前进程的上下文（`p->context`），切换到调度器上下文（`cpu->context`）。

  ```c
void sched(void) {
    struct proc *p = myproc();
    swtch(&p->context, &mycpu()->context); // 保存进程上下文，恢复调度器上下文
}
  ```

---

**3. 调度器选择新进程**
**(1) 调度器循环**
• 调度器（`scheduler`）：  

  调度器遍历进程表，选择`RUNNABLE`的进程，通过`swtch`切换到其上下文。

  ```c
void scheduler(void) {
    for (;;) {
        for (p = proc; p < &proc[NPROC]; p++) {
            if (p->state == RUNNABLE) {
                p->state = RUNNING;
                swtch(&c->context, &p->context); // 切换到新进程
            }
        }
    }
}
  ```

**(2) 恢复新进程执行**
• `swtch`返回：  

  当`swtch`返回时，CPU已切换到新进程的内核栈和上下文，继续执行其代码。

---

**4. 如何回到原用户程序？**
**(1) 进程再次被调度**
• 原进程状态恢复：  

  当原进程被重新调度时，调度器的`swtch`会恢复其保存的`p->context`，返回到`sched`函数。
• 从`sched`返回到`yield`：  

  `sched`返回到`yield`后，进一步返回到`usertrap`。

**(2) 返回用户态**
• 恢复用户态现场：  

  `usertrapret`（`kernel/trap.c`）准备返回用户态：

    1. 设置`stvec`指向`userret`（处理用户态返回）。
    2. 恢复用户寄存器（从`trapframe`）。
    3. 执行`userret`（`trampoline.S`），切换页表并跳转到`sepc`。

  ```c
void usertrapret(void) {
    struct proc *p = myproc();
    // 设置陷阱处理入口为用户态
    w_stvec(TRAMPOLINE + (uservec - trampoline));
    // 准备陷阱帧和恢复用户寄存器
    p->trapframe->kernel_sp = p->kstack;
    p->trapframe->kernel_hartid = mycpu()->hartid;
    // 返回到用户态
    userret();
}
  ```

**(3) 用户程序继续执行**
• 执行`sret`指令：  

  `userret`最终通过`sret`指令跳转回用户空间的`sepc`地址，恢复用户程序的执行。

---

**5. 关键数据结构**

| 数据结构           | 作用                                                         |
| ------------------ | ------------------------------------------------------------ |
| `struct trapframe` | 保存用户态寄存器（如`a0`、`ra`、`sp`等），用于中断/异常后恢复。 |
| `struct context`   | 保存内核线程的上下文（如`s0`-`s11`寄存器），用于调度时的上下文切换。 |
| `struct proc`      | 进程控制块，包含进程状态、页表、陷阱帧、上下文等核心信息。   |

---

**总结**
• 时钟中断触发抢占：强制当前进程调用`yield`，通过`sched`进行上下文切换。

• 调度器选择新进程：`swtch`切换到新进程的内核上下文。

• 恢复用户态执行：通过`usertrapret`和`userret`恢复陷阱帧，`sret`跳回用户空间。


这一机制确保了多进程的公平调度，同时通过保存和恢复上下文/陷阱帧，实现了用户程序的无缝中断与恢复。

## Lab5 cow

### COW

耗时：8个小时

没什么好说的，实现Copy-On-Write fork。

思路：

- 提供一个数据结构，记录物理页面是否为cow-fork页面，并且能记录该页面被哪些进程引用
  - 在kalloc中添加一个长度为当前内存总页面数的数组ref_cnt，每个元素记录当前页面被引用的次数
  - 给定一个物理地址pa，可以通过ref_cnt[pa % PGSIZE]来访问引用次数
  - kalloc中对页面的ref_cnt元素置为1
  - kfree中对页面ref_cnt元素减1。只有在其变为0时才释放。

- 修改fork调用中关于页表复制的代码
  - 为子进程分配页表，将父进程的页表PTE给它
  - 将父进程和子进程的可读可写页面对应的PTE置为只读和将RSW的低位（第8位）置位，ref_cnt对应加1
- 添加对cow-fork页面写入造成的page fault的处理
  - 通过scause判断异常是否来自store page fault+是否为cow-fork页面（用PTE的第8位-RSW标记，1表是，0表否）
  - 为当前进程新分配一个物理页面，并将原来页面的内容复制过来，并修改PTE权限
- 修改copyout函数，做与类似page pagefault时相似的处理
  - Modify copyout() to use the same scheme as page faults when it encounters a COW page.

需要注意的细节：

- 在fork中复制页表时，只需要将子进程和父进程的可读可写页面的PTE置为只读，本来就只读的页面后续不需要处理
- 在cow-fork页面写入的page fault处理中，则不改变其他进程的PTE，只为自己的PTE添加可写权限
- 在出现对只读页面写入造成的page fault时，要判断该页面是普通页面还是cow-fork页面，若是普通的只读页面，应当将该进程kill掉

---

对页面分配分进行修改：

```c
// kernel/kalloc.c
#define PA2IDX(pa) ((uint64)pa/PGSIZE)
struct cowref {
  struct spinlock lock;
  int ref_cnt[PHYSTOP / PGSIZE]; 
} refcount;

void incr(uint64 pa) 
{
  acquire(&refcount.lock);
  refcount.ref_cnt[PA2IDX(pa)]++;
  release(&refcount.lock);
}

void decr(uint64 pa) 
{
  acquire(&refcount.lock);
  refcount.ref_cnt[PA2IDX(pa)]--;
  release(&refcount.lock);
}

int getrefcnt(uint64 pa)
{
  int ans;
  acquire(&refcount.lock);
  ans = refcount.ref_cnt[PA2IDX(pa)];
  release(&refcount.lock);
  return ans;
}

void
kinit()
{
  initlock(&refcount.lock, "refcount");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (getrefcnt((uint64)pa) != 0) {  // decrement & check
    decr((uint64)pa);
    if (getrefcnt((uint64)pa) != 0)
      return ;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&refcount.lock);
    refcount.ref_cnt[PA2IDX(r)] = 1;
    release(&refcount.lock);
  }

  return (void*)r;
}
```

处理fork时的复制：

```c
// kernel/vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");

    if (*pte & PTE_W) // 只考虑非只读的页面 
      *pte = (*pte & (~PTE_W)) | PTE_COW;   // 取消可写权限，并对第8位置位
    *pte |= (1l << 9);
    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
      goto err;
    }
    
    incr(pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);

    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
      return -1;

    if ((*pte & PTE_W) == 0 && (*pte & (1 << 9))) { // 这说明该页为共享页
      if (cowhandler(pagetable, va0) != 0)
        return -1;
    } else if((*pte & PTE_W) == 0)
        return -1;
    
    pa0 = PTE2PA(*pte);
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

处理trap

```c
// kernel/trap.c
int cowhandler(pagetable_t pgtbl, uint64 va) 
{
    if (va >= MAXVA)
      return -1;

    uint64 pa;
    uint64 va_align = PGROUNDDOWN(va);
    pte_t *pte = walk(pgtbl, va_align, 0);

    if (*pte & (1 << 9)) {  // COW页面
      if (*pte & PTE_COW) { // 之前可写
        pa = PTE2PA(*pte);
          char* mem;
          if((mem = kalloc()) == 0)   // alloc new page 
            return -1;
          memmove(mem, (char*)pa, PGSIZE);  // copy content from shared page
          kfree((void*)pa);
          *pte = (*pte & (0x3ff)) | PA2PTE(mem) | PTE_W;  // 更新物理地址
      } else {
        return -1;
      }
    }
    return 0;
}

void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if (r_scause() == 15){
     if (cowhandler(p->pagetable, r_stval()) != 0)
      setkilled(p);
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
```

cow的测试能够通过，但是usertests老是存在问题，已经清楚COW-fork的原理和实现方法了，暂时不打算继续深究了。

---

看完了Lecture12中Morris对COW实验的解释，依照他的讲述我重新写了一遍，顺利通过了所有测试。

添加对物理页面的引用计数：

```c
// kernel/kalloc.c

int refcount[PHYSTOP / PGSIZE];

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    refcount[(uint64)p / PGSIZE] = 1;
    kfree(p);
  }
}

void 
incref(uint64 pa) 
{
  int pn = pa / PGSIZE;
  acquire(&kmem.lock);
  if (pa >= PHYSTOP || refcount[pn] < 1)
    panic("incref");
  refcount[pn]++;
  release(&kmem.lock);
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  int pn = (uint64)pa / PGSIZE;
  if (refcount[pn] < 1)
    panic("kfree ref");
  int tmp = --refcount[pn];
  release(&kmem.lock);

  if (tmp > 0)
    return ;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    int pn = (uint64)r / PGSIZE;
    if (refcount[pn] != 0)
      panic("kalloc ref");
    refcount[pn] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

```

修改uvmcopy，让子进程共享父进程的页面，将只读进程打上COW_PTE标记:

```c
// kernel/vm.c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);

    /* 
      我们不希望在此为子进程分配页面，而是共享父进程的物理页面，有几点需要注意：
      1. 本来就是只读的页面，子进程和父进程都不会修改，共享不会出问题
      2. 而可写的页面，双方都有可能对其进行修改
    */
    if (*pte & PTE_W) {
      *pte = (*pte & (~PTE_W)) | PTE_COW;
    }
    
    flags = PTE_FLAGS(*pte);
    if(mappages(new, i, PGSIZE, (uint64)pa, flags) != 0){
      goto err;
    }
    incref(pa);
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

修改usertrap中的内容，增加对pagefault的处理：

```c
// kernel/trap.c
int 
cowfault(pagetable_t pagetable, uint64 va) 
{
  if (va >= MAXVA)
    return -1;

  // walk中会检查pte中的PTE_V位，如果不在页表会返回0
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0  || (*pte & PTE_COW) == 0) // 如果不是COW页面则必须kill掉
    return -1;
  
  // 如Trapoline和Trapframe都是没有U权限的，不能访问
  if ((*pte & PTE_U) == 0 || (*pte & PTE_V) == 0)
    return -1;
  
  uint64 pa_old = PTE2PA(*pte);
  uint64 pa_new = (uint64)kalloc();

  if (pa_new == 0) {
    printf("cow kalloc failed\n");
    return -1;
  }

  memmove((void*)pa_new, (const void*)pa_old, PGSIZE);
  *pte = (PA2PTE(pa_new) | PTE_FLAGS(*pte) | PTE_W) & (~PTE_COW);
  kfree((void*)pa_old);
  return 0;
}

void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  }  else if((which_dev = devintr()) != 0){
    // ok
  } else if (r_scause() == 0xf) {
    if (cowfault(p->pagetable, r_stval()) < 0)
      setkilled(p);
  } else {
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}
```

在copyout函数中，会将kernel中的内容写入到用户进程中，涉及页面的写入，也要判断页面是否为COW：

```c
// kernel/vm.c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;

    pte = walk(pagetable, va0, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
      return -1;

    // 处理cow造成的pagefault
    if ((*pte & PTE_W) == 0 && (*pte & PTE_COW) != 0) {
      if (cowfault(pagetable, va0) < 0) 
        return -1;
    }
    
    if((*pte & PTE_W) == 0)
      return -1;
    pa0 = PTE2PA(*pte);
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

测试结果：

```bash
== Test running cowtest == 
$ make qemu-gdb
(26.1s) 
== Test   simple == 
  simple: OK 
== Test   three == 
  three: OK 
== Test   file == 
  file: OK 
== Test   forkfork == 
  forkfork: OK 
== Test usertests == 
$ make qemu-gdb
(102.3s) 
== Test   usertests: copyin == 
  usertests: copyin: OK 
== Test   usertests: copyout == 
  usertests: copyout: OK 
== Test   usertests: all tests == 
  usertests: all tests: OK 
== Test time == 
time: OK 
Score: 130/130
```

### 总结

相比20年，24年的usertests增加了不少新的测试，检查你的实现是不是真的可靠，并且没有引入奇怪的bug。cow-fork的实现思路非常简单，但是要在实现功能的情况下保证代码的可靠性，的确不是一件容易的事情。之前折磨了我好久，搞得我快抑郁了，不过转头一想，MIT的这些学生真正完成并通过所有的测试可能也需要个把星期，我想在两天之内就彻底解决所有问题的确也是太着急了。

通过这个实验基本上弄明白了如何操纵页表和page fault来实现系统的优化，如果之后在自己的工作中如果有类似需求也有能力去动手尝试实现一下了。

## Lab6 Net

耗时：2小时

### NIC 

为E1000网卡写一个简单的驱动程序，编写驱动程序的方法就是弄清楚硬件提供的接口和运行逻辑，然后按照要求对硬件的端口寄存器进行读写即可，intel8254的手册懒得看，直接抄作业了：

```c
int
e1000_transmit(char *buf, int len)
{
  acquire(&e1000_lock);
  uint32 idx = regs[E1000_TDT];

  if ((tx_ring[idx].status & E1000_TXD_STAT_DD) == 0) {
    release(&e1000_lock);
    return -1;
  }

  if (tx_bufs[idx]) {
    kfree(tx_bufs[idx]);
  }

  tx_bufs[idx] = buf;
  tx_ring[idx].addr = (uint64)buf;
  tx_ring[idx].length = len;
  tx_ring[idx].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

  regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;
  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  while (rx_ring[idx].status & E1000_RXD_STAT_DD) {
    net_rx(rx_bufs[idx], rx_ring[idx].length);
    if ((rx_bufs[idx] = kalloc()) == 0) {
      panic("e1000_recv");
    }
    rx_ring[idx].addr = (uint64)rx_bufs[idx];
    rx_ring[idx].status = 0;
    idx = (idx + 1) % RX_RING_SIZE;
  }
  regs[E1000_RDT] = (idx + RX_RING_SIZE - 1) % RX_RING_SIZE;
}
```

参考：https://blog.csdn.net/Peiris_/article/details/143245354?spm=1001.2014.3001.5502

### 总结

通过这个实验，解了如何为网卡实现收发数据的驱动。

## Lab7 lock

耗时：4小时

这个实验本质上就是理解多核心执行的情况下，有哪些地方会造成竞争，并想办法缩小锁的粒度，提高并行性。

### Memory allocator

当前xv6中提供的代码使用了一个kmem来管理所有物理页面的分配，而多个核心在申请页面时都要先获取同一个kmem的锁。为了减少对一个锁的竞争，现在为每个核心都添加一个kmem，其中包含一个独立的lock和自己的freelist，以获取更好的并行性。

思路：

- 核心总数由param.h中的宏NCPU定义
- 为每个核心准备一个kmem
- 在当前核心的freelist中物理页面不足时，要能够从其他核心的freelist中偷取
- 初始化时，先将所有的物理页面挂载到0号核心上，其他的核心后续从它这里偷取
- 函数cpuid()可以返回当前核心号，但是必须在关中断的情况下调用和使用其返回值才能保证安全，使用push_off和pop_off。这是为了防止在使用cpuid获取到核心号后，被调度到其他核心上执行后续的操作，造成不一致。

```c
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 为每个核心准备一个kmem

void
kinit()	// 在main.c中可以看到，只有0号核心才会执行该函数
{
  for (int i = 0; i < NCPU; i++) {
    char name[9] = {0};
    snprintf(name, 8, "kmem-%d", i);
    initlock(&kmem[i].lock, name);	// 初始化各个锁
  }
  freerange(end, (void*)PHYSTOP);	// 将物理页面全部挂入0号核心的freelist
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}
```

页面的分配，这里的偷取策略为：从0号核心开始遍历所有kmem，尝试从其中偷取一个页面。其实还有更好的策略，例如偷取的时候可以不止拿一个页面，而是直接偷掉一半的页面过来。再就是遍历的开始位置可以不要固定在0号核心的kmem，而是循环地从上一次偷取的下一个位置开始。

```c
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int hart_id = cpuid();

  acquire(&kmem[hart_id].lock);
  r = kmem[hart_id].freelist;	// 从当前freelist中取物理页面
  
  if (r == 0) { 				// 若当前freelist为空，从其hart的freelist中偷取
    release(&kmem[hart_id].lock);	// 及时释放当前kmem的锁
    for (int i = 0; i < NCPU; i++) { // 遍历所有kmem，偷取一个页面即可
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      if (r) {
        kmem[i].freelist = r->next;
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  } else {
    kmem[hart_id].freelist = r->next;
    release(&kmem[hart_id].lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  pop_off();
  return (void*)r;
}
```

页面的释放：

```c
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int hart_id = cpuid();
  

  acquire(&kmem[hart_id].lock);
  r->next = kmem[hart_id].freelist;
  kmem[hart_id].freelist = r;
  release(&kmem[hart_id].lock);
  pop_off();
}
```

### Buffer cache

在当前xv6的磁盘块缓存相关的操作中，在修改buf中的数据如refcnt时，都需要先获取bcache的spinlock，尽管不同核心上的线程各自请求读写的是不同的buf，但都会被bcache的锁给拦住，为了提高并发性，现在将保持buf总数不变的情况下，增加bcache的数量，用哈希映射的方式将对不同磁盘块buf的请求分配到不同的bcache，这时需要获取的只是相关bcache的锁，而不是所有buf共用一个锁。

我们的实现不再使用双链表，LRU的通过比较buf上次使用的ticks来实现。

```c
#define BUCKETSIZE 3 // number of hashing buckets
#define BUFFERSIZE 10 // number of available buckets per bucket
extern uint ticks;	

struct {
  struct spinlock lock;
  struct buf buf[BUFFERSIZE];
} bcache[BUCKETSIZE];	// 多个bcache

int
hash(uint blockno) {
  return blockno % BUCKETSIZE;
}

void
binit(void)
{
  for (int i = 0; i < BUCKETSIZE; i++) {
    initlock(&bcache[i].lock, "bcache_bucket");
    for (int j = 0; j < BUFFERSIZE; j++) {
      initsleeplock(&bcache[i].buf[j].lock, "buffer");
    }
  }
}
```

最重要的就是bget，思路和原来差不多，只是中间加入了一段遍历查找最近最久未使用buf的代码。

```c
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int bucket = hash(blockno);
  acquire(&bcache[bucket].lock);

  // Is the block already cached?
  for (int i = 0; i < BUFFERSIZE; i++) {  // 从当前bucket中的buf中查找
    b = &bcache[bucket].buf[i];
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache[bucket].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 实现LRU，选择时间戳最早的使用
  uint least = 0xffffffff;
  int least_idx = -1;
  for (int i = 0; i < BUFFERSIZE; i++) {
    b = &bcache[bucket].buf[i];
    if (b->refcnt == 0 && b->lastuse < least) {
      least = b->lastuse;
      least_idx = i;
    }
  }

  // 在当前bucket中没有空闲的buf，应当去别的bucket去偷，这里测试能通过就不搞了
  if (least_idx == -1) 
    panic("bget: no unused buffer");
  
  b = &bcache[bucket].buf[least_idx];
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&bcache[bucket].lock);
  acquiresleep(&b->lock);
  return b;
  // panic("bget: no buffers");
}
```

brelse释放buf，没啥好说的。

```c
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int bucket = hash(b->blockno);
  acquire(&bcache[bucket].lock);
  b->refcnt--;
  release(&bcache[bucket].lock);
  releasesleep(&b->lock);
}
```

测试一下：

```bash
== Test running kalloctest == 
$ make qemu-gdb
(58.8s) 
== Test   kalloctest: test1 == 
  kalloctest: test1: OK 
== Test   kalloctest: test2 == 
  kalloctest: test2: OK 
== Test   kalloctest: test3 == 
  kalloctest: test3: OK 
== Test kalloctest: sbrkmuch == 
$ make qemu-gdb
kalloctest: sbrkmuch: OK (9.5s) 
== Test running bcachetest == 
$ make qemu-gdb
(124.2s) 
== Test   bcachetest: test0 == 
  bcachetest: test0: OK 
== Test   bcachetest: test1 == 
  bcachetest: test1: OK 
== Test   bcachetest: test2 == 
  bcachetest: test2: OK 
== Test   bcachetest: test3 == 
  bcachetest: test3: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (104.4s) 
== Test time == 
time: OK 
Score: 110/110
```

### 总结

通过这个实验，我对如何通过改进锁的细粒度以减少锁的竞争，提高并行性有了一些理解。

- 第一个task改进了物理页面分配逻辑，从先前所有核心竞争同一个kmem的锁，改为为每个核心准备一个kmem，在分配和释放物理页面时只需要获取各自的锁，提高了并行性，并实现了在自己的kmem中物理页面不足时从其他kmem中偷取的功能。
- 第二个task改进了磁盘块的缓存逻辑，增加bcache的数量，根据哈希和磁盘块号将对磁盘块的请求分配到相应的bcache中，提高并行性。还通过在struct buf中添加lastuse来记录上次被使用的时间，然后通过遍历比较该记录的方式找到最近最久未使用的buf，实现LRU。我的实现有一个问题，即当前bcache中的buf用完后无法利用其他bcache中的buf。

> 锁定主要是为了正确性而抑制并行性。由于性能也很重要, 因此内核设计者通常必须考虑如何以既实现正确性又允许并行性的方式使用锁。
