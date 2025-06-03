#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"
#include "ide.h"
#include "fs.h"
#include "super_block.h"
#include "interrupt.h"
#include "string.h"
#include "debug.h"

// inode 结构
struct inode {
    uint32_t i_no;          // inode 编号
    uint32_t i_size;        // 文件大小，字节为单位
    uint32_t i_open_cnts;   // 记录此文件被打开的次数
    bool write_deny;        // 写文件不能并行, 进程写文件前检查此标识
    uint32_t i_sectors[13]; // i_sectors[0-11]是直接块, i_sectors[13]用来存储一级间接块指针
    struct list_elem inode_tag; // 用于加入已打开的文件(inode)队列
};

// 将 inode 写入到硬盘分区 part
void inode_sync(struct partition* , struct inode* , void* );
// 根据 i 结点号返回相应的 i 结点
struct inode* inode_open(struct partition* , uint32_t );
// 关闭 inode 或减少 inode 的打开数
void inode_close(struct inode* );
// 初始化 new_inode
void inode_init(uint32_t , struct inode* );
// 将硬盘分区 part 上的 inode 清空
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);
// 回收 inode 的数据块和 inode 本身
void inode_release(struct partition* part, uint32_t inode_no);
#endif