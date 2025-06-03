TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0
section .data
put_int_buffer dq 0    ; 8字节，存储一个32位整数转换后的字符串

[bits 32]
section .text
;------------------------ put_str ------------------------------
;功能描述：put_str通过put_char来打印以0字符结尾的字符串
;---------------------------------------------------------------
global put_str
put_str:
    push ebx
    push ecx
    xor ecx, ecx
    mov ebx, [esp + 12]     ; 获取待打印的字符串
.goon:
    mov cl, [ebx]
    cmp cl, 0               ; 当前字符不为'\0'
    je .str_over
    push ecx                ; 插入字符，作为put_char的参数
    call put_char
    add esp, 4              ; 回收参数占用的栈空间
    inc ebx
    jmp .goon

.str_over:
    pop ecx
    pop ebx
    ret

;------------------------ put_char -----------------------------
;功能描述:把栈中的 1 个字符写入光标所在处
;---------------------------------------------------------------
global put_char     ; 声明全局函数
put_char:
    pushad          ; 将8个通用寄存器压入栈中
    mov ax, SELECTOR_VIDEO ; 选择视频段
    mov gs, ax      ; 因为从内核态返回时CPU将gs的值置为0，因此每次调用前需要重新准备选择子

; 获取当前光标位置
    mov dx, 0x03d4  ; 索引寄存器
    mov al, 0x0e    ; 用于提供光标位置的高8位
    out dx, al
    mov dx, 0x03d5
    in al, dx       ; 读取光标位置的高8位
    mov ah, al      ; 保存高8位

    mov dx, 0x03d4  ; 索引寄存器
    mov al, 0x0f    ; 用于提供光标位置的低8位
    out dx, al
    mov dx, 0x03d5
    in al, dx       ; 读取光标位置的低8位

    ; 将光标存入bx
    mov bx, ax
    mov ecx, [esp + 36] ; 获取待打印字符，栈前面有8个通用寄存器和返回地址

    cmp cl, 0xd         ; 回车CR是0xd, 换行LF是0xa
    je .is_carriage_return 
    cmp cl, 0xa
    je .is_line_feed

    cmp cl, 0x8        ; BS(backspace)的 asc 码是 8
    je .is_backspace
    jmp .put_other

.is_backspace:
    cmp bx, 0
    je set_cursor

    dec bx
    shl bx, 1      ; 光标位置乘2，得到光标对应显存中的偏移字节,后续除以2
    mov word [gs:bx], 0x0720 ; 将该位置填充上空格
    shr bx, 1
    jmp set_cursor

.put_other:
    shl bx, 1

    mov [gs:bx], cl         ; 将字符写入显存
    inc bx
    mov byte [gs:bx], 0x07 ; 字符属性
    inc bx
    shr bx, 1
    cmp bx, 2000
    jl set_cursor          ; 光标值小于2000，说明未到达最后一行
                            ; 否则需要换行

.is_line_feed:              ; 处理换行符 LF(\n)
.is_carriage_return:        ; 处理回车符 CR(\r)

; 如果是CR，将光标移动到行首
    xor dx, dx
    mov ax, bx
    mov si, 80
    div si                  ; 当前光标值除80
    sub bx, dx              ; 将余数从光标值中减去，表示回车

.is_carriage_return_end:    
    add bx, 80              ; 加80表示使光标指向下一行
    cmp bx, 2000            ; 如果光标值大于2000，需要滚屏
.is_line_feed_end:
    jl set_cursor

; 屏幕行范围为0~24，通过将屏幕第1~24行移动到0~23行实现滚屏
; 将第24行使用空格填充
.roll_screen:
    cld             ; 每次调用movsd，esi和edi自动增加
    mov ecx, 960    ; 需要搬2000-80=1920个字符，共1920*2=3840个字节
                    ; 一次搬运4个字节，共3840/4=960次
    mov esi, 0xc00b80a0     ; 第1行行首
    mov edi, 0xc00b8000     ; 第0行行首
    rep movsd                      

; 将最后一行填充为空白
    mov ebx, 3840   ; 最后一行第一个字节的偏移为1920*2 = 3840
    mov ecx, 80     ; 一行80个字符

.cls:
    mov word [gs:ebx], 0x0720
    add ebx, 2
    loop .cls
    mov bx, 1920

global set_cursor
set_cursor:        ; 将光标设置为bx的值
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5  
    mov al, bh      ; 设置高8位
    out dx, al

    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al

.put_char_done:
    popad
    ret


;----------将小端字节序的数字变成对应的 ASCII 后,倒置----------
;输入:栈中参数为待打印的数字
;输出:在屏幕上打印十六进制数字,并不会打印前缀 0x
;如打印十进制 15 时,只会直接打印 f,不会是 0xf
;-----------------------------------------------------------
global put_int
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp + 4 * 9]      ; call的返回地址占4字节 + pushad的8个4字节
    mov edx, eax
    mov edi, 7                  ; 指定在put_int_buffer中初始的偏移量
    mov ecx, 8                  ; 32位数字，十六进制位数为8
    mov ebx, put_int_buffer

; 将32位数字按照十六进制形式从低位到高位逐个处理，共处理8个十六进制数
.16based_4bits:
    and edx, 0x0000000F
    cmp edx, 9                  ; 对于0~9和a~f分别处理
    jg .is_A2F
    add edx, '0'                ; 将其转换为字符
    jmp .store
.is_A2F:
    sub edx, 10                
    add edx, 'A'

.store:
    mov [ebx + edi], dl         ; 低位数字放在高地址处，类似大端法
    dec edi
    shr eax, 4
    mov edx, eax
    loop .16based_4bits

; 打印put_int_buffer之前把高位连续字符去掉
.ready_to_print:
    inc edi                     ; edi+1，变为0
.skip_prefix_0:
    cmp edi, 8                  ; 找出连续的前导0字符
    je .full0

; edi为第一个非0字符的偏移
.go_on_skip:
    mov cl, [put_int_buffer + edi]
    inc edi
    cmp cl, '0'
    je .skip_prefix_0
    dec edi
    jmp .put_each_num

.full0:
    mov cl, '0'

.put_each_num:
    push ecx                    ; 此时cl中为可打印字符
    call put_char
    add esp, 4
    inc edi
    mov cl, [put_int_buffer+edi]
    cmp edi, 8
    jl .put_each_num
    popad
    ret


global cls_screen
cls_screen:
   pushad
   ;;;;;;;;;;;;;;;
	; 由于用户程序的cpl为3,显存段的dpl为0,故用于显存段的选择子gs在低于自己特权的环境中为0,
	; 导致用户程序再次进入中断后,gs为0,故直接在put_str中每次都为gs赋值. 
   mov ax, SELECTOR_VIDEO	       ; 不能直接把立即数送入gs,须由ax中转
   mov gs, ax

   mov ebx, 0
   mov ecx, 80*25
 .cls:
   mov word [gs:ebx], 0x0720		  ;0x0720是黑底白字的空格键
   add ebx, 2
   loop .cls 
   mov ebx, 0

 .set_cursor:				  ;直接把set_cursor搬过来用,省事
;;;;;;; 1 先设置高8位 ;;;;;;;;
   mov dx, 0x03d4			  ;索引寄存器
   mov al, 0x0e				  ;用于提供光标位置的高8位
   out dx, al
   mov dx, 0x03d5			  ;通过读写数据端口0x3d5来获得或设置光标位置 
   mov al, bh
   out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5 
   mov al, bl
   out dx, al
   popad
   ret
   