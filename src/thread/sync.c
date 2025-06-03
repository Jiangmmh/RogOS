#include "sync.h"
#include "global.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"

/* 初始化信号量 */
void sema_init(struct semaphore* psema, uint8_t value) {
    psema->value = value;
    list_init(&psema->waiters);
}

/* 初始化锁 */
void lock_init(struct lock* plock) {
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);  // 信号量初值为1
}

/* 信号量down操作 */
void sema_down(struct semaphore* psema) {
    /* 关中断来保证原子操作 */
    enum intr_status old_status = intr_disable();

    while (psema->value == 0) {     // 若当前信号量（资源量）为0，则将线程放入waiter队列中，并将其阻塞
        ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
        list_append(&psema->waiters, &running_thread()->general_tag);   // 将当前线程阻塞
        thread_block(TASK_BLOCKED);
    }

    /* 若信号量为1或被唤醒后，执行下面代码，获得锁 */
    psema->value--;
    ASSERT(psema->value == 0);
    intr_set_status(old_status);
}

/* 信号量的up操作 */
void sema_up(struct semaphore* psema) {
    /* 关中断，保证原子操作 */
    enum intr_status old_status = intr_disable();
    ASSERT(psema->value == 0);
    if (!list_empty(&psema->waiters)) {
        struct task_struct* thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
        thread_unblock(thread_blocked);
    }

    psema->value++;
    ASSERT(psema->value == 1);
    /* 恢复之前的中断状态 */
    intr_set_status(old_status);
}

/* 获取锁plock */
void lock_acquire(struct lock* plock) {
    /* 排除曾经自己已经持有锁但还未将其释放的情况 */
    if (plock->holder != running_thread()) {    // 锁的拥有者不是当前线程
        sema_down(&plock->semaphore);   // 对信号量执行P操作，原子操作
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    } else {
        plock->holder_repeat_nr++;
    }
}

/* 释放锁plock */
void lock_release(struct lock* plock) {
    ASSERT(plock->holder == running_thread()); // 只有锁的拥有者才能释放锁
    if (plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return ;
    }
    ASSERT(plock->holder_repeat_nr == 1);

    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);     // 信号量的V操作，原子操作
}