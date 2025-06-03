#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H
#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64


/* 环形队列 */
struct ioqueue {
    struct lock lock;   // 缓冲区的锁，保证对该缓冲区的互斥访问
    /* 生产者，缓冲区不满时就继续往里面放数据，否则就睡眠 */
    struct task_struct* producer;
     /* 消费者，缓冲区不为空时就继续往里面放数据，否则就睡眠 */
    struct task_struct* consumer;
    char buf[bufsize];  // 缓冲区
    int32_t head;       // 队首，数据往队首处写入
    int32_t tail;       // 队尾，数据往队尾处读出
};

void ioqueue_init(struct ioqueue* ioq);
bool ioqueue_full(struct ioqueue* ioq);
bool ioqueue_empty(struct ioqueue* ioq);
char ioqueue_getchar(struct ioqueue* ioq);
void ioqueue_putchar(struct ioqueue* ioq, char byte);

#endif