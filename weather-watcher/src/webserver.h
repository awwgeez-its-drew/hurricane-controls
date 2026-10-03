#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <set>
#include <cctype>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"
#include "nws_client.h"

extern void applyTimeConfig(); // src/main.cpp

// Same policy as the main Hurricane Controls board: 8+ chars, at least one
// uppercase, lowercase, digit, and special character.
static bool isStrongPassword(const char* pw) {
    size_t len = strlen(pw);
    if (len < 8) return false;
    bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;
    for (size_t i = 0; i < len; i++) {
        char c = pw[i];
        if (isupper((unsigned char)c)) hasUpper = true;
        else if (islower((unsigned char)c)) hasLower = true;
        else if (isdigit((unsigned char)c)) hasDigit = true;
        else hasSpecial = true;
    }
    return hasUpper && hasLower && hasDigit && hasSpecial;
}

// Valid DHCP/mDNS hostname: 1-32 chars, letters/digits/hyphens only, no
// leading or trailing hyphen.
static bool isValidHostname(const char* h) {
    size_t len = strlen(h);
    if (len < 1 || len > 32) return false;
    if (h[0] == '-' || h[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++) {
        char c = h[i];
        if (!isalnum((unsigned char)c) && c != '-') return false;
    }
    return true;
}

// ── Login page ───────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Watcher</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath fill='%2300d4ff' d='M6 16a4 4 0 0 1 .4-7.97 5.5 5.5 0 0 1 10.6 1.5A4 4 0 0 1 17 16H6z'/%3E%3Cpath fill='%23fbbf24' d='M13 17l-2 4h2l-1 3 4-5h-2l1-2z'/%3E%3C/svg%3E">
<style>
:root{--bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;--radius:4px}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:#eaeaea;
     font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
.card{background:var(--surface);border-radius:var(--radius);padding:36px 28px;width:100%;max-width:340px;
      text-align:center;box-shadow:0 1px 3px rgba(0,0,0,.35)}
h1{font-size:1.4rem;color:var(--cyan);letter-spacing:1px;margin-bottom:4px}
.sub{color:#9aa3af;font-size:.85rem;margin-bottom:28px}
input{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:var(--radius);padding:13px 16px;font-size:1rem;margin-bottom:12px;font-family:inherit}
input:focus{outline:none;border-color:var(--cyan)}
button{width:100%;padding:14px;background:var(--green);color:#04220f;border:none;
       border-radius:var(--radius);font-size:1rem;font-weight:700;cursor:pointer;letter-spacing:1px;
       font-family:inherit}
button:active{opacity:.8}
.err{color:#f87171;font-size:.85rem;margin-top:10px;min-height:1.1em}
</style></head><body>
<div class="card">
  <h1>&#x26C8; Weather Watcher</h1>
  <p class="sub">NWS Alert Monitor</p>
  <form onsubmit="login(event)">
    <input type="password" id="pw" placeholder="Password" autocomplete="current-password" autofocus>
    <button type="submit">UNLOCK</button>
  </form>
  <div class="err" id="err"></div>
</div>
<script>
function login(e){
  e.preventDefault();
  fetch('/auth/login',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({password:document.getElementById('pw').value})
  }).then(r=>r.json()).then(d=>{
    if(d.ok){location.href='/settings';}
    else if(d.locked){document.getElementById('err').textContent='Too many attempts. Try again in '+d.retryAfter+'s.';}
    else{document.getElementById('err').textContent='Incorrect password';
         document.getElementById('pw').value='';}
  }).catch(()=>{document.getElementById('err').textContent='Connection error';});
}
</script></body></html>
)rawliteral";

// ── Dashboard (status + current/recent alerts) ───────────────────────────────
static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Watcher</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath fill='%2300d4ff' d='M6 16a4 4 0 0 1 .4-7.97 5.5 5.5 0 0 1 10.6 1.5A4 4 0 0 1 17 16H6z'/%3E%3Cpath fill='%23fbbf24' d='M13 17l-2 4h2l-1 3 4-5h-2l1-2z'/%3E%3C/svg%3E">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Roboto:wght@400;700&display=swap" rel="stylesheet">
<style>
:root{--bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;--radius:4px}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;min-height:100vh}
.mono{font-family:ui-monospace,"SF Mono","Cascadia Code","Courier New",monospace}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;justify-content:space-between;
        padding:0 16px;z-index:20}
.navbar h1{font-size:1.05rem;color:var(--cyan)}
.navbar h1 a{color:inherit;text-decoration:none}
.navicons{display:flex;gap:14px;align-items:center}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.content{max-width:480px;margin:0 auto;padding:76px 16px 40px}
.card{background:var(--surface);border-radius:var(--radius);padding:20px;margin-bottom:14px;
      box-shadow:0 1px 3px rgba(0,0,0,.35)}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:1px;color:#9aa3af;margin-bottom:12px}
.hint{font-size:.78rem;color:#9aa3af}
.status-row{display:flex;align-items:center;cursor:pointer;padding:8px 0}
.dot{width:12px;height:12px;border-radius:50%;display:inline-block;margin-right:10px;background:#3a3f4a;flex-shrink:0}
.dot.green{background:var(--green);box-shadow:0 0 6px var(--green)}
.dot.red{background:#f87171;box-shadow:0 0 6px #f87171}
.status-label{font-size:.92rem}
.status-detail{display:none;font-size:.8rem;color:#9aa3af;padding:2px 0 10px 22px;line-height:1.4}
.status-detail.show{display:block}
.statline{text-align:center;font-size:.78rem;color:#9aa3af;padding-top:10px;border-top:1px solid #1a2a3a;margin-top:6px}
.clock{text-align:center;font-size:1.6rem;font-weight:700;letter-spacing:2px;margin:6px 0 2px;
       font-family:'Roboto',Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif}
.ver{text-align:center;color:#c9cdd3;font-size:.72rem;margin:6px 0 4px;padding:8px;
     background:var(--surface);border-radius:var(--radius)}
.ver a{color:inherit;text-decoration:none}
.no-alerts{display:flex;align-items:center;justify-content:center;gap:8px;border:2px solid var(--green);
    border-radius:4px;padding:16px;color:var(--green);font-weight:700;font-size:.9rem}
.alert-item{display:block;padding:12px 14px;border-radius:4px;margin-bottom:8px;text-decoration:none;color:#fff}
.alert-purple{background:#6b21a8}
.alert-red{background:#7f1d1d}
.alert-orange{background:#92400e}
.alert-head{font-weight:700;font-size:.9rem;display:flex;align-items:center;flex-wrap:wrap;gap:6px}
.alert-tag{background:rgba(0,0,0,.35);padding:2px 7px;border-radius:3px;font-size:.65rem;letter-spacing:.5px}
.alert-sub{font-size:.78rem;opacity:.9;margin-top:4px}
.recent-item{padding:8px 0;border-top:1px solid #1a2a3a;font-size:.85rem}
.recent-item:first-child{border-top:none}
.recent-time{color:#9aa3af;font-size:.75rem}
</style></head>
<body>
<div class="navbar">
  <h1><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Weather Watcher</a></h1>
  <div class="navicons">
    <a href="/settings" class="ibtn" title="Settings">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
    </a>
  </div>
</div>

<div class="content">

<div class="card">
  <div class="status-row" onclick="toggleDetail('wifiDetail')">
    <span class="dot" id="wifiDot"></span><span class="status-label">Wi-Fi</span>
  </div>
  <div class="status-detail" id="wifiDetail"></div>
  <div class="status-row" onclick="toggleDetail('apiDetail')">
    <span class="dot" id="apiDot"></span><span class="status-label">NWS API</span>
  </div>
  <div class="status-detail" id="apiDetail"></div>
  <div class="status-row" onclick="testLink()">
    <span class="dot" id="linkDot"></span><span class="status-label">Controller Link</span>
  </div>
  <div class="status-detail" id="linkDetail"></div>
  <div class="clock mono" id="clock">--:--:--</div>
  <div class="mono" style="font-size:.72rem;color:#8892a0;text-align:center;margin-bottom:8px">Watcher <span id="tempF">—</span>&deg;F &nbsp;&bull;&nbsp; Uptime <span id="uptime">—</span></div>
  <div class="statline" id="lastPoll">Last polling attempt: —</div>
</div>

<div class="card">
  <h2>Current Alerts</h2>
  <div id="currentAlerts"><p class="hint">Loading&hellip;</p></div>
</div>

<div class="card">
  <h2>Recent Alerts</h2>
  <p class="hint" style="margin-bottom:10px">Last 5 alerts that triggered a siren activation.</p>
  <div id="recentAlerts"><p class="hint">None yet.</p></div>
</div>

<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Weather Watcher &middot; v<span id="verNum">&mdash;</span></a></div>
<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a></div>

</div>

<script>
function toggleDetail(id){document.getElementById(id).classList.toggle('show');}

function esc(s){const d=document.createElement('div');d.textContent=s||'';return d.innerHTML;}

function renderCurrentAlerts(list){
  const el=document.getElementById('currentAlerts');
  if(!list||!list.length){
    el.innerHTML='<div class="no-alerts"><svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg> No active alerts for your location</div>';
    return;
  }
  el.innerHTML=list.map(a=>
    '<a class="alert-item alert-'+a.color+'" href="'+esc(a.id)+'" target="_blank" rel="noopener">'+
      '<div class="alert-head">'+esc(a.event)+(a.triggered?' <span class="alert-tag">SIREN ACTIVATED</span>':'')+'</div>'+
      '<div class="alert-sub">'+esc(a.headline)+'</div>'+
    '</a>'
  ).join('');
}

function renderRecentAlerts(list){
  const el=document.getElementById('recentAlerts');
  if(!list||!list.length){el.innerHTML='<p class="hint">None yet.</p>';return;}
  el.innerHTML=list.map(r=>
    '<div class="recent-item"><b>'+esc(r.event)+'</b> &rarr; '+esc(r.mode)+
    '<div class="recent-time">'+esc(r.time)+'</div></div>'
  ).join('');
}

function fmtUp(s){
  const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),
        m=Math.floor((s%3600)/60),sc=s%60;
  return (d?d+'d ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sc).padStart(2,'0');
}

function testLink(){
  toggleDetail('linkDetail');
  fetch('/test-link',{method:'POST'});
}

function refresh(){
  fetch('/status-data').then(r=>r.json()).then(d=>{
    document.getElementById('wifiDot').className='dot '+(d.wifiConnected?'green':'red');
    document.getElementById('wifiDetail').textContent=d.wifiDetail;
    document.getElementById('apiDot').className='dot '+(d.apiGreen?'green':'red');
    document.getElementById('apiDetail').textContent=d.apiDetail;
    document.getElementById('linkDot').className='dot'+(d.everTestedLink?(d.linkOk?' green':' red'):'');
    document.getElementById('linkDetail').textContent=d.linkDetail;
    document.getElementById('clock').textContent=d.currentTime;
    if(d.tempF!=null)document.getElementById('tempF').textContent=d.tempF;
    if(d.uptime!=null)document.getElementById('uptime').textContent=fmtUp(d.uptime);
    document.getElementById('lastPoll').textContent='Last polling attempt: '+d.lastPoll;
    renderCurrentAlerts(d.currentAlerts);
    renderRecentAlerts(d.recentAlerts);
    if(d.fwVersion)document.getElementById('verNum').textContent=d.fwVersion;
  }).catch(()=>{});
}
refresh();
setInterval(refresh,1000);
</script>
</body></html>
)rawliteral";

// ── Settings page ──────────────────────────────────────────────────────────────
static const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings — Weather Watcher</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Cpath fill='%2300d4ff' d='M6 16a4 4 0 0 1 .4-7.97 5.5 5.5 0 0 1 10.6 1.5A4 4 0 0 1 17 16H6z'/%3E%3Cpath fill='%23fbbf24' d='M13 17l-2 4h2l-1 3 4-5h-2l1-2z'/%3E%3C/svg%3E">
<style>
:root{--bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;--radius:4px}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;min-height:100vh}
.mono{font-family:ui-monospace,"SF Mono","Cascadia Code","Courier New",monospace}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;justify-content:space-between;
        padding:0 16px;z-index:20}
.navbar h1{font-size:1.05rem;color:var(--cyan)}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.content{max-width:480px;margin:0 auto;padding:76px 16px 40px}
.card{background:var(--surface);border-radius:var(--radius);padding:20px;margin-bottom:14px;
      box-shadow:0 1px 3px rgba(0,0,0,.35)}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:1px;color:#9aa3af}
.card-head{display:flex;align-items:center;justify-content:space-between;cursor:pointer;gap:12px}
.card-head h2{margin-bottom:0}
.chevron{color:#9aa3af;flex-shrink:0;transition:transform .15s}
.card.expanded .chevron{transform:rotate(180deg)}
.card-body{display:none;margin-top:14px}
.card.expanded .card-body{display:block}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px}
.row{margin-bottom:10px}
.hint{font-size:.78rem;color:#9aa3af;margin-bottom:12px}
.hint code{font-family:ui-monospace,"SF Mono","Cascadia Code","Courier New",monospace;background:#0a0e18;padding:1px 4px;border-radius:3px}
label{font-size:.78rem;color:#9aa3af;display:block;margin-bottom:3px}
input,select{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:var(--radius);padding:9px 11px;font-size:.9rem;font-family:inherit}
input:focus,select:focus{outline:none;border-color:var(--cyan)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--cyan);cursor:pointer;flex-shrink:0}
.trow{display:flex;align-items:center;justify-content:space-between;padding:8px 0;
      border-top:1px solid #1a2a3a;margin-top:10px}
.trow label:first-child{font-size:.85rem;color:#eaeaea}
.btn{width:100%;margin-top:12px;padding:12px;font-weight:700;border:none;
     border-radius:var(--radius);cursor:pointer;font-size:.95rem;letter-spacing:.5px;font-family:inherit}
.btn:active{opacity:.8}
.save{background:var(--green);color:#04220f}
.danger{background:#7f1d1d;color:#fff;margin-top:6px}
.logout{background:#1b2438;color:#9aa3af;margin-top:6px}
.msg{font-size:.8rem;margin-top:6px;min-height:1.1em;text-align:center}
.ok{color:var(--green)}.er{color:#f87171}
.ws{font-size:.8rem;padding:9px 12px;background:#0a0e18;border-radius:var(--radius);
    margin-bottom:12px;color:#9aa3af;border-left:3px solid transparent}
.ws.ok{color:var(--green);border-left-color:var(--green)}
.ver{text-align:center;color:#c9cdd3;font-size:.72rem;margin:6px 0 4px;padding:8px;
     background:var(--surface);border-radius:var(--radius)}
.ver a{color:inherit;text-decoration:none}
.overlay{position:fixed;inset:0;background:#0009;display:none;align-items:center;justify-content:center;z-index:50}
.mbox{background:var(--surface);border-radius:var(--radius);padding:28px 24px;text-align:center;
      max-width:280px;width:calc(100% - 32px)}
.mbox p{margin-bottom:20px;font-size:1rem}
.mgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.mok{padding:12px;background:#7f1d1d;color:#fff;border:none;border-radius:var(--radius);
     font-weight:700;cursor:pointer;font-family:inherit}
.mcancel{padding:12px;background:#1b2438;color:#9aa3af;border:none;border-radius:var(--radius);
         font-weight:700;cursor:pointer;font-family:inherit}
.chklist{max-height:260px;overflow-y:auto;margin:10px 0;border:1px solid #1a2a3a;border-radius:var(--radius);padding:4px 10px}
.chkrow{display:flex;align-items:center;gap:10px;padding:7px 0;border-top:1px solid #1a2a3a}
.chkrow:first-child{border-top:none}
.chkrow label{font-size:.85rem;color:#eaeaea;margin:0}
.chkrow input[type=checkbox]{width:18px;height:18px}
</style></head>
<body>
<div class="navbar">
  <a href="/" class="ibtn" title="Back">
    <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <h1>Settings</h1>
  <button class="ibtn" onclick="showRestart()" title="Restart">
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
  </button>
</div>

<div class="content">

<!-- WiFi -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Wi-Fi</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div id="ws" class="ws">Checking&hellip;</div>
  <div class="row"><label>Device Hostname</label><input type="text" id="wHost" placeholder="weather-watcher" maxlength="32"></div>
  <p class="hint">Letters, numbers, and hyphens only. Sets both the name your router shows for this device and its <code>http://&lt;hostname&gt;.local</code> address. Takes effect after a restart.</p>
  <button class="btn save" onclick="saveHostname()">Save Hostname</button>
  <div class="msg" id="hm"></div>
  <div class="row" style="margin-top:14px"><label>Network SSID</label><input type="text" id="wSSID" placeholder="Your WiFi name"></div>
  <div class="row"><label>Password</label><input type="password" id="wPass" placeholder="WiFi password"></div>
  <button class="btn save" onclick="saveWifi()">Connect to Network</button>
  <button class="btn danger" onclick="clearWifi()">Use AP Mode Only</button>
  <div class="msg" id="wm"></div>
  </div>
</div>

<!-- Security -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Security</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="row"><label>New Password</label><input type="password" id="p1" placeholder="New password"></div>
  <div class="row"><label>Confirm Password</label><input type="password" id="p2" placeholder="Confirm"></div>
  <button class="btn save" onclick="savePw()">Change Password</button>
  <button class="btn logout" onclick="logout()">Log Out</button>
  <div class="msg" id="pm"></div>
  </div>
</div>

<!-- Alert Location -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Alert Location</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Point coordinates, not a county/zip — this is what lets a storm-based warning polygon be matched precisely instead of your whole county.</p>
  <div class="grid2">
    <div><label>Latitude</label><input type="text" id="lat" placeholder="e.g. 35.4676"></div>
    <div><label>Longitude</label><input type="text" id="lon" placeholder="e.g. -97.5164"></div>
  </div>
  <div class="row"><label>Poll interval (seconds)</label><input type="number" id="pollSec" min="30" step="1"></div>
  <div class="row"><label>User-Agent contact (required by the NWS API)</label>
    <input type="text" id="uaContact" placeholder="e.g. you@example.com"></div>
  <button class="btn save" onclick="saveLocation()">Save Alert Settings</button>
  <div class="msg" id="xm"></div>
  </div>
</div>

<!-- Trigger Modes -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Trigger Modes</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Which Hurricane Controls run mode to request for each qualifying alert category. Sent as "WX &lt;MODE&gt;" over the dedicated UART link. OFF means the alert still shows on the Dashboard but never triggers the siren.</p>
  <div class="row"><label>Tornado Warning — unconfirmed</label>
    <select id="torUcMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Tornado Warning — confirmed (observed or damage-tagged)</label>
    <select id="torCfMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Tornado Emergency (catastrophic damage threat)</label>
    <select id="torEmMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Severe T-storm Warning — base (no damage tag)</label>
    <select id="svrBaMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Severe T-storm Warning — considerable damage threat</label>
    <select id="svrCoMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Severe T-storm Warning — destructive damage threat</label>
    <select id="svrDeMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="trow">
    <label for="repeatUpg">Re-trigger if an already-triggered alert escalates further</label>
    <input type="checkbox" id="repeatUpg" onchange="saveModes()">
  </div>
  <button class="btn save" onclick="saveModes()">Save Trigger Modes</button>
  <div class="msg" id="mm"></div>

  <p class="hint" style="margin-top:18px;border-top:1px solid #1a2a3a;padding-top:14px">
    <b style="color:#eaeaea">Other Extreme Emergency</b> &mdash; any alert marked Extreme severity, Immediate urgency, and Observed certainty (NWS's own "this is happening right now, as serious as it gets" markers) that isn't a Tornado or Severe T-storm Warning. These don't carry a dedicated damage tag, so each specific warning type below must be checked on to ever trigger — unchecked types never will, no matter how severe, so e.g. a Flood Warning can stay off if it's not a risk for your location.
  </p>
  <div class="row"><label>Siren mode for any checked type below</label>
    <select id="otherMode"><option>OFF</option><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="chklist" id="otherList"></div>
  <button class="btn save" onclick="saveModes()">Save Other Extreme Emergency</button>
  </div>
</div>

<!-- Time / NTP -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Time (NTP)</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Used for the dashboard's clock and alert timestamps. Requires an internet connection to sync.</p>
  <div class="row"><label>NTP server</label><input type="text" id="ntpServer" placeholder="pool.ntp.org"></div>
  <div class="row"><label>Update frequency (hours)</label><input type="number" id="ntpUpdateHours" min="1" step="1"></div>
  <div class="row"><label>Time zone</label>
    <select id="timeZone">
      <option value="EASTERN">Eastern</option>
      <option value="CENTRAL">Central</option>
      <option value="MOUNTAIN">Mountain</option>
      <option value="ARIZONA">Arizona (no DST)</option>
      <option value="PACIFIC">Pacific</option>
      <option value="ALASKA">Alaska</option>
      <option value="HAWAII">Hawaii (no DST)</option>
    </select></div>
  <div class="trow">
    <label for="autoDst">Automatically adjust for Daylight Saving Time</label>
    <input type="checkbox" id="autoDst">
  </div>
  <button class="btn save" onclick="saveTime()">Save Time Settings</button>
  <div class="msg" id="tmz"></div>
  </div>
</div>

<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Weather Watcher &middot; v<span id="verNum">&mdash;</span></a></div>
<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a></div>

</div>

<div class="overlay" id="rmModal">
  <div class="mbox">
    <p>Restart the Weather Watcher?</p>
    <div class="mgrid">
      <button class="mok"     onclick="doRestart()">Restart</button>
      <button class="mcancel" onclick="closeModal()">Cancel</button>
    </div>
  </div>
</div>
<script>
function toggleCard(headEl){ headEl.parentElement.classList.toggle('expanded'); }

function post(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(body)}).then(r=>r.json());}
function msg(id,txt,ok){
  const e=document.getElementById(id);e.textContent=txt;e.className='msg '+(ok?'ok':'er');
  setTimeout(()=>{e.textContent='';e.className='msg';},3500);
}

// Mirrors weather-watcher/src/other_extreme_types.h — keep in sync if that table changes.
const OTHER_EXTREME_CANDIDATES=[
  ["FIRE","Fire Warning"],["CEM","Civil Emergency Message"],["CIVDNG","Civil Danger Warning"],
  ["HAZMAT","Hazardous Materials Warning"],["RAD","Radiological Hazard Warning"],
  ["NUKE","Nuclear Power Plant Warning"],["SHELTER","Shelter In Place Warning"],
  ["EVAC","Evacuation Immediate"],["LAWENF","Law Enforcement Warning"],
  ["LOCAL","Local Area Emergency"],["911","911 Telephone Outage Emergency"],
  ["FFW","Flash Flood Warning"],["FLW","Flood Warning"],["XWIND","Extreme Wind Warning"],
  ["TSUNAMI","Tsunami Warning"],["VOLCANO","Volcano Warning"],["EQ","Earthquake Warning"],
  ["DUST","Dust Storm Warning"],
];
function renderOtherList(includedCsv){
  const included=new Set((includedCsv||'').split(',').map(s=>s.trim()).filter(Boolean));
  document.getElementById('otherList').innerHTML=OTHER_EXTREME_CANDIDATES.map(([code,label])=>
    '<div class="chkrow"><input type="checkbox" id="oe_'+code+'" data-code="'+code+'"'+
    (included.has(code)?' checked':'')+'><label for="oe_'+code+'">'+label+'</label></div>'
  ).join('');
}
function collectOtherIncluded(){
  return Array.from(document.querySelectorAll('#otherList input[type=checkbox]:checked'))
    .map(el=>el.dataset.code).join(',');
}

fetch('/settings-data').then(r=>r.json()).then(d=>{
  document.getElementById('lat').value=d.latitude||'';
  document.getElementById('lon').value=d.longitude||'';
  document.getElementById('pollSec').value=d.pollIntervalSec||120;
  document.getElementById('uaContact').value=d.userAgentContact||'';
  document.getElementById('torUcMode').value=d.tornadoUnconfirmedMode||'OFF';
  document.getElementById('torCfMode').value=d.tornadoConfirmedMode||'WAIL';
  document.getElementById('torEmMode').value=d.tornadoEmergencyMode||'WAIL';
  document.getElementById('svrBaMode').value=d.thunderstormBaseMode||'OFF';
  document.getElementById('svrCoMode').value=d.thunderstormConsiderableMode||'WAIL';
  document.getElementById('svrDeMode').value=d.thunderstormDestructiveMode||'WAIL';
  document.getElementById('otherMode').value=d.otherExtremeMode||'OFF';
  renderOtherList(d.otherExtremeIncluded);
  document.getElementById('repeatUpg').checked=!!d.repeatOnUpgrade;
  document.getElementById('ntpServer').value=d.ntpServer||'pool.ntp.org';
  document.getElementById('ntpUpdateHours').value=d.ntpUpdateHours||12;
  document.getElementById('timeZone').value=d.timeZone||'EASTERN';
  document.getElementById('autoDst').checked=d.autoDst!==false;
  document.getElementById('verNum').textContent=d.fwVersion||'-';
});

fetch('/wifi-data').then(r=>r.json()).then(d=>{
  const el=document.getElementById('ws');
  if(d.connected){el.textContent='Connected: '+d.ssid+' ('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
  document.getElementById('wHost').value=d.hostname||'weather-watcher';
});

function saveWifi(){
  const ssid=document.getElementById('wSSID').value.trim();
  const pass=document.getElementById('wPass').value;
  post('/wifi-data',{ssid,pass}).then(()=>msg('wm','Saved — rebooting...',true));
}
function clearWifi(){
  post('/wifi-data',{clear:true}).then(()=>msg('wm','Cleared — rebooting into AP mode...',true));
}
function saveHostname(){
  const hostname=document.getElementById('wHost').value.trim();
  const valid=/^[A-Za-z0-9-]{1,32}$/.test(hostname) && !hostname.startsWith('-') && !hostname.endsWith('-');
  if(!valid){msg('hm','Letters, numbers, and hyphens only (no leading/trailing hyphen)',false);return;}
  post('/wifi-data',{hostname}).then(d=>msg('hm',d.ok?'Saved — rebooting...':'Failed',d.ok));
}
function savePw(){
  const p1=document.getElementById('p1').value;
  const p2=document.getElementById('p2').value;
  const strong = p1.length>=8 && /[A-Z]/.test(p1) && /[a-z]/.test(p1) && /[0-9]/.test(p1) && /[^A-Za-z0-9]/.test(p1);
  if(!strong){msg('pm','Min 8 chars, with upper, lower, number, and special character',false);return;}
  if(p1!==p2){msg('pm','Passwords do not match',false);return;}
  post('/password',{password:p1}).then(d=>{
    msg('pm',d.ok?'Password changed':'Failed',d.ok);
    if(d.ok){document.getElementById('p1').value='';document.getElementById('p2').value='';}
  });
}
function logout(){post('/auth/logout',{}).then(()=>location.href='/login');}
function saveLocation(){
  const b={
    latitude: parseFloat(document.getElementById('lat').value||0),
    longitude: parseFloat(document.getElementById('lon').value||0),
    pollIntervalSec: parseInt(document.getElementById('pollSec').value||120,10),
    userAgentContact: document.getElementById('uaContact').value.trim(),
  };
  post('/settings-data',b).then(d=>msg('xm',d.ok?'Saved!':'Error',d.ok));
}
function saveModes(){
  const b={
    tornadoUnconfirmedMode: document.getElementById('torUcMode').value,
    tornadoConfirmedMode: document.getElementById('torCfMode').value,
    tornadoEmergencyMode: document.getElementById('torEmMode').value,
    thunderstormBaseMode: document.getElementById('svrBaMode').value,
    thunderstormConsiderableMode: document.getElementById('svrCoMode').value,
    thunderstormDestructiveMode: document.getElementById('svrDeMode').value,
    otherExtremeMode: document.getElementById('otherMode').value,
    otherExtremeIncluded: collectOtherIncluded(),
    repeatOnUpgrade: document.getElementById('repeatUpg').checked,
  };
  post('/settings-data',b).then(d=>msg('mm',d.ok?'Saved!':'Error',d.ok));
}
function saveTime(){
  const b={
    ntpServer: document.getElementById('ntpServer').value.trim(),
    ntpUpdateHours: parseInt(document.getElementById('ntpUpdateHours').value||12,10),
    timeZone: document.getElementById('timeZone').value,
    autoDst: document.getElementById('autoDst').checked,
  };
  post('/settings-data',b).then(d=>msg('tmz',d.ok?'Saved!':'Error',d.ok));
}

function showRestart(){document.getElementById('rmModal').style.display='flex';}
function closeModal(){document.getElementById('rmModal').style.display='none';}
function doRestart(){closeModal();fetch('/restart',{method:'POST'});}
</script>
</body></html>
)rawliteral";

class WeatherWebUI {
public:
    void begin() {
        setupRoutes();
        server_.begin();
    }

    void update() {}

private:
    AsyncWebServer   server_{80};
    std::set<String> sessions_;

    // ── Login rate-limiting (mirrors the main board's single-user approach) ──
    uint8_t  failedLoginAttempts_ = 0;
    uint32_t loginLockoutUntil_   = 0;
    static constexpr uint8_t  MAX_LOGIN_ATTEMPTS = 5;
    static constexpr uint32_t LOGIN_LOCKOUT_MS   = 30000;

    // ── Auth helpers ─────────────────────────────────────────────────────────

    String generateToken() {
        char buf[33];
        for (int i = 0; i < 4; i++) snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
        buf[32] = '\0';
        return String(buf);
    }

    String getSessionToken(AsyncWebServerRequest* req) {
        const AsyncWebHeader* h = req->getHeader("Cookie");
        if (!h) return "";
        String c = h->value();
        int idx = c.indexOf("sid=");
        if (idx < 0) return "";
        int end = c.indexOf(';', idx);
        return (end < 0) ? c.substring(idx + 4) : c.substring(idx + 4, end);
    }

    bool isAuthed(AsyncWebServerRequest* req) {
        return sessions_.count(getSessionToken(req)) > 0;
    }

    void redirectLogin(AsyncWebServerRequest* req) {
        AsyncWebServerResponse* r = req->beginResponse(302);
        r->addHeader("Location", "/login");
        req->send(r);
    }

    static void bodyAccumulator(AsyncWebServerRequest* req,
                                uint8_t* data, size_t len,
                                size_t index, size_t total) {
        if (index == 0) req->_tempObject = malloc(total + 1);
        if (req->_tempObject) {
            memcpy((uint8_t*)req->_tempObject + index, data, len);
            if (index + len == total)
                ((char*)req->_tempObject)[total] = '\0';
        }
    }

    // ── Route setup ──────────────────────────────────────────────────────────

    void setupRoutes() {
        server_.on("/login", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/html", LOGIN_HTML);
        });

        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // Public dashboard — status/alert awareness needs no login; only
            // Settings and anything that changes device state is gated.
            req->send(200, "text/html", DASHBOARD_HTML);
        });

        server_.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            req->send(200, "text/html", SETTINGS_HTML);
        });

        server_.on("/auth/login", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                uint32_t now = millis();
                if (now < loginLockoutUntil_) {
                    if (req->_tempObject) { free(req->_tempObject); req->_tempObject = nullptr; }
                    uint32_t retryAfter = (loginLockoutUntil_ - now + 999) / 1000;
                    req->send(200, "application/json",
                        "{\"ok\":false,\"locked\":true,\"retryAfter\":" + String(retryAfter) + "}");
                    return;
                }
                bool ok = false;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* pw = doc["password"] | "";
                    ok = (strcmp(pw, settingsMgr.s.webPassword) == 0);
                }
                if (ok) {
                    failedLoginAttempts_ = 0;
                    String token = generateToken();
                    sessions_.insert(token);
                    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", "{\"ok\":true}");
                    resp->addHeader("Set-Cookie", "sid=" + token + "; Path=/; HttpOnly; SameSite=Strict");
                    req->send(resp);
                } else {
                    failedLoginAttempts_++;
                    if (failedLoginAttempts_ >= MAX_LOGIN_ATTEMPTS) {
                        loginLockoutUntil_ = now + LOGIN_LOCKOUT_MS;
                        failedLoginAttempts_ = 0;
                    }
                    req->send(200, "application/json", "{\"ok\":false}");
                }
            },
            nullptr, bodyAccumulator
        );

        server_.on("/auth/logout", HTTP_GET, [this](AsyncWebServerRequest* req) {
            sessions_.erase(getSessionToken(req));
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json", "{\"ok\":true}");
            req->send(r);
        });
        server_.on("/auth/logout", HTTP_POST, [this](AsyncWebServerRequest* req) {
            sessions_.erase(getSessionToken(req));
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/password", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                bool ok = false;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* pw = doc["password"] | "";
                    if (isStrongPassword(pw)) {
                        strlcpy(settingsMgr.s.webPassword, pw, sizeof(settingsMgr.s.webPassword));
                        settingsMgr.save();
                        sessions_.clear();
                        ok = true;
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/status-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // Public — feeds the unauthenticated dashboard.
            req->send(200, "application/json", buildStatusJson());
        });

        server_.on("/settings-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", buildSettingsJson());
        });

        server_.on("/settings-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    Settings& s = settingsMgr.s;
                    if (!doc["latitude"].isNull())  s.latitude  = doc["latitude"].as<float>();
                    if (!doc["longitude"].isNull()) s.longitude = doc["longitude"].as<float>();
                    if (!doc["pollIntervalSec"].isNull()) s.pollIntervalSec = doc["pollIntervalSec"].as<uint32_t>();
                    if (doc["userAgentContact"].is<const char*>())
                        strlcpy(s.userAgentContact, doc["userAgentContact"].as<const char*>(), sizeof(s.userAgentContact));
                    if (doc["tornadoUnconfirmedMode"].is<const char*>())
                        strlcpy(s.tornadoUnconfirmedMode, doc["tornadoUnconfirmedMode"].as<const char*>(), sizeof(s.tornadoUnconfirmedMode));
                    if (doc["tornadoConfirmedMode"].is<const char*>())
                        strlcpy(s.tornadoConfirmedMode, doc["tornadoConfirmedMode"].as<const char*>(), sizeof(s.tornadoConfirmedMode));
                    if (doc["tornadoEmergencyMode"].is<const char*>())
                        strlcpy(s.tornadoEmergencyMode, doc["tornadoEmergencyMode"].as<const char*>(), sizeof(s.tornadoEmergencyMode));
                    if (doc["thunderstormBaseMode"].is<const char*>())
                        strlcpy(s.thunderstormBaseMode, doc["thunderstormBaseMode"].as<const char*>(), sizeof(s.thunderstormBaseMode));
                    if (doc["thunderstormConsiderableMode"].is<const char*>())
                        strlcpy(s.thunderstormConsiderableMode, doc["thunderstormConsiderableMode"].as<const char*>(), sizeof(s.thunderstormConsiderableMode));
                    if (doc["thunderstormDestructiveMode"].is<const char*>())
                        strlcpy(s.thunderstormDestructiveMode, doc["thunderstormDestructiveMode"].as<const char*>(), sizeof(s.thunderstormDestructiveMode));
                    if (doc["otherExtremeMode"].is<const char*>())
                        strlcpy(s.otherExtremeMode, doc["otherExtremeMode"].as<const char*>(), sizeof(s.otherExtremeMode));
                    if (doc["otherExtremeIncluded"].is<const char*>())
                        strlcpy(s.otherExtremeIncluded, doc["otherExtremeIncluded"].as<const char*>(), sizeof(s.otherExtremeIncluded));
                    if (doc["repeatOnUpgrade"].is<bool>()) s.repeatOnUpgrade = doc["repeatOnUpgrade"].as<bool>();
                    if (doc["ntpServer"].is<const char*>())
                        strlcpy(s.ntpServer, doc["ntpServer"].as<const char*>(), sizeof(s.ntpServer));
                    if (!doc["ntpUpdateHours"].isNull()) s.ntpUpdateHours = doc["ntpUpdateHours"].as<uint32_t>();
                    if (doc["timeZone"].is<const char*>())
                        strlcpy(s.timeZone, doc["timeZone"].as<const char*>(), sizeof(s.timeZone));
                    if (doc["autoDst"].is<bool>()) s.autoDst = doc["autoDst"].as<bool>();
                    settingsMgr.save();
                    applyTimeConfig();
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/wifi-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            JsonDocument doc;
            doc["mode"]      = (wifiMgr.getMode() == WiFiManager::Mode::STA) ? "sta" : "ap";
            doc["connected"] = wifiMgr.isConnected();
            doc["ssid"]      = wifiMgr.getSSID();
            doc["ip"]        = wifiMgr.getIP();
            doc["hostname"]  = wifiMgr.getHostname();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        server_.on("/wifi-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    bool changed = false;
                    if (doc["clear"].as<bool>()) {
                        wifiMgr.clearCredentials();
                        changed = true;
                    } else if (!doc["hostname"].is<const char*>()) {
                        // Only touch ssid/pass on a request that isn't a
                        // hostname-only save — the password field is never
                        // pre-filled, so bundling them would force
                        // re-entering credentials just to rename the device.
                        const char* ssid = doc["ssid"] | "";
                        const char* pass = doc["pass"] | "";
                        if (strlen(ssid) > 0) { wifiMgr.saveCredentials(ssid, pass); changed = true; }
                    }
                    if (doc["hostname"].is<const char*>()) {
                        const char* hostname = doc["hostname"].as<const char*>();
                        if (!isValidHostname(hostname)) {
                            req->send(200, "application/json", "{\"ok\":false}");
                            return;
                        }
                        wifiMgr.saveHostname(hostname);
                        changed = true;
                    }
                    if (changed) wifiMgr.scheduleRestart(1500);
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/restart", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
            wifiMgr.scheduleRestart(500);
        });

        server_.on("/test-link", HTTP_POST, [this](AsyncWebServerRequest* req) {
            // Public — the dashboard's Controller Link row triggers this with
            // no login; it only re-tests the UART wire, nothing mutates settings.
            nwsClient.requestLinkTest();
            req->send(200, "application/json", "{\"ok\":true}");
        });
    }

    // ── JSON builders ────────────────────────────────────────────────────────

    static String buildSettingsJson() {
        const Settings& s = settingsMgr.s;
        JsonDocument doc;
        doc["latitude"]         = s.latitude;
        doc["longitude"]        = s.longitude;
        doc["pollIntervalSec"]  = s.pollIntervalSec;
        doc["tornadoUnconfirmedMode"]        = s.tornadoUnconfirmedMode;
        doc["tornadoConfirmedMode"]          = s.tornadoConfirmedMode;
        doc["tornadoEmergencyMode"]          = s.tornadoEmergencyMode;
        doc["thunderstormBaseMode"]          = s.thunderstormBaseMode;
        doc["thunderstormConsiderableMode"]  = s.thunderstormConsiderableMode;
        doc["thunderstormDestructiveMode"]   = s.thunderstormDestructiveMode;
        doc["otherExtremeMode"]              = s.otherExtremeMode;
        doc["otherExtremeIncluded"]          = s.otherExtremeIncluded;
        doc["userAgentContact"] = s.userAgentContact;
        doc["repeatOnUpgrade"]  = s.repeatOnUpgrade;
        doc["ntpServer"]        = s.ntpServer;
        doc["ntpUpdateHours"]   = s.ntpUpdateHours;
        doc["timeZone"]         = s.timeZone;
        doc["autoDst"]          = s.autoDst;
        doc["fwVersion"]        = FW_VERSION;
        String out;
        serializeJson(doc, out);
        return out;
    }

    static String formatEpoch(uint32_t epoch, const char* fmt) {
        time_t t = epoch;
        char buf[32];
        strftime(buf, sizeof(buf), fmt, localtime(&t));
        return String(buf);
    }

    static String buildStatusJson() {
        JsonDocument doc;
        doc["fwVersion"] = FW_VERSION;

        bool wifiOk = wifiMgr.isConnected();
        doc["wifiConnected"] = wifiOk;
        doc["wifiDetail"] = wifiOk
            ? ("Connected: " + wifiMgr.getSSID() + " (" + wifiMgr.getIP() + ")")
            : String("Not connected to a network — device is in AP mode, or Wi-Fi join failed. Check Settings > Wi-Fi.");

        bool locationOk = nwsClient.locationConfigured();
        bool apiGreen = locationOk && nwsClient.everPolled() && nwsClient.lastPollSuccess();
        doc["apiGreen"] = apiGreen;
        String apiDetail;
        if (!locationOk) apiDetail = "Location not configured — set latitude/longitude in Settings > Alert Location.";
        else if (!nwsClient.everPolled()) apiDetail = "Waiting for the first poll...";
        else if (!nwsClient.lastPollSuccess()) apiDetail = nwsClient.lastPollError();
        else apiDetail = "Status OK";
        doc["apiDetail"] = apiDetail;

        doc["everTestedLink"] = nwsClient.everTestedLink();
        doc["linkOk"]         = nwsClient.linkOk();
        doc["linkDetail"]     = nwsClient.linkTestInProgress() ? String("Testing...") : nwsClient.linkDetail();

        time_t t = time(nullptr);
        doc["currentTime"] = (t > 1000000000) ? formatEpoch((uint32_t)t, "%H:%M:%S") : String("Not synced");
        doc["uptime"] = (uint32_t)(millis() / 1000);
        float tempF = temperatureRead() * 9.0f / 5.0f + 32.0f;
        doc["tempF"] = (int)roundf(tempF);

        if (!nwsClient.everPolled()) {
            doc["lastPoll"] = "Never";
        } else {
            uint32_t ep = nwsClient.lastPollEpoch();
            doc["lastPoll"] = (ep > 0) ? formatEpoch(ep, "%Y-%m-%d %H:%M:%S") : String("Just now (clock not yet synced)");
        }

        JsonArray arr = doc["currentAlerts"].to<JsonArray>();
        for (uint8_t i = 0; i < nwsClient.currentAlertCount(); i++) {
            const CurrentAlert& a = nwsClient.currentAlert(i);
            JsonObject o = arr.add<JsonObject>();
            o["id"]        = a.id;
            o["event"]     = a.event;
            o["headline"]  = a.headline;
            o["areaDesc"]  = a.areaDesc;
            o["color"]     = categoryColor(a.category);
            o["triggered"] = a.triggeredSiren;
        }

        JsonArray rec = doc["recentAlerts"].to<JsonArray>();
        for (uint8_t i = 0; i < nwsClient.recentTriggerCount(); i++) {
            const RecentTrigger& r = nwsClient.recentTrigger(i);
            JsonObject o = rec.add<JsonObject>();
            o["event"] = r.event;
            o["mode"]  = r.mode;
            o["time"]  = (r.epoch > 0) ? formatEpoch(r.epoch, "%Y-%m-%d %H:%M:%S") : String("Unknown time");
        }

        String out;
        serializeJson(doc, out);
        return out;
    }
};

extern WeatherWebUI webUI;
