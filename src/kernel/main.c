#include "print.h"
#include "init.h"
#include "fork.h"
#include "stdio.h"
#include "syscall.h"
#include "debug.h"
#include "shell.h"
#include "console.h"
#include "ide.h"

void init(void);

int main(void) {
   put_str("I am kernel\n");
   init_all();

/*************    写入应用程序    *************/
   uint32_t file_size = 4488; 
   uint32_t sec_cnt = DIV_ROUND_UP(file_size, 512);
   struct disk* sda = &channels[0].device[0];
   void* prog_buf = sys_malloc(file_size);
   ide_read(sda, 300, prog_buf, sec_cnt);
   int32_t fd = sys_open("/prog_no_arg", O_CREAT|O_RDWR);
   if (fd != -1) {
      if(sys_write(fd, prog_buf, file_size) == -1) {
         printf("file write error!\n");
         while(1);
      }
   }
//    my_shell();
/*************    写入应用程序结束   *************/
   cls_screen();
   console_put_str("[rog@localhost /]$ ");

   while(1);
   return 0;
}

/* init进程 */
void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid)
    { // 父进程
        while (1)
            ;
    }
    else
    { // 子进程
        my_shell();
    }
    PANIC("init: should not be here");
}