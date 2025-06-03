[bits 32]
%define ERROR_CODE nop      ; 在相关异常中已自动压入错误码，这里不做任何操作
%define ZERO push 0         ; 若没有自动压入错误码，则手动压入一个0

extern put_str
extern idt_table

section .data
global intr_entry_table     
intr_entry_table:

%macro VECTOR 2
section .text
intr%1entry:        ; 定义各自的中断向量处理程序
    %2

    push ds         ; 保存上下文环境
    push es
    push fs
    push gs
    pushad

    ; 如果是从从片上进入中断，除了往从片上发送EOI外，还要往主片上发送EOI
    mov al, 0x20    ; 中断结束命令
    out 0xa0, al    ; 向从片发送
    out 0x20, al    ; 向主片发送

    push %1         ; 不管idt_table中的目标程序是否需要参数
    call [idt_table + %1*4] ; 将中断请求转发到idt_table中的中断处理函数去
    jmp intr_exit 

section .data
    dd intr%1entry  ; 存储各个中断入口程序的地址
%endmacro

section .text
global intr_exit
intr_exit:  
    add esp, 4      ; 跳过中断号
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4      ; 跳过error_code
    iretd


VECTOR 0x00, ZERO
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO
VECTOR 0x08, ZERO
VECTOR 0x09, ZERO
VECTOR 0x0a, ZERO
VECTOR 0x0b, ZERO
VECTOR 0x0c, ZERO
VECTOR 0x0d, ZERO
VECTOR 0x0e, ZERO
VECTOR 0x0f, ZERO
VECTOR 0x10, ZERO
VECTOR 0x11, ZERO
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO
VECTOR 0x18, ZERO
VECTOR 0x19, ZERO
VECTOR 0x1a, ZERO
VECTOR 0x1b, ZERO
VECTOR 0x1c, ZERO
VECTOR 0x1d, ZERO
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO
VECTOR 0x20, ZERO       ; 时钟中断入口
VECTOR 0x21, ZERO       ; 键盘中断入口
VECTOR 0x22, ZERO       ; 级联用的
VECTOR 0x23, ZERO       ; 串口2对应入口
VECTOR 0x24, ZERO       ; 串口1对应入口
VECTOR 0x25, ZERO       ; 并口2对应入口
VECTOR 0x26, ZERO       ; 软盘对应入口
VECTOR 0x27, ZERO       ; 并口1对应入口
VECTOR 0x28, ZERO       ; 实时时钟对应入口
VECTOR 0x29, ZERO       ; 重定向
VECTOR 0x2a, ZERO       ; 保留
VECTOR 0x2b, ZERO       ; 保留
VECTOR 0x2c, ZERO       ; ps/2鼠标
VECTOR 0x2d, ZERO       ; fpu浮点单元异常
VECTOR 0x2e, ZERO       ; 硬盘
VECTOR 0x2f, ZERO       ; 保留


;;;;;;;;;;;;;;;; 0x80 号中断 ;;;;;;;;;;;;;;;;
[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
    ; 1. 保存上下文
    push 0      ; error_code, 使栈中格式统一，无意义

    push ds
    push es
    push fs
    push gs
    pushad      ; 压入8个通用寄存器

    push 0x80   ; 压入中断向量号，也是为了保持栈格式统一

    ; 2. 为系统调用压入参数
    push edx    ; 第三个参数
    push ecx    ; 第二个参数
    push ebx    ; 第一个参数

    ; 3. 调用系统调用号对应的处理函数
    call [syscall_table + eax * 4]  ; 所有处理函数都按系统调用号放在syscall_table中
    add esp, 12

    ; 4. 将call之后的返回值放入内核栈的eax中
    mov [esp + 8 * 4], eax          ; 放到pushd压入的eax在栈中的位置，之后由popd弹出
    jmp intr_exit