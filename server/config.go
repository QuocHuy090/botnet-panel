package main

import (
    "crypto/rand"
    "encoding/hex"
    "log"
    "os"
    "strconv"
    "time"
)

// Cau truc Config chua tat ca thiet lap cua server
type Config struct {
    Port           string
    Domain         string
    EncryptionKey  string
    JWTSecret      string
    BotTimeout     time.Duration
    CleanupInterval time.Duration
    CmdRetention    time.Duration
    RateLimitMax   int
    DBPath         string
    CertFile       string
    KeyFile        string
}

var AppConfig *Config

// Ham khoi tao Config tu bien moi truong
func InitConfig() {
    AppConfig = &Config{}
    
    // Doc cong tu bien moi truong, neu khong co thi dung gia tri mac dinh
    port := os.Getenv("C2_PORT")
    if port == "" {
        port = "8443"
    }
    AppConfig.Port = port
    
    domain := os.Getenv("C2_DOMAIN")
    if domain == "" {
        domain = "localhost"
    }
    AppConfig.Domain = domain
    
    // Tao encryption key ngau nhien neu chua co trong bien moi truong
    encKey := os.Getenv("ENCRYPTION_KEY")
    if encKey == "" {
        keyBytes := make([]byte, 32)
        _, err := rand.Read(keyBytes)
        if err != nil {
            log.Fatal("Khong the tao encryption key: ", err)
        }
        encKey = hex.EncodeToString(keyBytes)
    }
    AppConfig.EncryptionKey = encKey
    
    // Tao JWT secret ngau nhien neu chua co
    jwtSecret := os.Getenv("JWT_SECRET")
    if jwtSecret == "" {
        jwtBytes := make([]byte, 32)
        _, err := rand.Read(jwtBytes)
        if err != nil {
            log.Fatal("Khong the tao JWT secret: ", err)
        }
        jwtSecret = hex.EncodeToString(jwtBytes)
    }
    AppConfig.JWTSecret = jwtSecret
    
    // Thoi gian timeout bot (phut)
    timeoutStr := os.Getenv("BOT_TIMEOUT_MINUTES")
    if timeoutStr == "" {
        timeoutStr = "10"
    }
    timeoutMin, err := strconv.Atoi(timeoutStr)
    if err != nil {
        timeoutMin = 10
    }
    AppConfig.BotTimeout = time.Duration(timeoutMin) * time.Minute
    
    // Thoi gian don dep bot offline (phut)
    cleanupStr := os.Getenv("CLEANUP_INTERVAL_MINUTES")
    if cleanupStr == "" {
        cleanupStr = "5"
    }
    cleanupMin, err := strconv.Atoi(cleanupStr)
    if err != nil {
        cleanupMin = 5
    }
    AppConfig.CleanupInterval = time.Duration(cleanupMin) * time.Minute
    
    // Thoi gian giu lenh cu (gio)
    retentionStr := os.Getenv("CMD_RETENTION_HOURS")
    if retentionStr == "" {
        retentionStr = "24"
    }
    retentionHr, err := strconv.Atoi(retentionStr)
    if err != nil {
        retentionHr = 24
    }
    AppConfig.CmdRetention = time.Duration(retentionHr) * time.Hour
    
    // So request toi da trong 1 phut cho rate limiting
    rateStr := os.Getenv("RATE_LIMIT_MAX")
    if rateStr == "" {
        rateStr = "100"
    }
    rateMax, err := strconv.Atoi(rateStr)
    if err != nil {
        rateMax = 100
    }
    AppConfig.RateLimitMax = rateMax
    
    // Duong dan file database
    dbPath := os.Getenv("DB_PATH")
    if dbPath == "" {
        dbPath = "./c2_botnet.db"
    }
    AppConfig.DBPath = dbPath
    
    // File chung chi TLS
    certFile := os.Getenv("CERT_FILE")
    if certFile == "" {
        certFile = "./server.crt"
    }
    AppConfig.CertFile = certFile
    
    keyFile := os.Getenv("KEY_FILE")
    if keyFile == "" {
        keyFile = "./server.key"
    }
    AppConfig.KeyFile = keyFile
    
    log.Println("[CONFIG] Da khoi tao config xong")
    log.Printf("[CONFIG] Port: %s, Domain: %s, BotTimeout: %v", AppConfig.Port, AppConfig.Domain, AppConfig.BotTimeout)
}