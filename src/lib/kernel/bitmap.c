#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/* 位图初始化 */
void bitmap_init(struct bitmap* btmp) {
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}


/* 判断bit_idx位是否为1，若为1，则返回true，否则返回false */
bool bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx) {
    uint32_t byte_idx = bit_idx / 8;    // 获取目标bit对应字节
    uint32_t bit_odd = bit_idx % 8;     // 获取对应bit在该字节中的偏移
    return (btmp->bits[byte_idx]) & (BITMAP_MASK << bit_odd);
}

/* 在位图中申请连续cnt个位，成功返回其起始下标，失败返回-1*/
int bitmap_scan(struct bitmap* btmp, uint32_t cnt) {
    uint32_t idx_byte = 0;
    while ((0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len)) {
        idx_byte++;
    }
    ASSERT(idx_byte < btmp->btmp_bytes_len);    // 保证还有空闲的页

    int idx_bit = 0; 
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte]) {
        idx_bit++;      // 找第一个为0的bit，即第一个空闲的页
    }

    int bit_idx_start = idx_byte * 8 + idx_bit;  // 空闲页在位图中的起始下标
    if (cnt == 1) {
        return bit_idx_start;
    }

    uint32_t bit_left = (btmp->btmp_bytes_len * 8 - bit_idx_start); // 记录剩下的总位数
    if (bit_left < cnt - 1) // 不够分配cnt页，直接返回失败-1
        return -1;
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1;     // 记录空闲位的个数
    bit_idx_start = -1;
    
    while (bit_left-- > 0) {
        if (!(bitmap_scan_test(btmp, next_bit))) { // 如果next_bit为0
            count++;
        } else {
            count = 0;      // 如果next_bit为1，则重新计数
        }

        if (count == cnt) {
            bit_idx_start = next_bit - cnt + 1;
            break;
        }
        next_bit++;
    }
    return bit_idx_start;
}

/* 将位图 btmp 的 bit_idx 位设置为 value */
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value) {
    ASSERT(value == 0 || value == 1);
    uint32_t byte_idx = bit_idx / 8;
    uint32_t bit_odd = bit_idx % 8;
    if (value == 1) {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    } else {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}
