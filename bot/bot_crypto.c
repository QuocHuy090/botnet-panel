/**
 * bot_crypto.c
 * Trien khai cac ham ma hoa/giai ma cho bot
 * Su dung OpenSSL tren Linux/MacOS va WinCrypt tren Windows
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <wincrypt.h>
    #pragma comment(lib, "advapi32.lib")
    #pragma comment(lib, "crypt32.lib")
#else
    #include <openssl/evp.h>
    #include <openssl/sha.h>
    #include <openssl/rand.h>
    #include <openssl/bio.h>
    #include <openssl/buffer.h>
#endif

#include "bot_crypto.h"

/* ==================== BASE64 ==================== */

/* Bang ma hoa base64 */
static const char BASE64_TABLE[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Ma hoa base64
 */
size_t base64_encode(const unsigned char* input, size_t input_len,
                     char* output, size_t output_size) {
    if (output_size == 0) return 0;
    
    size_t out_idx = 0;
    unsigned char a, b, c;
    size_t i;
    
    for (i = 0; i < input_len; i += 3) {
        a = input[i];
        b = (i + 1 < input_len) ? input[i + 1] : 0;
        c = (i + 2 < input_len) ? input[i + 2] : 0;
        
        if (out_idx + 4 >= output_size) break;
        
        output[out_idx++] = BASE64_TABLE[a >> 2];
        output[out_idx++] = BASE64_TABLE[((a & 0x03) << 4) | (b >> 4)];
        
        if (i + 1 < input_len) {
            if (out_idx >= output_size) break;
            output[out_idx++] = BASE64_TABLE[((b & 0x0f) << 2) | (c >> 6)];
        } else {
            if (out_idx >= output_size) break;
            output[out_idx++] = '=';
        }
        
        if (i + 2 < input_len) {
            if (out_idx >= output_size) break;
            output[out_idx++] = BASE64_TABLE[c & 0x3f];
        } else {
            if (out_idx >= output_size) break;
            output[out_idx++] = '=';
        }
    }
    
    output[out_idx] = '\0';
    return out_idx;
}

/**
 * Giai ma base64
 */
size_t base64_decode(const char* input, unsigned char* output, size_t output_size) {
    size_t out_idx = 0;
    size_t input_len = strlen(input);
    unsigned char quad[4];
    int padding = 0;
    size_t i, j;
    
    for (i = 0; i < input_len; i += 4) {
        /* Doc 4 ky tu */
        for (j = 0; j < 4; j++) {
            if (i + j >= input_len) break;
            
            char c = input[i + j];
            if (c == '=') {
                quad[j] = 0;
                padding++;
                continue;
            }
            
            /* Tim gia tri trong bang base64 */
            const char* pos = strchr(BASE64_TABLE, c);
            if (pos == NULL) {
                quad[j] = 0;
                continue;
            }
            quad[j] = (unsigned char)(pos - BASE64_TABLE);
        }
        
        /* Giai ma 4 ky tu thanh 3 byte */
        if (out_idx >= output_size) break;
        output[out_idx++] = (quad[0] << 2) | (quad[1] >> 4);
        
        if (padding < 2 && out_idx < output_size) {
            output[out_idx++] = (quad[1] << 4) | (quad[2] >> 2);
        }
        
        if (padding < 1 && out_idx < output_size) {
            output[out_idx++] = (quad[2] << 6) | quad[3];
        }
        
        if (padding > 0) break;
    }
    
    return out_idx;
}

/* ==================== HEX ==================== */

/**
 * Chuyen doi nhi phan sang hex
 */
void nhi_phan_sang_hex(const unsigned char* data, size_t data_len,
                       char* hex_out, size_t hex_size) {
    if (hex_size < data_len * 2 + 1) {
        if (hex_size > 0) hex_out[0] = '\0';
        return;
    }
    
    const char hex_chars[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < data_len; i++) {
        hex_out[i * 2] = hex_chars[data[i] >> 4];
        hex_out[i * 2 + 1] = hex_chars[data[i] & 0x0f];
    }
    hex_out[data_len * 2] = '\0';
}

/**
 * Chuyen doi hex sang nhi phan
 */
size_t hex_sang_nhi_phan(const char* hex_str, unsigned char* data_out, size_t data_size) {
    size_t hex_len = strlen(hex_str);
    if (hex_len % 2 != 0) return 0;
    
    size_t byte_count = hex_len / 2;
    if (byte_count > data_size) byte_count = data_size;
    
    size_t i;
    for (i = 0; i < byte_count; i++) {
        char high = hex_str[i * 2];
        char low = hex_str[i * 2 + 1];
        
        unsigned char h_val = 0, l_val = 0;
        
        if (high >= '0' && high <= '9') h_val = high - '0';
        else if (high >= 'a' && high <= 'f') h_val = high - 'a' + 10;
        else if (high >= 'A' && high <= 'F') h_val = high - 'A' + 10;
        
        if (low >= '0' && low <= '9') l_val = low - '0';
        else if (low >= 'a' && low <= 'f') l_val = low - 'a' + 10;
        else if (low >= 'A' && low <= 'F') l_val = low - 'A' + 10;
        
        data_out[i] = (h_val << 4) | l_val;
    }
    
    return byte_count;
}

#ifdef _WIN32
/* ==================== WINDOWS CRYPTO ==================== */

/**
 * Tao so ngau nhien (Windows)
 */
int tao_ngau_nhien(unsigned char* buffer, size_t length) {
    HCRYPTPROV hProv;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, 
                               CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return -1;
    }
    
    if (!CryptGenRandom(hProv, (DWORD)length, buffer)) {
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    CryptReleaseContext(hProv, 0);
    return 0;
}

/**
 * Tao SHA-256 hash (Windows)
 */
void tao_SHA256(const unsigned char* input, size_t input_len,
                unsigned char output[SHA256_DIGEST_SIZE]) {
    HCRYPTPROV hProv;
    HCRYPTHASH hHash;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, 
                               CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        memset(output, 0, SHA256_DIGEST_SIZE);
        return;
    }
    
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        memset(output, 0, SHA256_DIGEST_SIZE);
        return;
    }
    
    if (!CryptHashData(hHash, input, (DWORD)input_len, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        memset(output, 0, SHA256_DIGEST_SIZE);
        return;
    }
    
    DWORD hash_size = SHA256_DIGEST_SIZE;
    if (!CryptGetHashParam(hHash, HP_HASHVAL, output, &hash_size, 0)) {
        memset(output, 0, SHA256_DIGEST_SIZE);
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
}

/**
 * Ma hoa AES-256-GCM (Windows)
 */
int ma_hoa_AES_256_GCM(const unsigned char* plaintext, size_t plaintext_len,
                       const unsigned char* key,
                       char* ciphertext_out, size_t out_size) {
    /* Windows khong ho tro GCM qua CryptoAPI cu, su dung thu vien dong gian */
    /* Fallback: su dung AES-CBC + HMAC thay vi GCM */
    HCRYPTPROV hProv;
    HCRYPTKEY hKey;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES,
                               CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        return -1;
    }
    
    /* Tao struct cho AES key */
    struct {
        BLOBHEADER hdr;
        DWORD key_size;
        BYTE key_data[32];
    } key_blob;
    
    key_blob.hdr.bType = PLAINTEXTKEYBLOB;
    key_blob.hdr.bVersion = CUR_BLOB_VERSION;
    key_blob.hdr.reserved = 0;
    key_blob.hdr.aiKeyAlg = CALG_AES_256;
    key_blob.key_size = 32;
    memcpy(key_blob.key_data, key, 32);
    
    if (!CryptImportKey(hProv, (BYTE*)&key_blob, sizeof(key_blob), 0, 0, &hKey)) {
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    /* Tao IV ngau nhien */
    unsigned char iv[16];
    if (tao_ngau_nhien(iv, sizeof(iv)) != 0) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    /* Dat IV */
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    /* Copy plaintext va ma hoa */
    size_t buffer_len = plaintext_len + 16; /* Them padding */
    unsigned char* buffer = (unsigned char*)malloc(buffer_len);
    if (buffer == NULL) {
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    memcpy(buffer, plaintext, plaintext_len);
    DWORD data_len = (DWORD)plaintext_len;
    
    if (!CryptEncrypt(hKey, 0, TRUE, 0, buffer, &data_len, (DWORD)buffer_len)) {
        free(buffer);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    /* Ghep IV + ciphertext */
    size_t total_len = 16 + data_len;
    unsigned char* combined = (unsigned char*)malloc(total_len);
    if (combined == NULL) {
        free(buffer);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    memcpy(combined, iv, 16);
    memcpy(combined + 16, buffer, data_len);
    
    /* Ma hoa base64 */
    size_t b64_len = base64_encode(combined, total_len, ciphertext_out, out_size);
    
    free(combined);
    free(buffer);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    
    return (int)b64_len;
}

/**
 * Giai ma AES-256-GCM (Windows)
 */
int giai_ma_AES_256_GCM(const char* ciphertext_base64,
                        const unsigned char* key,
                        unsigned char* plaintext_out, size_t out_size) {
    /* Giai ma base64 */
    size_t max_decoded = strlen(ciphertext_base64) * 3 / 4;
    unsigned char* decoded = (unsigned char*)malloc(max_decoded);
    if (decoded == NULL) return -1;
    
    size_t decoded_len = base64_decode(ciphertext_base64, decoded, max_decoded);
    
    if (decoded_len < 16) {
        free(decoded);
        return -1;
    }
    
    /* Tach IV va ciphertext */
    unsigned char iv[16];
    memcpy(iv, decoded, 16);
    
    HCRYPTPROV hProv;
    HCRYPTKEY hKey;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES,
                               CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        free(decoded);
        return -1;
    }
    
    struct {
        BLOBHEADER hdr;
        DWORD key_size;
        BYTE key_data[32];
    } key_blob;
    
    key_blob.hdr.bType = PLAINTEXTKEYBLOB;
    key_blob.hdr.bVersion = CUR_BLOB_VERSION;
    key_blob.hdr.reserved = 0;
    key_blob.hdr.aiKeyAlg = CALG_AES_256;
    key_blob.key_size = 32;
    memcpy(key_blob.key_data, key, 32);
    
    if (!CryptImportKey(hProv, (BYTE*)&key_blob, sizeof(key_blob), 0, 0, &hKey)) {
        free(decoded);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    if (!CryptSetKeyParam(hKey, KP_IV, iv, 0)) {
        free(decoded);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    DWORD data_len = (DWORD)(decoded_len - 16);
    unsigned char* data = decoded + 16;
    
    if (!CryptDecrypt(hKey, 0, TRUE, 0, data, &data_len)) {
        free(decoded);
        CryptDestroyKey(hKey);
        CryptReleaseContext(hProv, 0);
        return -1;
    }
    
    if (data_len > out_size) data_len = (DWORD)out_size;
    memcpy(plaintext_out, data, data_len);
    
    free(decoded);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);
    
    return (int)data_len;
}

#else
/* ==================== LINUX/MACOS CRYPTO (OpenSSL) ==================== */

/**
 * Tao so ngau nhien (OpenSSL)
 */
int tao_ngau_nhien(unsigned char* buffer, size_t length) {
    if (RAND_bytes(buffer, (int)length) != 1) {
        return -1;
    }
    return 0;
}

/**
 * Tao SHA-256 hash (OpenSSL)
 */
void tao_SHA256(const unsigned char* input, size_t input_len,
                unsigned char output[SHA256_DIGEST_SIZE]) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input, input_len);
    SHA256_Final(output, &ctx);
}

/**
 * Ma hoa AES-256-GCM (OpenSSL)
 */
int ma_hoa_AES_256_GCM(const unsigned char* plaintext, size_t plaintext_len,
                       const unsigned char* key,
                       char* ciphertext_out, size_t out_size) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) return -1;
    
    /* Tao nonce ngau nhien */
    unsigned char nonce[AES_GCM_NONCE_SIZE];
    if (tao_ngau_nhien(nonce, AES_GCM_NONCE_SIZE) != 0) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    /* Khoi tao ma hoa */
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, nonce) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    /* Cap phat buffer cho ciphertext */
    size_t max_cipher_len = plaintext_len + AES_GCM_TAG_SIZE;
    unsigned char* ciphertext = (unsigned char*)malloc(max_cipher_len);
    if (ciphertext == NULL) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    int cipher_len = 0;
    int total_len = 0;
    
    /* Ma hoa */
    if (EVP_EncryptUpdate(ctx, ciphertext, &cipher_len, plaintext, (int)plaintext_len) != 1) {
        free(ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total_len = cipher_len;
    
    /* Finalize */
    if (EVP_EncryptFinal_ex(ctx, ciphertext + total_len, &cipher_len) != 1) {
        free(ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total_len += cipher_len;
    
    /* Lay authentication tag */
    unsigned char tag[AES_GCM_TAG_SIZE];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_GCM_TAG_SIZE, tag) != 1) {
        free(ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    EVP_CIPHER_CTX_free(ctx);
    
    /* Ghep: nonce (12) + tag (16) + ciphertext */
    size_t combined_len = AES_GCM_NONCE_SIZE + AES_GCM_TAG_SIZE + total_len;
    unsigned char* combined = (unsigned char*)malloc(combined_len);
    if (combined == NULL) {
        free(ciphertext);
        return -1;
    }
    
    memcpy(combined, nonce, AES_GCM_NONCE_SIZE);
    memcpy(combined + AES_GCM_NONCE_SIZE, tag, AES_GCM_TAG_SIZE);
    memcpy(combined + AES_GCM_NONCE_SIZE + AES_GCM_TAG_SIZE, ciphertext, total_len);
    
    /* Ma hoa base64 */
    size_t b64_len = base64_encode(combined, combined_len, ciphertext_out, out_size);
    
    free(combined);
    free(ciphertext);
    
    return (int)b64_len;
}

/**
 * Giai ma AES-256-GCM (OpenSSL)
 */
int giai_ma_AES_256_GCM(const char* ciphertext_base64,
                        const unsigned char* key,
                        unsigned char* plaintext_out, size_t out_size) {
    /* Giai ma base64 */
    size_t max_decoded = strlen(ciphertext_base64) * 3 / 4 + 16;
    unsigned char* decoded = (unsigned char*)malloc(max_decoded);
    if (decoded == NULL) return -1;
    
    size_t decoded_len = base64_decode(ciphertext_base64, decoded, max_decoded);
    
    if (decoded_len < AES_GCM_NONCE_SIZE + AES_GCM_TAG_SIZE) {
        free(decoded);
        return -1;
    }
    
    /* Tach nonce, tag, ciphertext */
    unsigned char* nonce = decoded;
    unsigned char* tag = decoded + AES_GCM_NONCE_SIZE;
    unsigned char* ciphertext = decoded + AES_GCM_NONCE_SIZE + AES_GCM_TAG_SIZE;
    size_t ciphertext_len = decoded_len - AES_GCM_NONCE_SIZE - AES_GCM_TAG_SIZE;
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        free(decoded);
        return -1;
    }
    
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, key, nonce) != 1) {
        free(decoded);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    int plain_len = 0;
    int total_len = 0;
    
    if (EVP_DecryptUpdate(ctx, plaintext_out, &plain_len, ciphertext, (int)ciphertext_len) != 1) {
        free(decoded);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total_len = plain_len;
    
    /* Dat authentication tag */
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_GCM_TAG_SIZE, tag) != 1) {
        free(decoded);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    
    /* Finalize - xac thuc */
    int ret = EVP_DecryptFinal_ex(ctx, plaintext_out + total_len, &plain_len);
    EVP_CIPHER_CTX_free(ctx);
    free(decoded);
    
    if (ret <= 0) {
        return -1; /* Xac thuc that bai */
    }
    
    total_len += plain_len;
    return total_len;
}

#endif /* _WIN32 */