#ifndef __DEVICE_KEYBOARD_H
#define __DEVICE_KEYBOARD_H

extern struct ioqueue kbd_buf; // 为了测试

void keyboard_init();
static void intr_keyboard_handler(void);


#endif