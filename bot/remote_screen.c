/**
 * remote_screen.c
 * Chup man hinh, chup webcam, ghi am thanh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <gdiplus.h>
    #pragma comment(lib, "gdiplus.lib")
    #pragma comment(lib, "ole32.lib")
#else
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#include "remote_screen.h"

#ifdef _WIN32
/* ==================== WINDOWS SCREENSHOT ==================== */

/**
 * Lay encoder CLSID cho GDI+
 */
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;
    
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    pImageCodecInfo = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (pImageCodecInfo == NULL) return -1;
    
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; j++) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    
    free(pImageCodecInfo);
    return -1;
}

/**
 * Chup man hinh va tra ve base64
 */
int chup_man_hinh(char* ket_qua, size_t ket_qua_size) {
    /* Khoi tao GDI+ */
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    /* Lay man hinh */
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, screen_width, screen_height);
    HBITMAP old_bitmap = (HBITMAP)SelectObject(mem_dc, bitmap);
    
    BitBlt(mem_dc, 0, 0, screen_width, screen_height, screen_dc, 0, 0, SRCCOPY);
    
    /* Luu thanh JPEG trong bo nho */
    Gdiplus::Bitmap* gdi_bitmap = Gdiplus::Bitmap::FromHBITMAP(bitmap, NULL);
    
    /* Tao stream */
    IStream* stream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    
    CLSID jpeg_clsid;
    GetEncoderClsid(L"image/jpeg", &jpeg_clsid);
    
    gdi_bitmap->Save(stream, &jpeg_clsid, NULL);
    
    /* Lay du lieu tu stream */
    LARGE_INTEGER pos;
    pos.QuadPart = 0;
    stream->Seek(pos, STREAM_SEEK_SET, NULL);
    
    STATSTG stat;
    stream->Stat(&stat, STATFLAG_NONAME);
    DWORD image_size = (DWORD)stat.cbSize.QuadPart;
    
    unsigned char* image_data = (unsigned char*)malloc(image_size);
    if (image_data != NULL) {
        ULONG bytes_doc;
        stream->Read(image_data, image_size, &bytes_doc);
        
        /* Base64 encode */
        const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t b64_size = ((image_size + 2) / 3) * 4 + 1;
        char* b64 = (char*)malloc(b64_size);
        
        if (b64 != NULL) {
            size_t b64_offset = 0;
            for (DWORD i = 0; i < image_size; i += 3) {
                unsigned char a = image_data[i];
                unsigned char b = (i + 1 < image_size) ? image_data[i + 1] : 0;
                unsigned char c = (i + 2 < image_size) ? image_data[i + 2] : 0;
                b64[b64_offset++] = b64_chars[a >> 2];
                b64[b64_offset++] = b64_chars[((a & 0x03) << 4) | (b >> 4)];
                b64[b64_offset++] = (i + 1 < image_size) ? b64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
                b64[b64_offset++] = (i + 2 < image_size) ? b64_chars[c & 0x3f] : '=';
            }
            b64[b64_offset] = '\0';
            
            snprintf(ket_qua, ket_qua_size,
                "{\"type\":\"screenshot\",\"format\":\"jpeg\",\"width\":%d,\"height\":%d,\"data\":\"%s\"}",
                screen_width, screen_height, b64);
            free(b64);
        }
        free(image_data);
    }
    
    stream->Release();
    delete gdi_bitmap;
    SelectObject(mem_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
    
    Gdiplus::GdiplusShutdown(gdiplusToken);
    
    return 0;
}

/**
 * Chup anh tu webcam (stub - can DirectShow)
 */
int chup_webcam(char* ket_qua, size_t ket_qua_size) {
    snprintf(ket_qua, ket_qua_size, "{\"type\":\"webcam\",\"status\":\"chua_ho_tro\"}");
    return -1;
}

/**
 * Ghi am tu microphone (stub)
 */
int ghi_am(int so_giay, char* ket_qua, size_t ket_qua_size) {
    snprintf(ket_qua, ket_qua_size, 
        "{\"type\":\"audio\",\"duration\":%d,\"status\":\"chua_ho_tro\"}", so_giay);
    return -1;
}

#else
/* ==================== LINUX/MACOS SCREENSHOT ==================== */

int chup_man_hinh(char* ket_qua, size_t ket_qua_size) {
    /* Dung xdotool hoac scrot de chup man hinh */
    char temp_file[256];
    snprintf(temp_file, sizeof(temp_file), "/tmp/screenshot_%d.png", (int)time(NULL));
    
    /* Thu nhieu cach */
    int ret = system("which scrot > /dev/null 2>&1 && scrot -q 50 /tmp/screenshot_$$.png 2>/dev/null");
    if (ret != 0) {
        ret = system("which import > /dev/null 2>&1 && import -window root /tmp/screenshot_$$.png 2>/dev/null");
    }
    if (ret != 0) {
        ret = system("which xwd > /dev/null 2>&1 && xwd -root -out /tmp/screenshot_$$.xwd 2>/dev/null");
    }
    
    /* Tim file screenshot */
    char search_cmd[256];
    snprintf(search_cmd, sizeof(search_cmd), "ls -t /tmp/screenshot_*.png /tmp/screenshot_*.xwd 2>/dev/null | head -1");
    FILE* f = popen(search_cmd, "r");
    if (f == NULL) {
        snprintf(ket_qua, ket_qua_size, "{\"type\":\"screenshot\",\"status\":\"loi\"}");
        return -1;
    }
    
    char file_found[256] = {0};
    fgets(file_found, sizeof(file_found), f);
    pclose(f);
    
    /* Xoa newline */
    size_t len = strlen(file_found);
    if (len > 0 && file_found[len - 1] == '\n') file_found[len - 1] = '\0';
    
    if (strlen(file_found) == 0) {
        snprintf(ket_qua, ket_qua_size, "{\"type\":\"screenshot\",\"status\":\"khong_co_cong_cu\"}");
        return -1;
    }
    
    /* Doc file va base64 */
    FILE* img = fopen(file_found, "rb");
    if (img == NULL) {
        unlink(file_found);
        return -1;
    }
    
    fseek(img, 0, SEEK_END);
    long size = ftell(img);
    if (size > 5242880) size = 5242880;
    fseek(img, 0, SEEK_SET);
    
    unsigned char* data = (unsigned char*)malloc(size);
    if (data != NULL) {
        fread(data, 1, size, img);
        
        const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t b64_size = ((size + 2) / 3) * 4 + 1;
        char* b64 = (char*)malloc(b64_size);
        
        if (b64 != NULL) {
            size_t b64_offset = 0;
            for (long i = 0; i < size; i += 3) {
                unsigned char a = data[i];
                unsigned char b = (i + 1 < size) ? data[i + 1] : 0;
                unsigned char c = (i + 2 < size) ? data[i + 2] : 0;
                b64[b64_offset++] = b64_chars[a >> 2];
                b64[b64_offset++] = b64_chars[((a & 0x03) << 4) | (b >> 4)];
                b64[b64_offset++] = (i + 1 < size) ? b64_chars[((b & 0x0f) << 2) | (c >> 6)] : '=';
                b64[b64_offset++] = (i + 2 < size) ? b64_chars[c & 0x3f] : '=';
            }
            b64[b64_offset] = '\0';
            
            snprintf(ket_qua, ket_qua_size,
                "{\"type\":\"screenshot\",\"format\":\"png\",\"size\":%ld,\"data\":\"%s\"}",
                size, b64);
            free(b64);
        }
        free(data);
    }
    
    fclose(img);
    unlink(file_found);
    return 0;
}

int chup_webcam(char* ket_qua, size_t ket_qua_size) {
    snprintf(ket_qua, ket_qua_size, "{\"type\":\"webcam\",\"status\":\"chua_ho_tro\"}");
    return -1;
}

int ghi_am(int so_giay, char* ket_qua, size_t ket_qua_size) {
    snprintf(ket_qua, ket_qua_size,
        "{\"type\":\"audio\",\"duration\":%d,\"status\":\"chua_ho_tro\"}", so_giay);
    return -1;
}

#endif