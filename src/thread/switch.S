[bits 32]
section .text
global switch_to
switch_to:
    ; 栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20] ; 得到栈中的参数cur，即当前线程的PCB
    mov [eax], esp      ; 保存栈顶指针esp到PCB中的self_kstack中

    ;------- 以上是备份当前线程的环境,下面是恢复下一个线程的环境 ---------
    mov eax, [esp + 24] ; 得到参数next，即下一个线程的PCB
    mov esp, [eax]      ; PCB的第一个成员是内核栈的栈顶指针，在其中保存了用户栈的栈顶指针

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret         ; 若未由中断进入,第一次执行时会返回到 kernel_thread
