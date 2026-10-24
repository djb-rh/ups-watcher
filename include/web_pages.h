// Embedded web UI. Plain HTML/CSS/JS, no external assets, served from flash.
#pragma once
#include <Arduino.h>

static const char PAGE_CSS[] PROGMEM = R"CSS(
:root{--bg:#0f1115;--card:#171a21;--fg:#e6e8ee;--mut:#8b93a7;--ok:#2ecc71;--warn:#f1c40f;--bad:#e74c3c;--acc:#4ea1ff;--line:#262b36}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
a{color:var(--acc);text-decoration:none}header{display:flex;align-items:center;gap:16px;padding:12px 16px;border-bottom:1px solid var(--line);background:#12151b;position:sticky;top:0}
header h1{font-size:18px;margin:0;flex:1}header nav a{margin-left:14px;color:var(--mut)}header nav a.on{color:var(--fg)}
main{max-width:1000px;margin:0 auto;padding:16px}.grid{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}.card h2{margin:0 0 8px;font-size:13px;color:var(--mut);text-transform:uppercase;letter-spacing:.06em}
.big{font-size:30px;font-weight:600}.badge{display:inline-block;padding:4px 10px;border-radius:999px;font-weight:600;font-size:14px}
.ok{background:rgba(46,204,113,.15);color:var(--ok)}.warn{background:rgba(241,196,15,.15);color:var(--warn)}.bad{background:rgba(231,76,60,.18);color:var(--bad)}.mut{background:rgba(139,147,167,.15);color:var(--mut)}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:5px 6px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}th{color:var(--mut);font-weight:500}
td.k{color:var(--mut);font-family:ui-monospace,Menlo,monospace;font-size:13px}td.v{font-family:ui-monospace,Menlo,monospace;font-size:13px;word-break:break-all}
.bar{height:10px;background:#262b36;border-radius:5px;overflow:hidden;margin-top:8px}.bar i{display:block;height:100%;background:var(--ok)}
button,input[type=submit]{background:var(--acc);color:#fff;border:0;border-radius:7px;padding:8px 14px;font-size:14px;cursor:pointer}button.sec{background:#2a3140}button.danger{background:var(--bad)}
input,select{background:#0f1115;color:var(--fg);border:1px solid #2f3645;border-radius:7px;padding:8px;font-size:14px;width:100%}
label{display:block;font-size:13px;color:var(--mut);margin:10px 0 4px}form .row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.ev{font-family:ui-monospace,Menlo,monospace;font-size:13px;white-space:pre-wrap}.small{color:var(--mut);font-size:13px}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}pre{overflow:auto;font-size:12px}
)CSS";

static const char PAGE_INDEX[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS Watcher</title><link rel="stylesheet" href="/style.css"></head><body>
<header><h1>UPS Watcher</h1><nav><a class="on" href="/">Status</a><a href="/hid">HID</a><a href="/config">Settings</a><a href="/update">Update</a></nav></header>
<main>
<div class="grid">
 <div class="card"><h2>UPS status</h2><div id="st" class="big">…</div><div id="model" class="small"></div><div id="alarm" class="small" style="color:var(--bad)"></div></div>
 <div class="card"><h2>Battery</h2><div class="big"><span id="chg">–</span>%</div><div class="bar"><i id="chgbar" style="width:0"></i></div><div class="small">Runtime <b id="rt">–</b> · <span id="bv"></span></div></div>
 <div class="card"><h2>Load</h2><div class="big"><span id="load">–</span>%</div><div class="small"><span id="watts"></span></div></div>
 <div class="card"><h2>Input</h2><div class="big"><span id="vin">–</span> V</div><div class="small" id="xfer"></div></div>
</div>
<div class="grid" style="margin-top:14px">
 <div class="card"><h2>Watcher</h2><table><tr><td class="k">Power</td><td id="wpow"></td></tr><tr><td class="k">Network</td><td id="wnet"></td></tr><tr><td class="k">NUT</td><td id="wnut"></td></tr><tr><td class="k">USB</td><td id="wusb"></td></tr><tr><td class="k">Clock</td><td id="wclk"></td></tr><tr><td class="k">Uptime</td><td id="wup"></td></tr></table>
  <div class="btns"><button class="sec" onclick="cmd('beeper.mute')">Mute beeper</button><button class="sec" onclick="cmd('beeper.enable')">Beeper on</button><button class="sec" onclick="cmd('beeper.disable')">Beeper off</button><button class="sec" onclick="cmd('test.battery.start.quick')">Quick self-test</button></div>
  <div class="small" id="cmdres"></div></div>
 <div class="card"><h2>Recent events</h2><div class="ev" id="events">…</div></div>
</div>
<div class="card" style="margin-top:14px"><h2>All NUT variables <span class="small">(what <code>upsc</code> would show)</span></h2><table id="vars"></table></div>
</main>
<script>
function fmtRt(s){s=+s;if(!(s>=0))return'–';var h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+'h '+m+'m':m+'m '+(s%60)+'s'}
function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'d ':'')+h+'h '+m+'m'}
function cls(st){if(!st)return'mut';if(/OFF|LB|ALARM/.test(st))return'bad';if(/OB/.test(st))return'warn';if(/OL/.test(st))return'ok';return'mut'}
function words(st){if(!st)return'No data';var w=[];if(/OL/.test(st))w.push('Online');if(/OB/.test(st))w.push('On battery');if(/LB/.test(st))w.push('LOW BATTERY');if(/OFF/.test(st))w.push('UPS lost');if(/CHRG/.test(st))w.push('charging');if(/DISCHRG/.test(st))w.push('discharging');if(/RB/.test(st))w.push('replace battery');if(/OVER/.test(st))w.push('overload');return w.join(', ')||st}
async function cmd(c){var r=await fetch('/api/instcmd?cmd='+c,{method:'POST'});document.getElementById('cmdres').textContent=c+': '+(r.ok?'ok':'failed ('+r.status+')')}
async function tick(){try{var r=await fetch('/api/status');var d=await r.json();var v=d.vars||{};var st=v['ups.status']||'';
var e=document.getElementById('st');e.textContent=words(st);e.innerHTML='<span class="badge '+cls(st)+'">'+st+'</span> '+words(st);
document.getElementById('model').textContent=(v['ups.mfr']||'')+' '+(v['ups.model']||'')+(v['ups.serial']?' · S/N '+v['ups.serial']:'');
document.getElementById('alarm').textContent=v['ups.alarm']||'';
document.getElementById('chg').textContent=v['battery.charge']||'–';document.getElementById('chgbar').style.width=(v['battery.charge']||0)+'%';
document.getElementById('rt').textContent=fmtRt(v['battery.runtime']);document.getElementById('bv').textContent=v['battery.voltage']?v['battery.voltage']+' V battery':'';
document.getElementById('load').textContent=v['ups.load']||'–';var nom=+v['ups.realpower.nominal'];document.getElementById('watts').textContent=(nom&&v['ups.load'])?'≈ '+Math.round(nom*v['ups.load']/100)+' W of '+nom+' W':'';
document.getElementById('vin').textContent=v['input.voltage']||'–';document.getElementById('xfer').textContent=(v['input.transfer.low']?'transfer '+v['input.transfer.low']+'–'+v['input.transfer.high']+' V':'')+(v['input.transfer.reason']?' · last: '+v['input.transfer.reason']:'');
var w=d.watcher;document.getElementById('wpow').textContent=w.ext_power_known?(w.ext_power?'external 5 V':'INTERNAL BATTERY '+w.core_batt+'%'):'unknown';
document.getElementById('wnet').textContent=(w.ap_mode?'AP '+w.ap_ssid+' ':'')+(w.ip||'')+(w.rssi?' ('+w.rssi+' dBm)':'')+' · '+w.hostname+'.local';
document.getElementById('wnut').textContent='port 3493 · ups name "'+w.ups_name+'" · '+w.nut_clients+' client(s), '+w.nut_logins+' logged in';
document.getElementById('wusb').textContent=w.usb_connected?('connected, '+w.polls+' polls, '+w.inputs+' input reports'):(w.sim?'SIMULATED UPS':'no UPS on USB');
document.getElementById('wclk').textContent=w.time;document.getElementById('wup').textContent=fmtUp(w.uptime)+' · '+Math.round(w.heap/1024)+' KB free';
document.getElementById('events').textContent=(d.events||[]).join('\n');
var t='';Object.keys(v).sort().forEach(function(k){t+='<tr><td class="k">'+k+'</td><td class="v">'+v[k]+'</td></tr>'});document.getElementById('vars').innerHTML=t;
}catch(e){document.getElementById('st').textContent='offline?'}}
tick();setInterval(tick,2000);
</script></body></html>)HTML";

static const char PAGE_CONFIG[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS Watcher settings</title><link rel="stylesheet" href="/style.css"></head><body>
<header><h1>UPS Watcher</h1><nav><a href="/">Status</a><a href="/hid">HID</a><a class="on" href="/config">Settings</a><a href="/update">Update</a></nav></header>
<main><form method="post" action="/api/config" id="f">
<div class="card"><h2>Wi-Fi</h2>
<label>Network</label><input name="wifi_ssid" id="ssid" list="nets" autocomplete="off"><datalist id="nets"></datalist>
<div class="btns"><button type="button" class="sec" onclick="scan()">Scan</button><span class="small" id="scanres"></span></div>
<label>Password</label><input name="wifi_pass" type="password" placeholder="(unchanged)">
<label>Hostname (mDNS name)</label><input name="hostname"></div>
<div class="card" style="margin-top:14px"><h2>NUT server</h2>
<div class="row"><div><label>UPS name (as in upsc name@host)</label><input name="ups_name"></div><div><label>Description</label><input name="ups_desc"></div></div>
<div class="row"><div><label>Username (blank = open)</label><input name="nut_user"></div><div><label>Password</label><input name="nut_pass" type="password" placeholder="(unchanged)"></div></div>
<label>Poll interval (seconds)</label><input name="poll_s" type="number" min="2" max="60">
<label>When the UPS disappears from USB</label><select name="disconnect_stale"><option value="0">Report status ALARM OFF with the last known values</option><option value="1">Report DATA-STALE (NUT default behaviour)</option></select></div>
<div class="card" style="margin-top:14px"><h2>Display &amp; misc</h2>
<div class="row"><div><label>Backlight off after (seconds, 0 = never)</label><input name="disp_timeout_s" type="number" min="0"></div><div><label>Brightness (1–255)</label><input name="brightness" type="number" min="1" max="255"></div></div>
<div class="row"><div><label>Time zone (POSIX TZ)</label><input name="tz"></div><div><label>NTP server</label><input name="ntp"></div></div>
<label>Web UI password (user "admin", blank = none)</label><input name="web_pass" type="password" placeholder="(unchanged)">
<label>Simulated UPS when none is attached (for testing clients)</label><select name="sim"><option value="0">Off</option><option value="1">On</option></select>
<div class="row"><div><label>MAX3421E CS GPIO (0 = auto)</label><input name="usb_cs" type="number"></div><div><label>MAX3421E INT GPIO</label><input name="usb_int" type="number"></div></div>
</div>
<div class="btns" style="margin-top:14px"><input type="submit" value="Save"><button type="button" class="sec" onclick="post('/api/reboot')">Reboot</button><button type="button" class="danger" onclick="if(confirm('Erase all settings?'))post('/api/factory')">Factory reset</button></div>
<p class="small">Changing Wi-Fi or hostname reboots the device. Simulator controls: <button type="button" class="sec" onclick="sim('ol')">online</button> <button type="button" class="sec" onclick="sim('ob')">on battery</button> <button type="button" class="sec" onclick="sim('lb')">low battery</button> <button type="button" class="sec" onclick="sim('off')">lost</button></p>
</form></main>
<script>
async function load(){var d=await (await fetch('/api/config')).json();for(var k in d){var e=document.querySelector('[name='+k+']');if(e&&e.type!='password')e.value=d[k]}}
async function scan(){document.getElementById('scanres').textContent='scanning…';var d=await (await fetch('/api/scan')).json();var l=document.getElementById('nets');l.innerHTML='';d.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm';l.appendChild(o)});document.getElementById('scanres').textContent=d.length+' networks'}
async function post(u){var r=await fetch(u,{method:'POST'});alert(r.ok?'OK':'failed')}
async function sim(m){await fetch('/api/sim?mode='+m,{method:'POST'})}
load();
</script></body></html>)HTML";

static const char PAGE_UPDATE[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS Watcher update</title><link rel="stylesheet" href="/style.css"></head><body>
<header><h1>UPS Watcher</h1><nav><a href="/">Status</a><a href="/hid">HID</a><a href="/config">Settings</a><a class="on" href="/update">Update</a></nav></header>
<main><div class="card"><h2>Firmware update</h2><p class="small">Upload <code>firmware.bin</code> built with <code>pio run -e app</code>. The device reboots when done.</p>
<form method="post" action="/update" enctype="multipart/form-data"><input type="file" name="fw" accept=".bin"><div class="btns"><input type="submit" value="Flash"></div></form>
<p class="small" id="ver"></p></div></main>
<script>fetch('/api/status').then(r=>r.json()).then(d=>{document.getElementById('ver').textContent='Running version '+d.watcher.version})</script>
</body></html>)HTML";

static const char PAGE_HID[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>UPS Watcher HID</title><link rel="stylesheet" href="/style.css"></head><body>
<header><h1>UPS Watcher</h1><nav><a href="/">Status</a><a class="on" href="/hid">HID</a><a href="/config">Settings</a><a href="/update">Update</a></nav></header>
<main><div class="card"><h2>Device</h2><div id="dev" class="small"></div></div>
<div class="card" style="margin-top:14px"><h2>Feature reports being polled</h2><table id="reps"><tr><th>ID</th><th>Bytes</th><th>Raw</th><th>Age</th><th>Fails</th></tr></table></div>
<div class="card" style="margin-top:14px"><h2>Parsed report descriptor</h2><table id="fields"><tr><th>RID</th><th>T</th><th>Off</th><th>Bits</th><th>Logical</th><th>Unit</th><th>Exp</th><th>Path</th></tr></table></div>
<div class="card" style="margin-top:14px"><h2>Raw descriptor</h2><pre id="raw"></pre></div></main>
<script>
fetch('/api/hid').then(r=>r.json()).then(d=>{document.getElementById('dev').textContent=d.connected?('VID '+d.vid+' PID '+d.pid+' · '+d.mfr+' · '+d.product+' · S/N '+d.serial+' · CS G'+d.cs+' INT G'+d.int+' · descriptor '+d.desc_len+' bytes'):'No device';
var t=document.getElementById('reps');d.reports.forEach(function(r){t.innerHTML+='<tr><td>'+r.id+'</td><td>'+r.len+'</td><td class="v">'+r.raw+'</td><td>'+(r.ok?r.age+' s':'–')+'</td><td>'+r.fails+'</td></tr>'});
var f=document.getElementById('fields');d.fields.forEach(function(x){f.innerHTML+='<tr><td>'+x.rid+'</td><td>'+x.t+'</td><td>'+x.off+'</td><td>'+x.bits+'</td><td>'+x.lmin+'..'+x.lmax+'</td><td>'+x.unit+'</td><td>'+x.exp+'</td><td class="v">'+x.path+'</td></tr>'});
document.getElementById('raw').textContent=d.raw;});
</script></body></html>)HTML";
