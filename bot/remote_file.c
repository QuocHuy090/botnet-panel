/**
 * remote_file.c
 * Quan ly file tu xa: upload, download, duyet thu muc, xoa, nen/giai nen
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

#include "remote_file.h"

#ifdef _WIN32
/* ==================== WINDOWS FILE OPERATIONS ==================== */

/**
 * Duyet thu muc va liet ke file
 */
int duyet_thu_muc(const char* duong_dan, char* ket_qua, size_t ket_qua_size) {
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", duong_dan);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) {
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"Khong the mo thu muc\"}");
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"path\":\"%s\",\"files\":[", duong_dan);
    int dau_tien = 1;
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        if (!dau_tien) {
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
        }
        dau_tien = 0;
        
        int la_thu_muc = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        
        /* Escape ten file */
        offset += snprintf(ket_qua + offset, ket_qua_size - offset,
            "{\"name\":\"%s\",\"size\":%lu,\"is_dir\":%s,\"modified\":%lu}",
            find_data.cFileName,
            la_thu_muc ? 0 : find_data.nFileSizeLow,
            la_thu_muc ? "true" : "false",
            (unsigned long)find_data.ftLastWriteTime.dwLowDateTime);
        
    } while (FindNextFileA(find_handle, &find_data));
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    FindClose(find_handle);
    
    return 0;
}

/**
 * Upload file (doc file va tra ve base64)
 */
int upload_file(const char* duong_dan, char* ket_qua, size_t ket_qua_size) {
    HANDLE file = CreateFileA(duong_dan, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"Khong the mo file\"}");
        return -1;
    }
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size > 5242880) { /* Gioi han 5MB */
        CloseHandle(file);
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"File qua lon (>5MB)\"}");
        return -1;
    }
    
    unsigned char* buffer = (unsigned char*)malloc(file_size);
    if (buffer == NULL) {
        CloseHandle(file);
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"Khong du bo nho\"}");
        return -1;
    }
    
    DWORD bytes_doc;
    ReadFile(file, buffer, file_size, &bytes_doc, NULL);
    CloseHandle(file);
    
    /* Base64 encode */
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_size = ((file_size + 2) / 3) * 4 + 1;
    char* b64 = (char*)malloc(b64_size);
    
    if (b64 != NULL) {
        size_t b64_offset = 0;
        for (DWORD i = 0; i < file_size; i += 3) {
            unsigned char a = buffer[i];
            unsigned char b = (i + 1 < file_size) ? buffer[i + 1] : 0;
            unsigned char c = (i + 2 < file_size) ? buffer[i + 2] : 0;
            
            b64[b64_offset++] = b64_chars[a >> 2];
            b64[b64_offset++] = b64_chars[((a & 0x03) << 4) | (b >> 4)];
            b64[b64_offset++] = (i + 1 < file_size) ? b64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
            b64[b64_offset++] = (i + 2 < file_size) ? b64_chars[c & 0x3f] : '=';
        }
        b64[b64_offset] = '\0';
        
        snprintf(ket_qua, ket_qua_size, "{\"filename\":\"%s\",\"size\":%lu,\"data\":\"%s\"}",
                 duong_dan, file_size, b64);
        free(b64);
    }
    
    free(buffer);
    return 0;
}

/**
 * Download file (ghi du lieu tu base64 xuong file)
 */
int download_file(const char* duong_dan, const char* du_lieu_base64) {
    /* Giai ma base64 */
    size_t b64_len = strlen(du_lieu_base64);
    size_t max_size = (b64_len * 3) / 4 + 1;
    unsigned char* buffer = (unsigned char*)malloc(max_size);
    if (buffer == NULL) return -1;
    
    /* Giai ma base64 don gian */
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_idx = 0;
    int padding = 0;
    
    for (size_t i = 0; i < b64_len; i += 4) {
        unsigned char quad[4];
        for (int j = 0; j < 4; j++) {
            if (i + j >= b64_len) {
                quad[j] = 0;
                padding++;
                continue;
            }
            char c = du_lieu_base64[i + j];
            if (c == '=') {
                quad[j] = 0;
                padding++;
                continue;
            }
            const char* pos = strchr(b64_chars, c);
            quad[j] = pos ? (unsigned char)(pos - b64_chars) : 0;
        }
        
        if (out_idx < max_size) buffer[out_idx++] = (quad[0] << 2) | (quad[1] >> 4);
        if (padding < 2 && out_idx < max_size) buffer[out_idx++] = (quad[1] << 4) | (quad[2] >> 2);
        if (padding < 1 && out_idx < max_size) buffer[out_idx++] = (quad[2] << 6) | quad[3];
        if (padding > 0) break;
    }
    
    /* Ghi file */
    HANDLE file = CreateFileA(duong_dan, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        free(buffer);
        return -1;
    }
    
    DWORD bytes_ghi;
    WriteFile(file, buffer, (DWORD)out_idx, &bytes_ghi, NULL);
    CloseHandle(file);
    free(buffer);
    
    return (bytes_ghi == out_idx) ? 0 : -1;
}

/**
 * Xoa file hoac thu muc
 */
int xoa_file(const char* duong_dan) {
    DWORD attr = GetFileAttributesA(duong_dan);
    if (attr == INVALID_FILE_ATTRIBUTES) return -1;
    
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        /* Xoa thu muc */
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", duong_dan);
        system(cmd);
        return 0;
    } else {
        /* Xoa file */
        if (DeleteFileA(duong_dan)) return 0;
        return -1;
    }
}

/**
 * Nen/giai nen file (dung PowerShell)
 */
int nen_giai_nen(const char* duong_dan, const char* thao_tac) {
    if (strcmp(thao_tac, "zip") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "powershell -Command \"Compress-Archive -Path '%s' -DestinationPath '%s.zip' -Force\"",
                 duong_dan, duong_dan);
        system(cmd);
        return 0;
    } else if (strcmp(thao_tac, "unzip") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s_extracted' -Force\"",
                 duong_dan, duong_dan);
        system(cmd);
        return 0;
    }
    return -1;
}

#else
/* ==================== LINUX/MACOS FILE OPERATIONS ==================== */

int duyet_thu_muc(const char* duong_dan, char* ket_qua, size_t ket_qua_size) {
    DIR* dir = opendir(duong_dan);
    if (dir == NULL) {
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"Khong the mo thu muc\"}");
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"path\":\"%s\",\"files\":[", duong_dan);
    int dau_tien = 1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", duong_dan, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        if (!dau_tien) {
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
        }
        dau_tien = 0;
        
        offset += snprintf(ket_qua + offset, ket_qua_size - offset,
            "{\"name\":\"%s\",\"size\":%ld,\"is_dir\":%s,\"modified\":%ld}",
            entry->d_name, S_ISDIR(st.st_mode) ? 0 : (long)st.st_size,
            S_ISDIR(st.st_mode) ? "true" : "false", (long)st.st_mtime);
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    closedir(dir);
    return 0;
}

int upload_file(const char* duong_dan, char* ket_qua, size_t ket_qua_size) {
    FILE* f = fopen(duong_dan, "rb");
    if (f == NULL) {
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"Khong the mo file\"}");
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size > 5242880) {
        fclose(f);
        snprintf(ket_qua, ket_qua_size, "{\"error\":\"File qua lon\"}");
        return -1;
    }
    fseek(f, 0, SEEK_SET);
    
    unsigned char* buffer = (unsigned char*)malloc(size);
    if (buffer == NULL) {
        fclose(f);
        return -1;
    }
    
    fread(buffer, 1, size, f);
    fclose(f);
    
    /* Base64 encode */
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_size = ((size + 2) / 3) * 4 + 1;
    char* b64 = (char*)malloc(b64_size);
    
    if (b64 != NULL) {
        size_t b64_offset = 0;
        for (long i = 0; i < size; i += 3) {
            unsigned char a = buffer[i];
            unsigned char b = (i + 1 < size) ? buffer[i + 1] : 0;
            unsigned char c = (i + 2 < size) ? buffer[i + 2] : 0;
            b64[b64_offset++] = b64_chars[a >> 2];
            b64[b64_offset++] = b64_chars[((a & 0x03) << 4) | (b >> 4)];
            b64[b64_offset++] = (i + 1 < size) ? b64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
            b64[b64_offset++] = (i + 2 < size) ? b64_chars[c & 0x3f] : '=';
        }
        b64[b64_offset] = '\0';
        
        snprintf(ket_qua, ket_qua_size, "{\"filename\":\"%s\",\"size\":%ld,\"data\":\"%s\"}",
                 duong_dan, size, b64);
        free(b64);
    }
    
    free(buffer);
    return 0;
}

int download_file(const char* duong_dan, const char* du_lieu_base64) {
    /* Giai ma base64 */
    size_t b64_len = strlen(du_lieu_base64);
    size_t max_size = (b64_len * 3) / 4 + 1;
    unsigned char* buffer = (unsigned char*)malloc(max_size);
    if (buffer == NULL) return -1;
    
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_idx = 0;
    int padding = 0;
    
    for (size_t i = 0; i < b64_len; i += 4) {
        unsigned char quad[4];
        for (int j = 0; j < 4; j++) {
            if (i + j >= b64_len) { quad[j] = 0; padding++; continue; }
            char c = du_lieu_base64[i + j];
            if (c == '=') { quad[j] = 0; padding++; continue; }
            const char* pos = strchr(b64_chars, c);
            quad[j] = pos ? (unsigned char)(pos - b64_chars) : 0;
        }
        if (out_idx < max_size) buffer[out_idx++] = (quad[0] << 2) | (quad[1] >> 4);
        if (padding < 2 && out_idx < max_size) buffer[out_idx++] = (quad[1] << 4) | (quad[2] >> 2);
        if (padding < 1 && out_idx < max_size) buffer[out_idx++] = (quad[2] << 6) | quad[3];
        if (padding > 0) break;
    }
    
    FILE* f = fopen(duong_dan, "wb");
    if (f == NULL) { free(buffer); return -1; }
    
    fwrite(buffer, 1, out_idx, f);
    fclose(f);
    free(buffer);
    return 0;
}

int xoa_file(const char* duong_dan) {
    struct stat st;
    if (stat(duong_dan, &st) != 0) return -1;
    
    if (S_ISDIR(st.st_mode)) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", duong_dan);
        system(cmd);
        return 0;
    } else {
        if (unlink(duong_dan) == 0) return 0;
        return -1;
    }
}

int nen_giai_nen(const char* duong_dan, const char* thao_tac) {
    if (strcmp(thao_tac, "zip") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "cd \"%s/..\" && zip -r \"%s.zip\" \"%s\" 2>/dev/null",
                 duong_dan, duong_dan, duong_dan);
        system(cmd);
        return 0;
    } else if (strcmp(thao_tac, "unzip") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "unzip -o \"%s\" -d \"%s_extracted\" 2>/dev/null",
                 duong_dan, duong_dan);
        system(cmd);
        return 0;
    }
    return -1;
}

#endif