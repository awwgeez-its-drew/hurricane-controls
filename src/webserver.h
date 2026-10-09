#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <cctype>
#include "config.h"
#include "sync.h"
#include "clock.h"
#include "settings.h"
#include "runlog.h"
#include "statemachine.h"
#include "motors.h"
#include "wifi_manager.h"
#include "buttons.h"
#include "weather_link.h"
#include "assets.h"

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

// Web App Manifest — lets Android/Chrome find a large enough icon for a
// home-screen bookmark instead of falling back to a generic letter
// monogram (the small favicon.ico alone isn't trusted for that).
static const char MANIFEST_JSON[] PROGMEM = R"json({
  "name": "Hurricane Controls",
  "short_name": "Hurricane",
  "start_url": "/",
  "scope": "/",
  "display": "standalone",
  "background_color": "#848482",
  "theme_color": "#0e1320",
  "icons": [
    {"src": "/icon-192.png", "sizes": "192x192", "type": "image/png"},
    {"src": "/icon-512.png", "sizes": "512x512", "type": "image/png"}
  ]
})json";

// ── Login page ────────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hurricane Controls</title>
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
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
.login-siren{width:120px;height:120px;display:block;margin:0 auto 12px}
.sub{color:#9aa3af;font-size:.85rem;margin-bottom:28px}
input{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:var(--radius);padding:13px 16px;font-size:1rem;margin-bottom:12px;font-family:inherit}
input:focus{outline:none;border-color:var(--cyan)}
button{width:100%;padding:14px;background:var(--green);color:#04220f;border:none;
       border-radius:var(--radius);font-size:1rem;font-weight:700;cursor:pointer;letter-spacing:1px;
       font-family:inherit}
button:active{opacity:.8}
.err{color:#f87171;font-size:.85rem;margin-top:10px;min-height:1.1em}
.pill{display:none;width:max-content;margin:0 auto 22px;padding:5px 18px;border-radius:var(--radius);
      font-size:.78rem;font-weight:700;letter-spacing:2px}
.pill.idle,.pill.seq,.pill.stop,.pill.wail,.pill.attack,.pill.fastwail,.pill.manual,.pill.growl,.pill.test{display:block}
.pill.idle{color:#9aa3af;border:1px solid var(--cyan)}
.pill.wail{background:var(--wail);color:#bcd2ff}
.pill.attack,.pill.test{background:var(--attack);color:#ffc9c9}
.pill.fastwail{background:var(--fastwail);color:#ffd9a0}
.pill.manual{background:var(--manual);color:#e3caff}
.pill.growl{background:#0f4c3d;color:#a7f3d0}
.pill.seq,.pill.stop{background:var(--green);color:#04220f}
</style></head><body>
<div class="card">
  <img class="login-siren" src="/login-siren.png" alt="">
  <h1>Hurricane Controls</h1>
  <p class="sub">Siren Controller</p>
  <div id="pill" class="pill"></div>
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
// Public one-word status (no login needed) — hidden if the device is unreachable.
const PL={idle:'IDLE',seq:'STARTING',wail:'WAIL',attack:'ATTACK',fastwail:'FAST WAIL',
          growl:'GROWL TEST',manual:'MANUAL',stop:'STOPPING',test:'TEST MODE'};
function pill(){
  const p=document.getElementById('pill');
  fetch('/pub-status').then(r=>r.json()).then(d=>{
    p.textContent=PL[d.s]||String(d.s).toUpperCase();p.className='pill '+d.s;
  }).catch(()=>{p.className='pill';});
}
pill();setInterval(pill,2000);
</script></body></html>
)rawliteral";

// ── Main control page ─────────────────────────────────────────────────────────
static const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hurricane Controls</title>
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
<style>
:root{
  --bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80;
  --wail:#1e3a8a;--attack:#7f1d1d;--fastwail:#78350f;--manual:#4c1d95;--growl:#0f4c3d;
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
.brandrow{display:flex;align-items:center;gap:8px;overflow:hidden;margin-right:8px}
.brand-icon{width:28px;height:28px;flex-shrink:0}
.navbar .brand{display:flex;flex-direction:column;justify-content:center;overflow:hidden}
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
.growl   {background:var(--growl);color:#a7f3d0}
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
  <div class="brandrow">
    <img class="brand-icon" src="/brand-icon.png" alt="">
    <div class="brand">
      <h1>Hurricane Controls</h1>
      <span class="subtitle">CONTROL PANEL</span>
    </div>
  </div>
  <div class="navicons">
    <button class="ibtn" onclick="goHome()" title="Log out">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
    </button>
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
  <div class="mono" style="font-size:.72rem;color:#8892a0;margin-top:10px">Controller <span id="tempF">—</span>&deg;F &nbsp;&bull;&nbsp; Uptime <span id="uptime">—</span> &nbsp;&bull;&nbsp; <span id="clockTime">--:--:--</span></div>
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
let manOn=false, sirenActive=false, webManualHeld=false, manKa=null;

function connect(){
  const ws=new WebSocket('ws://'+location.hostname+'/ws');
  ws.onmessage=e=>draw(JSON.parse(e.data));
  // One poll + one reconnect attempt per drop (also fires when a
  // restart drops the socket); 401 means the session is gone.
  ws.onclose=()=>{
    fetch('/status').then(r=>{if(r.status===401){location.href='/login';return null;}return r.json();})
      .then(d=>{if(d)draw(d);}).catch(()=>{});
    setTimeout(connect,2000);
  };
}
connect();

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
  if(d.currentTime)document.getElementById('clockTime').textContent=d.currentTime;
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
  // Keep-alive while held — the controller stops a web Manual run ~2 s
  // after these stop arriving (phone off Wi-Fi, screen locked, tab closed).
  manKa=setInterval(()=>cmd('manual-ka'),500);
}
function manualUp(e){
  if(e)e.preventDefault();
  if(!webManualHeld)return;
  webManualHeld=false;clearInterval(manKa);manKa=null;cmd('stop');
}
document.addEventListener('visibilitychange',()=>{if(document.hidden)manualUp();});
function toggleLock(){cmd('btn-lock');}
function goHome(){fetch('/auth/logout',{method:'POST'}).then(()=>location.href='/login');}
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
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
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
.brandrow{display:flex;align-items:center;gap:8px;overflow:hidden}
.brand-icon{width:26px;height:26px;flex-shrink:0}
.navicons{display:flex;gap:14px;align-items:center}
.ibtn{background:none;border:none;cursor:pointer;color:var(--cyan);padding:2px;line-height:0;display:inline-flex}
.ibtn.locked{color:#f87171}
.content{max-width:480px;margin:0 auto;padding:76px 16px 100px}
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
label{font-size:.78rem;color:#9aa3af;display:block;margin-bottom:3px}
label.strong{font-weight:700;color:#eaeaea}
input{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:var(--radius);padding:9px 11px;font-size:.9rem;font-family:inherit}
input:focus{outline:none;border-color:var(--cyan)}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--cyan);cursor:pointer;flex-shrink:0}
.ws{font-size:.8rem;padding:9px 12px;background:#0a0e18;border-radius:var(--radius);
    margin-bottom:12px;color:#9aa3af;display:flex;align-items:center;gap:8px;border-left:3px solid transparent}
.ws.ok{color:var(--green);border-left-color:var(--green)}
.ws.er{color:#f87171;border-left-color:#f87171}
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
.ver a{color:inherit;text-decoration:none}
.lrow{display:flex;justify-content:space-between;gap:10px;padding:7px 0;border-top:1px solid #1a2a3a;
      font-size:.75rem;color:#c9cdd3}
.lrow span:last-child{color:#9aa3af;text-align:right}
.ver a:hover{text-decoration:underline}
.stopbar{position:fixed;bottom:0;left:0;right:0;background:var(--stop-bg);color:var(--stop-text);
         display:flex;align-items:center;justify-content:center;gap:8px;font-weight:700;letter-spacing:1px;
         padding:18px;cursor:pointer;border-top:2px solid rgba(0,0,0,.15);z-index:20;font-size:1.1rem}
.stopbar:active{opacity:.85}
</style></head><body>
<div class="navbar">
  <a href="/" class="ibtn" title="Back">
    <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <div class="brandrow"><img class="brand-icon" src="/brand-icon.png" alt=""><h1>Settings</h1></div>
  <div class="navicons">
    <button class="ibtn" onclick="logout()" title="Log out">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
    </button>
    <button class="ibtn" id="btnLock" onclick="toggleLock()" title="Lock physical buttons">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="10" rx="2"/><path id="lockShackle" d="M8 11V7a4 4 0 0 1 7.5-2"/></svg>
    </button>
    <button class="ibtn" onclick="doRestart()" title="Restart">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
    </button>
    <a href="/test" class="ibtn" title="Component Test">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>
    </a>
  </div>
</div>

<div class="content">

<!-- WiFi -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Wi-Fi</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div id="ws" class="ws">Checking&hellip;</div>
  <div class="row"><label>Device Hostname</label>
    <input type="text" id="wHost" placeholder="hurricane" maxlength="32" autocomplete="off"></div>
  <p class="hint">Letters, numbers, and hyphens only. Sets both the name your router shows for this device and its <code>http://&lt;hostname&gt;.local</code> address. Takes effect after a restart.</p>
  <button class="btn save" onclick="saveHostname()">Save Hostname</button>
  <div class="msg" id="hm"></div>
  <div class="row" style="margin-top:14px"><label>Access Point Password</label>
    <input type="password" id="apPw" placeholder="New AP password (8-63 characters)" autocomplete="new-password"></div>
  <p class="hint" id="apHint">Needed to join the &quot;HurricaneControls&quot; network this device broadcasts when it isn't on your home Wi-Fi. Takes effect after a restart.</p>
  <button class="btn save" onclick="saveApPw()">Save AP Password</button>
  <div class="msg" id="apm"></div>
  <div class="row" style="margin-top:14px"><label>Network SSID</label>
    <input type="text" id="wSSID" placeholder="Your WiFi name" autocomplete="off"></div>
  <div class="row"><label>Password</label>
    <input type="password" id="wPass" placeholder="WiFi password" autocomplete="new-password"></div>
  <button class="btn save" onclick="saveWifi()">Connect to Network</button>
  <button class="btn danger" onclick="clearWifi()">Use AP Mode Only</button>
  <div class="msg" id="wm"></div>
  </div>
</div>

<!-- Security -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Security</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="row"><label>New Password</label>
    <input type="password" id="p1" placeholder="New password" autocomplete="new-password"></div>
  <div class="row"><label>Confirm Password</label>
    <input type="password" id="p2" placeholder="Confirm" autocomplete="new-password"></div>
  <button class="btn save" onclick="savePw()">Change Password</button>
  <button class="btn logout" onclick="logout()">Log Out</button>
  <div class="msg" id="pm"></div>
  </div>
</div>

<!-- Mesh Settings -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Mesh Settings</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="row"><label>Allowed sender IDs (comma-separated, leave empty to block all)</label>
    <input type="text" id="meshWL" placeholder="e.g. 3A3C, 1B93"></div>
  <div class="row"><label>Command Password (optional, plaintext)</label>
    <input type="text" id="meshPW" placeholder="leave blank for no password"></div>
  <button class="btn save" onclick="saveMeshWL()">Save Mesh Settings</button>
  <div class="msg" id="mwlm"></div>
  </div>
</div>

<!-- Weather Watcher -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Weather Watcher</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Allows a second, separately-wired ESP32 polling National Weather Service alerts to automatically trigger the siren. See docs/weather-watcher.md. This does not bypass TEST MODE.</p>
  <div id="wwStatus" class="ws">Checking&hellip;</div>
  <div class="trow">
    <label for="wxAutoTrig">Automatic weather-triggered activation</label>
    <input type="checkbox" id="wxAutoTrig" onchange="saveWeatherAutoTrigger()">
  </div>
  <div class="msg" id="wxm"></div>
  </div>
</div>

<!-- Time / NTP fallback -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Time (NTP Fallback)</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Used only as a fallback — this board normally gets its clock from the Weather Watcher over the UART link. These settings take effect if the Weather Watcher is offline or not yet paired.</p>
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

<!-- Startup Sequence -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Startup Sequence</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label>Chopper delay (sec)</label><input type="number" id="s_chopperDelay" min="0" step="0.1"></div>
    <div><label>Blower delay (sec)</label><input type="number" id="s_blowerDelay" min="0" step="0.1"></div>
    <div><label>Rotator delay (sec)</label><input type="number" id="s_rotatorDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Startup Sequence</button>
  <div class="msg" id="tmStart"></div>
  </div>
</div>

<!-- Shutdown Sequence -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Shutdown Sequence</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label>Chopper delay (sec)</label><input type="number" id="s_stopChopperDelay" min="0" step="0.1"></div>
    <div><label>Blower delay (sec)</label><input type="number" id="s_stopBlowerDelay" min="0" step="0.1"></div>
    <div><label>Rotator delay (sec)</label><input type="number" id="s_stopRotDelay" min="0" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Shutdown Sequence</button>
  <div class="msg" id="tmStop"></div>
  </div>
</div>

<!-- General Timing -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Timing</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label class="strong">Wail duration (sec)</label><input type="number" id="s_wailDuration" min="1" step="1"></div>
    <div><label>Long-press threshold (sec)</label><input type="number" id="s_longPressMs" min="0.1" step="0.1"></div>
    <div><label>Button debounce (sec)</label><input type="number" id="s_buttonDebounceMs" min="0" step="0.01"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Timing</button>
  <div class="msg" id="tm"></div>
  </div>
</div>

<!-- Attack Mode -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Attack Mode</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label>Attack duration (sec)</label><input type="number" id="s_attackDuration" min="1" step="1"></div>
    <div><label>Attack ON time (sec)</label><input type="number" id="s_attackOnTime" min="0.5" step="0.1"></div>
    <div><label>Attack OFF time (sec)</label><input type="number" id="s_attackOffTime" min="0.5" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Attack Settings</button>
  <div class="msg" id="tm2"></div>
  </div>
</div>

<!-- Fast Wail Mode -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Fast Wail Mode</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label>Fast Wail duration (sec)</label><input type="number" id="s_fastWailDuration" min="1" step="1"></div>
    <div><label>Fast Wail ON time (sec)</label><input type="number" id="s_fastWailOnTime" min="0.5" step="0.1"></div>
    <div><label>Fast Wail OFF time (sec)</label><input type="number" id="s_fastWailOffTime" min="0.5" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Fast Wail Settings</button>
  <div class="msg" id="tm3"></div>
  </div>
</div>

<!-- Growl Test -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Growl Test</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div class="grid2">
    <div><label>Blower time (sec)</label><input type="number" id="s_growlBlowerTime" min="0.5" step="0.1"></div>
    <div><label>Rotator time (sec)</label><input type="number" id="s_growlRotatorTime" min="0.5" step="0.1"></div>
    <div><label>Chopper time (sec)</label><input type="number" id="s_growlChopperTime" min="0.5" step="0.1"></div>
  </div>
  <button class="btn save" onclick="saveTiming()">Save Growl Settings</button>
  <div class="msg" id="tm4"></div>
  </div>
</div>

<!-- Usage -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Usage</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <div id="usageTot" class="ws">Loading&hellip;</div>
  <div id="usageList"></div>
  <p class="hint" style="margin-top:10px">Recent runs are kept until the next restart; the totals are permanent.</p>
  </div>
</div>

<!-- Firmware Update -->
<div class="card">
  <div class="card-head" onclick="toggleCard(this)"><h2>Firmware Update</h2><svg class="chevron" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"/></svg></div>
  <div class="card-body">
  <p class="hint">Upload a new <code>firmware.bin</code> (from <code>.pio/build/esp32dev/</code> after a build). Only allowed while the siren is idle and TEST MODE is off; the controller restarts when it finishes.</p>
  <input type="file" id="fwFile" accept=".bin">
  <button class="btn save" onclick="uploadFw()">Upload Firmware</button>
  <div class="msg" id="fwm"></div>
  </div>
</div>

<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Hurricane Controls &middot; v<span id="verNum">—</span></a></div>
<div class="ver mono">Last restart: <span id="rstReason">—</span></div>
<div class="ver mono"><a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a></div>

</div>

<div class="stopbar" onclick="claxon();cmd('stop')">&#9632; STOP</div>

<script>
// All timing fields stored as ms, displayed as seconds
const TS=['wailDuration','attackDuration','attackOnTime','attackOffTime',
          'chopperDelay','blowerDelay','rotatorDelay',
          'stopBlowerDelay','stopChopperDelay','stopRotDelay','longPressMs','buttonDebounceMs',
          'fastWailDuration','fastWailOnTime','fastWailOffTime',
          'growlBlowerTime','growlRotatorTime','growlChopperTime'];

function fillTiming(d){
  TS.forEach(f=>{const e=document.getElementById('s_'+f);if(e)e.value=+(d[f]/1000).toFixed(2).replace(/\.?0+$/,'');});
}
fetch('/settings-data').then(r=>r.json()).then(d=>{
  fillTiming(d);
  document.getElementById('verNum').textContent=d.fwVersion||'—';
  document.getElementById('rstReason').textContent=d.resetReason||'—';
  document.getElementById('meshWL').value=d.meshWhitelist||'';
  document.getElementById('meshPW').value=d.meshPassword||'';
  document.getElementById('ntpServer').value=d.ntpServer||'pool.ntp.org';
  document.getElementById('ntpUpdateHours').value=d.ntpUpdateHours||12;
  document.getElementById('timeZone').value=d.timeZone||'EASTERN';
  document.getElementById('autoDst').checked=d.autoDst!==false;
  document.getElementById('wxAutoTrig').checked=!!d.weatherAutoTriggerEnabled;
  const wwEl=document.getElementById('wwStatus');
  if(!d.weatherAutoTriggerEnabled){wwEl.textContent='Disabled';wwEl.className='ws';}
  else if(!d.wwEverReported){wwEl.textContent='No status received yet from the Weather Watcher board';wwEl.className='ws';}
  else if(d.wwPending){wwEl.textContent='Starting up — waiting for its first NWS poll';wwEl.className='ws';}
  else if(d.wwOk){wwEl.textContent='OK — '+d.wwDetail;wwEl.className='ws ok';}
  else{wwEl.textContent='Error — '+d.wwDetail;wwEl.className='ws er';}
});

function refreshLockIcon(){
  fetch('/status').then(r=>r.json()).then(d=>{
    const bl=document.getElementById('btnLock');
    const lk=!!d.btnLocked;
    document.getElementById('lockShackle').setAttribute('d', lk ? 'M8 11V7a4 4 0 0 1 8 0v4' : 'M8 11V7a4 4 0 0 1 7.5-2');
    bl.classList.toggle('locked',lk);
    bl.title=lk?'Physical buttons LOCKED — click to unlock':'Lock physical buttons';
  });
}
refreshLockIcon();
function toggleLock(){
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:'btn-lock'})})
    .then(refreshLockIcon);
}
function doRestart(){
  if(!confirm('Restart the controller?'))return;
  fetch('/restart',{method:'POST'});
}

fetch('/wifi-data').then(r=>r.json()).then(d=>{
  const el=document.getElementById('ws');
  const wifiIcon='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>';
  if(d.connected){el.innerHTML=wifiIcon+'Connected: '+d.ssid+' ('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
  document.getElementById('wHost').value=d.hostname||'hurricane';
  if(!d.apSecured)document.getElementById('apHint').textContent+=' The access point is currently OPEN (no password).';
});
loadLog();

function toggleCard(headEl){
  headEl.parentElement.classList.toggle('expanded');
}

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
function saveHostname(){
  const hostname=document.getElementById('wHost').value.trim();
  const valid=/^[A-Za-z0-9-]{1,32}$/.test(hostname) && !hostname.startsWith('-') && !hostname.endsWith('-');
  if(!valid){msg('hm','Letters, numbers, and hyphens only (no leading/trailing hyphen)',false);return;}
  post('/wifi-data',{hostname}).then(d=>msg('hm',d.ok?'Saved — restarting…':'Failed',d.ok));
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
function saveTime(){
  const b={
    ntpServer: document.getElementById('ntpServer').value.trim(),
    ntpUpdateHours: parseInt(document.getElementById('ntpUpdateHours').value||12,10),
    timeZone: document.getElementById('timeZone').value,
    autoDst: document.getElementById('autoDst').checked,
  };
  post('/settings-data',b).then(d=>msg('tmz',d.ok?'Saved!':'Error',d.ok));
}
function saveMeshWL(){
  const wl=document.getElementById('meshWL').value.trim();
  const pw=document.getElementById('meshPW').value.trim();
  post('/settings-data',{meshWhitelist:wl,meshPassword:pw}).then(d=>msg('mwlm',d.ok?'Saved!':'Error',d.ok));
}
function saveWeatherAutoTrigger(){
  const on=document.getElementById('wxAutoTrig').checked;
  post('/settings-data',{weatherAutoTriggerEnabled:on}).then(d=>msg('wxm',d.ok?'Saved!':'Error',d.ok));
}
function saveTiming(){
  const b={};
  // Blank fields are skipped (left unchanged) rather than sent as 0.
  TS.forEach(f=>{const e=document.getElementById('s_'+f);
    if(e&&e.value.trim()!==''&&!isNaN(parseFloat(e.value)))b[f]=Math.round(parseFloat(e.value)*1000);});
  // report to whichever save-msg div is visible in the submitting card
  const id=document.activeElement.closest('.card')?.querySelector('.msg')?.id||'tm';
  post('/settings-data',b).then(d=>{
    msg(id,d.ok?'Saved!':'Error',d.ok);
    // Re-read so any value the controller clamped to its allowed range shows.
    fetch('/settings-data').then(r=>r.json()).then(fillTiming);
  });
}
function saveApPw(){
  const pw=document.getElementById('apPw').value;
  if(pw.length<8||pw.length>63){msg('apm','Must be 8-63 characters',false);return;}
  if(!confirm('Change the access point password? The device will restart, and anything joined to its network will need the new password.'))return;
  post('/ap-password',{password:pw}).then(d=>msg('apm',d.ok?'Saved — restarting…':'Failed',d.ok));
}
const LM=['—','Wail','Attack','Fast Wail','Manual','Growl Test'],LS=['Local','Web','Mesh','NWS Alert','Auto'];
function fmtDur(s){return s>=60?Math.floor(s/60)+'m '+(s%60)+'s':s+'s';}
function fmtAgo(s){if(s<60)return'just now';if(s<3600)return Math.floor(s/60)+' min ago';
  if(s<86400)return Math.floor(s/3600)+' h ago';return Math.floor(s/86400)+' d ago';}
function loadLog(){
  fetch('/log').then(r=>r.json()).then(d=>{
    document.getElementById('usageTot').textContent=
      d.runs+' total run'+(d.runs===1?'':'s')+' · '+(d.secs/3600).toFixed(1)+' motor hours';
    const L=document.getElementById('usageList');L.innerHTML='';
    if(!d.entries.length){L.className='hint';L.textContent='No runs since the last restart.';return;}
    L.className='';
    d.entries.forEach(e=>{
      const when=e.e?new Date(e.e*1000).toLocaleString():fmtAgo(d.uptime-e.u);
      const end=e.x===0?'completed':e.x===2?'failsafe stop':'stopped ('+LS[e.ss]+')';
      const r=document.createElement('div');r.className='lrow';
      const a=document.createElement('span'),b=document.createElement('span');
      a.textContent=(LM[e.m]||'?')+' · '+(LS[e.s]||'?')+' · '+fmtDur(e.d);
      b.textContent=when+' · '+end;
      r.appendChild(a);r.appendChild(b);L.appendChild(r);
    });
  });
}
function uploadFw(){
  const f=document.getElementById('fwFile').files[0];
  const el=document.getElementById('fwm');
  if(!f){msg('fwm','Choose a .bin file first',false);return;}
  if(!confirm('Upload '+f.name+' and restart the controller?'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/update');
  x.upload.onprogress=e=>{if(e.lengthComputable){el.className='msg';el.textContent='Uploading… '+Math.round(e.loaded*100/e.total)+'%';}};
  x.onload=()=>{let d={};try{d=JSON.parse(x.responseText);}catch(err){}
    if(d.ok){el.className='msg ok';el.textContent='Update installed — restarting…';setTimeout(()=>location.reload(),15000);}
    else{el.className='msg er';el.textContent='Update failed: '+(d.error||('HTTP '+x.status));}};
  x.onerror=()=>{el.className='msg er';el.textContent='Upload failed — connection lost';};
  x.send(fd);
}
</script></body></html>
)rawliteral";

// ── Component test page ─────────────────────────────────────────────────────────
static const char TEST_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Component Test — Hurricane Controls</title>
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
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
.brandrow{display:flex;align-items:center;gap:8px;overflow:hidden}
.brand-icon{width:26px;height:26px;flex-shrink:0}
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
  <div class="brandrow"><img class="brand-icon" src="/brand-icon.png" alt=""><h1>Component Test</h1></div>
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

<div class="card">
  <h2>Growl Test</h2>
  <p class="hint">Activates blower, then rotator, then chopper — one at a time, for their configured durations (Settings → Growl Test). Requires the siren to be idle and TEST MODE off.</p>
  <button class="ttest" onclick="claxon();cmd('growl')">START GROWL TEST</button>
</div>
</div>

<div class="stopbar" onclick="claxon();cmd('stop')">&#9632; STOP</div>

<script>
let held={chopper:false,blower:false,rotator:false}, kaT={};
const warnIcon='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>';

function connect(){
  const ws=new WebSocket('ws://'+location.hostname+'/ws');
  ws.onmessage=e=>drawDots(JSON.parse(e.data));
  // One poll + one reconnect attempt per drop (also fires when a
  // restart drops the socket); 401 means the session is gone.
  ws.onclose=()=>{
    fetch('/status').then(r=>{if(r.status===401){location.href='/login';return null;}return r.json();})
      .then(d=>{if(d)drawDots(d);}).catch(()=>{});
    setTimeout(connect,2000);
  };
}
connect();

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
      if(d.ok){
        if(!held[comp])return; // already released before the reply arrived (testUp sent the off)
        btnEl(comp).classList.add('on');
        // Keep-alive while held — the controller switches this component
        // off ~2 s after these stop arriving.
        kaT[comp]=setInterval(()=>fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
          body:JSON.stringify({mode:'test-ka',component:comp})}),500);
      }
      else{held[comp]=false;msg('Enable TEST MODE first',false);}
    });
}
function testUp(e,comp){
  if(e)e.preventDefault();
  if(!held[comp])return;
  held[comp]=false;
  clearInterval(kaT[comp]);
  btnEl(comp).classList.remove('on');
  fetch('/cmd',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({mode:'test',component:comp,on:false})});
}
</script></body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────────────────────

extern const char* resetReasonName();  // src/main.cpp

class WebUI {
public:
    void begin() {
        setupRoutes();
        ws_.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                           AwsEventType t, void* a, uint8_t* d, size_t l) {
            (void)s;(void)c;(void)t;(void)a;(void)d;(void)l;
        });
        // The live status stream needs the same session cookie as /status.
        ws_.setFilter([this](AsyncWebServerRequest* r) { return isAuthed(r); });
        server_.addHandler(&ws_);
        server_.begin();
    }

    void update() {
        uint32_t now = millis();
        if (now - lastPush_ >= 500) {
            lastPush_ = now;
            if (ws_.count() > 0) {
                char buf[STATUS_JSON_MAX];
                size_t n = buildStatusJson(buf, sizeof(buf));
                ws_.textAll(buf, n);
            }
        }
        ws_.cleanupClients();
        checkTestKeepalives(now);
        checkOtaStall(now);
    }

private:
    static constexpr size_t   STATUS_JSON_MAX = 416;
    static constexpr size_t   MAX_BODY        = 1024;   // largest JSON POST body accepted
    static constexpr uint8_t  MAX_SESSIONS    = 4;      // oldest login is evicted beyond this
    static constexpr uint32_t TEST_KEEPALIVE_TIMEOUT_MS = 2000;
    static constexpr uint32_t OTA_STALL_TIMEOUT_MS      = 60000;

    AsyncWebServer    server_{80};
    AsyncWebSocket    ws_{"/ws"};
    uint32_t          lastPush_ = 0;

    // Fixed-size session table — every login used to add a String to an
    // ever-growing std::set that was only cleared by a password change.
    char     sessions_[MAX_SESSIONS][33] = {};
    uint8_t  nextSession_ = 0;

    // ── Login rate-limiting ──────────────────────────────────────────────────
    // Single-user device: a global (not per-IP) failed-attempt counter is
    // sufficient to slow down brute-forcing without added per-client state.
    uint8_t  failedLoginAttempts_ = 0;
    uint32_t loginLockoutUntil_   = 0;   // 0 = not locked out
    static constexpr uint8_t  MAX_LOGIN_ATTEMPTS = 5;
    static constexpr uint32_t LOGIN_LOCKOUT_MS   = 30000;

    // ── Test page hold-to-run keep-alives ────────────────────────────────────
    // Same failsafe idea as web Manual: while a CHOPPER/BLOWER/ROTATOR button
    // is held the Test page refreshes its keep-alive every ~500 ms; if they
    // stop (phone dropped off Wi-Fi mid-hold), that component switches off.
    bool     testHeld_[3] = {false, false, false};
    uint32_t testKaTs_[3] = {0, 0, 0};

    // ── OTA ──────────────────────────────────────────────────────────────────
    // Per-upload result, kept in req->_tempObject (malloc'd; the request
    // frees it). Only one upload can own the flash writer at a time.
    struct OtaJob {
        bool owner;
        bool ok;
        char err[64];
    };
    volatile bool     otaRunning_      = false;
    volatile uint32_t otaLastDataMs_   = 0;
    volatile uint32_t otaStartMs_      = 0;
    volatile uint32_t otaBytesWritten_ = 0;

    // ── Auth helpers ──────────────────────────────────────────────────────────

    static void generateToken(char* out) {
        for (int i = 0; i < 4; i++) {
            snprintf(out + i * 8, 9, "%08x", (unsigned)esp_random());
        }
        out[32] = '\0';
    }

    // Copies the "sid" cookie value (if any) into out[33]. Only matches a
    // cookie actually named "sid" — not e.g. "xsid".
    static void sessionToken(AsyncWebServerRequest* req, char* out) {
        out[0] = '\0';
        const AsyncWebHeader* h = req->getHeader("Cookie");
        if (!h) return;
        const String& v = h->value();
        const char* c = v.c_str();
        const char* p = c;
        while ((p = strstr(p, "sid=")) != nullptr) {
            if (p == c || p[-1] == ' ' || p[-1] == ';') {
                p += 4;
                size_t n = strcspn(p, ";");
                if (n > 32) n = 32;
                memcpy(out, p, n);
                out[n] = '\0';
                return;
            }
            p += 4;
        }
    }

    bool isAuthed(AsyncWebServerRequest* req) {
        char tok[33];
        sessionToken(req, tok);
        if (strlen(tok) != 32) return false;
        for (auto& s : sessions_) if (!strcmp(s, tok)) return true;
        return false;
    }

    void addSession(const char* tok) {
        strlcpy(sessions_[nextSession_], tok, sizeof(sessions_[0]));
        nextSession_ = (nextSession_ + 1) % MAX_SESSIONS;
    }

    void removeSession(const char* tok) {
        if (!*tok) return;
        for (auto& s : sessions_) if (!strcmp(s, tok)) s[0] = '\0';
    }

    void clearSessions() {
        for (auto& s : sessions_) s[0] = '\0';
    }

    void redirectLogin(AsyncWebServerRequest* req) {
        AsyncWebServerResponse* r = req->beginResponse(302);
        r->addHeader("Location", "/login");
        req->send(r);
    }

    static void unauth(AsyncWebServerRequest* req) {
        req->send(401, "application/json", "{\"error\":\"unauth\"}");
    }

    // Streams a page straight out of flash — the const char* overload of
    // send() copies the whole page into a heap String on every request.
    static void sendPage(AsyncWebServerRequest* req, const char* page, const char* type = "text/html") {
        req->send(200, type, (const uint8_t*)page, strlen(page));
    }

    // ── Test component helpers ────────────────────────────────────────────────

    static int componentIndex(const char* c) {
        if (!strcmp(c, "chopper")) return 0;
        if (!strcmp(c, "blower"))  return 1;
        if (!strcmp(c, "rotator")) return 2;
        return -1;
    }

    static const char* componentName(int i) {
        return i == 0 ? "chopper" : i == 1 ? "blower" : "rotator";
    }

    static void setComponent(int i, bool on) {
        if      (i == 0) on ? chopperOn() : chopperOff();
        else if (i == 1) on ? blowerOn()  : blowerOff();
        else if (i == 2) on ? rotatorOn() : rotatorOff();
    }

    void checkTestKeepalives(uint32_t now) {
        CtrlLock lock;
        for (int i = 0; i < 3; i++) {
            if (!testHeld_[i]) continue;
            // Leaving TEST MODE (manually or by auto-expiry) already ran allOff().
            if (!buttons.testModeActive) { testHeld_[i] = false; continue; }
            if (now - testKaTs_[i] >= TEST_KEEPALIVE_TIMEOUT_MS) {
                testHeld_[i] = false;
                setComponent(i, false);
                Serial.printf("[%lu] test %s: keep-alive lost, switched off\n", now, componentName(i));
            }
        }
    }

    // An upload whose client vanished mid-transfer never reaches its request
    // handler — without this, sm.otaActive would block every run until a reboot.
    // otaLastDataMs_ is written from the async_tcp task while this runs on
    // loopTask — a plain `now - otaLastDataMs_` races: if the upload task
    // updates otaLastDataMs_ to a value a few ms "ahead" of this task's
    // `now` (an entirely benign scheduling race, not an actual stall), the
    // unsigned subtraction underflows to a huge number and instantly
    // (mis)fires. deadlinePassed() reads millis() fresh and compares via a
    // signed cast, so a benign skew reads as a small negative number
    // instead of wrapping — same fix as every other deadline check here.
    void checkOtaStall(uint32_t now) {
        if (!otaRunning_ || !deadlinePassed(otaLastDataMs_ + OTA_STALL_TIMEOUT_MS)) return;
        if (Update.isRunning()) Update.abort();
        otaRunning_ = false;
        CtrlLock lock;
        sm.otaActive = false;
        Serial.printf("OTA upload stalled - aborted (%u bytes written, %ldms since last data, %ldms total)\n",
            (unsigned)otaBytesWritten_, (long)(int32_t)(now - otaLastDataMs_), (long)(int32_t)(now - otaStartMs_));
    }

    void otaFail(OtaJob* job, const char* stage, const char* err) {
        Serial.printf("OTA %s failed: %s (%u bytes written)\n", stage, err, (unsigned)otaBytesWritten_);
        strlcpy(job->err, err, sizeof(job->err));
        if (Update.isRunning()) Update.abort();
    }

    // ── Route setup ───────────────────────────────────────────────────────────

    void setupRoutes() {

        // Login page (no auth)
        server_.on("/login", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendPage(req, LOGIN_HTML);
        });

        // Image assets (no auth — the login page and favicon need to load
        // before a session exists). Long cache lifetime since these only
        // change on a firmware reflash.
        server_.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/x-icon", FAVICON_ICO, FAVICON_ICO_LEN);
            r->addHeader("Cache-Control", "public, max-age=604800");
            req->send(r);
        });
        server_.on("/brand-icon.png", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/png", BRAND_ICON_PNG, BRAND_ICON_PNG_LEN);
            r->addHeader("Cache-Control", "public, max-age=604800");
            req->send(r);
        });
        server_.on("/login-siren.png", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/png", LOGIN_SIREN_PNG, LOGIN_SIREN_PNG_LEN);
            r->addHeader("Cache-Control", "public, max-age=604800");
            req->send(r);
        });
        server_.on("/icon-192.png", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/png", ICON_192_PNG, ICON_192_PNG_LEN);
            r->addHeader("Cache-Control", "public, max-age=604800");
            req->send(r);
        });
        server_.on("/icon-512.png", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/png", ICON_512_PNG, ICON_512_PNG_LEN);
            r->addHeader("Cache-Control", "public, max-age=604800");
            req->send(r);
        });
        server_.on("/manifest.json", HTTP_GET, [](AsyncWebServerRequest* req) {
            sendPage(req, MANIFEST_JSON, "application/manifest+json");
        });

        // Public at-a-glance state for the login page's status pill (no auth).
        // Deliberately just one word — no timers, relays, settings or source.
        server_.on("/pub-status", HTTP_GET, [](AsyncWebServerRequest* req) {
            const char* st;
            {
                CtrlLock lock;
                if      (buttons.testModeActive)       st = "test";
                else if (sm.state == State::STARTING)  st = "seq";
                else if (sm.state == State::STOPPING)  st = "stop";
                else                                   st = sm.modeName();  // "idle" when idle
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "{\"s\":\"%s\"}", st);
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json", buf);
            r->addHeader("Cache-Control", "no-store");
            req->send(r);
        });

        // Root → main page (auth required)
        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            sendPage(req, MAIN_HTML);
        });

        // Settings page (auth required)
        server_.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            sendPage(req, SETTINGS_HTML);
        });

        // Component test page (auth required)
        server_.on("/test", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            sendPage(req, TEST_HTML);
        });

        // Status JSON (auth required)
        server_.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            char buf[STATUS_JSON_MAX];
            buildStatusJson(buf, sizeof(buf));
            req->send(200, "application/json", buf);
        });

        // Settings JSON GET (auth required)
        server_.on("/settings-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            req->send(200, "application/json", buildSettingsJson());
        });

        // WiFi status GET (auth required)
        server_.on("/wifi-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            req->send(200, "application/json", buildWifiJson());
        });

        // Run log + lifetime totals (auth required)
        server_.on("/log", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            req->send(200, "application/json", buildLogJson());
        });

        // Logout — the Settings page POSTs; GET kept for a plain link/bookmark.
        auto logout = [this](AsyncWebServerRequest* req) {
            char tok[33];
            sessionToken(req, tok);
            removeSession(tok);
            AsyncWebServerResponse* r = req->beginResponse(200, "application/json", "{\"ok\":true}");
            r->addHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
            req->send(r);
        };
        server_.on("/auth/logout", HTTP_GET, logout);
        server_.on("/auth/logout", HTTP_POST, logout);

        // ── POST /auth/login ──────────────────────────────────────────────────
        server_.on("/auth/login", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                uint32_t now = millis();
                if (loginLockoutUntil_ && deadlinePassed(loginLockoutUntil_)) loginLockoutUntil_ = 0;
                if (loginLockoutUntil_) {
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
                    CtrlLock lock;
                    ok = (strcmp(pw, settingsMgr.s.webPassword) == 0);
                }
                if (ok) {
                    failedLoginAttempts_ = 0;
                    char token[33];
                    generateToken(token);
                    addSession(token);
                    AsyncWebServerResponse* resp =
                        req->beginResponse(200, "application/json", "{\"ok\":true}");
                    resp->addHeader("Set-Cookie",
                        String("sid=") + token + "; Path=/; HttpOnly; SameSite=Strict");
                    req->send(resp);
                } else {
                    failedLoginAttempts_++;
                    if (failedLoginAttempts_ >= MAX_LOGIN_ATTEMPTS) {
                        loginLockoutUntil_ = (now + LOGIN_LOCKOUT_MS) | 1;  // never 0 (= not locked)
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
                if (!isAuthed(req)) { unauth(req); return; }
                bool ok = true;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* mode = doc["mode"] | "";
                    CtrlLock lock;
                    // While TEST MODE is active, only the Test page's own component
                    // control ("test") may touch outputs — everything that would
                    // start or stop a siren program is blocked, including stop:
                    // the owner has an independent analog E-stop cutting all 120V
                    // power, so no software escape hatch is needed here.
                    bool blockedByTestMode = buttons.testModeActive && (
                        strcmp(mode, "wail")     == 0 || strcmp(mode, "attack") == 0 ||
                        strcmp(mode, "fastwail") == 0 || strcmp(mode, "manual") == 0 ||
                        strcmp(mode, "growl")    == 0 || strcmp(mode, "stop")  == 0);
                    if      (blockedByTestMode)              ok = false;
                    else if (strcmp(mode, "wail")      == 0) sm.trigger(RunMode::WAIL, TriggerSource::WEB);
                    else if (strcmp(mode, "attack")    == 0) sm.trigger(RunMode::ATTACK, TriggerSource::WEB);
                    else if (strcmp(mode, "fastwail")  == 0) sm.trigger(RunMode::FAST_WAIL, TriggerSource::WEB);
                    else if (strcmp(mode, "manual")    == 0) sm.trigger(RunMode::MANUAL, TriggerSource::WEB);
                    else if (strcmp(mode, "manual-ka") == 0) sm.webKeepalive();
                    else if (strcmp(mode, "growl")     == 0) sm.trigger(RunMode::GROWL, TriggerSource::WEB);
                    else if (strcmp(mode, "stop")      == 0) sm.stop(TriggerSource::WEB);
                    else if (strcmp(mode, "btn-lock")  == 0) buttons.setLocked(!buttons.locked, false);
                    else if (strcmp(mode, "test-lock") == 0) {
                        bool on = doc["on"] | false;
                        buttons.setTestMode(on);
                    }
                    else if (strcmp(mode, "test")      == 0) {
                        ok = buttons.testModeActive && sm.isIdle();
                        int i = componentIndex(doc["component"] | "");
                        if (ok && i >= 0) {
                            bool on = doc["on"] | false;
                            Serial.printf("[%lu] /cmd test: component=%s on=%d\n", millis(), componentName(i), on);
                            setComponent(i, on);
                            testHeld_[i] = on;
                            testKaTs_[i] = millis();
                        }
                    }
                    else if (strcmp(mode, "test-ka")   == 0) {
                        int i = componentIndex(doc["component"] | "");
                        if (i >= 0 && testHeld_[i]) testKaTs_[i] = millis();
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /settings-data ───────────────────────────────────────────────
        server_.on("/settings-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    CtrlLock lock;
                    Settings& s = settingsMgr.s;
                    // Every timing value is clamped to a sane range — an ON or
                    // OFF time of 0 would toggle the chopper relay on every loop
                    // pass, thousands of times a second.
                    applyUInt(doc, "chopperDelay",     s.chopperDelay,     0, 60000);
                    applyUInt(doc, "blowerDelay",      s.blowerDelay,      0, 60000);
                    applyUInt(doc, "rotatorDelay",     s.rotatorDelay,     0, 60000);
                    applyUInt(doc, "wailDuration",     s.wailDuration,     1000, 3600000);
                    applyUInt(doc, "attackDuration",   s.attackDuration,   1000, 3600000);
                    applyUInt(doc, "attackOnTime",     s.attackOnTime,     500, 600000);
                    applyUInt(doc, "attackOffTime",    s.attackOffTime,    500, 600000);
                    applyUInt(doc, "fastWailDuration", s.fastWailDuration, 1000, 3600000);
                    applyUInt(doc, "fastWailOnTime",   s.fastWailOnTime,   500, 600000);
                    applyUInt(doc, "fastWailOffTime",  s.fastWailOffTime,  500, 600000);
                    applyUInt(doc, "stopBlowerDelay",  s.stopBlowerDelay,  0, 60000);
                    applyUInt(doc, "stopChopperDelay", s.stopChopperDelay, 0, 60000);
                    applyUInt(doc, "stopRotDelay",     s.stopRotDelay,     0, 60000);
                    applyUInt(doc, "growlBlowerTime",  s.growlBlowerTime,  500, 60000);
                    applyUInt(doc, "growlRotatorTime", s.growlRotatorTime, 500, 60000);
                    applyUInt(doc, "growlChopperTime", s.growlChopperTime, 500, 60000);
                    applyUInt(doc, "longPressMs",      s.longPressMs,      200, 5000);
                    applyUInt(doc, "buttonDebounceMs", s.buttonDebounceMs, 0, 1000);
                    applyString(doc, "meshWhitelist", s.meshWhitelist, sizeof(s.meshWhitelist));
                    applyString(doc, "meshPassword", s.meshPassword, sizeof(s.meshPassword));
                    applyBool(doc, "weatherAutoTriggerEnabled", s.weatherAutoTriggerEnabled);
                    applyString(doc, "ntpServer", s.ntpServer, sizeof(s.ntpServer));
                    applyUInt(doc, "ntpUpdateHours", s.ntpUpdateHours, 1, 168);
                    applyString(doc, "timeZone", s.timeZone, sizeof(s.timeZone));
                    applyBool(doc, "autoDst", s.autoDst);
                    settingsMgr.save();
                    // Takes effect immediately: always re-apply the timezone, and
                    // if the WX link hasn't provided a time, kick the fallback
                    // NTP sync right away too rather than waiting for its next
                    // periodic re-sync.
                    if (wxSynced()) applyTimeZone(s);
                    else applyTimeConfig(s);
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /wifi-data ───────────────────────────────────────────────────
        server_.on("/wifi-data", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
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

        // ── POST /ap-password ─────────────────────────────────────────────────
        server_.on("/ap-password", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                bool ok = false;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* pw = doc["password"] | "";
                    if (WiFiManager::isValidApPassword(pw)) {
                        wifiMgr.saveApPassword(pw);
                        wifiMgr.scheduleRestart(1500);
                        ok = true;
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /password ────────────────────────────────────────────────────
        server_.on("/password", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                bool ok = false;
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* pw = doc["password"] | "";
                    if (isStrongPassword(pw)) {
                        {
                            CtrlLock lock;
                            strlcpy(settingsMgr.s.webPassword, pw, sizeof(settingsMgr.s.webPassword));
                            settingsMgr.save();
                        }
                        clearSessions(); // force re-login with new password
                        ok = true;
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // ── POST /update (OTA firmware upload) ────────────────────────────────
        server_.on("/update", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                OtaJob* job = (OtaJob*)req->_tempObject;
                bool ok = job && job->ok;
                if (job && job->owner && !ok) {
                    if (Update.isRunning()) Update.abort();
                    otaRunning_ = false;
                    CtrlLock lock;
                    sm.otaActive = false;
                }
                JsonDocument doc;
                doc["ok"] = ok;
                if (!ok) doc["error"] = (job && job->err[0]) ? job->err : "No firmware file received";
                String out;
                serializeJson(doc, out);
                req->send(200, "application/json", out);
            },
            [this](AsyncWebServerRequest* req, const String& filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
                (void)filename;
                OtaJob* job = (OtaJob*)req->_tempObject;
                if (index == 0) {
                    job = (OtaJob*)calloc(1, sizeof(OtaJob));
                    req->_tempObject = job;
                    if (!job) return;
                    if (!isAuthed(req)) { otaFail(job, "auth", "Not logged in"); return; }
                    {
                        CtrlLock lock;
                        if      (otaRunning_)            strlcpy(job->err, "Another update is already in progress", sizeof(job->err));
                        else if (!sm.isIdle())           strlcpy(job->err, "Siren is running - stop it first", sizeof(job->err));
                        else if (buttons.testModeActive) strlcpy(job->err, "Turn TEST MODE off first", sizeof(job->err));
                        else { sm.otaActive = true; otaRunning_ = true; job->owner = true; }
                    }
                    if (!job->owner) return;
                    otaStartMs_      = millis();
                    otaLastDataMs_   = otaStartMs_;
                    otaBytesWritten_ = 0;
                    Serial.printf("OTA upload started (free heap: %u bytes, content-length: %u bytes)\n",
                        (unsigned)ESP.getFreeHeap(), (unsigned)req->contentLength());
                    // A known size (even just the multipart request's
                    // Content-Length, a bit larger than the true firmware
                    // size) lets IDF erase only what's needed instead of the
                    // whole OTA partition up front — with UPDATE_SIZE_UNKNOWN
                    // that full-partition erase can block long enough to trip
                    // the stall watchdog below on a perfectly healthy upload.
                    uint32_t beginStartMs = millis();
                    bool began = Update.begin(req->contentLength(), U_FLASH);
                    Serial.printf("OTA Update.begin() took %lums\n", (unsigned long)(millis() - beginStartMs));
                    if (!began) { otaFail(job, "begin", Update.errorString()); return; }
                }
                if (!job || !job->owner || job->err[0]) return;
                otaLastDataMs_ = millis();
                if (len && Update.write(data, len) != len) { otaFail(job, "write", Update.errorString()); return; }
                otaBytesWritten_ += len;
                if (final) {
                    if (Update.end(true)) {
                        job->ok = true;
                        Serial.printf("OTA upload complete (%u bytes) - restarting\n", (unsigned)(index + len));
                        wifiMgr.scheduleRestart(1500);  // even if the client never reads the reply
                    } else {
                        otaFail(job, "end", Update.errorString());
                    }
                }
            }
        );

        // ── POST /restart ─────────────────────────────────────────────────────
        server_.on("/restart", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
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
    // Collects chunked POST body into req->_tempObject (null-terminated heap
    // buffer). Bodies over MAX_BODY are dropped without allocating — handlers
    // then see no body and do nothing — so a bogus Content-Length (even on
    // the unauthenticated login route) can't exhaust the heap.
    static void bodyAccumulator(AsyncWebServerRequest* req,
                                uint8_t* data, size_t len,
                                size_t index, size_t total) {
        if (total > MAX_BODY) return;
        if (index == 0) req->_tempObject = malloc(total + 1);
        if (req->_tempObject && index + len <= total) {
            memcpy((uint8_t*)req->_tempObject + index, data, len);
            if (index + len == total)
                ((char*)req->_tempObject)[total] = '\0';
        }
    }

    // ── JSON builders ─────────────────────────────────────────────────────────

    static void applyUInt(JsonDocument& doc, const char* key, uint32_t& field, uint32_t lo, uint32_t hi) {
        if (!doc[key].is<double>()) return;
        double v = doc[key].as<double>();
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        field = (uint32_t)v;
    }

    static void applyString(JsonDocument& doc, const char* key, char* field, size_t fieldSize) {
        if (doc[key].is<const char*>()) strlcpy(field, doc[key].as<const char*>(), fieldSize);
    }

    static void applyBool(JsonDocument& doc, const char* key, bool& field) {
        if (doc[key].is<bool>()) field = doc[key].as<bool>();
    }

    static const char* tf(bool b) { return b ? "true" : "false"; }

    // Pushed to every WebSocket client twice a second — formatted on the
    // stack rather than via JsonDocument/String to avoid that heap churn.
    // Every value is a fixed token or a number, so nothing needs escaping.
    static size_t buildStatusJson(char* buf, size_t size) {
        CtrlLock lock;
        uint8_t rs = relayState();
        TimerInfo ti = sm.getTimerInfo();
        char clockBuf[12] = "--:--:--";
        if (clockValid()) {
            time_t t = time(nullptr);
            strftime(clockBuf, sizeof(clockBuf), "%H:%M:%S", localtime(&t));
        }
        int n = snprintf(buf, size,
            "{\"mode\":\"%s\",\"runMode\":\"%s\",\"uptime\":%lu,\"relays\":[%u,%u,%u],"
            "\"elapsed\":%lu,\"remaining\":%lu,\"hasRemaining\":%s,\"btnLocked\":%s,"
            "\"testMode\":%s,\"lockAutoExpire\":%s,\"lockRemaining\":%lu,\"tempF\":%d,"
            "\"currentTime\":\"%s\"}",
            sm.stateName(), sm.modeName(), (unsigned long)(millis() / 1000),
            (unsigned)(rs & 1), (unsigned)((rs >> 1) & 1), (unsigned)((rs >> 2) & 1),
            (unsigned long)(ti.totalElapsedMs / 1000), (unsigned long)(ti.totalRemainingMs / 1000),
            tf(ti.hasRemaining), tf(buttons.locked), tf(buttons.testModeActive),
            tf(buttons.lockAutoExpiring()), (unsigned long)buttons.lockRemainingSec(),
            (int)roundf(temperatureRead() * 9.0f / 5.0f + 32.0f), clockBuf);
        if (n < 0) { buf[0] = '\0'; return 0; }
        return ((size_t)n >= size) ? size - 1 : (size_t)n;
    }

    static String buildSettingsJson() {
        CtrlLock lock;
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
        doc["stopBlowerDelay"]  = s.stopBlowerDelay;
        doc["stopChopperDelay"] = s.stopChopperDelay;
        doc["stopRotDelay"]     = s.stopRotDelay;
        doc["growlBlowerTime"]  = s.growlBlowerTime;
        doc["growlRotatorTime"] = s.growlRotatorTime;
        doc["growlChopperTime"] = s.growlChopperTime;
        doc["longPressMs"]         = s.longPressMs;
        doc["buttonDebounceMs"]    = s.buttonDebounceMs;
        doc["meshWhitelist"]       = s.meshWhitelist;
        doc["meshPassword"]        = s.meshPassword;
        doc["weatherAutoTriggerEnabled"] = s.weatherAutoTriggerEnabled;
        doc["ntpServer"]        = s.ntpServer;
        doc["ntpUpdateHours"]   = s.ntpUpdateHours;
        doc["timeZone"]         = s.timeZone;
        doc["autoDst"]          = s.autoDst;
        doc["wwEverReported"] = weatherLink.everReported();
        doc["wwOk"]           = weatherLink.reportedOk();
        doc["wwPending"]      = weatherLink.pending();
        doc["wwDetail"]       = weatherLink.detail();
        doc["resetReason"]    = resetReasonName();
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
        doc["hostname"]  = wifiMgr.getHostname();
        doc["apSecured"] = wifiMgr.apSecured();
        String out;
        serializeJson(doc, out);
        return out;
    }

    // Short keys: e=epoch (0 = clock not set), u=start uptime s, d=duration s,
    // m=RunMode, s=TriggerSource, x=RunEnd, ss=stop TriggerSource.
    static String buildLogJson() {
        CtrlLock lock;
        JsonDocument doc;
        doc["runs"]   = runLog.totalRuns();
        doc["secs"]   = runLog.totalSecs();
        doc["uptime"] = (uint32_t)(millis() / 1000);
        JsonArray arr = doc["entries"].to<JsonArray>();
        for (uint8_t i = 0; i < runLog.count(); i++) {
            const RunEntry& e = runLog.recent(i);
            JsonObject o = arr.add<JsonObject>();
            o["e"]  = e.epoch;
            o["u"]  = e.startUptimeSec;
            o["d"]  = e.durationSec;
            o["m"]  = e.mode;
            o["s"]  = e.source;
            o["x"]  = e.end;
            o["ss"] = e.stopSource;
        }
        String out;
        serializeJson(doc, out);
        return out;
    }
};

extern WebUI webUI;
