#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/* 初始化io队列 */
void ioqueue_init(struct ioqueue* ioq) {
    lock_init(&ioq->lock);
    ioq->producer = NULL;
    ioq->head = ioq->tail = 0;
}

/* 返回pos在缓冲区中的下一个位置值 */
static int32_t next_pos(int32_t pos) {
    return (pos + 1) % bufsize;
}

/* 判断队列是否已满 */
bool ioqueue_full(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail; // head总指向队尾元素的下一个位置
}

/* 判断队列是否为空 */
bool ioqueue_empty(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

/* 使当前消费者或生产者在此缓冲区上等待 */
static void ioqueue_wait(struct task_struct** waiter) {
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

/* 唤醒waiter */
static void wakeup(struct task_struct** waiter) {
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

/* 消费者从ioq队列中获取一个字符 */
char ioqueue_getchar(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioqueue_empty(ioq)) {
        lock_acquire(&ioq->lock);       // 上锁，防止其他消费者也进入ioq的等待
        ioqueue_wait(&ioq->consumer);   // 缓冲区空，将ioq->consumer记为自己
        lock_release(&ioq->lock);
    }

    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);

    if (ioq->producer != NULL) {
        wakeup(&ioq->producer);     // 唤醒生产者
    }

    return byte;
}

/* 生产者往ioq队列中写入一个字符byte */
void ioqueue_putchar(struct ioqueue* ioq, char byte) {
    ASSERT(intr_get_status() == INTR_OFF);
    while (ioqueue_full(ioq)) {
        lock_acquire(&ioq->lock);
        ioqueue_wait(&ioq->producer);   // 缓冲区满，将ioq->producer记为自己
        lock_release(&ioq->lock);
    }
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);

    if (ioq->consumer != NULL) {
        wakeup(&ioq->consumer);     // 唤醒消费者
    }
}