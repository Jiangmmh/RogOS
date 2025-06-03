    %include "boot.inc"
    section loader vstart=LOADER_BASE_ADDR
    LOADER_STACK_TOP equ LOADER_BASE_ADDR

; 构造gdt及其内部的描述符
GDT_BASE:   dd 0x00000000               ; gdt第一个描述符无用，置为全0
            dd 0x00000000

CODE_DESC:  dd 0x0000FFFF               ; 代码段
            dd DESC_CODE_HIGH4
    
DATA_STACK_DESC:    dd 0x0000FFFF       ; 数据段
                    dd DESC_DATA_HIGH4

VIDEO_DESC: dd 0x80000007               ; 显存段，limit=(0xbffff-0xb8000)/4k=0x7
            dd DESC_VIDEO_HIGH4         ; 此时 dpl 为 0

    GDT_SIZE equ $ - GDT_BASE
    GDT_LIMIT equ GDT_SIZE - 1
    times 60 dq 0   ; 预留 60个描述符的空位

    ; total_mem_bytes用于保存内存容量，以字节为单位，此位置比较好记
    ; 当前偏移loader.bin头文件0x200字节，即512个字节
    ; loader.bin的加载地址时0x900
    ; 故total_mem_bytes内存中的地址是0xb00
    ; 将来在内核中咱们会引用此地址
    total_mem_bytes dd 0    ; 存放在内存地址0xb00处

    SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0
    SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
    SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

; 以下是 gdt 的指针,前 2 字节是 gdt 界限,后 4 字节是 gdt 起始地址
    gdt_ptr dw GDT_LIMIT
            dd GDT_BASE

; 人工对齐:total_mem_bytes4+gdt_ptr6+ards_buf244+ards_nr2,共 256 字节
    ards_buf times 244 db 0
    ards_nr dw 0                ; 用于记录ARDS结构体的数量

; 0xc00处
loader_start:
    ;------------------------- 读取内存大小 ----------------------
    mov sp, LOADER_BASE_ADDR   
; int 15h eax = 0000E820h ,edx = 534D4150h ('SMAP') 获取内存布局
    xor ebx, ebx            ; 第一次调用时，ebx值要为0
    mov edx, 0x543d4150     ; edx只赋值一次，循环体中不会改变
    mov di, ards_buf        ; ards结构缓冲区
.e820_mem_get_loop:
    mov eax, 0x0000e820     ; 执行int 0x15后，eax值编程0x534d4150
                            ; 所以每次执行int前要更新为子功能号
    mov ecx, 20             ; ARDS地址范围描述符结构大小是20字节
    int 0x15
    jc .e820_failed_so_try_e801 ; 若cf位为1则有错误发生，尝试0xe801子功能
    add di, cx              ; cx为读取字节数，正常情况下是20个字节，将di指向缓冲区中新的ARDS结构位置
    inc word [ards_nr]      ; 记录ARDS数量
    cmp ebx, 0              ; 自动更新指向下一个待返回的ARDS结构，若ebx为0且cf不为1，说明ards全部返回  
                            ; 当前已是最后一个
    jnz .e820_mem_get_loop

; 在所有ards结构中找出(base_add_low + length_low)的最大值，即内存容量
    mov cx, [ards_nr]       ; 遍历每一个ARDS结构体，循环次数是ARDS的数量
    mov ebx, ards_buf
    xor edx, edx            ; edx为最大的内存容量，先清0
.find_max_mem_area:
    mov eax, [ebx]          ; base_add_low，基地址的低32位
    add eax, [ebx+8]        ; length_low，内存长度的低32位，以字节位单位
    add ebx, 20             ; 指向下一个ARDS结构体
    cmp edx, eax            ; 冒泡排序，找出最大，edx寄存器始终是最大内存容量
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok

; int 0x15 ax = 0xE801 获取内存大小，最大支持4G
; 返回后，ax和cx的值一样，以KB为单位，bx dx值一样，以64KB为单位
; 在ax和cx寄存器中为低16MB，在bx和dx寄存器中为16MB到4GB
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88    ; 若当前e801方法失败，尝试0x88方法

; 1. 先算出低15MB的内存, ax和cx中都是以KB为单位的内存数量，将其转换为以字节为单位
    mov cx, 0x400           ; 低于15MB的内存存放在ax中，以1KB为单位
    mul cx                  ; 现将其转换为以字节为单位
    shl edx, 16             ; 乘积的高位在edx中，先左移16位再求或就得到了完整的32位乘积
    and eax, 0x0000FFFF     
    or edx, eax
    add edx, 0x100000       ; ax只是15MB，故要加1MB，这是历史遗留问题
    mov esi, edx            ; 先把低15MB的内存容量存入esi寄存器备份

; 2. 再将16MB以上的内存转换为byte为单位
;    寄存器bx和dx中是以64KB为单位的内存数量
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000        ; 0x1000十进制为64KB，16MB~4GB内存放在BX和DX中，以64KB为单位，将其转换为以字节为单位
    mul ecx                 ; 32位乘法，默认的被乘数是eax，积为64位
                            ; 高32位存入edx，低32位存入eax
    add esi, eax            ; 由于此方法只能测出4GB以内的内存，故32为eax足够了
                            ; edx肯定为0，只加eax即可
    mov edx, esi            ; edx为总内存大小
    jmp .mem_get_ok

; int 0x15 ah = 0x88 获取内存大小，只能获取64MB以内的内存
.e801_failed_so_try88:
    ; int 15后，ax存入的是以KB为单位的内存容量
    mov ah, 0x88
    int 0x15
    jc .error_hlt
    and eax, 0x0000FFFF
    mov cx, 0x400           ; 乘以1K，将单位从KB转换为字节
    mul cx
    shl edx, 16
    or edx, eax
    add edx, 0x100000       ; 0x88子功能只会返回1M以上的内存
                            ; 故实际内存大小要加上1MB
    jmp .mem_get_ok

.error_hlt:
    jmp $

.mem_get_ok:
    mov [total_mem_bytes], edx

; ------------------------- 准备进入保护模式 ----------------------
    ; 1. 打开A20
    in al, 0x92
    or al, 0000_0010B
    out 0x92, al

    ; 2. 加载gdt
    lgdt [gdt_ptr]

    ; 3. 将cr0的pe位置1
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax
    jmp dword SELECTOR_CODE:p_mode_start    ; 刷新流水线

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA       ; 数据段
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP   
    mov ax, SELECTOR_VIDEO      ; 显存段  
    mov gs, ax

; ------------------------- 加载 kernel ----------------------
    mov eax, KERNEL_START_SECTOR    ; kernel.bin所在的扇区号
    mov ebx, KERNEL_BIN_BASE_ADDR   ; 从磁盘读出后，写入ebx地址处
    mov ecx, 200                    ; 读入扇区数
    
    call rd_disk_m_32

; ------------------ 创建并初始化页目录表和页表 ----------------------
    ; 创建页目录及页表并初始化页内存位图
    call setup_page
    
    ; 要将描述符地址及偏移量写入内存gdt_ptr，一会儿用新地址重新加载
    sgdt [gdt_ptr]

    ; 将gdt描述符中的显示段描述符中的基地址 + 0xc0000000，将显存段基址移动到内核空间中
    mov ebx, [gdt_ptr + 2]
    or dword [ebx + 0x18 + 4], 0xc0000000   ; 视频段是第3个段描述符，每个描述符是8字节
                                            ; 故0x18段描述符的高4字节的最高位是段基址第31~24位
    
    ; gdt的基址加上0xc0000000，表示将gdt表移动到内核中去
    add dword [gdt_ptr + 2], 0xc0000000
    
    add esp, 0xc0000000                     ; 将栈指针同样映射到内核地址

    ; 把页目录地址赋给cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ; 打开cr0的pg位(第31位)
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ;在开启分页后，用gdt新的地址重新加载
    lgdt [gdt_ptr]      

    jmp SELECTOR_CODE:enter_kernel ; 刷新流水线
enter_kernel:
    call kernel_init        ; 关于ELF格式这部分没有理解清楚，后续还要再看
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT

;---------- 将 kernel.bin 中的 segment 拷贝到编译的地址 -----------
kernel_init:
    xor eax, eax
    xor ebx, ebx        ; ebx记录程序头表地址
    xor ecx, ecx        ; cx记录程序头表中的program header数量
    xor edx, edx        ; dx记录program header尺寸，即e_phentsize

    mov dx, [KERNEL_BIN_BASE_ADDR + 42]     ; 偏移42字节处为e_phentsize，表示program header的大小
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]    ; 偏移28字节处为e_phoff，表示第1个program header在文件中的偏移
                                            ; 其实该值为0x34，不过还是谨慎一些，这里读取实际值
    add ebx, KERNEL_BIN_BASE_ADDR
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]     ; 偏移44字节处是e_phnum，表示program header的数量
    
.each_segment:
    cmp byte [ebx + 0], PT_NULL             ; 若p_type==PT_NULL，说明此program header未使用
    je .PTNULL

    ; 为函数memcpy压入参数, 参数从右往左压入
    ; 原型为memcpy(dst, src, size)
    push dword [ebx + 16]                   ; 程序头偏移16字节处为p_filesz
    mov eax, [ebx + 4]                      ; 程序头偏移4字节处为p_offset
    add eax, KERNEL_BIN_BASE_ADDR           ; 加上kernel.bin被加载到的物理地址，eax为该段的物理地址
    push eax                                ; 参数src压栈
    push dword [ebx + 8]                    ; 参数dest压栈
                                            ; 程序头偏移8字节处为p_vaddr，目的地址
    call mem_cpy
    add esp, 12
.PTNULL:
    add ebx, edx                            ; edx为program header的大小，即e_phentsize
                                            ; 在此指向下一个program header
    loop .each_segment
    ret

;---------- 逐字节拷贝 mem_cpy(dst,src,size) ------------
;输入:栈中三个参数(dst,src,size)
;输出:无
;---------------------------------------------------------
mem_cpy:
    cld
    push ebp
    mov ebp, esp
    push ecx                ; rep指令使用了ecx
    mov edi, [ebp + 8]      ; dest
    mov esi, [ebp + 12]     ; src
    mov ecx, [ebp + 16]     ; size
    rep movsb               ; 移动字节

    ; 恢复环境
    pop ecx
    pop ebp
    ret

; 创建页目录表及页表
setup_page:
; 先把页目录表占用的空间逐字节清0
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

; 开始创建页目录项
.create_pde:            ; 创建Page Directory Entry
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000     ; 此时eax为第一个页表的位置及属性,页目录表基址 + 4K
    mov ebx, eax        ; 此处为ebx赋值，是为.craete_pte做准备，ebx为第一个页表的基址

; 下面将页目录项0和0xc00都存为第一个页表的地址，每个页表表示4MB内存
; 这样0xc03fffff以下的地址和0x003fffff以下的地址都指向相同的页表
; 这是为将地址映射为内核地址做准备
; 这里将地址0x0~0x3fffff映射到0~4M
; 将虚拟地址0xc0000000~0xc03fffff也映射到0~4M
    or  eax, PG_US_U | PG_RW_W | PG_P        ; 页目录项的属性RW和P位为1，US为1，表示用户属性，所有特权级别都可以访问
    mov [PAGE_DIR_TABLE_POS + 0x0], eax     ; 第1个目录项
                                            ; 在页目录表中的第1个目录项写入第一个页表的位置(0x101000)及属性(7)
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax   ; 一个页表项占用4个字节
                                            ; 0xc00表示第768个页表占用的目录项, 0xc00以上的目录项用于内核空间   
                                            ; 也就是页表的0xc0000000~0xffffffff共1G，属于内核
                                            ; 0x0~0xbfffffff共计3G属于用户进程
    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax    ; 使最后一个目录项指向页目录表自己的地址

; 下面创建页表项(PTE),为我们所使用的1M空间分配物理页，即设置页表项
    xor edx, edx
    mov ecx, 256            ; 1M低端内存 / 每页大小4k = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P   ; 属性为7，US=1，RW=1，P=1
.create_pte:    ; 创建 Page Table Entry
    mov [ebx+esi*4], edx    ; 此时的ebx已经在上面通过eax赋值为0x101000，也就是第一个页表的地址
    add edx, 4096
    inc esi
    loop .create_pte

; 创建内核其他页表的PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000             ; 此时eax为第二个页表的位置
    or eax, PG_US_U | PG_RW_W | PG_P    ; 页目录项的属性US、RW和P位都为1
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254                ; 范围为769~1022的所有目录项数量
    mov esi, 769

.create_kernel_pde:
    mov [ebx+esi*4], eax        
    inc esi
    add eax, 0x1000             ; 指向下一个页表
    loop .create_kernel_pde
    ret


;------------------------ rd_disk_m_32  在mbr.S复制粘贴过来的 修改了点代码 ----------------------
rd_disk_m_32:
    ;1 写入待操作磁盘数
    ;2 写入LBA 低24位寄存器 确认扇区
    ;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
    ;4 command 写指令
    ;5 读取status状态寄存器 判断是否完成工作
    ;6 完成工作 取出数据
     
     ;;;;;;;;;;;;;;;;;;;;;
     ;1 写入待操作磁盘数
     ;;;;;;;;;;;;;;;;;;;;;
        mov esi,eax   ; !!! 备份eax
        mov di,cx     ; !!! 备份cx
        
        mov dx,0x1F2  ; 0x1F2为Sector Count 端口号 送到dx寄存器中
        mov al,cl     ; !!! 忘了只能由ax al传递数据
        out dx,al     ; !!! 这里修改了 原out dx,cl
        
        mov eax,esi   ; !!!袄无! 原来备份是这个用 前面需要ax来传递数据 麻了
        
    ;;;;;;;;;;;;;;;;;;;;;
    ;2 写入LBA 24位寄存器 确认扇区
    ;;;;;;;;;;;;;;;;;;;;;
        mov cl,0x8    ; shr 右移8位 把24位给送到 LBA low mid high 寄存器中
    
        mov dx,0x1F3  ; LBA low
        out dx,al 
        
        mov dx,0x1F4  ; LBA mid
        shr eax,cl    ; eax为32位 ax为16位 eax的低位字节 右移8位即8~15
        out dx,al
        
        mov dx,0x1F5
        shr eax,cl
        out dx,al
        
    ;;;;;;;;;;;;;;;;;;;;;
    ;3 device 寄存器 第4位主次盘 第6位LBA模式 改为1
    ;;;;;;;;;;;;;;;;;;;;;
    
                 
                  ; 24 25 26 27位 尽管我们知道ax只有2 但还是需要按规矩办事 
                  ; 把除了最后四位的其他位置设置成0
        shr eax,cl
        
        and al,0x0f 
        or al,0xe0   ;!!! 把第四-七位设置成0111 转换为LBA模式
        mov dx,0x1F6 ; 参照硬盘控制器端口表 Device 
        out dx,al
    
    ;;;;;;;;;;;;;;;;;;;;;
    ;4 向Command写操作 Status和Command一个寄存器
    ;;;;;;;;;;;;;;;;;;;;;
    
        mov dx,0x1F7 ; Status寄存器端口号
        mov ax,0x20  ; 0x20是读命令
        out dx,al
        
    ;;;;;;;;;;;;;;;;;;;;;
    ;5 向Status查看是否准备好惹 
    ;;;;;;;;;;;;;;;;;;;;;
        
               ;设置不断读取重复 如果不为1则一直循环
      .not_ready:     
        nop           ; !!! 空跳转指令 在循环中达到延时目的
        in al,dx      ; 把寄存器中的信息返还出来
        and al,0x88   ; !!! 0100 0100 0x88
        cmp al,0x08
        jne .not_ready ; !!! jump not equal == 0
        
        
    ;;;;;;;;;;;;;;;;;;;;;
    ;6 读取数据
    ;;;;;;;;;;;;;;;;;;;;;
    
        mov ax,di      ;把 di 储存的cx 取出来
        mov dx,256
        mul dx        ;与di 与 ax 做乘法 计算一共需要读多少次 方便作循环 低16位放ax 高16位放dx
        mov cx,ax      ;loop 与 cx相匹配 cx-- 当cx == 0即跳出循环
        mov dx,0x1F0
     .go_read_loop:
        in ax,dx      ;两字节dx 一次读两字
        mov [ebx],ax
        add ebx,2
        loop .go_read_loop
        ret ;与call 配对返回原来的位置 跳转到call下一条指令
