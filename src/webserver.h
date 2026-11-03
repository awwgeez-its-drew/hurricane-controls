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
#include "mesh_health.h"
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
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap">
<style>
:root{--bg:#0A0E13;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;--text:#E7ECF3;--soft:#C9D4E3;
      --muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--green:#4ADE80;--red:#F87171;
      --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:var(--sans);
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:32px 16px}
.wrap{width:100%;max-width:380px;display:flex;flex-direction:column;gap:20px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:20px;padding:36px 28px 30px;
      display:flex;flex-direction:column;align-items:center;gap:22px;text-align:center}
.login-siren{width:140px;height:140px;display:block;margin-bottom:-8px}
h1{font-size:24px;font-weight:700;letter-spacing:.01em}
.sub{color:var(--muted);font-size:14px;margin-top:6px}
.pill{display:none;align-items:center;gap:8px;padding:7px 14px;border-radius:999px;background:var(--inset);
      border:1px solid #222D3D;font-size:12px;font-weight:600;letter-spacing:.12em;color:var(--soft)}
.pill .dot{width:8px;height:8px;border-radius:50%;background:#64748B}
.pill.idle,.pill.seq,.pill.stop,.pill.wail,.pill.attack,.pill.fastwail,.pill.manual,.pill.growl,.pill.test{display:inline-flex}
.pill.wail .dot{background:#8DB4FF}.pill.attack .dot,.pill.test .dot{background:var(--red)}
.pill.fastwail .dot{background:#FDBA74}.pill.manual .dot{background:#C9B0FF}.pill.growl .dot{background:#6EE7C8}
.pill.seq .dot,.pill.stop .dot{background:var(--green)}
form{width:100%;display:flex;flex-direction:column;gap:12px;text-align:left}
label{font-size:13px;font-weight:600;color:var(--soft)}
input{width:100%;min-height:50px;padding:0 16px;border-radius:12px;border:1px solid var(--line2);background:var(--inset);
      color:var(--text);font-family:var(--sans);font-size:16px}
input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(56,189,248,.2)}
input::placeholder{color:var(--faint)}
button{min-height:52px;border:0;border-radius:12px;background:var(--green);color:#052E16;font-family:var(--sans);
       font-size:15px;font-weight:700;letter-spacing:.1em;cursor:pointer;margin-top:4px}
button:active{opacity:.85}
.err{color:var(--red);font-size:13px;min-height:18px;text-align:center}
.foot{text-align:center;font-size:12px;color:var(--faint)}
</style></head><body>
<div class="wrap">
<div class="card">
  <img class="login-siren" src="/login-siren.png" alt="">
  <div><h1>Hurricane Controls</h1><p class="sub">Siren controller</p></div>
  <div id="pill" class="pill"><span class="dot"></span><span id="pillTxt"></span></div>
  <form onsubmit="login(event)">
    <label for="pw">Password</label>
    <input type="password" id="pw" placeholder="Enter the device password" autocomplete="current-password" autofocus>
    <button type="submit">UNLOCK</button>
    <div class="err" id="err"></div>
  </form>
</div>
<p class="foot">Refreshes every 2 seconds.</p>
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
    document.getElementById('pillTxt').textContent=PL[d.s]||String(d.s).toUpperCase();p.className='pill '+d.s;
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
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@600;700&family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#0A0E13;--bar:#0D1219;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;
  --text:#E7ECF3;--soft:#C9D4E3;--muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--green:#4ADE80;--red:#F87171;
  --wail:#1D4ED8;--attack:#B91C1C;--fastwail:#A84A0A;--manual:#6D28D9;--growl:#0F766E;
  --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  --mono:"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Consolas,monospace;
  --disp:"Barlow Condensed","Arial Narrow",Inter,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:var(--text);font-family:var(--sans);min-height:100vh;display:flex;flex-direction:column}
.mono{font-family:var(--mono)}.disp{font-family:var(--disp)}
.eyebrow{font-size:11px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.top{position:sticky;top:0;z-index:20;background:var(--bar);border-bottom:1px solid var(--line)}
.topin{max-width:1200px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.brand{display:flex;align-items:center;gap:12px;min-width:0}
.tile{width:40px;height:40px;border-radius:10px;background:#172030;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0}
.tile img{width:32px;height:32px;display:block}
.bt{display:flex;flex-direction:column;gap:2px;min-width:0}
.bt b{font-size:17px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.nav{display:flex;gap:8px;align-items:center;flex-shrink:0}
.ibtn{width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;border-radius:10px;
      border:1px solid #222D3D;background:var(--panel);color:var(--soft);cursor:pointer;text-decoration:none;padding:0}
.ibtn:hover{background:#172030;color:#fff}
.ibtn.locked{color:var(--red);border-color:#7A2531}
main{flex:1;width:100%;max-width:1200px;margin:0 auto;padding:20px;display:flex;flex-direction:column;gap:20px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:16px}
.chip{display:inline-flex;align-items:center;gap:8px;padding:7px 12px;border-radius:999px;background:var(--bar);
      border:1px solid #222D3D;font-size:13px;color:var(--soft)}
.dot{width:8px;height:8px;border-radius:50%;background:#64748B;flex-shrink:0}
.dot.ok{background:var(--green)}.dot.warn{background:#FACC15}.dot.error{background:var(--red)}
/* Active warnings */
#warnBanner{display:none;padding:16px 18px;flex-direction:column;gap:10px;border-color:#5B2230;background:#160F15}
#warnBanner.show{display:flex}
.whead{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}
.warnItem{display:flex;align-items:center;gap:12px;flex-wrap:wrap;padding:12px 14px;border-radius:10px;background:#2A1218}
.warnItem.orange{background:#251A12}.warnItem.purple{background:#1E1530}.warnItem.yellow{background:#2A2512}
.warnItem.stale{opacity:.55}
.wdot{width:12px;height:12px;border-radius:50%;flex-shrink:0;background:#64748B}
.warnItem.red .wdot{background:#EF4444}.warnItem.orange .wdot{background:#F97316}.warnItem.purple .wdot{background:#A855F7}.warnItem.yellow .wdot{background:#FACC15}
.wname{flex:1;min-width:180px;display:flex;align-items:center;gap:8px;flex-wrap:wrap;font-weight:600}
.wtag{font-size:11px;font-weight:700;letter-spacing:.08em;padding:3px 8px;border-radius:999px;border:1px solid var(--red);color:#FECACA}
.warnItem.orange .wtag{border-color:#F97316;color:#FED7AA}.warnItem.purple .wtag{border-color:#A855F7;color:#E9D5FF}.warnItem.yellow .wtag{border-color:#FACC15;color:#FEF08A}
.wthis{font-size:11px;font-weight:700;letter-spacing:.08em;padding:4px 8px;border-radius:6px;background:#14301F;color:#86EFAC}
.wage{font-size:12px;color:var(--soft)}
/* Layout */
.cols{display:flex;flex-wrap:wrap;gap:20px;align-items:stretch}
.left{flex:1 1 520px;min-width:0;display:flex;flex-direction:column;gap:20px}
/* Status hero */
.hero{padding:26px 28px;display:flex;flex-direction:column;gap:22px;transition:background .3s,border-color .3s}
.hrow{display:flex;align-items:center;justify-content:space-between;gap:12px}
.hmain{display:flex;align-items:flex-end;justify-content:space-between;gap:20px;flex-wrap:wrap}
.ml{font-family:var(--disp);font-size:72px;line-height:.9;font-weight:700;letter-spacing:.02em;text-transform:uppercase;color:var(--soft)}
.tsub{font-size:15px;color:var(--soft);min-height:1.3em;margin-top:4px}
.tright{display:flex;flex-direction:column;align-items:flex-end;gap:4px}
.timer{font-family:var(--mono);font-size:56px;font-weight:500;line-height:1;letter-spacing:.02em;font-variant-numeric:tabular-nums}
.hero[data-m=wail]{background:#0F1930;border-color:#2B4C8F}.hero[data-m=wail] .ml{color:#8DB4FF}
.hero[data-m=attack]{background:#22111A;border-color:#7A2531}.hero[data-m=attack] .ml{color:#FF9A9A}
.hero[data-m=fastwail]{background:#21170F;border-color:#7A4A1A}.hero[data-m=fastwail] .ml{color:#FDBA74}
.hero[data-m=manual]{background:#19132E;border-color:#4C2F8A}.hero[data-m=manual] .ml{color:#C9B0FF}
.hero[data-m=growl]{background:#0E211F;border-color:#1F5E57}.hero[data-m=growl] .ml{color:#6EE7C8}
.hero[data-m=stop] .ml{color:#86EFAC}
.hero[data-m=test]{background:#22111A;border-color:#7A2531}.hero[data-m=test] .ml{color:#FF9A9A}
@keyframes strobe{0%,45%{opacity:1;box-shadow:0 0 8px 2px rgba(74,222,128,.85)}50%,100%{opacity:.18;box-shadow:none}}
.dot.strobe{background:var(--green);animation:strobe .9s linear infinite}
.dot.test{background:var(--red)}
.relays{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
.relay{display:flex;align-items:center;gap:12px;padding:14px;border-radius:12px;background:var(--inset);border:1px solid var(--line);min-width:0}
.lamp{width:14px;height:14px;border-radius:50%;flex-shrink:0;background:#263142;transition:background .3s,box-shadow .3s}
.lamp.on{background:var(--green);box-shadow:0 0 10px rgba(74,222,128,.7)}
.rname{font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase}
.rstate{font-family:var(--mono);font-size:12px;color:var(--faint)}
.relay.on .rstate{color:#86EFAC}
.meta{display:flex;flex-wrap:wrap;gap:8px 22px;font-family:var(--mono);font-size:13px;color:var(--muted);padding-top:16px;border-top:1px solid var(--line)}
.meta span span{color:var(--text)}
/* Health */
.health{padding:0 20px}
.health[open]{padding-bottom:18px}
.health summary{display:flex;align-items:center;gap:10px;min-height:52px;cursor:pointer;list-style:none}
.health summary::-webkit-details-marker{display:none}
.health summary .dot{width:10px;height:10px}
.hsum{flex:1;text-align:right;font-size:13px;color:var(--muted)}
.chev{color:var(--muted);flex-shrink:0;transition:transform .15s}
.health[open] .chev{transform:rotate(180deg)}
.chips{display:flex;flex-wrap:wrap;gap:8px}
/* Controls */
.ctrl{flex:1 1 420px;min-width:0;padding:24px;display:flex;flex-direction:column;gap:18px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
.btn{position:relative;overflow:hidden;border:0;border-radius:14px;min-height:136px;padding:18px 20px;text-align:left;color:#fff;
     cursor:pointer;display:flex;flex-direction:column;justify-content:space-between;gap:14px;font-family:var(--sans);
     touch-action:none;user-select:none;-webkit-user-select:none;transition:filter .15s}
.btn:hover{filter:brightness(1.1)}
.btn .bl{position:relative;font-family:var(--disp);font-size:34px;font-weight:700;letter-spacing:.04em;line-height:1}
.btn .bc{position:relative;font-size:13px;opacity:.88}
.bwail{background:var(--wail)}.battack{background:var(--attack)}.bfastwail{background:var(--fastwail)}.bmanual{background:var(--manual)}
.bmanual.on{box-shadow:0 0 0 3px var(--bg),0 0 0 5px var(--green)}
/* Hold-to-confirm fill — a bottom-anchored overlay that grows with the
   hold, so the button only fires once it's full (WP6). touch-action:none
   stops the browser treating the hold as a scroll/selection gesture. */
.holdable{position:relative;overflow:hidden;touch-action:none;user-select:none;-webkit-user-select:none}
.hfill{position:absolute;left:0;bottom:0;width:100%;height:0%;background:rgba(255,255,255,.28);
       pointer-events:none;transition:height .05s linear}
.lockrow{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;padding:14px 16px;
         border-radius:12px;background:var(--inset);border:1px solid var(--line)}
.lockl{display:flex;align-items:center;gap:14px;min-width:0}
.locktxt{display:flex;flex-direction:column;gap:2px}
.locktxt b{font-size:14px}
#lockTxt{font-size:13px;color:var(--muted)}
#lockTxt.locked{color:var(--red)}
.lockbtn{cursor:pointer;min-height:44px;padding:0 16px;font-family:var(--sans)}
.lockbtn:hover{background:#172030}
/* STOP */
.stopbar{position:sticky;bottom:0;z-index:20;width:100%;border:0;background:#FECACA;color:#3B0A0A;cursor:pointer;
         display:flex;align-items:center;justify-content:center;gap:14px;min-height:76px;font-family:var(--sans)}
.stopbar:hover{background:#FDB4B4}
.stopbar .hfill{background:rgba(127,29,29,.28)}
.stopbar svg,.stopbar .sl{position:relative}
.sl{display:flex;flex-direction:column;align-items:flex-start;gap:2px}
.sl b{font-family:var(--disp);font-size:30px;letter-spacing:.12em;line-height:1}
.sl span{font-size:12px;font-weight:500}
/* Restart modal */
.overlay{position:fixed;inset:0;background:rgba(4,6,10,.72);display:none;align-items:center;justify-content:center;padding:16px;z-index:50}
.mbox{width:100%;max-width:360px;padding:24px;display:flex;flex-direction:column;gap:18px;background:#131B26}
.mbox p{font-size:14px;color:var(--muted);line-height:1.5}
.mgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.mok,.mcancel{min-height:46px;border-radius:10px;font-weight:700;font-family:var(--sans);cursor:pointer;font-size:14px}
.mok{border:0;background:var(--attack);color:#fff}
.mcancel{border:1px solid var(--line2);background:#172030;color:var(--text)}
/* Single column (phones/narrow windows): Siren status, System status,
   Run modes — the controls stay close to the top with System status
   collapsed (see healthCard below). */
@media (max-width:959px){
  .cols{flex-direction:column;flex-wrap:nowrap}
  .left{display:contents}
  .hero{order:1}#healthCard{order:2}.ctrl{order:3}
}
@media (max-width:560px){
  .ml{font-size:56px}.timer{font-size:44px}.hero{padding:20px}.ctrl{padding:18px}
  .btn{min-height:116px}.btn .bl{font-size:28px}
  .relays{gap:6px}.relay{padding:10px;gap:8px}
}
@media (max-width:420px){
  .topin{padding:10px 14px}
  .bt b{white-space:normal;font-size:15px;line-height:1.15}.bt .eyebrow{display:none}
  .nav{gap:6px}.ibtn{width:40px;height:40px}
}
@media (prefers-reduced-motion:reduce){
  .dot.strobe{animation:none;box-shadow:0 0 8px 2px rgba(74,222,128,.85)}
}
</style></head><body>
<header class="top"><div class="topin">
  <div class="brand">
    <span class="tile"><img src="/brand-icon.png" alt=""></span>
    <div class="bt"><b>Hurricane Controls</b><span class="eyebrow" style="font-size:10px">Control panel</span></div>
  </div>
  <nav class="nav" aria-label="Device actions">
    <button class="ibtn" id="btnLock" onclick="toggleLock()" title="Lock physical buttons" aria-label="Lock physical buttons">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="5" y="11" width="14" height="10" rx="2"/><path id="lockShackle" d="M8 11V7a4 4 0 0 1 7.5-2"/></svg>
    </button>
    <button class="ibtn" onclick="showRestart()" title="Restart" aria-label="Restart controller">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
    </button>
    <a href="/settings" class="ibtn" title="Settings" aria-label="Settings">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
    </a>
    <button class="ibtn" onclick="goHome()" title="Log out" aria-label="Log out">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
    </button>
  </nav>
</div></header>

<main>
<section class="panel" id="warnBanner" aria-label="Active weather alerts">
  <div class="whead"><span class="eyebrow" style="color:#FCA5A5">Active alerts &middot; from Weather Watcher</span><span class="wage" id="warnAge"></span></div>
  <div id="warnList" style="display:flex;flex-direction:column;gap:10px"></div>
</section>

<div class="cols">
  <div class="left">
    <section class="panel hero" id="hero" data-m="idle" aria-label="Siren status">
      <div class="hrow"><span class="eyebrow">Siren status</span><span class="chip"><span class="dot" id="chipDot"></span><span id="chipTxt">Ready</span></span></div>
      <div class="hmain">
        <div><div class="ml" id="modeLbl">Idle</div><div class="tsub" id="tsub">Ready</div></div>
        <div class="tright"><span class="eyebrow" id="bigLbl">Remaining</span><span class="timer" id="timer">--:--</span></div>
      </div>
      <div class="relays">
        <div class="relay" id="r0"><span class="lamp" id="d0"></span><span><span class="rname">Chopper</span><br><span class="rstate" id="s0">OFF</span></span></div>
        <div class="relay" id="r1"><span class="lamp" id="d1"></span><span><span class="rname">Blower</span><br><span class="rstate" id="s1">OFF</span></span></div>
        <div class="relay" id="r2"><span class="lamp" id="d2"></span><span><span class="rname">Rotator</span><br><span class="rstate" id="s2">OFF</span></span></div>
      </div>
      <div class="meta">
        <span>Controller <span id="tempF">&mdash;</span>&deg;F</span>
        <span>Uptime <span id="uptime">&mdash;</span></span>
        <span>Clock <span id="clockTime">--:--:--</span></span>
      </div>
    </section>

    <details class="panel health" id="healthCard" aria-label="System status">
      <summary>
        <span class="eyebrow">System status</span><span class="dot" id="hdot"></span>
        <span class="hsum" id="hsum">Checking&hellip;</span>
        <svg class="chev" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="6 9 12 15 18 9"/></svg>
      </summary>
      <div class="chips" id="hchips"></div>
    </details>
  </div>

  <section class="panel ctrl" aria-label="Run modes">
    <span class="eyebrow">Run modes</span>
    <div class="grid">
      <button class="btn bwail holdable" id="bWail"><div class="hfill"></div><span class="bl">WAIL</span><span class="bc">Hold to start</span></button>
      <button class="btn battack holdable" id="bAttack"><div class="hfill"></div><span class="bl">ATTACK</span><span class="bc">Hold to start</span></button>
      <button class="btn bfastwail holdable" id="bFastWail"><div class="hfill"></div><span class="bl">FAST WAIL</span><span class="bc">Hold to start</span></button>
      <button class="btn bmanual" id="bm"
        onmousedown="manualDown(event)" onmouseup="manualUp(event)" onmouseleave="manualUp(event)"
        ontouchstart="manualDown(event)" ontouchend="manualUp(event)" ontouchcancel="manualUp(event)"><span class="bl" id="bmLbl">MANUAL</span><span class="bc">Sounds only while held</span></button>
    </div>
    <div class="lockrow">
      <div class="lockl">
        <svg width="48" height="48" viewBox="0 0 48 48" role="img" id="padIcon" aria-label="Panel buttons unlocked" style="flex-shrink:0">
          <circle cx="21" cy="21" r="19" fill="#2B3543" stroke="#556377" stroke-width="1.5"/>
          <circle cx="21" cy="21" r="15" fill="#151C26"/>
          <circle cx="21" cy="21" r="12" fill="#3A4554"/>
          <ellipse cx="21" cy="16" rx="8" ry="4.5" fill="rgba(255,255,255,.16)"/>
          <circle cx="36" cy="36" r="11.5" fill="#0B1017"/>
          <path id="padShackle" d="M32 34.5v-3.5a4 4 0 0 1 7.6-1.8" fill="none" stroke="#22C55E" stroke-width="2.4" stroke-linecap="round"/>
          <rect id="padBody" x="29.5" y="34.5" width="13" height="10" rx="2.2" fill="#22C55E"/>
          <circle cx="36" cy="39.5" r="1.4" fill="#0B1017"/>
        </svg>
        <div class="locktxt"><b>Physical buttons</b><span id="lockTxt">Unlocked &mdash; the panel buttons work</span></div>
      </div>
      <button class="chip lockbtn" id="lockAct" onclick="toggleLock()">Lock</button>
    </div>
  </section>
</div>
</main>

<button class="stopbar holdable" id="bStop"><div class="hfill"></div>
  <svg width="22" height="22" viewBox="0 0 22 22" aria-hidden="true"><rect x="2" y="2" width="18" height="18" rx="3" fill="#B91C1C"/></svg>
  <span class="sl"><b>STOP</b><span>Hold briefly to stop the siren</span></span>
</button>

<div class="overlay" id="rmModal">
  <div class="panel mbox" role="dialog" aria-modal="true" aria-labelledby="rmTitle">
    <div><b id="rmTitle" style="font-size:18px">Restart the controller?</b><p style="margin-top:6px">The page reconnects on its own once the board is back up.</p></div>
    <div class="mgrid">
      <button class="mcancel" onclick="closeModal()">Cancel</button>
      <button class="mok"     onclick="doRestart()">Restart</button>
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
function esc(s){const d=document.createElement('div');d.textContent=s||'';return d.innerHTML;}
const MESH_FRESH_SEC=6*3600; // heard within this long = green dot
function ago(s){
  if(s<60)return s+'s ago';
  if(s<3600)return Math.floor(s/60)+'m ago';
  if(s<86400)return Math.floor(s/3600)+'h ago';
  return Math.floor(s/86400)+'d ago';
}

function drawHealth(h){
  if(!h)return;
  const wwWord={'OFF':'off','N/A':'not connected','LOST':'LOST','STARTING':'starting up','OK':'OK','ERROR':'ERROR'}[h.ww]||h.ww;
  const wwCls=h.ww==='OK'?'ok':(h.ww==='LOST'||h.ww==='ERROR')?'error':(h.ww==='STARTING')?'warn':'';
  document.getElementById('hdot').className='dot '+h.overall;
  document.getElementById('hsum').textContent=h.overall==='ok'?'All systems normal':h.overall==='warn'?'Check system status':'Attention needed';
  const chip=(cls,txt)=>'<span class="chip"><span class="dot '+cls+'"></span>'+txt+'</span>';
  // Meshtastic: passive only (Text Message mode can't query the node), so a
  // quiet channel stays grey rather than red — it isn't proof of a fault.
  const m=h.mesh||{rx:-1};
  const meshChip=m.rx<0?chip('','Meshtastic: nothing heard yet')
    :chip(m.rx<MESH_FRESH_SEC?'ok':'','Meshtastic heard '+ago(m.rx));
  document.getElementById('hchips').innerHTML=
    chip(h.wifi?'ok':'error','Wi-Fi '+(h.wifi?'connected':'disconnected'))+
    chip(wwCls,'Weather Watcher '+wwWord)+
    meshChip+
    chip('','Free memory '+h.freeHeapKB+' KB')+
    (h.otaActive?chip('warn','Firmware update in progress'):'');
}
// A static "until 11pm" (viewer's own local time, from the alert's exp
// epoch) — no live countdown, unlike the Weather Watcher dashboard's.
// Drops ":00" on the hour to match how people actually say it.
function fmtUntil(exp){
  if(!exp)return'';
  const d=new Date(exp*1000);
  let h=d.getHours();const m=d.getMinutes();
  const ap=h>=12?'pm':'am';
  h=h%12;if(h===0)h=12;
  return ' until '+h+(m?':'+String(m).padStart(2,'0'):'')+ap;
}
function refreshAlerts(){
  fetch('/alerts').then(r=>r.json()).then(d=>{
    const box=document.getElementById('warnBanner');
    let list=d.alerts||[];
    if(d.nowEpoch) list=list.filter(a=>!a.exp||a.exp>d.nowEpoch);
    if(!list.length){box.classList.remove('show');document.getElementById('warnList').innerHTML='';return;}
    const ageMs=d.receivedMs?(d.uptimeMs-d.receivedMs):null;
    const stale=ageMs!=null&&ageMs>180000;
    document.getElementById('warnAge').textContent=stale?'Last heard '+Math.floor(ageMs/60000)+'m ago':'';
    document.getElementById('warnList').innerHTML=list.map(a=>{
      let act='';
      if(a.activatedThis){
        act='SIREN ACTIVATED'+(a.actMode?' &mdash; '+esc(a.actMode):'')+(a.actTime?' at '+esc(a.actTime):'');
        act='<span class="wthis">'+act+'</span>';
      }
      return '<div class="warnItem '+esc(a.color)+(stale?' stale':'')+'">'+
        '<span class="wdot" role="img" aria-label="'+esc(a.color||'')+' alert"></span>'+
        '<span class="wname">'+esc(a.event)+fmtUntil(a.exp)+(a.tag?'<span class="wtag">'+esc(a.tag)+'</span>':'')+'</span>'+
        act+'<span class="wage">'+(stale?'stale':'')+'</span></div>';
    }).join('');
    box.classList.add('show');
  }).catch(()=>{});
}
refreshAlerts();
setInterval(refreshAlerts,5000);

// System status starts expanded on wide screens and collapsed on phones,
// where its title light is enough and Run modes stay higher up. Crossing
// that width later (rotating a tablet, resizing a window) follows suit.
(function(){
  const card=document.getElementById('healthCard'), wide=window.matchMedia('(min-width:960px)');
  card.open=wide.matches;
  if(wide.addEventListener)wide.addEventListener('change',e=>{card.open=e.matches;});
})();

const ML={idle:'Idle',wail:'Wail',attack:'Attack',fastwail:'Fast Wail',manual:'Manual',growl:'Growl Test',stop:'Stopping',test:'Test Mode'};
function draw(d){
  drawHealth(d.health);
  const st=d.mode||'idle', rm=d.runMode||'idle';
  let key;
  if(d.testMode&&st==='idle') key='test';
  else if(st==='idle') key='idle';
  else if(st==='stop') key='stop';
  else key=(rm!=='idle')?rm:(st.startsWith('attack')?'attack':st.startsWith('fastwail')?'fastwail':st);
  document.getElementById('hero').dataset.m=key;
  document.getElementById('modeLbl').textContent=ML[key]||key;

  const chipDot=document.getElementById('chipDot'),chipTxt=document.getElementById('chipTxt');
  if(key==='test'){chipDot.className='dot test';chipTxt.textContent='Test mode';}
  else if(st==='idle'){chipDot.className='dot';chipTxt.textContent='Ready';}
  else{chipDot.className='dot strobe';chipTxt.textContent=st==='stop'?'Stopping':'Running';}

  const r=d.relays||[0,0,0];
  [0,1,2].forEach(i=>{
    const on=!!r[i];
    document.getElementById('d'+i).classList.toggle('on',on);
    document.getElementById('r'+i).classList.toggle('on',on);
    document.getElementById('s'+i).textContent=on?'ON':'OFF';
  });

  const te=document.getElementById('timer'),ts=document.getElementById('tsub'),bl=document.getElementById('bigLbl');
  if(key==='test'){bl.textContent='Remaining';te.textContent='--:--';ts.textContent='Software STOP disabled — use the hardware E-stop';}
  else if(st==='idle'){bl.textContent='Remaining';te.textContent='--:--';ts.textContent='Ready';}
  else if(st==='seq'){bl.textContent='Remaining';te.textContent='--:--';ts.textContent='Starting…';}
  else if(st==='stop'){bl.textContent='Elapsed';te.textContent=fmt(d.elapsed);ts.textContent='Stopping…';}
  else if(d.hasRemaining){bl.textContent='Remaining';te.textContent=fmt(d.remaining);ts.textContent=fmt(d.elapsed)+' elapsed';}
  else{bl.textContent='Elapsed';te.textContent=fmt(d.elapsed);ts.textContent=rm==='manual'?'Sounding while held':'';}

  if(d.tempF!=null)document.getElementById('tempF').textContent=d.tempF;
  if(d.uptime!=null)document.getElementById('uptime').textContent=fmtUp(d.uptime);
  if(d.currentTime)document.getElementById('clockTime').textContent=d.currentTime;

  const lk=!!d.btnLocked, btn=document.getElementById('btnLock');
  document.getElementById('lockShackle').setAttribute('d', lk ? 'M8 11V7a4 4 0 0 1 8 0v4' : 'M8 11V7a4 4 0 0 1 7.5-2');
  btn.classList.toggle('locked',lk);
  btn.title=lk?'Physical buttons LOCKED — click to unlock':'Lock physical buttons';
  btn.setAttribute('aria-label',btn.title);
  const pc=lk?'#EF4444':'#22C55E';
  document.getElementById('padShackle').setAttribute('d', lk ? 'M32 34.5v-3.5a4 4 0 0 1 8 0v3.5' : 'M32 34.5v-3.5a4 4 0 0 1 7.6-1.8');
  document.getElementById('padShackle').setAttribute('stroke',pc);
  document.getElementById('padBody').setAttribute('fill',pc);
  document.getElementById('padIcon').setAttribute('aria-label',lk?'Panel buttons locked':'Panel buttons unlocked');
  const lt=document.getElementById('lockTxt');
  lt.textContent=lk?'Locked — the panel buttons are ignored':'Unlocked — the panel buttons work';
  lt.classList.toggle('locked',lk);
  document.getElementById('lockAct').textContent=lk?'Unlock':'Lock';

  sirenActive=(st!=='idle');
  manOn=(rm==='manual');
  document.getElementById('bmLbl').textContent=manOn?'MANUAL ●':'MANUAL';
  document.getElementById('bm').classList.toggle('on',manOn);
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

// Hold-to-confirm (WP6): Wail/Attack/Fast-Wail/Stop only fire once the
// button has been held for the full duration — a bottom-up fill shows
// progress. Manual's own momentary-hold (manualDown/manualUp below) and
// the Test page's component buttons are untouched; /cmd itself doesn't
// change, this is purely a client-side gate before the same POST fires.
const HOLD_START_MS=1000, HOLD_STOP_MS=400;
const REDUCE_MOTION=window.matchMedia&&matchMedia('(prefers-reduced-motion: reduce)').matches;
function armHold(id,ms,onComplete){
  const el=document.getElementById(id);
  if(!el)return;
  const fill=el.querySelector('.hfill');
  let raf=null,startT=0;
  function frame(now){
    const pct=Math.min(1,(now-startT)/ms);
    if(fill&&!REDUCE_MOTION)fill.style.height=(pct*100)+'%';
    if(pct>=1){cancel();onComplete();return;}
    raf=requestAnimationFrame(frame);
  }
  function cancel(){if(raf)cancelAnimationFrame(raf);raf=null;if(fill)fill.style.height='0%';}
  function down(e){
    e.preventDefault();
    if(el.setPointerCapture)try{el.setPointerCapture(e.pointerId);}catch(err){}
    startT=performance.now();
    raf=requestAnimationFrame(frame);
  }
  el.addEventListener('pointerdown',down);
  el.addEventListener('pointerup',cancel);
  el.addEventListener('pointercancel',cancel);
  el.addEventListener('pointerleave',cancel);
}
armHold('bWail',     HOLD_START_MS, ()=>activate('wail'));
armHold('bAttack',   HOLD_START_MS, ()=>activate('attack'));
armHold('bFastWail', HOLD_START_MS, ()=>activate('fastwail'));
armHold('bStop',     HOLD_STOP_MS,  ()=>{claxon();cmd('stop');});

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
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@600;700&family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#0A0E13;--bar:#0D1219;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;
  --text:#E7ECF3;--soft:#C9D4E3;--muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--accent2:#0EA5E9;--green:#4ADE80;--red:#F87171;
  --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  --mono:"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Consolas,monospace;
  --disp:"Barlow Condensed","Arial Narrow",Inter,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:var(--text);font-family:var(--sans);min-height:100vh;display:flex;flex-direction:column}
a{color:var(--accent)}
.mono{font-family:var(--mono)}
.eyebrow{font-size:11px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.top{position:sticky;top:0;z-index:20;background:var(--bar);border-bottom:1px solid var(--line)}
.topin{max-width:1200px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.brand{display:flex;align-items:center;gap:12px;min-width:0}
.bt{display:flex;flex-direction:column;gap:2px;min-width:0}
.bt b{font-size:17px}
.nav{display:flex;gap:8px;align-items:center;flex-shrink:0}
.ibtn{width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;border-radius:10px;
      border:1px solid #222D3D;background:var(--panel);color:var(--soft);cursor:pointer;text-decoration:none;padding:0}
.ibtn:hover{background:#172030;color:#fff}
.ibtn.locked{color:var(--red);border-color:#7A2531}
.layout{flex:1;width:100%;max-width:1200px;margin:0 auto;padding:20px;display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
.side{flex:1 1 200px;max-width:240px;display:flex;flex-direction:column;gap:2px;position:sticky;top:88px}
.side .eyebrow{padding:16px 12px 8px}.side .eyebrow:first-child{padding-top:0}
.side a{display:flex;align-items:center;min-height:40px;padding:0 12px;border-radius:8px;color:var(--soft);text-decoration:none;font-size:14px}
.side a:hover{background:#151D29;color:#fff}
.content{flex:999 1 560px;min-width:0;display:flex;flex-direction:column;gap:20px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:24px;display:flex;flex-direction:column;gap:18px;scroll-margin-top:88px}
h2{font-size:18px;font-weight:700;display:flex;align-items:center;gap:10px}
.desc{margin-top:4px;font-size:14px;color:var(--muted);line-height:1.55}
.hint{font-size:13px;color:var(--muted);line-height:1.5}
label{display:block;font-size:13px;font-weight:600;color:var(--soft);margin-bottom:6px}
input,select{width:100%;min-height:44px;padding:0 14px;border-radius:10px;border:1px solid var(--line2);background:var(--inset);
      color:var(--text);font-family:var(--sans);font-size:15px}
input:focus,select:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(56,189,248,.2)}
input::placeholder{color:var(--faint)}
input[type=number]{font-family:var(--mono);text-align:right}
code{font-family:var(--mono);font-size:.92em;background:var(--inset);border:1px solid var(--line);border-radius:5px;padding:1px 5px}
.g2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}
.g3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px}
.inl{display:flex;flex-wrap:wrap;gap:14px;align-items:flex-end}.inl>div{flex:1 1 260px}
.sub{padding-top:18px;border-top:1px solid var(--line);display:flex;flex-direction:column;gap:14px}
.actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
.btn{min-height:44px;padding:0 18px;border-radius:10px;border:0;font-family:var(--sans);font-size:14px;font-weight:700;cursor:pointer}
.save{background:var(--accent2);color:#04131D}.save:hover{background:var(--accent)}
.logout{background:#172030;color:var(--text);border:1px solid var(--line2);font-weight:600}.logout:hover{background:#1D2838}
.danger{background:transparent;color:#FCA5A5;border:1px solid #7A2531;font-weight:600}.danger:hover{background:#22111A}
.msg{font-size:13px;min-height:1.1em}
.ok{color:var(--green)}.er{color:var(--red)}
.ws{display:flex;align-items:center;gap:10px;padding:12px 14px;border-radius:10px;background:var(--inset);border:1px solid var(--line);font-size:14px;color:var(--muted)}
.ws.ok{color:#86EFAC;border-color:#1F5236}.ws.er{color:#FCA5A5;border-color:#7A2531}
.trow{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 0;border-top:1px solid var(--line)}
.trow label{margin:0;font-size:14px;font-weight:400;color:var(--text);line-height:1.45}
input[type=checkbox]{appearance:none;-webkit-appearance:none;width:46px;min-height:0;height:28px;padding:0;border:0;border-radius:999px;
      background:#263142;position:relative;cursor:pointer;flex-shrink:0;transition:background .15s}
input[type=checkbox]::after{content:"";position:absolute;top:4px;left:4px;width:20px;height:20px;border-radius:50%;background:var(--muted);transition:left .15s}
input[type=checkbox]:checked{background:var(--accent2)}
input[type=checkbox]:checked::after{left:22px;background:#fff}
input[type=checkbox]:focus-visible{box-shadow:0 0 0 3px rgba(56,189,248,.35)}
.tblw{overflow-x:auto}
.tbl{display:grid;gap:10px 14px;align-items:center}
.seq{grid-template-columns:minmax(120px,1.3fr) repeat(2,minmax(110px,1fr));min-width:380px}
.modes{grid-template-columns:minmax(120px,1.3fr) repeat(3,minmax(90px,1fr));min-width:520px}
.th{font-size:11px;font-weight:600;letter-spacing:.1em;text-transform:uppercase;color:var(--muted)}
.th.r{text-align:right}
.mn{display:flex;align-items:center;gap:10px;font-weight:600}
.sw{width:10px;height:10px;border-radius:3px;flex-shrink:0}
.na{text-align:right;color:var(--faint)}
.stats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
.stat{padding:12px 14px;border-radius:10px;background:var(--inset);border:1px solid var(--line);display:flex;flex-direction:column;gap:4px}
.stat b{font-family:var(--mono);font-size:26px}
.ulog{display:grid;grid-template-columns:minmax(150px,1.4fr) minmax(90px,1fr) minmax(80px,.8fr) minmax(70px,.6fr) minmax(150px,1.4fr);min-width:620px;font-size:14px}
.ulog>span{padding:11px 8px 11px 0;border-top:1px solid var(--line)}
.ulog>.th{padding:8px 0;border-top:0}
.ulog .when{font-family:var(--mono);color:var(--soft)}
.ulog .len{font-family:var(--mono)}
.ulog .ok{color:#86EFAC}.ulog .bad{color:var(--red);font-weight:600}
.ulog .ua{grid-column:1/-1;border-top:0;padding:0 0 10px;font-size:12px;color:var(--muted);word-break:break-word}
.mstat{display:flex;flex-direction:column;border-radius:10px;background:var(--inset);border:1px solid var(--line)}
.mstat>div{display:flex;align-items:center;gap:10px;padding:11px 14px;font-size:14px;color:var(--soft)}
.mstat>div+div{border-top:1px solid var(--line)}
.mstat span:nth-child(2){flex:1}
.mstat b{font-family:var(--mono);font-size:13px;font-weight:500;color:var(--text);text-align:right}
.mstat .dot{width:8px;height:8px;border-radius:50%;background:#64748B;flex-shrink:0}
.mstat .dot.ok{background:var(--green)}
.fw{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:18px;border:1px dashed #2F3D52;border-radius:12px;background:var(--inset)}
.fw input[type=file]{position:absolute;width:1px;height:1px;opacity:0}
.fw label{margin:0;display:inline-flex;align-items:center}
#fwName{flex:1;min-width:160px;font-size:14px;color:var(--muted)}
.foot{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px 20px;padding:8px 4px 24px;font-family:var(--mono);font-size:12px;color:var(--muted)}
.foot a{color:var(--muted)}
.stopbar{position:sticky;bottom:0;z-index:20;width:100%;border:0;background:#FECACA;color:#3B0A0A;cursor:pointer;
         display:flex;align-items:center;justify-content:center;gap:14px;min-height:68px}
.stopbar:hover{background:#FDB4B4}
.stopbar b{font-family:var(--disp);font-size:28px;letter-spacing:.12em;line-height:1}
@media (max-width:800px){
  .side{position:static;max-width:none;flex-basis:100%;flex-direction:row;overflow-x:auto;gap:6px;padding-bottom:4px}
  .side .eyebrow{display:none}
  .side a{white-space:nowrap;border:1px solid var(--line);background:var(--panel);min-height:36px;font-size:13px}
}
@media (max-width:560px){.g2,.g3{grid-template-columns:1fr}.card{padding:18px}}
</style></head><body>
<header class="top"><div class="topin">
  <div class="brand">
    <a href="/" class="ibtn" title="Back" aria-label="Back to control panel">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
    </a>
    <div class="bt"><span class="eyebrow" style="font-size:10px">Hurricane Controls</span><b>Settings</b></div>
  </div>
  <nav class="nav" aria-label="Device actions">
    <button class="ibtn" id="btnLock" onclick="toggleLock()" title="Lock physical buttons" aria-label="Lock physical buttons">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="5" y="11" width="14" height="10" rx="2"/><path id="lockShackle" d="M8 11V7a4 4 0 0 1 7.5-2"/></svg>
    </button>
    <button class="ibtn" onclick="doRestart()" title="Restart" aria-label="Restart controller">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
    </button>
    <a href="/test" class="ibtn" title="Component Test" aria-label="Component test">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg>
    </a>
    <button class="ibtn" onclick="logout()" title="Log out" aria-label="Log out">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>
    </button>
  </nav>
</div></header>

<div class="layout">
<nav class="side" aria-label="Settings sections">
  <span class="eyebrow">Device</span>
  <a href="#network">Wi-Fi &amp; network</a>
  <a href="#security">Security</a>
  <a href="#time">Time</a>
  <a href="#statusbc">Status Broadcast</a>
  <span class="eyebrow">Integrations</span>
  <a href="#weather">Weather Watcher</a>
  <a href="#mesh">Meshtastic</a>
  <span class="eyebrow">Siren</span>
  <a href="#sequence">Start &amp; stop sequence</a>
  <a href="#modes">Mode timing</a>
  <a href="#buttons">Physical buttons</a>
  <span class="eyebrow">Maintenance</span>
  <a href="#usage">Usage log</a>
  <a href="#firmware">Firmware</a>
</nav>

<main class="content">

<section class="card" id="network">
  <div><h2>Wi-Fi &amp; network</h2><p class="desc">How this controller joins your network, and the fallback access point it broadcasts when it can't.</p></div>
  <div id="ws" class="ws">Checking&hellip;</div>
  <div class="inl">
    <div><label for="wHost">Device hostname</label><input type="text" id="wHost" placeholder="hurricane" maxlength="32" autocomplete="off"></div>
    <button class="btn save" onclick="saveHostname()">Save hostname</button>
  </div>
  <p class="hint" style="margin-top:-8px">Letters, numbers and hyphens. Also sets the <code>http://&lt;hostname&gt;.local</code> address. Applies after a restart.</p>
  <div class="msg" id="hm"></div>
  <div class="sub">
    <span class="eyebrow">Join a network</span>
    <div class="g2">
      <div><label for="wSSID">Network name (SSID)</label><input type="text" id="wSSID" placeholder="Your Wi-Fi name" autocomplete="off"></div>
      <div><label for="wPass">Password</label><input type="password" id="wPass" placeholder="Wi-Fi password" autocomplete="new-password"></div>
    </div>
    <div class="actions"><button class="btn save" onclick="saveWifi()">Connect to network</button><button class="btn danger" onclick="clearWifi()">Use access point only</button></div>
    <div class="msg" id="wm"></div>
  </div>
  <div class="sub">
    <span class="eyebrow">Fallback access point</span>
    <div class="inl">
      <div><label for="apPw">Access point password</label><input type="password" id="apPw" placeholder="8&ndash;63 characters" autocomplete="new-password"></div>
      <button class="btn save" onclick="saveApPw()">Save AP password</button>
    </div>
    <p class="hint" id="apHint" style="margin-top:-8px">Used to join the &quot;HurricaneControls&quot; network this device broadcasts when it isn't on your home Wi-Fi. Applies after a restart.</p>
    <div class="msg" id="apm"></div>
  </div>
</section>

<section class="card" id="security">
  <div><h2>Security</h2><p class="desc">The password that unlocks this control panel.</p></div>
  <div class="g2">
    <div><label for="p1">New password</label><input type="password" id="p1" autocomplete="new-password"></div>
    <div><label for="p2">Confirm password</label><input type="password" id="p2" autocomplete="new-password"></div>
  </div>
  <div class="actions"><button class="btn save" onclick="savePw()">Change password</button><button class="btn logout" onclick="logout()">Log out</button></div>
  <div class="msg" id="pm"></div>
</section>

<section class="card" id="time">
  <div><h2>Time</h2><p class="desc">A fallback only &mdash; the clock normally comes from the Weather Watcher over the UART link. These apply when it is offline or not yet paired.</p></div>
  <div class="g3">
    <div><label for="ntpServer">NTP server</label><input type="text" id="ntpServer" placeholder="pool.ntp.org"></div>
    <div><label for="ntpUpdateHours">Sync every (hours)</label><input type="number" id="ntpUpdateHours" min="1" step="1"></div>
    <div><label for="timeZone">Time zone</label>
      <select id="timeZone">
        <option value="EASTERN">Eastern</option>
        <option value="CENTRAL">Central</option>
        <option value="MOUNTAIN">Mountain</option>
        <option value="ARIZONA">Arizona (no DST)</option>
        <option value="PACIFIC">Pacific</option>
        <option value="ALASKA">Alaska</option>
        <option value="HAWAII">Hawaii (no DST)</option>
      </select></div>
  </div>
  <div class="trow"><label for="autoDst">Adjust for daylight saving time automatically</label><input type="checkbox" id="autoDst"></div>
  <div class="actions"><button class="btn save" onclick="saveTime()">Save time settings</button></div>
  <div class="msg" id="tmz"></div>
</section>

<section class="card" id="statusbc">
  <div><h2>Status Broadcast</h2><p class="desc">The periodic status line this board sends out, over both Meshtastic and to the Weather Watcher. Up to 3 times a day, and which data points it includes.</p></div>
  <div class="g3">
    <div><label for="statusTime1">Time 1</label><input type="time" id="statusTime1"></div>
    <div><label for="statusTime2">Time 2</label><input type="time" id="statusTime2"></div>
    <div><label for="statusTime3">Time 3 (optional)</label><input type="time" id="statusTime3"></div>
  </div>
  <div>
    <span class="eyebrow">Include in the status message</span>
    <div class="trow" style="margin-top:8px"><label for="statusIncludeMode">Mode / idle state</label><input type="checkbox" id="statusIncludeMode"></div>
    <div class="trow"><label for="statusIncludeLock">Local control lock state</label><input type="checkbox" id="statusIncludeLock"></div>
    <div class="trow"><label for="statusIncludeUptime">Uptime</label><input type="checkbox" id="statusIncludeUptime"></div>
    <div class="trow"><label for="statusIncludeCpuTemp">CPU temperature</label><input type="checkbox" id="statusIncludeCpuTemp"></div>
    <div class="trow"><label for="statusIncludeWwHealth">Weather Watcher health</label><input type="checkbox" id="statusIncludeWwHealth"></div>
    <div class="trow"><label for="statusIncludeWifi">Wi-Fi status</label><input type="checkbox" id="statusIncludeWifi"></div>
    <div class="trow"><label for="statusIncludeTime">Current time</label><input type="checkbox" id="statusIncludeTime"></div>
    <div class="trow"><label for="statusIncludeLastRun">Last activation</label><input type="checkbox" id="statusIncludeLastRun"></div>
    <div class="trow"><label for="statusIncludeAlerts">Current alerts (from the Weather Watcher)</label><input type="checkbox" id="statusIncludeAlerts"></div>
  </div>
  <p class="hint">This card only controls timing and content. Whether it's actually sent over each channel is set where that channel lives: <b>Boot announcement and periodic status</b> on the <a href="#mesh">Meshtastic</a> card below for the mesh line, and <b>Boot announcement</b>/<b>Scheduled status updates</b> under Push notifications on the Weather Watcher's own Settings page for the ntfy push.</p>
  <p class="hint">A <code>SIREN PING</code> over Meshtastic replies with these same fields, on demand &mdash; it always answers, regardless of the toggles above or the schedule.</p>
  <div class="actions"><button class="btn save" onclick="saveStatusBroadcast()">Save status broadcast settings</button></div>
  <div class="msg" id="sbcm"></div>
</section>

<section class="card" id="weather">
  <div><h2>Weather Watcher</h2><p class="desc">Lets the separately wired Weather Watcher board start the siren when the National Weather Service issues a qualifying warning. It never overrides TEST MODE. See docs/weather-watcher.md.</p></div>
  <div id="wwStatus" class="ws">Checking&hellip;</div>
  <div class="trow"><label for="wxAutoTrig"><b>Automatic weather-triggered activation</b><br><span class="hint">Saves as soon as you flip it.</span></label><input type="checkbox" id="wxAutoTrig" onchange="saveWeatherAutoTrigger()"></div>
  <div class="msg" id="wxm"></div>
</section>

<section class="card" id="mesh">
  <div><h2><svg width="24" height="24" viewBox="0 0 512 512" aria-hidden="true" style="flex-shrink:0"><rect width="512" height="512" rx="64" fill="#67EA94"/><g transform="matrix(1.79 0 0 1.79 313.74 258.36)"><path fill="#2C2D3C" fill-rule="evenodd" transform="translate(-250.97,-362.41)" d="M250.908 330.267L193.126 415.005L180.938 406.694L244.802 313.037C246.174 311.024 248.453 309.819 250.889 309.816C253.326 309.814 255.606 311.015 256.982 313.026L320.994 406.536L308.821 414.869L250.908 330.267Z"/></g><g transform="matrix(1.81 0 0 1.81 145 256.15)"><path fill="#2C2D3C" fill-rule="evenodd" transform="translate(-115.14,-528.06)" d="M87.642 581.398L154.757 482.977L142.638 474.713L75.523 573.134L87.642 581.398Z"/></g></svg>Meshtastic</h2>
    <p class="desc">Who may send siren commands over the mesh, and which events this controller announces on its own.</p></div>
  <div class="mstat">
    <div><span class="dot" id="meshDot"></span><span>Last heard from node</span><b id="meshRx">&mdash;</b></div>
    <div><span class="dot" id="pingDot"></span><span>Last <code>SIREN PING</code> answered</span><b id="meshPing">&mdash;</b></div>
    <div><span class="dot" id="txDot"></span><span>Last message sent</span><b id="meshTx">&mdash;</b></div>
  </div>
  <p class="hint" style="margin-top:-8px">The node can't be queried directly, so these only update when something passes through it. A quiet channel isn't a fault &mdash; send <code>SIREN PING</code> from another node to confirm the whole path.</p>
  <div class="g2">
    <div><label for="meshWL">Allowed sender short names</label><input type="text" id="meshWL" class="mono" placeholder="e.g. 3A3C, VSR"><p class="hint" style="margin-top:6px">Comma-separated, matching each node's Meshtastic short name (not its node ID). Leave empty to block everyone.</p></div>
    <div><label for="meshPW">Command password (optional)</label><input type="text" id="meshPW" placeholder="No password"><p class="hint" style="margin-top:6px">Sent in plain text over the mesh.</p></div>
  </div>
  <div class="trow" style="margin-top:8px"><label for="meshWhitelistEnabled">Enforce sender whitelist</label><input type="checkbox" id="meshWhitelistEnabled" onchange="refreshMeshWLWarn()"></div>
  <p class="hint" id="meshWLWarn" style="display:none;color:var(--red)">&#9888; Whitelist disabled &mdash; <b>any</b> node on this Meshtastic channel can control the siren with a correctly-addressed, correctly-passworded command, regardless of the sender list above. This is less secure; turn it back on unless you specifically mean to accept commands from any node.</p>
  <div>
    <span class="eyebrow">Broadcast over mesh</span>
    <div class="trow" style="margin-top:8px"><label for="meshNotifyActivation">Activations and stops</label><input type="checkbox" id="meshNotifyActivation"></div>
    <div class="trow"><label for="meshNotifySystemEvents">Lockout or weather trigger toggled, locked button pressed</label><input type="checkbox" id="meshNotifySystemEvents"></div>
    <div class="trow"><label for="meshNotifyWwHealth">Weather Watcher health (lost, recovered, error)</label><input type="checkbox" id="meshNotifyWwHealth"></div>
    <div class="trow"><label for="meshNotifyStatus">Boot announcement and periodic status</label><input type="checkbox" id="meshNotifyStatus"></div>
    <div class="trow"><label for="meshNotifyWatches">A new Tornado/Severe T-storm Watch appears (display-only, never triggers)</label><input type="checkbox" id="meshNotifyWatches"></div>
    <p class="hint">A direct <code>SIREN PING</code> always gets a reply &mdash; these only control unprompted broadcasts.</p>
  </div>
  <div class="actions"><button class="btn save" onclick="saveMeshWL()">Save mesh settings</button></div>
  <div class="msg" id="mwlm"></div>
</section>

<section class="card" id="sequence">
  <div><h2>Start &amp; stop sequence</h2><p class="desc">How long after a start or stop command each component switches. Seconds.</p></div>
  <div class="tblw"><div class="tbl seq">
    <span class="th">Component</span><span class="th r">Start delay</span><span class="th r">Stop delay</span>
    <span class="mn">Chopper</span><input type="number" id="s_chopperDelay" min="0" step="0.1" aria-label="Chopper start delay"><input type="number" id="s_stopChopperDelay" min="0" step="0.1" aria-label="Chopper stop delay">
    <span class="mn">Blower</span><input type="number" id="s_blowerDelay" min="0" step="0.1" aria-label="Blower start delay"><input type="number" id="s_stopBlowerDelay" min="0" step="0.1" aria-label="Blower stop delay">
    <span class="mn">Rotator</span><input type="number" id="s_rotatorDelay" min="0" step="0.1" aria-label="Rotator start delay"><input type="number" id="s_stopRotDelay" min="0" step="0.1" aria-label="Rotator stop delay">
  </div></div>
  <div class="actions"><button class="btn save" onclick="saveTiming()">Save sequence</button></div>
  <div class="msg" id="tmStart"></div>
</section>

<section class="card" id="modes">
  <div><h2>Mode timing</h2><p class="desc">Run length for each mode, and the chopper on/off cycle for the pulsing modes. Seconds.</p></div>
  <div class="tblw"><div class="tbl modes">
    <span class="th">Mode</span><span class="th r">Duration</span><span class="th r">Chopper on</span><span class="th r">Chopper off</span>
    <span class="mn"><span class="sw" style="background:#1D4ED8"></span>Wail</span><input type="number" id="s_wailDuration" min="1" step="1" aria-label="Wail duration"><span class="na">&mdash;</span><span class="na">&mdash;</span>
    <span class="mn"><span class="sw" style="background:#B91C1C"></span>Attack</span><input type="number" id="s_attackDuration" min="1" step="1" aria-label="Attack duration"><input type="number" id="s_attackOnTime" min="0.5" step="0.1" aria-label="Attack on time"><input type="number" id="s_attackOffTime" min="0.5" step="0.1" aria-label="Attack off time">
    <span class="mn"><span class="sw" style="background:#A84A0A"></span>Fast Wail</span><input type="number" id="s_fastWailDuration" min="1" step="1" aria-label="Fast Wail duration"><input type="number" id="s_fastWailOnTime" min="0.5" step="0.1" aria-label="Fast Wail on time"><input type="number" id="s_fastWailOffTime" min="0.5" step="0.1" aria-label="Fast Wail off time">
  </div></div>
  <div class="sub">
    <div><span class="eyebrow">Growl test</span><p class="hint" style="margin-top:4px">Each component runs alone, in this order, for these times.</p></div>
    <div class="g3">
      <div><label for="s_growlBlowerTime">1 &middot; Blower</label><input type="number" id="s_growlBlowerTime" min="0.5" step="0.1"></div>
      <div><label for="s_growlRotatorTime">2 &middot; Rotator</label><input type="number" id="s_growlRotatorTime" min="0.5" step="0.1"></div>
      <div><label for="s_growlChopperTime">3 &middot; Chopper</label><input type="number" id="s_growlChopperTime" min="0.5" step="0.1"></div>
    </div>
  </div>
  <div class="actions"><button class="btn save" onclick="saveTiming()">Save mode timing</button></div>
  <div class="msg" id="tm2"></div>
</section>

<section class="card" id="buttons">
  <div><h2>Physical buttons</h2><p class="desc">How the panel buttons on the controller respond. A long press turns Wail into Manual and Attack into Fast Wail.</p></div>
  <div class="g2">
    <div><label for="s_longPressMs">Long-press threshold (sec)</label><input type="number" id="s_longPressMs" min="0.1" step="0.1"></div>
    <div><label for="s_buttonDebounceMs">Debounce (sec)</label><input type="number" id="s_buttonDebounceMs" min="0" step="0.01"></div>
  </div>
  <div class="actions"><button class="btn save" onclick="saveTiming()">Save button timing</button></div>
  <div class="msg" id="tm"></div>
</section>

<section class="card" id="usage">
  <div style="display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:flex-start">
    <div><h2>Usage log</h2><p class="desc">The last 20 runs. Totals and the full history survive a restart.</p></div>
    <a class="btn logout" href="/log.csv" style="display:inline-flex;align-items:center;gap:8px;text-decoration:none">
      <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>Export CSV</a>
  </div>
  <div class="stats">
    <div class="stat"><span class="eyebrow">Total runs</span><b id="usageRuns">&mdash;</b></div>
    <div class="stat"><span class="eyebrow">Motor hours</span><b id="usageHours">&mdash;</b></div>
  </div>
  <div class="tblw" id="usageList"><p class="hint">Loading&hellip;</p></div>
</section>

<section class="card" id="firmware">
  <div><h2>Firmware</h2><p class="desc">Upload a new <code>firmware.bin</code> from <code>.pio/build/esp32dev/</code>. Only allowed while the siren is idle and TEST MODE is off. The controller restarts when it finishes.</p></div>
  <div class="fw">
    <label for="fwFile" class="btn logout">Choose .bin file</label>
    <input type="file" id="fwFile" accept=".bin" onchange="document.getElementById('fwName').textContent=this.files[0]?this.files[0].name:'No file chosen'">
    <span id="fwName">No file chosen</span>
    <button class="btn save" onclick="uploadFw()">Upload firmware</button>
  </div>
  <div class="msg" id="fwm"></div>
</section>

<footer class="foot">
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Hurricane Controls &middot; v<span id="verNum">&mdash;</span></a>
  <span>Last restart: <span id="rstReason">&mdash;</span></span>
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a>
</footer>
</main>
</div>

<button class="stopbar" onclick="claxon();cmd('stop')">
  <svg width="20" height="20" viewBox="0 0 22 22" aria-hidden="true"><rect x="2" y="2" width="18" height="18" rx="3" fill="#B91C1C"/></svg><b>STOP</b>
</button>

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
  document.getElementById('meshWhitelistEnabled').checked=d.meshWhitelistEnabled!==false;
  refreshMeshWLWarn();
  document.getElementById('meshNotifyActivation').checked=d.meshNotifyActivation!==false;
  document.getElementById('meshNotifySystemEvents').checked=d.meshNotifySystemEvents!==false;
  document.getElementById('meshNotifyWwHealth').checked=d.meshNotifyWwHealth!==false;
  document.getElementById('meshNotifyStatus').checked=d.meshNotifyStatus!==false;
  document.getElementById('meshNotifyWatches').checked=d.meshNotifyWatches!==false;
  document.getElementById('ntpServer').value=d.ntpServer||'pool.ntp.org';
  document.getElementById('ntpUpdateHours').value=d.ntpUpdateHours||12;
  document.getElementById('timeZone').value=d.timeZone||'EASTERN';
  document.getElementById('autoDst').checked=d.autoDst!==false;
  document.getElementById('statusTime1').value=d.statusTime1||'';
  document.getElementById('statusTime2').value=d.statusTime2||'';
  document.getElementById('statusTime3').value=d.statusTime3||'';
  document.getElementById('statusIncludeMode').checked=d.statusIncludeMode!==false;
  document.getElementById('statusIncludeLock').checked=d.statusIncludeLock!==false;
  document.getElementById('statusIncludeUptime').checked=d.statusIncludeUptime!==false;
  document.getElementById('statusIncludeCpuTemp').checked=d.statusIncludeCpuTemp!==false;
  document.getElementById('statusIncludeWwHealth').checked=d.statusIncludeWwHealth!==false;
  document.getElementById('statusIncludeWifi').checked=!!d.statusIncludeWifi;
  document.getElementById('statusIncludeTime').checked=!!d.statusIncludeTime;
  document.getElementById('statusIncludeLastRun').checked=!!d.statusIncludeLastRun;
  document.getElementById('statusIncludeAlerts').checked=!!d.statusIncludeAlerts;
  document.getElementById('wxAutoTrig').checked=!!d.weatherAutoTriggerEnabled;
  const wwEl=document.getElementById('wwStatus');
  if(!d.weatherAutoTriggerEnabled){wwEl.textContent='Disabled';wwEl.className='ws';}
  else if(!d.wwEverReported){wwEl.textContent='No status received yet from the Weather Watcher board';wwEl.className='ws';}
  else if(d.wwLost){wwEl.textContent='Lost — last heard '+Math.floor(d.wwLastHeardSec/60)+'m '+(d.wwLastHeardSec%60)+'s ago';wwEl.className='ws er';}
  else if(d.wwPending){wwEl.textContent='Starting up — waiting for its first NWS poll';wwEl.className='ws';}
  else if(d.wwOk){wwEl.textContent='OK — '+d.wwDetail+' (last heard '+d.wwLastHeardSec+'s ago)';wwEl.className='ws ok';}
  else{wwEl.textContent='Error — '+d.wwDetail+' (last heard '+d.wwLastHeardSec+'s ago)';wwEl.className='ws er';}
});

const MESH_FRESH_SEC=6*3600; // heard within this long = green dot
function ago(s){
  if(s<60)return s+'s ago';
  if(s<3600)return Math.floor(s/60)+'m ago';
  if(s<86400)return Math.floor(s/3600)+'h ago';
  return Math.floor(s/86400)+'d ago';
}
function drawMesh(m){
  if(!m)return;
  const set=(id,dot,s,extra)=>{
    document.getElementById(id).textContent=s<0?'not since restart':ago(s)+(extra||'');
    document.getElementById(dot).className='dot'+(s>=0&&s<MESH_FRESH_SEC?' ok':'');
  };
  set('meshRx','meshDot',m.rx,m.from?' ('+m.from+')':'');
  set('meshPing','pingDot',m.ping);
  set('meshTx','txDot',m.tx);
}
function refreshLockIcon(){
  fetch('/status').then(r=>r.json()).then(d=>{
    drawMesh(d.health&&d.health.mesh);
    const bl=document.getElementById('btnLock');
    const lk=!!d.btnLocked;
    document.getElementById('lockShackle').setAttribute('d', lk ? 'M8 11V7a4 4 0 0 1 8 0v4' : 'M8 11V7a4 4 0 0 1 7.5-2');
    bl.classList.toggle('locked',lk);
    bl.title=lk?'Physical buttons LOCKED — click to unlock':'Lock physical buttons';
    bl.setAttribute('aria-label',bl.title);
  });
}
refreshLockIcon();
setInterval(refreshLockIcon,10000);
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
  const wifiIcon='<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><line x1="12" y1="20" x2="12.01" y2="20"/></svg>';
  if(d.connected){el.innerHTML=wifiIcon+'<span>Connected to <b></b> <span class="mono"></span></span>';
    el.querySelector('b').textContent=d.ssid;el.querySelector('.mono').textContent='('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
  document.getElementById('wHost').value=d.hostname||'hurricane';
  if(!d.apSecured)document.getElementById('apHint').textContent+=' The access point is currently OPEN (no password).';
});
loadLog();

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
function saveStatusBroadcast(){
  const b={
    statusTime1: document.getElementById('statusTime1').value,
    statusTime2: document.getElementById('statusTime2').value,
    statusTime3: document.getElementById('statusTime3').value,
    statusIncludeMode: document.getElementById('statusIncludeMode').checked,
    statusIncludeLock: document.getElementById('statusIncludeLock').checked,
    statusIncludeUptime: document.getElementById('statusIncludeUptime').checked,
    statusIncludeCpuTemp: document.getElementById('statusIncludeCpuTemp').checked,
    statusIncludeWwHealth: document.getElementById('statusIncludeWwHealth').checked,
    statusIncludeWifi: document.getElementById('statusIncludeWifi').checked,
    statusIncludeTime: document.getElementById('statusIncludeTime').checked,
    statusIncludeLastRun: document.getElementById('statusIncludeLastRun').checked,
    statusIncludeAlerts: document.getElementById('statusIncludeAlerts').checked,
  };
  post('/settings-data',b).then(d=>msg('sbcm',d.ok?'Saved!':'Error',d.ok));
}
function refreshMeshWLWarn(){
  document.getElementById('meshWLWarn').style.display=
    document.getElementById('meshWhitelistEnabled').checked?'none':'block';
}
function saveMeshWL(){
  const wl=document.getElementById('meshWL').value.trim();
  const pw=document.getElementById('meshPW').value.trim();
  post('/settings-data',{
    meshWhitelist:wl,
    meshWhitelistEnabled: document.getElementById('meshWhitelistEnabled').checked,
    meshPassword:pw,
    meshNotifyActivation: document.getElementById('meshNotifyActivation').checked,
    meshNotifySystemEvents: document.getElementById('meshNotifySystemEvents').checked,
    meshNotifyWwHealth: document.getElementById('meshNotifyWwHealth').checked,
    meshNotifyStatus: document.getElementById('meshNotifyStatus').checked,
    meshNotifyWatches: document.getElementById('meshNotifyWatches').checked,
  }).then(d=>msg('mwlm',d.ok?'Saved!':'Error',d.ok));
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
  // report to the save-msg div in the submitting card
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
    document.getElementById('usageRuns').textContent=d.runs;
    document.getElementById('usageHours').textContent=(d.secs/3600).toFixed(1);
    const L=document.getElementById('usageList');L.innerHTML='';
    if(!d.entries.length){const p=document.createElement('p');p.className='hint';p.textContent='No runs since the last restart.';L.appendChild(p);return;}
    const g=document.createElement('div');g.className='ulog';
    const cell=(txt,cls)=>{const s=document.createElement('span');if(cls)s.className=cls;s.textContent=txt;g.appendChild(s);return s;};
    ['When','Mode','Source','Length','Ended'].forEach(h=>cell(h,'th'));
    d.entries.forEach(e=>{
      const when=e.e?new Date(e.e*1000).toLocaleString():fmtAgo(d.uptime-e.u);
      const end=e.x===0?'completed':e.x===2?'failsafe stop':e.x===3?'interrupted (power loss)':'stopped ('+LS[e.ss]+')';
      cell(when,'when');cell(LM[e.m]||'?');cell(LS[e.s]||'?');cell(fmtDur(e.d),'len');
      cell(end,e.x===0?'ok':e.x===3?'bad':'');
      if(e.at)cell(e.at+(e.ai?' (ID: '+e.ai+')':''),'ua');
    });
    L.appendChild(g);
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
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@600;700&family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#0A0E13;--bar:#0D1219;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;
  --text:#E7ECF3;--soft:#C9D4E3;--muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--green:#4ADE80;--red:#F87171;
  --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  --mono:"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Consolas,monospace;
  --disp:"Barlow Condensed","Arial Narrow",Inter,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:var(--text);font-family:var(--sans);min-height:100vh;display:flex;flex-direction:column}
.mono{font-family:var(--mono)}
.eyebrow{font-size:11px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.top{position:sticky;top:0;z-index:20;background:var(--bar);border-bottom:1px solid var(--line)}
.topin{max-width:1200px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;gap:14px}
.bt{display:flex;flex-direction:column;gap:2px}.bt b{font-size:17px}
.ibtn{width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;border-radius:10px;
      border:1px solid #222D3D;background:var(--panel);color:var(--soft);text-decoration:none}
.ibtn:hover{background:#172030;color:#fff}
main{flex:1;width:100%;max-width:1200px;margin:0 auto;padding:20px;display:flex;flex-direction:column;gap:20px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:16px}
.hint{font-size:14px;color:var(--muted);line-height:1.5}
/* Test-mode banner */
.tm{padding:22px 24px;display:flex;align-items:center;justify-content:space-between;gap:20px;flex-wrap:wrap;transition:background .3s,border-color .3s}
.tm.on{background:#22111A;border-color:#7A2531}
.tmtext{display:flex;gap:16px;align-items:flex-start;flex:1 1 420px;min-width:0}
.tmtext svg{flex-shrink:0;margin-top:2px;color:var(--muted)}
.tm.on .tmtext svg{color:var(--red)}
.tmtext b{font-size:18px;display:block;margin-bottom:6px}
.tmtext p{font-size:14px;color:#B6C2D3;line-height:1.55}
.cd{display:none;flex-direction:column;align-items:center;gap:6px;padding:12px 22px;border-radius:12px;background:#160B10;border:1px solid #7A2531;min-width:170px}
.cd.show{display:flex}
.cd .eyebrow{color:#FCA5A5}
#tmLeft{font-family:var(--mono);font-size:52px;font-weight:700;line-height:1;font-variant-numeric:tabular-nums;color:#fff}
.bar{width:100%;height:6px;border-radius:999px;background:#3A1820;overflow:hidden}
#tmBar{display:block;height:100%;border-radius:999px;background:#EF4444;width:100%;transition:width 1s linear}
.ttoggle{min-height:52px;min-width:200px;padding:0 22px;border-radius:12px;font-family:var(--sans);font-weight:700;font-size:15px;
         letter-spacing:.04em;cursor:pointer;border:1px solid var(--line2);background:#172030;color:var(--text)}
.ttoggle.on{border:0;background:#B91C1C;color:#fff}
.cols{display:flex;flex-wrap:wrap;gap:20px;align-items:stretch}
.relaycard{flex:3 1 560px;min-width:0;padding:24px;display:flex;flex-direction:column;gap:18px}
.growlcard{flex:2 1 380px;min-width:0;padding:24px;display:flex;flex-direction:column;gap:18px}
.rgrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}
.ttest{border-radius:14px;min-height:150px;padding:18px;display:flex;flex-direction:column;align-items:flex-start;justify-content:space-between;gap:16px;
       cursor:pointer;font-family:var(--sans);color:var(--text);text-align:left;background:var(--inset);border:1px solid var(--line);
       user-select:none;-webkit-user-select:none;touch-action:none;transition:background .15s,border-color .15s}
.ttest:hover:not(:disabled){filter:brightness(1.15)}
.ttest.on{background:#10261B;border-color:#2F7A4D}
.ttest:disabled{opacity:.45;cursor:not-allowed}
.lrow{display:flex;align-items:center;gap:10px}
.dot{width:16px;height:16px;border-radius:50%;background:#263142;transition:background .3s,box-shadow .3s}
.dot.on{background:var(--green);box-shadow:0 0 12px rgba(74,222,128,.75)}
.st{font-family:var(--mono);font-size:13px;color:var(--faint)}
.dot.on+.st{color:#86EFAC}
.tl{font-family:var(--disp);font-size:32px;font-weight:700;letter-spacing:.05em;line-height:1}
.tc{font-size:13px;color:var(--muted)}
.msg{font-size:13px;min-height:18px;display:flex;align-items:center;gap:6px;color:var(--faint)}
.msg.ok{color:#86EFAC}.msg.er{color:var(--red)}
ol{list-style:none;display:flex;flex-direction:column;gap:8px}
li{display:flex;align-items:center;gap:12px;padding:12px 14px;border-radius:10px;background:var(--inset);border:1px solid var(--line)}
.num{width:26px;height:26px;border-radius:50%;background:#0F3B33;color:#6EE7C8;display:inline-flex;align-items:center;justify-content:center;font-family:var(--mono);font-size:12px;font-weight:700}
li b{flex:1}
.gbtn{min-height:56px;border:0;border-radius:12px;background:#0F766E;color:#fff;font-family:var(--disp);font-size:24px;font-weight:700;letter-spacing:.08em;cursor:pointer}
.gbtn:hover{background:#13887F}
.stopbar{position:sticky;bottom:0;z-index:20;width:100%;border:0;background:#FECACA;color:#3B0A0A;cursor:pointer;
         display:flex;align-items:center;justify-content:center;gap:14px;min-height:76px}
.stopbar:hover{background:#FDB4B4}
.stopbar b{font-family:var(--disp);font-size:30px;letter-spacing:.12em;line-height:1}
@media (max-width:560px){.rgrid{gap:8px}.ttest{min-height:120px;padding:12px}.tl{font-size:24px}.tm,.relaycard,.growlcard{padding:18px}}
</style></head><body>
<header class="top"><div class="topin">
  <a href="/settings" class="ibtn" title="Back" aria-label="Back to settings">
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <div class="bt"><span class="eyebrow" style="font-size:10px">Hurricane Controls</span><b>Component Test</b></div>
</div></header>

<main>
<section class="panel tm" id="tmPanel" aria-label="Test mode">
  <div class="tmtext">
    <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>
    <div><b id="tmWord">Test mode is OFF</b>
      <p>Skips every startup and shutdown delay so each relay can be checked on its own. Only works while the siren is idle. While it is on, the software STOP is disabled &mdash; rely on the hardware emergency stop.</p></div>
  </div>
  <div class="cd" id="cd" role="timer" aria-live="off">
    <span class="eyebrow">Auto-off in</span>
    <span id="tmLeft">3:00</span>
    <span class="bar"><span id="tmBar"></span></span>
  </div>
  <button class="ttoggle" id="testModeBtn" onclick="toggleTestMode()" aria-pressed="false">TURN TEST MODE ON</button>
</section>

<div class="cols">
  <section class="panel relaycard" aria-label="Relay test">
    <div><span class="eyebrow">Relay test</span><p class="hint" style="margin-top:6px">Press and hold a relay to energize it. It switches off the moment you let go.</p></div>
    <div class="rgrid">
      <button class="ttest" id="tChopper" disabled
        onmousedown="testDown(event,'chopper')" onmouseup="testUp(event,'chopper')" onmouseleave="testUp(event,'chopper')"
        ontouchstart="testDown(event,'chopper')" ontouchend="testUp(event,'chopper')" ontouchcancel="testUp(event,'chopper')">
        <span class="lrow"><span class="dot" id="d0"></span><span class="st" id="s0">OFF</span></span>
        <span><span class="tl">CHOPPER</span><br><span class="tc">Hold to energize</span></span></button>
      <button class="ttest" id="tBlower" disabled
        onmousedown="testDown(event,'blower')" onmouseup="testUp(event,'blower')" onmouseleave="testUp(event,'blower')"
        ontouchstart="testDown(event,'blower')" ontouchend="testUp(event,'blower')" ontouchcancel="testUp(event,'blower')">
        <span class="lrow"><span class="dot" id="d1"></span><span class="st" id="s1">OFF</span></span>
        <span><span class="tl">BLOWER</span><br><span class="tc">Hold to energize</span></span></button>
      <button class="ttest" id="tRotator" disabled
        onmousedown="testDown(event,'rotator')" onmouseup="testUp(event,'rotator')" onmouseleave="testUp(event,'rotator')"
        ontouchstart="testDown(event,'rotator')" ontouchend="testUp(event,'rotator')" ontouchcancel="testUp(event,'rotator')">
        <span class="lrow"><span class="dot" id="d2"></span><span class="st" id="s2">OFF</span></span>
        <span><span class="tl">ROTATOR</span><br><span class="tc">Hold to energize</span></span></button>
    </div>
    <div class="msg" id="tmsg"></div>
  </section>

  <section class="panel growlcard" aria-label="Growl test">
    <div><span class="eyebrow">Growl test</span><p class="hint" style="margin-top:6px">Runs each component by itself, one after another, for the times set under Settings &rsaquo; Mode timing. Needs the siren idle and TEST MODE off.</p></div>
    <ol>
      <li><span class="num">1</span><b>Blower</b><span class="mono" id="g1" style="font-size:13px;color:var(--muted)">&mdash;</span></li>
      <li><span class="num">2</span><b>Rotator</b><span class="mono" id="g2" style="font-size:13px;color:var(--muted)">&mdash;</span></li>
      <li><span class="num">3</span><b>Chopper</b><span class="mono" id="g3" style="font-size:13px;color:var(--muted)">&mdash;</span></li>
    </ol>
    <button class="gbtn" onclick="claxon();cmd('growl')">START GROWL TEST</button>
  </section>
</div>
</main>

<button class="stopbar" onclick="claxon();cmd('stop')">
  <svg width="22" height="22" viewBox="0 0 22 22" aria-hidden="true"><rect x="2" y="2" width="18" height="18" rx="3" fill="#B91C1C"/></svg><b>STOP</b>
</button>

<script>
let held={chopper:false,blower:false,rotator:false}, kaT={};
const warnIcon='<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>';
// Matches TEST_LOCK_TIMEOUT_MS in buttons.h — only used to scale the bar.
const TEST_TIMEOUT_SEC=180;

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

fetch('/settings-data').then(r=>r.json()).then(d=>{
  const s=v=>(v/1000).toFixed(1)+' s';
  document.getElementById('g1').textContent=s(d.growlBlowerTime);
  document.getElementById('g2').textContent=s(d.growlRotatorTime);
  document.getElementById('g3').textContent=s(d.growlChopperTime);
}).catch(()=>{});

function drawDots(d){
  const r=d.relays||[0,0,0];
  [0,1,2].forEach(i=>{
    document.getElementById('d'+i).classList.toggle('on',!!r[i]);
    document.getElementById('s'+i).textContent=r[i]?'ENERGIZED':'OFF';
  });
  const btn=document.getElementById('testModeBtn');
  const on=!!d.testMode;
  btn.classList.toggle('on',on);
  btn.setAttribute('aria-pressed',on?'true':'false');
  btn.textContent=on?'TURN TEST MODE OFF':'TURN TEST MODE ON';
  document.getElementById('tmPanel').classList.toggle('on',on);
  document.getElementById('tmWord').textContent='Test mode is '+(on?'ON':'OFF');
  const cd=document.getElementById('cd');
  if(on && d.lockAutoExpire){
    const rem=d.lockRemaining||0, m=Math.floor(rem/60), sec=rem%60;
    document.getElementById('tmLeft').textContent=m+':'+String(sec).padStart(2,'0');
    document.getElementById('tmBar').style.width=Math.min(100,rem/TEST_TIMEOUT_SEC*100)+'%';
    cd.setAttribute('aria-label','Test mode turns off automatically in '+m+' minutes '+sec+' seconds');
    cd.classList.add('show');
  } else {
    cd.classList.remove('show');
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
    static constexpr size_t   STATUS_JSON_MAX = 640; // grown for the health{} object (WP5) and its mesh{} timestamps
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

        // Active-warning banner data (WP7) — polled separately from /status
        // at a much slower interval, since alerts don't change every 500ms
        // the way relay state does.
        server_.on("/alerts", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            req->send(200, "application/json", buildAlertsJson());
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

        // Full persisted run-log export (auth required) — streams the whole
        // LittleFS-backed history, not just the RAM cache's last 20 entries.
        server_.on("/log.csv", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            AsyncResponseStream* r = req->beginResponseStream("text/csv");
            r->addHeader("Content-Disposition", "attachment; filename=runlog.csv");
            writeLogCsv(*r);
            req->send(r);
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
                    applyBool(doc, "meshWhitelistEnabled", s.meshWhitelistEnabled);
                    applyString(doc, "meshPassword", s.meshPassword, sizeof(s.meshPassword));
                    applyBool(doc, "weatherAutoTriggerEnabled", s.weatherAutoTriggerEnabled);
                    applyBool(doc, "meshNotifyActivation", s.meshNotifyActivation);
                    applyBool(doc, "meshNotifySystemEvents", s.meshNotifySystemEvents);
                    applyBool(doc, "meshNotifyWwHealth", s.meshNotifyWwHealth);
                    applyBool(doc, "meshNotifyStatus", s.meshNotifyStatus);
                    applyBool(doc, "meshNotifyWatches", s.meshNotifyWatches);
                    applyString(doc, "ntpServer", s.ntpServer, sizeof(s.ntpServer));
                    applyUInt(doc, "ntpUpdateHours", s.ntpUpdateHours, 1, 168);
                    applyString(doc, "timeZone", s.timeZone, sizeof(s.timeZone));
                    applyBool(doc, "autoDst", s.autoDst);
                    applyTimeOfDay(doc, "statusTime1", s.statusTime1, sizeof(s.statusTime1));
                    applyTimeOfDay(doc, "statusTime2", s.statusTime2, sizeof(s.statusTime2));
                    applyTimeOfDay(doc, "statusTime3", s.statusTime3, sizeof(s.statusTime3));
                    applyBool(doc, "statusIncludeMode",     s.statusIncludeMode);
                    applyBool(doc, "statusIncludeLock",     s.statusIncludeLock);
                    applyBool(doc, "statusIncludeUptime",   s.statusIncludeUptime);
                    applyBool(doc, "statusIncludeCpuTemp",  s.statusIncludeCpuTemp);
                    applyBool(doc, "statusIncludeWwHealth", s.statusIncludeWwHealth);
                    applyBool(doc, "statusIncludeWifi",     s.statusIncludeWifi);
                    applyBool(doc, "statusIncludeTime",     s.statusIncludeTime);
                    applyBool(doc, "statusIncludeLastRun",  s.statusIncludeLastRun);
                    applyBool(doc, "statusIncludeAlerts",   s.statusIncludeAlerts);
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

    // A Status Broadcast time slot: "HH:MM" (24h) or empty to clear/unuse
    // the slot. Anything else is silently rejected (field unchanged) —
    // mesh.h's statusTimeMatches() would just never match a malformed
    // string anyway, but there's no reason to store garbage.
    static void applyTimeOfDay(JsonDocument& doc, const char* key, char* field, size_t fieldSize) {
        if (!doc[key].is<const char*>()) return;
        const char* v = doc[key].as<const char*>();
        if (!v[0]) { field[0] = '\0'; return; }
        int h, m, n;
        if (sscanf(v, "%d:%d%n", &h, &m, &n) == 2 && v[n] == '\0' &&
            h >= 0 && h <= 23 && m >= 0 && m <= 59) {
            strlcpy(field, v, fieldSize);
        }
    }

    static const char* tf(bool b) { return b ? "true" : "false"; }

    // Seconds since a millis() timestamp, or -1 for "never" (0).
    static long secsSince(uint32_t ms) { return ms ? (long)((millis() - ms) / 1000) : -1L; }

    // Pushed to every WebSocket client twice a second — formatted on the
    // stack rather than via JsonDocument/String to avoid that heap churn.
    // Every value is a fixed token or a number, so nothing needs escaping.
    // Same precedence/words as mesh.h's weatherWatcherWord() — duplicated
    // rather than exposed from MeshBridge, since it's a one-line read-only
    // classification over fields webserver.h already has access to, and
    // pulling in mesh.h here just for this would be a bigger coupling.
    static const char* wwHealthWord() {
        if (!settingsMgr.s.weatherAutoTriggerEnabled) return "OFF";
        if (!weatherLink.everReported()) return "N/A";
        if (weatherLink.lost()) return "LOST";
        if (weatherLink.pending()) return "STARTING";
        return weatherLink.reportedOk() ? "OK" : "ERROR";
    }

    // "ok" / "warn" / "error" — a single glance-able summary for the health
    // strip's dot color; the individual fields underneath cover the detail.
    static const char* overallHealth(bool wifiOk, const char* ww) {
        if (!wifiOk || !strcmp(ww, "ERROR") || !strcmp(ww, "LOST")) return "error";
        if (!strcmp(ww, "N/A") || !strcmp(ww, "STARTING")) return "warn";
        return "ok";
    }

    static size_t buildStatusJson(char* buf, size_t size) {
        CtrlLock lock;
        uint8_t rs = relayState();
        TimerInfo ti = sm.getTimerInfo();
        char clockBuf[12] = "--:--:--";
        if (clockValid()) {
            time_t t = time(nullptr);
            strftime(clockBuf, sizeof(clockBuf), "%H:%M:%S", localtime(&t));
        }
        bool wifiOk = wifiMgr.isConnected();
        const char* ww = wwHealthWord();
        int n = snprintf(buf, size,
            "{\"mode\":\"%s\",\"runMode\":\"%s\",\"uptime\":%lu,\"relays\":[%u,%u,%u],"
            "\"elapsed\":%lu,\"remaining\":%lu,\"hasRemaining\":%s,\"btnLocked\":%s,"
            "\"testMode\":%s,\"lockAutoExpire\":%s,\"lockRemaining\":%lu,\"tempF\":%d,"
            "\"currentTime\":\"%s\","
            "\"health\":{\"overall\":\"%s\",\"wifi\":%s,\"ww\":\"%s\",\"freeHeapKB\":%u,\"otaActive\":%s,"
            "\"mesh\":{\"rx\":%ld,\"ping\":%ld,\"tx\":%ld,\"from\":\"%s\"}}}",
            sm.stateName(), sm.modeName(), (unsigned long)(millis() / 1000),
            (unsigned)(rs & 1), (unsigned)((rs >> 1) & 1), (unsigned)((rs >> 2) & 1),
            (unsigned long)(ti.totalElapsedMs / 1000), (unsigned long)(ti.totalRemainingMs / 1000),
            tf(ti.hasRemaining), tf(buttons.locked), tf(buttons.testModeActive),
            tf(buttons.lockAutoExpiring()), (unsigned long)buttons.lockRemainingSec(),
            (int)roundf(temperatureRead() * 9.0f / 5.0f + 32.0f), clockBuf,
            overallHealth(wifiOk, ww), tf(wifiOk), ww, (unsigned)(ESP.getFreeHeap() / 1024), tf(sm.otaActive),
            secsSince(meshHealth.lastRxMs), secsSince(meshHealth.lastPingMs), secsSince(meshHealth.lastTxMs),
            meshHealth.lastSender);  // sender id is hex-only (mesh.h validates it), so safe unescaped
        if (n < 0) { buf[0] = '\0'; return 0; }
        return ((size_t)n >= size) ? size - 1 : (size_t)n;
    }

    // Same category -> color mapping as weather-watcher's categoryColor()
    // (nws_client.h) — duplicated rather than shared, since the two
    // firmwares don't share headers and this is a one-line lookup.
    static const char* alertCategoryColor(uint8_t c) {
        switch (c) {
        case 7: case 6: case 2: return "purple"; // TORNADO_EMERGENCY, TORNADO_CONFIRMED, OTHER_EXTREME
        case 4: case 5: case 3: return "red";    // TORNADO_UNCONFIRMED, THUNDERSTORM_DESTRUCTIVE, THUNDERSTORM_CONSIDERABLE
        case 1:                 return "orange"; // THUNDERSTORM_BASE
        case 8:                 return "red";    // TORNADO_WATCH (display-only, never triggers)
        case 9:                 return "yellow"; // SEVERE_TSTORM_WATCH (display-only, never triggers)
        default:                return "";
        }
    }

    // Damage-threat pill text for the Control Panel. TORNADO_CONFIRMED covers
    // both "observed" and a sub-catastrophic damage tag, so it reads CONFIRMED.
    static const char* alertCategoryTag(uint8_t c) {
        switch (c) {
        case 7: return "CATASTROPHIC";  // TORNADO_EMERGENCY
        case 6: return "CONFIRMED";     // TORNADO_CONFIRMED
        case 5: return "DESTRUCTIVE";   // THUNDERSTORM_DESTRUCTIVE
        case 3: return "CONSIDERABLE";  // THUNDERSTORM_CONSIDERABLE
        default: return "";
        }
    }

    static const char* runModeName(RunMode m) {
        switch (m) {
        case RunMode::WAIL:      return "WAIL";
        case RunMode::ATTACK:    return "ATTACK";
        case RunMode::FAST_WAIL: return "FAST WAIL";
        case RunMode::MANUAL:    return "MANUAL";
        case RunMode::GROWL:     return "GROWL TEST";
        default:                 return "";
        }
    }

    static String buildAlertsJson() {
        CtrlLock lock;
        JsonDocument doc;
        doc["receivedMs"] = weatherLink.alertsReceivedMs();
        doc["uptimeMs"]   = millis();
        time_t t = time(nullptr);
        doc["nowEpoch"]   = (t > 1600000000) ? (uint32_t)t : 0;
        JsonArray arr = doc["alerts"].to<JsonArray>();
        for (uint8_t i = 0; i < weatherLink.alertCount(); i++) {
            const WeatherLink::LinkedAlert& a = weatherLink.alert(i);
            JsonObject o = arr.add<JsonObject>();
            o["vtecKey"] = a.vtecKey;
            o["event"]   = a.event;
            o["color"]   = alertCategoryColor(a.category);
            o["exp"]     = a.expEpoch;
            o["tag"]     = alertCategoryTag(a.category);
            bool activated = (sm.lastTriggerSource == TriggerSource::NWS_ALERT) &&
                              a.vtecKey[0] && !strcmp(weatherLink.lastAlertId(), a.vtecKey);
            o["activatedThis"] = activated;
            if (activated) {
                o["actMode"] = runModeName(sm.lastTriggerMode());
                time_t at = (time_t)sm.lastTriggerEpoch();
                if (at) {
                    char buf[12];
                    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&at));
                    o["actTime"] = buf;
                }
            }
        }
        String out;
        serializeJson(doc, out);
        return out;
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
        doc["meshWhitelistEnabled"] = s.meshWhitelistEnabled;
        doc["meshPassword"]        = s.meshPassword;
        doc["weatherAutoTriggerEnabled"] = s.weatherAutoTriggerEnabled;
        doc["meshNotifyActivation"]   = s.meshNotifyActivation;
        doc["meshNotifySystemEvents"] = s.meshNotifySystemEvents;
        doc["meshNotifyWwHealth"]     = s.meshNotifyWwHealth;
        doc["meshNotifyStatus"]       = s.meshNotifyStatus;
        doc["meshNotifyWatches"]      = s.meshNotifyWatches;
        doc["ntpServer"]        = s.ntpServer;
        doc["ntpUpdateHours"]   = s.ntpUpdateHours;
        doc["timeZone"]         = s.timeZone;
        doc["autoDst"]          = s.autoDst;
        doc["statusTime1"] = s.statusTime1;
        doc["statusTime2"] = s.statusTime2;
        doc["statusTime3"] = s.statusTime3;
        doc["statusIncludeMode"]     = s.statusIncludeMode;
        doc["statusIncludeLock"]     = s.statusIncludeLock;
        doc["statusIncludeUptime"]   = s.statusIncludeUptime;
        doc["statusIncludeCpuTemp"]  = s.statusIncludeCpuTemp;
        doc["statusIncludeWwHealth"] = s.statusIncludeWwHealth;
        doc["statusIncludeWifi"]     = s.statusIncludeWifi;
        doc["statusIncludeTime"]     = s.statusIncludeTime;
        doc["statusIncludeLastRun"]  = s.statusIncludeLastRun;
        doc["statusIncludeAlerts"]   = s.statusIncludeAlerts;
        doc["wwEverReported"] = weatherLink.everReported();
        doc["wwOk"]           = weatherLink.reportedOk();
        doc["wwPending"]      = weatherLink.pending();
        doc["wwDetail"]       = weatherLink.detail();
        doc["wwLost"]         = weatherLink.lost();
        doc["wwLastHeardSec"] = weatherLink.everReported() ? weatherLink.lastRxAgeMs() / 1000 : 0;
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
            o["q"]  = e.seq;
            o["e"]  = e.epoch;
            o["u"]  = e.startUptimeSec;
            o["d"]  = e.durationSec;
            o["m"]  = e.mode;
            o["s"]  = e.source;
            o["x"]  = e.end;
            o["ss"] = e.stopSource;
            if (e.alertType[0]) o["at"] = e.alertType;
            if (e.alertId[0])   o["ai"] = e.alertId;
        }
        String out;
        serializeJson(doc, out);
        return out;
    }

    // UTC ISO-8601 — no local-time/DST ambiguity in an exported file, and
    // no coordinates/names anywhere in it (ground rule 5).
    static String isoUtc(uint32_t epoch) {
        if (!epoch) return "";
        time_t t = (time_t)epoch;
        struct tm tmv;
        gmtime_r(&t, &tmv);
        char buf[24];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        return String(buf);
    }

    static const char* csvMode(uint8_t m) {
        static const char* w[] = {"NONE", "WAIL", "ATTACK", "FASTWAIL", "MANUAL", "GROWL"};
        return m < 6 ? w[m] : "?";
    }
    static const char* csvSource(uint8_t s) {
        static const char* w[] = {"LOCAL", "WEB", "MESH", "NWS_ALERT", "AUTO"};
        return s < 5 ? w[s] : "?";
    }
    static const char* csvEnd(uint8_t e) {
        static const char* w[] = {"COMPLETED", "STOPPED", "FAILSAFE", "INTERRUPTED"};
        return e < 4 ? w[e] : "?";
    }
    static String csvEscape(const char* s) {
        String out(s);
        out.replace("\"", "\"\"");
        return out;
    }

    // Streams the full persisted log (not just the RAM cache), one file
    // handle open at a time, read line-by-line — bounded memory regardless
    // of file size. A line that fails to parse (e.g. torn by a power loss
    // mid-write) is skipped rather than aborting the whole export.
    static void writeLogCsv(Print& out) {
        out.println("seq,start_utc,start_uptime_sec,duration_sec,mode,source,end,stop_source,alert_type,alert_id");
        File f = runLog.openForExport();
        if (!f) return;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (!line.length()) continue;
            JsonDocument doc;
            if (deserializeJson(doc, line)) continue;
            uint32_t seq   = doc["seq"]    | 0;
            uint32_t epoch = doc["epoch"]  | 0;
            uint32_t upSec = doc["upSec"]  | 0;
            uint32_t dur   = doc["durSec"] | 0;
            uint8_t  mode  = doc["mode"]   | 0;
            uint8_t  src   = doc["source"] | 0;
            uint8_t  end   = doc["end"]    | 0;
            uint8_t  ssrc  = doc["stopSrc"] | 0;
            const char* at = doc["at"] | "";
            const char* ai = doc["ai"] | "";
            out.printf("%u,%s,%u,%u,%s,%s,%s,%s,\"%s\",\"%s\"\n",
                seq, isoUtc(epoch).c_str(), upSec, dur,
                csvMode(mode), csvSource(src), csvEnd(end), csvSource(ssrc),
                csvEscape(at).c_str(), csvEscape(ai).c_str());
        }
        f.close();
    }
};

extern WebUI webUI;
