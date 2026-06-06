#ifndef REMOTE_FILE_H
#define REMOTE_FILE_H
#include <stddef.h>
int duyet_thu_muc(const char* duong_dan, char* ket_qua, size_t ket_qua_size);
int upload_file(const char* duong_dan, char* ket_qua, size_t ket_qua_size);
int download_file(const char* duong_dan, const char* du_lieu_base64);
int xoa_file(const char* duong_dan);
int nen_giai_nen(const char* duong_dan, const char* thao_tac);
#endif