/**
 * bot_crypto.h
 * Khai bao prototype cho cac ham ma hoa/giai ma cua bot
 */

#ifndef BOT_CRYPTO_H
#define BOT_CRYPTO_H

#include <stddef.h>

/* Kich thuoc khoa AES-256 */
#define AES_KEY_SIZE 32
#define AES_GCM_NONCE_SIZE 12
#define AES_GCM_TAG_SIZE 16

/* Kich thuoc hash SHA-256 */
#define SHA256_DIGEST_SIZE 32

/**
 * Ma hoa du lieu su dung AES-256-GCM
 * Dau vao: plaintext - du lieu can ma hoa
 *          plaintext_len - do dai du lieu
 *          key - khoa ma hoa (32 bytes)
 *          ciphertext_out - buffer de chua ket qua ma hoa (base64)
 *          out_size - kich thuoc buffer
 * Tra ve: do dai chuoi base64, -1 neu loi
 */
int ma_hoa_AES_256_GCM(const unsigned char* plaintext, size_t plaintext_len,
                       const unsigned char* key,
                       char* ciphertext_out, size_t out_size);

/**
 * Giai ma du lieu su dung AES-256-GCM
 * Dau vao: ciphertext_base64 - du lieu da ma hoa (base64)
 *          key - khoa ma hoa (32 bytes)
 *          plaintext_out - buffer de chua ket qua giai ma
 *          out_size - kich thuoc buffer
 * Tra ve: do dai du lieu goc, -1 neu loi
 */
int giai_ma_AES_256_GCM(const char* ciphertext_base64,
                        const unsigned char* key,
                        unsigned char* plaintext_out, size_t out_size);

/**
 * Ma hoa base64
 * Dau vao: input - du lieu can ma hoa
 *          input_len - do dai du lieu
 *          output - buffer de chua ket qua
 *          output_size - kich thuoc buffer
 * Tra ve: do dai chuoi base64
 */
size_t base64_encode(const unsigned char* input, size_t input_len,
                     char* output, size_t output_size);

/**
 * Giai ma base64
 * Dau vao: input - chuoi base64 can giai ma
 *          output - buffer de chua ket qua
 *          output_size - kich thuoc buffer
 * Tra ve: do dai du lieu goc
 */
size_t base64_decode(const char* input, unsigned char* output, size_t output_size);

/**
 * Tao hash SHA-256
 * Dau vao: input - du lieu can hash
 *          input_len - do dai du lieu
 *          output - buffer de chua ket qua (32 bytes)
 */
void tao_SHA256(const unsigned char* input, size_t input_len, 
                unsigned char output[SHA256_DIGEST_SIZE]);

/**
 * Tao chuoi hex tu du lieu nhi phan
 * Dau vao: data - du lieu nhi phan
 *          data_len - do dai du lieu
 *          hex_out - buffer de chua ket qua
 *          hex_size - kich thuoc buffer
 */
void nhi_phan_sang_hex(const unsigned char* data, size_t data_len,
                       char* hex_out, size_t hex_size);

/**
 * Tao du lieu nhi phan tu chuoi hex
 * Dau vao: hex_str - chuoi hex
 *          data_out - buffer de chua ket qua
 *          data_size - kich thuoc buffer
 * Tra ve: so bytes da doc
 */
size_t hex_sang_nhi_phan(const char* hex_str, unsigned char* data_out, 
                         size_t data_size);

/**
 * Tao so ngau nhien an toan
 * Dau vao: buffer - buffer de chua so ngau nhien
 *          length - so byte can tao
 * Tra ve: 0 neu thanh cong, -1 neu that bai
 */
int tao_ngau_nhien(unsigned char* buffer, size_t length);

#endif /* BOT_CRYPTO_H */