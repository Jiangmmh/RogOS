#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "sync.h"

struct partition {
    uint32_t start_lba;         // 起始扇区
    uint32_t sec_cnt;           // 扇区数
    struct disk* my_disk;       // 分区所属的硬盘
    struct list_elem part_tag;  // 队列标记
    char name[8];               // 分区名
    struct super_block* sb;     // 本分区的超级块
    struct bitmap block_bitmap; // 块位图，管理本分区的块
    struct bitmap inode_bitmap; // i结点位图
    struct list open_inodes;    // 本分区打开的i结点队列
};

/* 硬盘结构 */
struct disk {
    char name[8];                       // 硬盘名
    struct ide_channel* my_channel;     // 此块硬盘属于哪个ide通道
    uint8_t dev_no;                     // 本硬盘是主0，还是从1
    struct partition prime_parts[4];    // 主分区最多4个
    struct partition logic_parts[8];    // 逻辑分区无限，但这里就支持8个
};

/* ata通道结构 */
struct ide_channel {
    char name[8];                   // 本ata通道名称
    uint16_t port_base;             // 本通道的起始端口号
    uint8_t irq_no;                 // 本通道所用的中断号
    struct lock lock;               // 通道锁，通道上有主从两块硬盘，设置锁实现互斥
    bool expecting_intr;            // 表示等待硬盘的中断
    struct semaphore disk_done;     // 用于阻塞、唤醒驱动程序
    struct disk device[2];          // 一个通道上连接两个硬盘
};

void intr_hd_handler(uint8_t irq_no);
void ide_init(void);
extern uint8_t channel_cnt;
extern struct ide_channel channels[];
extern struct list partition_list;
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
#endif