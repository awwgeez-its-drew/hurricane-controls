#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <set>
#include <cctype>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "motors.h"
#include "wifi_manager.h"
#include "buttons.h"

// Enforces the public-facing password policy: 8+ chars with at least one
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

// ── Login page ────────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hurricane Controls</title>
<style>
:root{
  --bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;
  --wail:#1e3a8a;--attack:#7f1d1d;--fastwail:#78350f;--manual:#4c1d95;
  --stop-bg:#fca5a5;--stop-text:#000;--radius:4px;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:#eaeaea;
     font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
.card{background:var(--surface);border-radius:var(--radius);padding:36px 28px;width:100%;max-width:340px;
      text-align:center;box-shadow:0 1px 3px rgba(0,0,0,.35)}
h1{font-size:1.5rem;color:var(--cyan);letter-spacing:1px;margin-bottom:4px}
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
  <h1>&#x1F32A; Hurricane Controls</h1>
  <p class="sub">Siren Controller</p>
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
    if(d.ok){location.href='/';}
    else if(d.locked){document.getElementById('err').textContent='Too many attempts. Try again in '+d.retryAfter+'s.';}
    else{document.getElementById('err').textContent='Incorrect password';
         document.getElementById('pw').value='';}
  }).catch(()=>{document.getElementById('err').textContent='Connection error';});
}
</script></body></html>
)rawliteral";

// ── Main control page ─────────────────────────────────────────────────────────
static const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hurricane Controls</title>
<style>
:root{
  --bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;
  --wail:#1e3a8a;--attack:#7f1d1d;--fastwail:#78350f;--manual:#4c1d95;
  --stop-bg:#fca5a5;--stop-text:#000;--radius:4px;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     min-height:100vh}
.mono{font-family:ui-monospace,"SF Mono","Cascadia Code","Courier New",monospace}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;justify-content:space-between;
        padding:0 16px;z-index:20}
.navbar .brand{display:flex;flex-direction:column;justify-content:center;overflow:hidden;margin-right:8px}
.navbar h1{font-size:.82rem;color:var(--cyan);letter-spacing:.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.navbar .subtitle{font-size:.62rem;color:#8892a0;letter-spacing:1px}
.navicons{display:flex;gap:14px;align-items:center;flex-shrink:0}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.ibtn.locked{color:#f87171}
.content{max-width:480px;margin:0 auto;padding:76px 16px 100px}
/* Status card */
.scard{width:100%;background:var(--surface);border-radius:var(--radius);padding:22px 20px;
       margin-bottom:14px;text-align:center;box-shadow:0 1px 3px rgba(0,0,0,.35)}
.badge{display:inline-block;padding:5px 18px;border-radius:var(--radius);font-size:.8rem;
       font-weight:700;letter-spacing:2px;margin-bottom:14px;font-family:inherit}
.idle    {background:transparent;color:#9aa3af;border:1px solid var(--cyan)}
.wail    {background:var(--wail);color:#bcd2ff}
.attack  {background:var(--attack);color:#ffc9c9}
.fastwail{background:var(--fastwail);color:#ffd9a0}
.manual  {background:var(--manual);color:#e3caff}
.seq,.stop{background:var(--green);color:#04220f}
.timer{font-size:3rem;font-weight:700;letter-spacing:3px;
       font-variant-numeric:tabular-nums;min-height:3.5rem;line-height:1}
.tsub{font-size:.9rem;color:#9aa3af;margin-top:8px;min-height:1.4em}
/* Relay dots */
.dots{display:flex;gap:24px;justify-content:center;margin-top:16px}
.dw{display:flex;flex-direction:column;align-items:center;gap:3px}
.dot{width:13px;height:13px;border-radius:50%;background:#2a2f3a;transition:background .3s}
.dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
.dl{font-size:.65rem;color:#9aa3af;letter-spacing:.5px}
/* Buttons */
.grid{width:100%;display:grid;grid-template-columns:1fr 1fr;gap:10px}
.btn{padding:26px 10px;border:none;border-radius:var(--radius);font-size:1.02rem;font-weight:700;
     cursor:pointer;letter-spacing:1px;transition:opacity .15s,transform .1s;width:100%;
     box-shadow:0 1px 3px rgba(0,0,0,.35);font-family:inherit}
.btn:active{transform:scale(.97)}
.bwail    {background:var(--wail);color:#eaf1ff}
.battack  {background:var(--attack);color:#ffeaea}
.bfastwail{background:var(--fastwail);color:#fff3e0}
.bmanual  {background:var(--manual);color:#f3eaff}
.bmanual.on{box-shadow:0 0 0 2px var(--green),0 1px 3px rgba(0,0,0,.35)}
.overlay{position:fixed;inset:0;background:#0009;display:none;align-items:center;justify-content:center;z-index:50}
.mbox{background:var(--surface);border-radius:var(--radius);padding:28px 24px;text-align:center;
      max-width:280px;width:calc(100% - 32px)}
.mbox p{margin-bottom:20px;font-size:1rem}
.mgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.mok{padding:12px;background:var(--attack);color:#fff;border:none;border-radius:var(--radius);
     font-weight:700;cursor:pointer;font-family:inherit}
.mcancel{padding:12px;background:#1b2438;color:#9aa3af;border:none;border-radius:var(--radius);
         font-weight:700;cursor:pointer;font-family:inherit}
.stopbar{position:fixed;bottom:0;left:0;right:0;background:var(--stop-bg);color:var(--stop-text);
         display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;letter-spacing:1px;
         padding:18px;cursor:pointer;border-top:2px solid rgba(0,0,0,.15);z-index:20;font-size:1.1rem}
.stopbar:active{opacity:.85}
</style></head><body>
<div class="navbar">
  <div class="brand">
    <h1>Hurricane Controls</h1>
    <span class="subtitle">CONTROL PANEL</span>
  </div>
  <div class="navicons">
    <button class="ibtn" id="btnLock" onclick="toggleLock()" title="Lock physical buttons">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="10" rx="2"/><path id="lockShackle" d="M8 11V7a4 4 0 0 1 7.5-2"/></svg>
    </button>
    <button class="ibtn" onclick="showRestart()" title="Restart">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
    </button>
    <a href="/settings" class="ibtn" title="Settings">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
    </a>
  </div>
</div>

<div class="content">
<div class="scard">
  <div><span id="badge" class="badge idle">IDLE</span></div>
  <div class="timer mono" id="timer">--:--</div>
  <div class="tsub"  id="tsub">Ready</div>
  <div class="dots">
    <div class="dw"><div id="d0" class="dot"></div><span class="dl">CHOPPER</span></div>
    <div class="dw"><div id="d1" class="dot"></div><span class="dl">BLOWER</span></div>
    <div class="dw"><div id="d2" class="dot"></div><span class="dl">ROTATOR</span></div>
  </div>
  <div class="mono" style="font-size:.72rem;color:#8892a0;margin-top:10px">Controller <span id="tempF">—</span>&deg;F &nbsp;&bull;&nbsp; Uptime <span id="uptime">—</span></div>
</div>

<div class="grid">
  <button class="btn bwail"     onclick="activate('wail')">WAIL</button>
  <button class="btn battack"   onclick="activate('attack')">ATTACK</button>
  <button class="btn bfastwail" onclick="activate('fastwail')">FAST WAIL</button>
  <button class="btn bmanual" id="bm"
    onmousedown="manualDown(event)" onmouseup="manualUp(event)" onmouseleave="manualUp(event)"
    ontouchstart="manualDown(event)" ontouchend="manualUp(event)" ontouchcancel="manualUp(event)">MANUAL</button>
</div>
</div>

<div class="stopbar" onclick="claxon();cmd('stop')">&#9632; STOP</div>

<div class="overlay" id="rmModal">
  <div class="mbox">
    <p>Restart the controller?</p>
    <div class="mgrid">
      <button class="mok"     onclick="doRestart()">Restart</button>
      <button class="mcancel" onclick="closeModal()">Cancel</button>
    </div>
  </div>
</div>

<script>
let manOn=false, sirenActive=false, webManualHeld=false;

const ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>draw(JSON.parse(e.data));
ws.onerror=ws.onclose=()=>setInterval(()=>fetch('/status').then(r=>r.json()).then(draw),1200);

function fmt(sec){
  if(sec==null||sec<0)return'--:--';
  const m=Math.floor(sec/60),s=sec%60;
  return String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
}

function draw(d){
  const st=d.mode||'idle', rm=d.runMode||'idle';
  const badge=document.getElementById('badge');
  if(d.testMode){
    badge.textContent='TEST MODE';
    badge.className='badge attack';
  } else {
    badge.textContent=rm.toUpperCase();
    badge.className='badge '+(st==='idle'?'idle':st.startsWith('seq')||st.startsWith('stop')?'seq':
      st.startsWith('attack')?'attack':st.startsWith('fastwail')?'fastwail':st);
  }

  const r=d.relays||[0,0,0];
  [0,1,2].forEach(i=>document.getElementById('d'+i).classList.toggle('on',!!r[i]));

  const te=document.getElementById('timer'),ts=document.getElementById('tsub');
  if(st==='idle'){te.textContent='--:--';ts.textContent='Ready';}
  else if(st.startsWith('seq')){te.textContent='--:--';ts.textContent='Starting…';}
  else if(st.startsWith('stop')){te.textContent=fmt(d.elapsed);ts.textContent='Stopping…';}
  else{
    te.textContent=fmt(d.elapsed);
    if(d.hasRemaining)ts.textContent=fmt(d.remaining)+' remaining';
    else ts.textContent='';
  }

  if(d.tempF!=null)document.getElementById('tempF').textContent=d.tempF;
  if(d.uptime!=null)document.getElementById('uptime').textContent=fmtUp(d.uptime);
  const bl=document.getElementById('btnLock');
  const lk=!!d.btnLocked;
  document.getElementById('lockShackle').setAttribute('d', lk ? 'M8 11V7a4 4 0 0 1 8 0v4' : 'M8 11V7a4 4 0 0 1 7.5-2');
  bl.classList.toggle('locked',lk);
  bl.title=lk?'Physical buttons LOCKED — click to unlock':'Lock physical buttons';
  sirenActive=(st!=='idle');
  manOn=(rm==='manual');
  const bm=document.getElementById('bm');
  bm.textContent=manOn?'MANUAL ●':'MANUAL';
  bm.classList.toggle('on',manOn);
}

function claxon(){
  try{
    const ctx=new(window.AudioContext||window.webkitAudioContext)();
    const beep=(f,t,d)=>{
      const o=ctx.createOscillator(),g=ctx.createGain();
      o.connect(g);g.connect(ctx.destination);
      o.type='sawtooth';o.frequency.value=f;
      g.gain.setValueAtTime(0.18,t);
      g.gain.exponentialRampToValueAtTime(0.001,t+d);
      o.start(t);o.stop(t+d);
    };
    const t=ctx.currentTime;
    beep(880,t,.13);beep(660,t+.13,.13);beep(880,t+.26,.13);
  }catch(e){}
}

function cmd(m){
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:m})});
}
function activate(m){claxon();cmd(m);}
function manualDown(e){
  e.preventDefault(); // suppress synthetic mouse events on touch
  if(sirenActive)return;
  webManualHeld=true;claxon();cmd('manual');
}
function manualUp(e){
  if(e)e.preventDefault();
  if(!webManualHeld)return;
  webManualHeld=false;cmd('stop');
}
function toggleLock(){cmd('btn-lock');}
function fmtUp(s){
  const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),
        m=Math.floor((s%3600)/60),sc=s%60;
  return (d?d+'d ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sc).padStart(2,'0');
}
function showRestart(){document.getElementById('rmModal').style.display='flex';}
function closeModal(){document.getElementById('rmModal').style.display='none';}
function doRestart(){closeModal();fetch('/restart',{method:'POST'});}
</script></body></html>
)rawliteral";

// ── Settings page ─────────────────────────────────────────────────────────────
static const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings — Hurricane Controls</title>
<style>
:root{
  --bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;
  --wail:#1e3a8a;--attack:#7f1d1d;--fastwail:#78350f;--manual:#4c1d95;
  --stop-bg:#fca5a5;--stop-text:#000;--radius:4px;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     min-height:100vh}
.mono{font-family:ui-monospace,"SF Mono","Cascadia Code","Courier New",monospace}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;justify-content:space-between;
        padding:0 16px;z-index:20}
.navbar h1{font-size:1.05rem;color:var(--cyan)}
.navicons{display:flex;gap:14px;align-items:center}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.content{max-width:480px;margin:0 auto;padding:76px 16px 100px}
.card{background:var(--surface);border-radius:var(--radius);padding:20px;margin-bottom:14px;
      box-shadow:0 1px 3px rgba(0,0,0,.35)}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:1px;color:#9aa3af;margin-bottom:14px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px}
.row{margin-bottom:10px}
label{font-size:.78rem;color:#9aa3af;display:block;margin-bottom:3px}
input{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:var(--radius);padding:9px 11px;font-size:.9rem;font-family:inherit}
input:focus{outline:none;border-color:var(--cyan)}
.ws{font-size:.8rem;padding:9px 12px;background:#0a0e18;border-radius:var(--radius);
    margin-bottom:12px;color:#9aa3af;display:flex;align-items:center;gap:8px;border-left:3px solid transparent}
.ws.ok{color:var(--green);border-left-color:var(--green)}
.btn{width:100%;margin-top:12px;padding:12px;font-weight:700;border:none;
     border-radius:var(--radius);cursor:pointer;font-size:.95rem;letter-spacing:.5px;font-family:inherit}
.btn:active{opacity:.8}
.save{background:var(--green);color:#04220f}
.danger{background:var(--attack);color:#fff;margin-top:6px}
.logout{background:#1b2438;color:#9aa3af;margin-top:6px}
.msg{font-size:.8rem;margin-top:6px;min-height:1.1em;text-align:center}
.ok{color:var(--green)}.er{color:#f87171}
.trow{display:flex;align-items:center;justify-content:space-between;padding:8px 0;
      border-top:1px solid #1a2a3a;margin-top:10px}
.trow label:first-child{font-size:.85rem;color:#eaeaea}
.ver{text-align:center;color:#c9cdd3;font-size:.72rem;margin:6px 0 4px;padding:8px;
     background:var(--surface);border-radius:var(--radius)}
.stopbar{position:fixed;bottom:0;left:0;right:0;background:var(--stop-bg);color:var(--stop-text);
         display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;letter-spacing:1px;
         padding:18px;cursor:pointer;border-top:2px solid rgba(0,0,0,.15);z-index:20;font-size:1.1rem}
.stopbar:active{opacity:.85}
</style></head><body>
<div class="navbar">
  <a href="/" class="ibtn" title="Back">
    <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <h1>Settings</h1>
  <div class="navicons">
    <a href="/test" class="ibtn" title="Component Test">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>
    </a>
  </div>
</div>

<div class="content">

<!-- WiFi -->
<div class="card">
  <h2>Wi-Fi</h2>
  <div id="ws" class="ws">Checking&hellip;</div>
  <div class="row"><label>Network SSID</label>
    <input type="text" id="wSSID" placeholder="Your WiFi name" autocomplete="off"></div>
  <div class="row"><label>Password</label>
    <input type="password" id="wPass" placeholder="WiFi password" autocomplete="new-password"></div>
  <button class="btn save" onclick="saveWifi()">Connect to Network</button>
  <button class="btn danger" onclick="clearWifi()">Use AP Mode Only</button>
  <div class="msg" id="wm"></div>
</div>

<!-- Security -->
<div class="card">
  <h2>Security</h2>
  <div class="row"><label>New Password</label>
    <input type="password" id="p1" placeholder="New password" autocomplete="new-password"></div>
  <div class="row"><label>Confirm Password</label>
    <input type="password" id="p2" placeholder="Confirm" autocomplete="new-password"></div>
  <button class="btn save" onclick="savePw()">Change Password</button>
  <button class="btn logout" onclick="logout()">Log Out</button>
  <div class="msg" id="pm"></div>
</div>

<!-- Startup Sequence -->
<div class="card">
  <h2>Startup Sequence</h2>
  <div class="grid2">
    <div><label>Chopper delay (sec)</label><input type="number" id="s_chopperDelay" min="0" step="0.1"></div>
    <div><label>Blower delay (sec)</label><input type="number" id="s_blowerDelay" min="0" step="0.1"></div>
    <div><label>Rotator delay (sec)</label><input type="number" id="s_rotatorDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Startup Sequence</button>
  <div class="msg" id="tmStart"></div>
</div>

<!-- Shutdown Sequence -->
<div class="card">
  <h2>Shutdown Sequence</h2>
  <div class="grid2">
    <div><label>Chopper delay (sec)</label><input type="number" id="s_stopChopperDelay" min="0" step="0.1"></div>
    <div><label>Blower delay (sec)</label><input type="number" id="s_stopBlowerDelay" min="0" step="0.1"></div>
    <div><label>Rotator delay (sec)</label><input type="number" id="s_stopRotDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Shutdown Sequence</button>
  <div class="msg" id="tmStop"></div>
</div>

<!-- General Timing -->
<div class="card">
  <h2>Timing</h2>
  <div class="grid2">
    <div><label>Wail duration (sec)</label><input type="number" id="s_wailDuration" min="1" step="1"></div>
    <div><label>Long-press threshold (sec)</label><input type="number" id="s_longPressMs" min="0.1" step="0.1"></div>
    <div><label>Button debounce (sec)</label><input type="number" id="s_buttonDebounceMs" min="0" step="0.01"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Timing</button>
  <div class="msg" id="tm"></div>
</div>

<!-- Attack Mode -->
<div class="card">
  <h2>Attack Mode</h2>
  <div class="grid2">
    <div><label>Attack duration (sec)</label><input type="number" id="s_attackDuration" min="1" step="1"></div>
    <div><label>Attack ON time (sec)</label><input type="number" id="s_attackOnTime" min="0.1" step="0.1"></div>
    <div><label>Attack OFF time (sec)</label><input type="number" id="s_attackOffTime" min="0.1" step="0.1"></div>
    <div><label>Chopper re-on delay (sec)</label><input type="number" id="s_attackChopperDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Attack Settings</button>
  <div class="msg" id="tm2"></div>
</div>

<!-- Fast Wail Mode -->
<div class="card">
  <h2>Fast Wail Mode</h2>
  <div class="grid2">
    <div><label>Fast Wail duration (sec)</label><input type="number" id="s_fastWailDuration" min="1" step="1"></div>
    <div><label>Fast Wail ON time (sec)</label><input type="number" id="s_fastWailOnTime" min="0.1" step="0.1"></div>
    <div><label>Fast Wail OFF time (sec)</label><input type="number" id="s_fastWailOffTime" min="0.1" step="0.1"></div>
    <div><label>Chopper re-on delay (sec)</label><input type="number" id="s_fastWailChopperDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Fast Wail Settings</button>
  <div class="msg" id="tm3"></div>
</div>

<div class="ver mono">Hurricane Controls &middot; v<span id="verNum">—</span></div>
<div class="ver mono">Created by awwgeez.its.drew &middot; Coded by Claude</div>

</div>

<div class="stopbar" onclick="claxon();cmd('stop')">&#9632; STOP</div>

<script>
// All timing fields stored as ms, displayed as seconds
const TS=['wailDuration','attackDuration','attackOnTime','attackOffTime','attackChopperDelay',
          'chopperDelay','blowerDelay','rotatorDelay',
          'stopBlowerDelay','stopChopperDelay','stopRotDelay','longPressMs','buttonDebounceMs',
          'fastWailDuration','fastWailOnTime','fastWailOffTime','fastWailChopperDelay'];

fetch('/settings-data').then(r=>r.json()).then(d=>{
  TS.forEach(f=>{const e=document.getElementById('s_'+f);if(e)e.value=+(d[f]/1000).toFixed(2).replace(/\.?0+$/,'');});
  document.getElementById('verNum').textContent=d.fwVersion||'—';
});

fetch('/wifi-data').then(r=>r.json()).then(d=>{
  const el=document.getElementById('ws');
  const wifiIcon='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>';
  if(d.connected){el.innerHTML=wifiIcon+'Connected: '+d.ssid+' ('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
});

function msg(id,txt,ok){
  const e=document.getElementById(id);e.textContent=txt;e.className='msg '+(ok?'ok':'er');
  setTimeout(()=>{e.textContent='';e.className='msg';},3500);
}

function cmd(m){
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:m})});
}
function claxon(){
  try{
    const ctx=new(window.AudioContext||window.webkitAudioContext)();
    const beep=(f,t,d)=>{
      const o=ctx.createOscillator(),g=ctx.createGain();
      o.connect(g);g.connect(ctx.destination);
      o.type='sawtooth';o.frequency.value=f;
      g.gain.setValueAtTime(0.18,t);
      g.gain.exponentialRampToValueAtTime(0.001,t+d);
      o.start(t);o.stop(t+d);
    };
    const t=ctx.currentTime;
    beep(880,t,.13);beep(660,t+.13,.13);beep(880,t+.26,.13);
  }catch(e){}
}
function post(url,body){return fetch(url,{method:'POST',
  headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(r=>r.json());}

function saveWifi(){
  const ssid=document.getElementById('wSSID').value.trim();
  if(!ssid){msg('wm','Enter an SSID',false);return;}
  const pass=document.getElementById('wPass').value;
  post('/wifi-data',{ssid,pass}).then(d=>{
    msg('wm',d.ok?'Saved! Device is restarting…':'Failed',d.ok);
  });
}
function clearWifi(){
  if(!confirm('Switch to AP-only mode? Device will restart.'))return;
  post('/wifi-data',{clear:true}).then(d=>msg('wm',d.ok?'Restarting in AP mode…':'Failed',d.ok));
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
function saveTiming(){
  const b={};
  TS.forEach(f=>{const e=document.getElementById('s_'+f);if(e)b[f]=Math.round(parseFloat(e.value||0)*1000);});
  // report to whichever save-msg div is visible in the submitting card
  const id=document.activeElement.closest('.card')?.querySelector('.msg')?.id||'tm';
  post('/settings-data',b).then(d=>msg(id,d.ok?'Saved!':'Error',d.ok));
}
</script></body></html>
)rawliteral";

// ── Component test page ─────────────────────────────────────────────────────────
static const char TEST_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Component Test — Hurricane Controls</title>
<style>
:root{
  --bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;
  --wail:#1e3a8a;--attack:#7f1d1d;--fastwail:#78350f;--manual:#4c1d95;
  --stop-bg:#fca5a5;--stop-text:#000;--radius:4px;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
     min-height:100vh}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;gap:12px;
        padding:0 16px;z-index:20}
.navbar h1{font-size:1.05rem;color:var(--cyan)}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.content{max-width:480px;margin:0 auto;padding:76px 16px 100px}
.card{background:var(--surface);border-radius:var(--radius);padding:20px;margin-bottom:14px;
      box-shadow:0 1px 3px rgba(0,0,0,.35)}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:1px;color:#9aa3af;margin-bottom:14px}
.hint{font-size:.78rem;color:#9aa3af;margin-bottom:16px}
/* Relay dots */
.dots{display:flex;gap:24px;justify-content:center;margin-bottom:18px}
.dw{display:flex;flex-direction:column;align-items:center;gap:3px}
.dot{width:13px;height:13px;border-radius:50%;background:#2a2f3a;transition:background .3s}
.dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
.dl{font-size:.65rem;color:#9aa3af;letter-spacing:.5px}
.ttest{width:100%;padding:22px;margin-bottom:10px;border:none;border-radius:var(--radius);
       font-size:1.02rem;font-weight:700;letter-spacing:1px;cursor:pointer;user-select:none;
       background:#1b2438;color:#eaeaea;transition:background .15s,color .15s,transform .1s;
       box-shadow:0 1px 3px rgba(0,0,0,.35);font-family:inherit}
.ttest:active{transform:scale(.98)}
.ttest.on{background:var(--green);color:#04220f}
.ttest:disabled{opacity:.4;cursor:not-allowed}
.ttoggle{width:100%;padding:16px;margin-bottom:16px;border:1px solid var(--cyan);border-radius:var(--radius);
         background:transparent;color:var(--cyan);font-size:.95rem;font-weight:700;letter-spacing:1px;
         cursor:pointer;font-family:inherit;transition:background .15s,color .15s}
.ttoggle.on{background:var(--attack);color:#fff;border-color:var(--attack)}
.msg{font-size:.8rem;margin-top:6px;min-height:1.1em;text-align:center;display:flex;
     align-items:center;justify-content:center;gap:6px}
.ok{color:var(--green)}.er{color:#f87171}
.stopbar{position:fixed;bottom:0;left:0;right:0;background:var(--stop-bg);color:var(--stop-text);
         display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;letter-spacing:1px;
         padding:18px;cursor:pointer;border-top:2px solid rgba(0,0,0,.15);z-index:20;font-size:1.1rem}
.stopbar:active{opacity:.85}
</style></head><body>
<div class="navbar">
  <a href="/settings" class="ibtn" title="Back">
    <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <h1>Component Test</h1>
</div>

<div class="content">
<div class="card">
  <h2>Relay Test — Hold to Activate</h2>
  <button class="ttoggle" id="testModeBtn" onclick="toggleTestMode()">TEST MODE: OFF</button>
  <p class="hint">Bypasses all startup/shutdown delays. Only works while the siren is idle.</p>
  <div class="dots">
    <div class="dw"><div id="d0" class="dot"></div><span class="dl">CHOPPER</span></div>
    <div class="dw"><div id="d1" class="dot"></div><span class="dl">BLOWER</span></div>
    <div class="dw"><div id="d2" class="dot"></div><span class="dl">ROTATOR</span></div>
  </div>
  <button class="ttest" id="tChopper"
    onmousedown="testDown(event,'chopper')" onmouseup="testUp(event,'chopper')" onmouseleave="testUp(event,'chopper')"
    ontouchstart="testDown(event,'chopper')" ontouchend="testUp(event,'chopper')" ontouchcancel="testUp(event,'chopper')">CHOPPER</button>
  <button class="ttest" id="tBlower"
    onmousedown="testDown(event,'blower')" onmouseup="testUp(event,'blower')" onmouseleave="testUp(event,'blower')"
    ontouchstart="testDown(event,'blower')" ontouchend="testUp(event,'blower')" ontouchcancel="testUp(event,'blower')">BLOWER</button>
  <button class="ttest" id="tRotator"
    onmousedown="testDown(event,'rotator')" onmouseup="testUp(event,'rotator')" onmouseleave="testUp(event,'rotator')"
    ontouchstart="testDown(event,'rotator')" ontouchend="testUp(event,'rotator')" ontouchcancel="testUp(event,'rotator')">ROTATOR</button>
  <div class="msg" id="tmsg"></div>
</div>
</div>

<div class="stopbar" onclick="claxon();cmd('stop')">&#9632; STOP</div>

<script>
let held={chopper:false,blower:false,rotator:false};
const warnIcon='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>';

const ws=new WebSocket('ws://'+location.hostname+'/ws');
ws.onmessage=e=>drawDots(JSON.parse(e.data));
ws.onerror=ws.onclose=()=>setInterval(()=>fetch('/status').then(r=>r.json()).then(drawDots),1200);

function drawDots(d){
  const r=d.relays||[0,0,0];
  [0,1,2].forEach(i=>document.getElementById('d'+i).classList.toggle('on',!!r[i]));
  const btn=document.getElementById('testModeBtn');
  const on=!!d.testMode;
  btn.classList.toggle('on',on);
  if(on && d.lockAutoExpire){
    const rem=d.lockRemaining||0, m=Math.floor(rem/60), sec=rem%60;
    btn.textContent='TEST MODE: ON (auto-off in '+m+':'+String(sec).padStart(2,'0')+')';
  } else if(on){
    btn.textContent='TEST MODE: ON';
  } else {
    btn.textContent='TEST MODE: OFF';
  }
  ['chopper','blower','rotator'].forEach(c=>btnEl(c).disabled=!on);
}

function toggleTestMode(){
  const next=!document.getElementById('testModeBtn').classList.contains('on');
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:'test-lock',on:next})});
}

function msg(txt,ok){
  const e=document.getElementById('tmsg');
  e.innerHTML=ok?txt:(warnIcon+txt);
  e.className='msg '+(ok?'ok':'er');
  setTimeout(()=>{e.textContent='';e.className='msg';},2500);
}

function cmd(m){
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:m})});
}
function claxon(){
  try{
    const ctx=new(window.AudioContext||window.webkitAudioContext)();
    const beep=(f,t,d)=>{
      const o=ctx.createOscillator(),g=ctx.createGain();
      o.connect(g);g.connect(ctx.destination);
      o.type='sawtooth';o.frequency.value=f;
      g.gain.setValueAtTime(0.18,t);
      g.gain.exponentialRampToValueAtTime(0.001,t+d);
      o.start(t);o.stop(t+d);
    };
    const t=ctx.currentTime;
    beep(880,t,.13);beep(660,t+.13,.13);beep(880,t+.26,.13);
  }catch(e){}
}

function btnEl(comp){return document.getElementById('t'+comp[0].toUpperCase()+comp.slice(1));}

function testDown(e,comp){
  e.preventDefault();
  if(held[comp])return;
  held[comp]=true;
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:'test',component:comp,on:true})})
    .then(r=>r.json()).then(d=>{
      if(d.ok){btnEl(comp).classList.add('on');}
      else{held[comp]=false;msg('Enable TEST MODE first',false);}
    });
}
function testUp(e,comp){
  if(e)e.preventDefault();
  if(!held[comp])return;
  held[comp]=false;
  btnEl(comp).classList.remove('on');
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:'test',component:comp,on:false})});
}
</script></body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────────────────────

class WebUI {
public:
    void begin() {
        setupRoutes();
        ws_.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                           AwsEventType t, void* a, uint8_t* d, size_t l) {
            (void)s;(void)c;(void)t;(void)a;(void)d;(void)l;
        });
        server_.addHandler(&ws_);
        server_.begin();
    }

    void update() {
        uint32_t now = millis();
        if (now - lastPush_ >= 500) {
            lastPush_ = now;
            if (ws_.count() > 0) ws_.textAll(buildStatusJson());
        }
        ws_.cleanupClients();
    }

private:
    AsyncWebServer    server_{80};
    AsyncWebSocket    ws_{"/ws"};
    std::set<String>  sessions_;
    uint32_t          lastPush_ = 0;

    // ── Login rate-limiting ──────────────────────────────────────────────────
    // Single-user device: a global (not per-IP) failed-attempt counter is
    // sufficient to slow down brute-forcing without added per-client state.
    uint8_t  failedLoginAttempts_ = 0;
    uint32_t loginLockoutUntil_   = 0;
    static constexpr uint8_t  MAX_LOGIN_ATTEMPTS = 5;
    static constexpr uint32_t LOGIN_LOCKOUT_MS   = 30000;

    // ── Auth helpers ──────────────────────────────────────────────────────────

    String generateToken() {
        char buf[33];
        for (int i = 0; i < 4; i++) {
            snprintf(buf + i * 8, 9, "%08x", (unsigned)esp_random());
        }
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

    // ── Route setup ───────────────────────────────────────────────────────────

    void setupRoutes() {

        // Login page (no auth)
        server_.on("/login", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/html", LOGIN_HTML);
        });

        // Root → main page (auth required)
        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            req->send(200, "text/html", MAIN_HTML);
        });

        // Settings page (auth required)
        server_.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            req->send(200, "text/html", SETTINGS_HTML);
        });

        // Component test page (auth required)
        server_.on("/test", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            req->send(200, "text/html", TEST_HTML);
        });

        // Status JSON (auth required)
        server_.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", buildStatusJson());
        });

        // Settings JSON GET (auth required)
        server_.on("/settings-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", buildSettingsJson());
        });

        // WiFi status GET (auth required)
        server_.on("/wifi-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", buildWifiJson());
        });

        // Logout
        server_.on("/auth/logout", HTTP_GET, [this](AsyncWebServerRequest* req) {
            sessions_.erase(getSessionToken(req));
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json", "{\"ok\":true}");
            r->addHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
            req->send(r);
        });

        // ── POST /auth/login ──────────────────────────────────────────────────
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
                    AsyncWebServerResponse* resp =
                        req->beginResponse(200, "application/json", "{\"ok\":true}");
                    resp->addHeader("Set-Cookie",
                        "sid=" + token + "; Path=/; HttpOnly; SameSite=Strict");
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

        // ── POST /cmd ─────────────────────────────────────────────────────────
        server_.on("/cmd", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                bool ok = true;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* mode = doc["mode"] | "";
                    // While TEST MODE is active, only the Test page's own component
                    // control ("test") may touch outputs — everything that would
                    // start or stop a siren program is blocked, including stop:
                    // the owner has an independent analog E-stop cutting all 120V
                    // power, so no software escape hatch is needed here.
                    bool blockedByTestMode = buttons.testModeActive && (
                        strcmp(mode, "wail")     == 0 || strcmp(mode, "attack") == 0 ||
                        strcmp(mode, "fastwail") == 0 || strcmp(mode, "manual") == 0 ||
                        strcmp(mode, "stop")     == 0);
                    if      (blockedByTestMode)             ok = false;
                    else if (strcmp(mode, "wail")     == 0) sm.trigger(RunMode::WAIL);
                    else if (strcmp(mode, "attack")   == 0) sm.trigger(RunMode::ATTACK);
                    else if (strcmp(mode, "fastwail") == 0) sm.trigger(RunMode::FAST_WAIL);
                    else if (strcmp(mode, "manual")   == 0) sm.trigger(RunMode::MANUAL);
                    else if (strcmp(mode, "stop")     == 0) sm.stop();
                    else if (strcmp(mode, "btn-lock") == 0) buttons.setLocked(!buttons.locked, false);
                    else if (strcmp(mode, "test-lock") == 0) {
                        bool on = doc["on"] | false;
                        buttons.setTestMode(on);
                    }
                    else if (strcmp(mode, "test")     == 0) {
                        ok = buttons.testModeActive && sm.isIdle();
                        if (ok) {
                            const char* comp = doc["component"] | "";
                            bool on = doc["on"] | false;
                            Serial.printf("[%lu] /cmd test: component=%s on=%d\n", millis(), comp, on);
                            if      (strcmp(comp, "chopper") == 0) on ? chopperOn() : chopperOff();
                            else if (strcmp(comp, "blower")  == 0) on ? blowerOn()  : blowerOff();
                            else if (strcmp(comp, "rotator") == 0) on ? rotatorOn() : rotatorOff();
                        }
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /settings-data ───────────────────────────────────────────────
        server_.on("/settings-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    Settings& s = settingsMgr.s;
                    applyUInt(doc, "chopperDelay",   s.chopperDelay);
                    applyUInt(doc, "blowerDelay",    s.blowerDelay);
                    applyUInt(doc, "rotatorDelay",   s.rotatorDelay);
                    applyUInt(doc, "wailDuration",   s.wailDuration);
                    applyUInt(doc, "attackDuration", s.attackDuration);
                    applyUInt(doc, "attackOnTime",   s.attackOnTime);
                    applyUInt(doc, "attackOffTime",  s.attackOffTime);
                    applyUInt(doc, "fastWailDuration",     s.fastWailDuration);
                    applyUInt(doc, "fastWailOnTime",       s.fastWailOnTime);
                    applyUInt(doc, "fastWailOffTime",      s.fastWailOffTime);
                    applyUInt(doc, "fastWailChopperDelay", s.fastWailChopperDelay);
                    applyUInt(doc, "stopBlowerDelay",  s.stopBlowerDelay);
                    applyUInt(doc, "stopChopperDelay", s.stopChopperDelay);
                    applyUInt(doc, "stopRotDelay",     s.stopRotDelay);
                    applyUInt(doc, "attackChopperDelay",  s.attackChopperDelay);
                    applyUInt(doc, "longPressMs",         s.longPressMs);
                    applyUInt(doc, "buttonDebounceMs",    s.buttonDebounceMs);
                    settingsMgr.save();
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /wifi-data ───────────────────────────────────────────────────
        server_.on("/wifi-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    if (doc["clear"].as<bool>()) {
                        wifiMgr.clearCredentials();
                    } else {
                        const char* ssid = doc["ssid"] | "";
                        const char* pass = doc["pass"] | "";
                        if (strlen(ssid) > 0) wifiMgr.saveCredentials(ssid, pass);
                    }
                    wifiMgr.scheduleRestart(1500);
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /password ────────────────────────────────────────────────────
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
                        sessions_.clear(); // force re-login with new password
                        ok = true;
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /restart ─────────────────────────────────────────────────────
        server_.on("/restart", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { req->send(401, "application/json", "{\"error\":\"unauth\"}"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
            wifiMgr.scheduleRestart(500);
        });

        server_.onNotFound([](AsyncWebServerRequest* req) {
            // Redirect unknown paths to login or main
            AsyncWebServerResponse* r = req->beginResponse(302);
            r->addHeader("Location", "/");
            req->send(r);
        });
    }

    // ── Shared body accumulator ───────────────────────────────────────────────
    // Collects chunked POST body into req->_tempObject (null-terminated heap buffer)
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

    // ── JSON builders ─────────────────────────────────────────────────────────

    static void applyUInt(JsonDocument& doc, const char* key, uint32_t& field) {
        if (!doc[key].isNull()) field = doc[key].as<uint32_t>();
    }

    static String buildStatusJson() {
        uint8_t rs = relayState();
        TimerInfo ti = sm.getTimerInfo();
        JsonDocument doc;
        doc["mode"]             = sm.stateName();
        doc["runMode"]          = sm.modeName();
        doc["uptime"]           = (uint32_t)(millis() / 1000);
        JsonArray rel           = doc["relays"].to<JsonArray>();
        rel.add((rs >> 0) & 1);
        rel.add((rs >> 1) & 1);
        rel.add((rs >> 2) & 1);
        doc["elapsed"]      = ti.totalElapsedMs   / 1000;
        doc["remaining"]    = ti.totalRemainingMs / 1000;
        doc["hasRemaining"] = ti.hasRemaining;
        doc["btnLocked"]      = buttons.locked;
        doc["testMode"]       = buttons.testModeActive;
        doc["lockAutoExpire"] = buttons.lockAutoExpiring();
        doc["lockRemaining"]  = buttons.lockRemainingSec();
        float tempF = temperatureRead() * 9.0f / 5.0f + 32.0f;
        doc["tempF"] = (int)roundf(tempF);
        String out;
        serializeJson(doc, out);
        return out;
    }

    static String buildSettingsJson() {
        const Settings& s = settingsMgr.s;
        JsonDocument doc;
        doc["chopperDelay"]   = s.chopperDelay;
        doc["blowerDelay"]    = s.blowerDelay;
        doc["rotatorDelay"]   = s.rotatorDelay;
        doc["wailDuration"]   = s.wailDuration;
        doc["attackDuration"] = s.attackDuration;
        doc["attackOnTime"]   = s.attackOnTime;
        doc["attackOffTime"]  = s.attackOffTime;
        doc["fastWailDuration"]     = s.fastWailDuration;
        doc["fastWailOnTime"]       = s.fastWailOnTime;
        doc["fastWailOffTime"]      = s.fastWailOffTime;
        doc["fastWailChopperDelay"] = s.fastWailChopperDelay;
        doc["stopBlowerDelay"]  = s.stopBlowerDelay;
        doc["stopChopperDelay"] = s.stopChopperDelay;
        doc["stopRotDelay"]     = s.stopRotDelay;
        doc["attackChopperDelay"]  = s.attackChopperDelay;
        doc["longPressMs"]         = s.longPressMs;
        doc["buttonDebounceMs"]    = s.buttonDebounceMs;
        doc["fwVersion"]           = FW_VERSION;
        String out;
        serializeJson(doc, out);
        return out;
    }

    static String buildWifiJson() {
        JsonDocument doc;
        doc["mode"]      = (wifiMgr.getMode() == WiFiManager::Mode::STA) ? "sta" : "ap";
        doc["connected"] = wifiMgr.isConnected();
        doc["ssid"]      = wifiMgr.getSSID();
        doc["ip"]        = wifiMgr.getIP();
        String out;
        serializeJson(doc, out);
        return out;
    }
};

extern WebUI webUI;
