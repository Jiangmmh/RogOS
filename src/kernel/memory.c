#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "debug.h"
#include "string.h"
#include "global.h"
#include "sync.h"
#include "thread.h"
#include "interrupt.h"

/************************ 位图地址 *****************************
 * 因为 0xc009f000 是内核主线程栈顶, 0xc009e000是内核主线程的 pcb起始地址。
 * 一个页框大小的位图可表示 4KB*4KB*8 = 128MB 内存,位图位置安排在地址 0xc009a000,
 * 这样本系统最大支持 4 个页框的位图,即 512MB */
#define MEM_BITMAP_BASE 0xc009a000
/***************************************************************/
/* 0xc0000000 是内核从虚拟地址 3G 起。0x100000 意指跨过低端 1MB 内存,使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)


/* 内存池结构,生成两个实例用于管理内核内存池和用户内存池 */
struct pool {
    struct bitmap pool_bitmap;  // 本内存池的位图，用于管理物理内存
    uint32_t phy_addr_start;    // 本内存池管理的物理内存的起始地址
    uint32_t pool_size;         // 本内存池容量，单位为字节
    struct lock lock;
};

/* 内存仓库 */
struct arena {
    struct mem_block_desc* desc;    // 此arena关联的mem_block_desc
    uint32_t cnt;                   // large为true时，cnt表示页框数，否则表示mem_block数量
    bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT];  // 内核内存块描述符数组

struct pool kernel_pool, user_pool; // 内核内存池和用户内存池
struct virtual_addr kernel_vaddr;   // 管理内核的虚拟地址

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页，成功则返回虚拟页的起始地址，失败则返回NULL */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);   // 检查是否有连续pg_cnt个页可供分配
        if (bit_idx_start == -1) {
            return NULL;
        }
        while (cnt < pg_cnt) {  // 将虚拟内存的位图中占用部分置1
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;  
    } else {    // 用户内存池的分配
        struct task_struct* cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }

        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));  // 确保虚拟内存在用户空间
    }
    return (void*)vaddr_start;
}

/* 得到虚拟地址vaddr对应的pte指针 */
uint32_t* pte_ptr(uint32_t vaddr) {
    /* 按照映射规则，给出一个vaddr，会用高10位访问页目录表，中间10位访问页表，低12位作为目标地址的偏移
       现在我们要获得的是页表项，因此重新构造地址，高10位为全1，先访问自己（页目录表的最后一项指向页目录表自身）；
       中间10位为原来高10位的内容，找到对应的页表；低12位为原来的中间10位内容，找到页表项  
    */
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
    return pte;
}

/* 得到虚拟地址vaddr对应的pde指针 */
uint32_t* pde_ptr(uint32_t vaddr) {
    /* 0xfffff用来访问到页表本身所在的地址，原理同上*/
    uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4); 
    return pde;
}

/* 在m_pool指向的物理内存池中分配1个物理页，成功返回页框物理地址，失败返回NULL */
static void* palloc(struct pool* m_pool) {
    /* 扫描或设置位图要保证原子操作 */
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) {
        return NULL;
    }
    
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);

    uint32_t page_phyaddr = (m_pool->phy_addr_start + (bit_idx * PG_SIZE));
    return (void*)page_phyaddr;
}


/* 页表中添加虚拟地址_vaddr与物理地址_page_phyaddr的映射 */
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);
    uint32_t* pte = pte_ptr(vaddr);

    /************************ 注意 *********************************
     * 执行*pte,会访问到空的 pde。所以确保 pde 创建完成后才能执行*pte,
     * 否则会引发 page_fault。因此在*pde 为 0 时,
     * pte 只能出现在下面 else 语句块中的*pde 后面。
     **************************************************************/
    
    // 现在页目录表内判断目录项的P位，1表示存在
    if (*pde & 0x00000001) {           // 页目录项和页表项第0位为P
        ASSERT(!(*pte & 0x00000001));  // 页表项必须不存在
        
        if (!(*pte & 0x00000001)) { // 页表项不存在
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);  // 修改页表项的值，vaddr指向该物理地址
        } else {
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    } else {    // 页目录项不存在,所以要先创建页目录再创建页表项
        /* 页表中用到的页框一律从内核空间分配 */
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);  // 为该页表项对应的页表分配页
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);      // 使该页目录项指向刚刚分配的页

        /* 分配到的物理页地址 pde_phyaddr 对应的物理内存清 0,
         * 避免里面的陈旧数据变成了页表项,从而让页表混乱。
         * 访问到 pde 对应的物理地址,用 pte 取高 20 位便可。
         * 因为 pte 基于该 pde 对应的物理地址内再寻址,
         * 把低 12 位置 0 便是该 pde 对应的物理页的起始*/
        memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE); // &0xfffff000的目的是找到对应页表，并将其初始化为0
        ASSERT(!(*pte & 0x00000001));                       // 该页表项还未进行初始化，检查一下
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 将对应物理页地址写入页表项中
    }
}

/* 分配 pg_cnt 个页空间,成功则返回起始虚拟地址,失败时返回 NULL */
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    /*********** malloc_page 的原理是三个动作的合成: **********************
     *  1 通过 vaddr_get 在虚拟内存池中申请虚拟地址
     *  2 通过 palloc 在物理内存池中申请物理页
     *  3 通过 page_table_add 将以上得到的虚拟地址和物理地址在页表中完成映射
     ********************************************************************/
    void* vaddr_start = vaddr_get(pf, pg_cnt);  // 申请虚拟地址
    if (vaddr_start == NULL) {
        return NULL;
    }

    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    /* 虚拟地址是连续的，物理地址则不一定，必须逐个申请并做映射 */
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);      // 每次分配1页物理内存
        if (page_phyaddr == NULL) {
            // put_str("Failed to alloc physical page!!\n");
            return NULL;
        }
        // put_str("vaddr: "); put_int(vaddr);put_str("\n");
        // put_str("paddr:"); put_int(page_phyaddr);put_str("\n");
        page_table_add((void*)vaddr, page_phyaddr); // 在页表中做映射
        vaddr += PG_SIZE;   // 虚拟页是连续的
    }
    return vaddr_start;
}


/* 从内核的内存空间中申请cnt页内存，成功返回其虚拟地址，失败返回NULL */
void* get_kernel_pages(uint32_t pg_cnt) {
    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) {    // 若分配成功则将分配的页框全部清0
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

/* 在用户内存空间中申请cnt页内存，并返回其虚拟地址 */
void* get_user_pages(uint32_t pg_cnt) {
    lock_acquire(&user_pool.lock);
    void* vaddr = malloc_page(PF_USER, pg_cnt);
    memset(vaddr, 0, pg_cnt * PG_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}

/* 将地址vaddr与pf池中的物理地址关联，仅支持一页空间分配 */
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    /* 先将虚拟地址对应的位图值1 */
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    /* 若当前进程为用户进程 */
    if (cur->pgdir != NULL && pf == PF_USER) {  // 用户进程
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    } else if (cur->pgdir == NULL && pf == PF_KERNEL) { // 内核线程
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("get_a_page: not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void* page_phyaddr = palloc(mem_pool);  // 从物理内存池中取一页
    if (page_phyaddr == NULL)
        return NULL;
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

/* 得到虚拟地址映射到的物理地址 */
uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff)); // 物理页起始地址+页内偏移
}

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
    put_str("    mem_pool_init start\n");
    // 页目录表1页+第0和第768个页目录项指向同一个页表+第769~1022个页目录项共指向254个页表，共256个页框
    uint32_t page_table_size = PG_SIZE * 256; 
    uint32_t used_mem = page_table_size + 0x100000; // 0x100000为低端1MB字节，表示已使用的内存
    uint32_t free_mem = all_mem - used_mem;

    // 只需要为空闲的内存建立bitmap
    uint16_t all_free_pages = free_mem / PG_SIZE;   // 1页为4KB，不管总内存是不是4K的倍数，都以页为单位分配

    // 空闲内存空间内核和用户各占一半
    uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    /* 为了简化位图操作，余数不作处理，缺点是会丢内存，优点是不用做内存越界检查 */
    uint32_t kbm_length = kernel_free_pages / 8;
    uint32_t ubm_length = user_free_pages / 8;

    uint32_t kp_start = used_mem;    // 记录内核物理内存的起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 记录用户物理内存起始地址

    // 将内核和用户物理内存的起始地址分别保存在各自的内存池中
    kernel_pool.phy_addr_start = kp_start;      
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;    // 各自的内存容量
    user_pool.pool_size = user_free_pages * PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;    // 各自bitmap所占字节数
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    /********* 内核内存池和用户内存池位图 ***********
     * 位图是全局的数据,长度不固定。
     * 全局或静态的数组需要在编译时知道其长度,
     * 而我们需要根据总内存大小算出需要多少字节,
     * 所以改为指定一块内存来生成位图。
     * ********************************************/
    // 内核内存池的位图放在MEM_BITMAP_BASE(0xc009a000)处
    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;

    // 用户内存池的位图紧随内核内存池位图之后
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

    /********************输出内存池信息**********************/
    put_str("       kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(" kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("       user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str(" user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    /* 将位图置0 */
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    /* 初始化内存池的锁 */
    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    /* 下面初始化内核虚拟地址的位图，按实际物理内存大小生成数组 */
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;    // 用于维护内核堆的虚拟地址,所以要和内核内存池大小一致

    /* 位图的数组指向一块未使用的内存, 目前定位在内核内存池和用户内存池之外*/
    kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length); // 紧随两个物理内存位图之后

    kernel_vaddr.vaddr_start = K_HEAP_START;    // 0xc0100000
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("   mem_pool_init done\n");
}

/* 为malloc做准备 */
void block_desc_init(struct mem_block_desc* desc_array) {
    uint16_t desc_idx, block_size = 16;

    // 初始化每个mem_block_desc描述符
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;
    }
}

/* 返回arena中第idx个内存块的地址 */
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
    return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

/* 返回内存块b所在的arena地址 */
static struct arena* block2arena(struct mem_block* b) {
    return (struct arena*)((uint32_t)b & 0xfffff000);   // 每次分配一个页框，arena在页框起始处
}

/* 将物理地址pg_phy_addr回收到物理内存池 */
void pfree(uint32_t pg_phy_addr) {
    struct pool* mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start) {  // 根据物理地址池的起始位置判断物理地址属于哪个内存池
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    } else {
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

/* 去掉页表中虚拟地址vaddr的映射，只去掉掉vaddr对应的pte */
static void page_table_pte_remove(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;    // 将pte的P位置0
    asm volatile ("invlpg %0"::"m"(vaddr):"memory");    // 更新tlb
}

/* 在虚拟地址池中释放以_vaddr起始的连续pg_cnt个虚拟页地址 */
static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

    if (pf == PF_KERNEL) {
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    } else {
        struct task_struct* cur_thread = running_thread();
        bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    }
}


/* 释放以虚拟地址vaddr为起始地址的cnt个物理页框 */
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
    pg_phy_addr = addr_v2p(vaddr);

    // 确保待释放的物理内存在低端1MB+1KB大小的页目录范围外
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    if (pg_phy_addr >= user_pool.phy_addr_start) {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            // 确保物理地址属于用户物理内存池
            ASSERT(pg_phy_addr % PG_SIZE == 0 && pg_phy_addr >= user_pool.phy_addr_start);
            // 将对应物理页归还内存池
            pfree(pg_phy_addr);
            // 将此虚拟地址所在的页从页表中清除
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    } else {
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            // 确保物理地址属于内核物理内存池
            ASSERT(pg_phy_addr % PG_SIZE == 0 && \
                pg_phy_addr >= kernel_pool.phy_addr_start && \
                pg_phy_addr < user_pool.phy_addr_start);
            // 将对应物理页归还内存池
            pfree(pg_phy_addr);
            // 将此虚拟地址所在的页从页表中清除
            page_table_pte_remove(vaddr);
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}


/* 在堆中申请size字节内存 */
void* sys_malloc(uint32_t size) {
    enum pool_flags PF;
    struct pool* mem_pool;
    uint32_t pool_size;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread();
    
    // 判断当前线程是内核线程还是用户线程
    if (cur_thread->pgdir == NULL) {    // 内核线程
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    } else {                            // 用户进程
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur_thread->u_block_desc;
    }

    // 申请的内存不在内存池容量范围内
    if (!(size > 0 && size < pool_size)) {
        return NULL;
    }

    struct arena* a;
    struct mem_block* b;
    lock_acquire(&mem_pool->lock);

    // 超过最大内存块1024，分配页框
    if (size > 1024) {
        // 向上取整页框数
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);
        
        a = malloc_page(PF, page_cnt);

        if (a != NULL) {
            memset(a, 0, page_cnt * PG_SIZE);
            /* 对于分配的大块页框，将desc置为NULL，cnt置为页框数，large置为true */
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void*)(a + 1);          // a为struct arena*类型，跨过arena大小，返回剩下的内存
        } else {
            lock_release(&mem_pool->lock);
            return NULL;
        }
    } else {    // 申请的内存小于等于1024
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) { // 遍历查找合适的内存块
            if (size <= descs[desc_idx].block_size) {
                break;
            }
        }

        // mem_block_desc的free_list中已经没有而可用的mem_block，需创建新的arena
        if (list_empty(&descs[desc_idx].free_list)) {
            a = malloc_page(PF, 1);
            if (a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;
            uint32_t block_idx;

            enum intr_status old_status = intr_disable();

            for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx++) {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);        // 将新arena中的每个块都插入到空闲链表中
            }
            intr_set_status(old_status);
        }

        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
        memset(b, 0, descs[desc_idx].block_size);

        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void*)b;
    }
}

/* 回收内存ptr */
void sys_free(void* ptr) {
    ASSERT(ptr != NULL);
    if (ptr != NULL) {
        enum pool_flags PF;
        struct pool* mem_pool;

        if (running_thread()->pgdir == NULL) {
            ASSERT((uint32_t)ptr > K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        } else {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block* b = ptr;
        struct arena* a = block2arena(b);

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == true) { // 大内存块，直接释放页面即可
            mfree_page(PF, a, a->cnt);
        } else {
            // 将内存块回收到free_list
            list_append(&a->desc->free_list, &b->free_elem);

            // 判断此arena中的内存块是否都是空闲，如果是就释放arena
            if (++a->cnt == a->desc->blocks_per_arena) {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) {
                    struct mem_block* b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem); // 释放arena中所有的block
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}

/* 安装1页大小的vaddr,专门针对fork时虚拟地址位图无须操作的情况 */
void* get_a_page_without_opvaddrbitmap(enum pool_flags pf, uint32_t vaddr) {
   struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
   lock_acquire(&mem_pool->lock);
   void* page_phyaddr = palloc(mem_pool);
   if (page_phyaddr == NULL) {
      lock_release(&mem_pool->lock);
      return NULL;
   }
   page_table_add((void*)vaddr, page_phyaddr); 
   lock_release(&mem_pool->lock);
   return (void*)vaddr;
}

/* 内存管理部分初始化入口 */
void mem_init() {
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00)); // 在loader.S中我们已经通过BIOS读出内存大小，存在0xb00处
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("mem_init done\n");
}
