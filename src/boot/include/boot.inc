; 配置信息
LOADER_BASE_ADDR equ 0x900      ; 定义了loader在内存中的位置
LOADER_START_SECTOR equ 0x2     ; 定义了loader在硬盘上的逻辑扇区号（LBA地址）

; 页目录表位置 
PAGE_DIR_TABLE_POS equ 0x100000

; gdt描述符属性
DESC_G_4K equ 1_00000000000000000000000b    ; G为1，因为使用平坦模式，只有一个4G的段
DESC_D_32 equ 1_0000000000000000000000b     ; D为1，表示使用32位操作数
DESC_L equ    0_000000000000000000000b      ; L为0，表示使用32位代码段，64位代码标记，此处标记为0即可
DESC_AVL equ  0_00000000000000000000b

DESC_LIMIT_CODE2    equ 1111_0000000000000000b ; 段界限的第2部分
DESC_LIMIT_DATA2    equ DESC_LIMIT_CODE2       
DESC_LIMIT_VIDEO2   equ 0000_0000000000000000b

DESC_P      equ 1_000000000000000b               ; P为1，表示段在内存中
DESC_DPL_0  equ 00_0000000000000b            ; 四种不同特权级的位模式
DESC_DPL_1  equ 01_0000000000000b
DESC_DPL_2  equ 10_0000000000000b
DESC_DPL_3  equ 11_0000000000000b

DESC_S_CODE equ 1_000000000000b             ; S=1，表示该段为普通段，而不是系统段
DESC_S_DATA equ DESC_S_CODE     
DESC_S_sys  equ 0_000000000000b              ; 这是啥玩意？

DESC_TYPE_CODE equ 1000_00000000b           ;x=1,c=0,r=0,a=0 代码段是可执行的,非一致性,不可读,已访问位 a 清 0
DESC_TYPE_DATA equ 0010_00000000b           ;x=0,e=0,w=1,a=0 数据段是不可执行的,向上扩展的,可写,已访问位 a 清 0

; 代码段的高32位
DESC_CODE_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
    DESC_P + DESC_DPL_0 + DESC_S_CODE +\
    DESC_TYPE_CODE + 0x00

; 数据段的高32位
DESC_DATA_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
    DESC_P + DESC_DPL_0 + DESC_S_DATA + \
    DESC_TYPE_DATA + 0x00

; 显存段的高32位
DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + \
    DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0B

; 选择子属性
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
TI_GDT equ 000b
TI_LDT equ 100b

; 页表相关属性
PG_P equ 1b
PG_RW_R equ 00b
PG_RW_W equ 10b
PG_US_S equ 000b
PG_US_U equ 100b


KERNEL_START_SECTOR equ 0x9
KERNEL_BIN_BASE_ADDR equ 0x70000
KERNEL_ENTRY_POINT equ 0xc0001500
PT_NULL equ 0