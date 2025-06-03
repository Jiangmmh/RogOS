; 主引导程序

%include "boot.inc"
SECTION MBR vstart=0x7c00
    mov ax, cs  ; BIOS最后的指令为 jmp 0:0x7c00，将cs设置为0
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00  ; 初始化栈指针
    mov ax, 0xb800  ; 让gs指向显存的位置
    mov gs, ax


; 清屏
; 输入：
; AH 功能号 = 0x06
; AL = 上卷的行数(0表全部)
; BH = 上卷行的属性
; (CL, CH) = 窗口左下角(X, Y)位置
; (DL, DH) = 窗口右下角(X, Y)位置
; 无返回值
    mov ax, 0x600
    mov bx, 0x700
    mov cx, 0       ; 左下角(0, 0)
    mov dx, 0x184f  ; 右下角(80,25)
    int 0x10

; 输出背景色绿色，前景色红色，并且跳动的字符串"1 MBR"
    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xA4

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

    ; 这里选择直接使用寄存器传递参数
    mov eax, LOADER_START_SECTOR    ; eax中存放起始扇区的LBA号
    mov ebx, LOADER_BASE_ADDR       ; ebx存放目的地址
    mov cx, 4                       ; 读入4个扇区
    call rd_disk_m_16

    jmp LOADER_BASE_ADDR + 0x300    ; 这里加上0x300是为了避免在loader最前面加上jmp，实现对齐

;-----------------------------------------------------------
; 功能：读取硬盘n个扇区
;-----------------------------------------------------------
rd_disk_m_16:
    mov esi, eax    ; 备份eax到esi中，因为后面out要用到al
    mov di, cx      ; 备份cx到di

; 读写硬盘
; 1. 设置要读取的扇区数
    mov dx, 0x1f2   ; 指定端口号
    mov al, cl
    out dx, al      ; 读取的扇区数

    mov eax, esi    ; 恢复ax

; 2. 将LBA地址存入0x1f3 ~ 0x1f6
    
    ; LBA地址7~0位写入端口0x1f3 
    mov dx, 0x1f3
    out dx, al

    ; LBA地址15~8位写入端口0x1f4
    mov cl, 8
    shr eax, cl
    mov dx, 0x1f4
    out dx, al

    ; LBA地址23~16位写入端口0x1f5 
    shr eax, cl
    mov dx, 0x1f5
    out dx, al

    shr eax, cl
    and al, 0x0f    ; LBA第24~27位
    or al, 0xe0     ; 设置7~4位为1110, 表示LBA模式
    mov dx, 0x1f6
    out dx, al

; 3. 向0x1f7端口写入读命令，0x20
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

; 4. 检测硬盘状态
.not_ready:
    ; 同一端口，写时表示写入命令字，读时表示读入硬盘状态
    nop
    in al, dx
    and al, 0x88    ; 第3位为1表示硬盘控制器已准备好数据传输
                    ; 第7位为1表示硬盘忙
    cmp al, 0x08    ; 在第7位为0，第3位为1时表示硬盘准备好了读取数据
    jnz .not_ready  ; 未准备好，继续等

; 5. 从0x1f0端口读入数据
    mov ax, di
    mov dx, 256
    mul dx
    mov cx, ax      ; di为要读取的扇区数，一个扇区512字节，每次读入一个字
                    ; 因此共需 di*512/2次，即di*256
    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [bx], ax    ; 读取一个字
    add bx, 2
    loop .go_on_read ; 直至cx等于0时，结束循环
    ret

times 510-($-$$) db 0  ; padding满512个字节 $标识本行的偏移 $$表示当前段的偏移
db 0x55, 0xaa          ; 在末尾放入0x55和0xaa标识MBR