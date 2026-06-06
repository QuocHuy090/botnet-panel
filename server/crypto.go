package main

import (
    "crypto/aes"
    "crypto/cipher"
    "crypto/ecdh"
    "crypto/hmac"
    "crypto/rand"
    "crypto/sha256"
    "encoding/base64"
    "encoding/hex"
    "encoding/json"
    "errors"
    "io"
    "time"
    
    "github.com/golang-jwt/jwt/v5"
)

// Tao cap khoa ECDHE su dung Curve25519
func GenerateECDHEKeyPair() (privateKeyHex string, publicKeyHex string, err error) {
    curve := ecdh.X25519()
    privateKey, err := curve.GenerateKey(rand.Reader)
    if err != nil {
        return "", "", err
    }
    publicKey := privateKey.PublicKey()
    privateKeyHex = hex.EncodeToString(privateKey.Bytes())
    publicKeyHex = hex.EncodeToString(publicKey.Bytes())
    return privateKeyHex, publicKeyHex, nil
}

// Ma hoa AES-256-GCM
func EncryptAESGCM(plaintext []byte, key []byte) (string, error) {
    // Tao AES cipher block
    block, err := aes.NewCipher(key)
    if err != nil {
        return "", err
    }
    
    // Tao GCM mode
    gcm, err := cipher.NewGCM(block)
    if err != nil {
        return "", err
    }
    
    // Tao nonce ngau nhien (12 bytes la toi uu cho GCM)
    nonce := make([]byte, gcm.NonceSize())
    _, err = io.ReadFull(rand.Reader, nonce)
    if err != nil {
        return "", err
    }
    
    // Ma hoa du lieu, authentication tag duoc tu dong append vao ciphertext
    ciphertext := gcm.Seal(nonce, nonce, plaintext, nil)
    
    // Tra ve base64 de de dang truyen qua JSON
    return base64.StdEncoding.EncodeToString(ciphertext), nil
}

// Giai ma AES-256-GCM
func DecryptAESGCM(encodedCiphertext string, key []byte) ([]byte, error) {
    // Giai ma base64
    ciphertext, err := base64.StdEncoding.DecodeString(encodedCiphertext)
    if err != nil {
        return nil, err
    }
    
    // Tao AES cipher block
    block, err := aes.NewCipher(key)
    if err != nil {
        return nil, err
    }
    
    // Tao GCM mode
    gcm, err := cipher.NewGCM(block)
    if err != nil {
        return nil, err
    }
    
    // Tach nonce tu ciphertext
    nonceSize := gcm.NonceSize()
    if len(ciphertext) < nonceSize {
        return nil, errors.New("ciphertext qua ngan")
    }
    
    nonce := ciphertext[:nonceSize]
    actualCiphertext := ciphertext[nonceSize:]
    
    // Giai ma va xac thuc
    plaintext, err := gcm.Open(nil, nonce, actualCiphertext, nil)
    if err != nil {
        return nil, err
    }
    
    return plaintext, nil
}

// Ma hoa JSON thanh chuoi da ma hoa base64
func EncryptJSON(data interface{}, keyBytes []byte) (string, error) {
    jsonData, err := json.Marshal(data)
    if err != nil {
        return "", err
    }
    
    return EncryptAESGCM(jsonData, keyBytes)
}

// Giai ma chuoi base64 thanh JSON
func DecryptJSON(encodedData string, keyBytes []byte, target interface{}) error {
    plaintext, err := DecryptAESGCM(encodedData, keyBytes)
    if err != nil {
        return err
    }
    
    return json.Unmarshal(plaintext, target)
}

// Hash mat khau su dung SHA-256
func HashPassword(password string) string {
    hash := sha256.Sum256([]byte(password))
    return hex.EncodeToString(hash[:])
}

// Kiem tra mat khau co khop voi hash khong
func CheckPassword(password string, hash string) bool {
    computedHash := HashPassword(password)
    return hmac.Equal([]byte(computedHash), []byte(hash))
}

// Tao JWT token cho admin
func CreateJWT(username string) (string, error) {
    claims := jwt.MapClaims{
        "username": username,
        "role":     "admin",
        "iat":      time.Now().Unix(),
        "exp":      time.Now().Add(24 * time.Hour).Unix(), // Het han sau 24 gio
    }
    
    token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
    keyBytes := []byte(AppConfig.JWTSecret)
    
    tokenString, err := token.SignedString(keyBytes)
    if err != nil {
        return "", err
    }
    
    return tokenString, nil
}

// Xac thuc JWT token
func ValidateJWT(tokenString string) (*jwt.MapClaims, error) {
    token, err := jwt.Parse(tokenString, func(token *jwt.Token) (interface{}, error) {
        // Kiem tra phuong thuc ky
        if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
            return nil, errors.New("phuong thuc ky khong hop le")
        }
        return []byte(AppConfig.JWTSecret), nil
    })
    
    if err != nil {
        return nil, err
    }
    
    if claims, ok := token.Claims.(jwt.MapClaims); ok && token.Valid {
        return &claims, nil
    }
    
    return nil, errors.New("token khong hop le")
}

// Tao encryption key tu bot key va server key (ECDHE key exchange)
func DeriveSharedKey(botPublicKeyHex string, serverPrivateKeyHex string) ([]byte, error) {
    botPublicBytes, err := hex.DecodeString(botPublicKeyHex)
    if err != nil {
        return nil, err
    }
    
    serverPrivateBytes, err := hex.DecodeString(serverPrivateKeyHex)
    if err != nil {
        return nil, err
    }
    
    curve := ecdh.X25519()
    
    // Tao private key tu bytes
    serverPrivateKey, err := curve.NewPrivateKey(serverPrivateBytes)
    if err != nil {
        return nil, err
    }
    
    // Tao public key tu bytes
    botPublicKey, err := curve.NewPublicKey(botPublicBytes)
    if err != nil {
        return nil, err
    }
    
    // Tao shared secret
    sharedSecret, err := serverPrivateKey.ECDH(botPublicKey)
    if err != nil {
        return nil, err
    }
    
    // Hash shared secret de lay key 32 bytes
    hash := sha256.Sum256(sharedSecret)
    return hash[:], nil
}

// Tao UUID don gian (khong can goi package UUID rieng)
func GenerateUUID() string {
    b := make([]byte, 16)
    _, err := rand.Read(b)
    if err != nil {
        // Fallback: tao tu timestamp
        b = []byte(time.Now().String())
        for len(b) < 16 {
            b = append(b, b...)
        }
        b = b[:16]
    }
    
    // Version 4 UUID
    b[6] = (b[6] & 0x0f) | 0x40
    b[8] = (b[8] & 0x3f) | 0x80
    
    return hex.EncodeToString(b[:4]) + "-" +
        hex.EncodeToString(b[4:6]) + "-" +
        hex.EncodeToString(b[6:8]) + "-" +
        hex.EncodeToString(b[8:10]) + "-" +
        hex.EncodeToString(b[10:16])
}