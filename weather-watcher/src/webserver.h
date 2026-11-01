#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <cctype>
#include <time.h>
#include "config.h"
#include "sync.h"
#include "settings.h"
#include "wifi_manager.h"
#include "nws_client.h"
#include "ntfy.h"
#include "assets.h"

extern void applyTimeConfig(); // src/main.cpp
extern const char* resetReasonName(); // src/main.cpp

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

// Web App Manifest — lets Android/Chrome find a large enough icon for a
// home-screen bookmark instead of falling back to a generic letter
// monogram (the small favicon.ico alone isn't trusted for that).
static const char MANIFEST_JSON[] PROGMEM = R"json({
  "name": "Weather Watcher",
  "short_name": "Weather Watcher",
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

// ── Login page ───────────────────────────────────────────────────────────────
static const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Watcher</title>
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
.card{position:relative;background:var(--panel);border:1px solid var(--line);border-radius:20px;padding:36px 28px 30px;
      display:flex;flex-direction:column;align-items:center;gap:22px;text-align:center}
.back-btn{position:absolute;top:14px;left:14px;width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;
          border-radius:10px;border:1px solid #222D3D;background:var(--inset);color:var(--soft);text-decoration:none}
.back-btn:hover{background:#172030;color:#fff}
.login-hero{width:130px;height:auto;display:block;margin-bottom:-6px}
h1{font-size:24px;font-weight:700;letter-spacing:.01em}
.sub{color:var(--muted);font-size:14px;margin-top:6px}
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
  <a href="/" class="back-btn" title="Back to Dashboard" aria-label="Back to dashboard">
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
  </a>
  <img class="login-hero" src="/dashboard-hero.png" alt="">
  <div><h1>Weather Watcher</h1><p class="sub">NWS alert monitor</p></div>
  <form onsubmit="login(event)">
    <label for="pw">Password</label>
    <input type="password" id="pw" placeholder="Enter the device password" autocomplete="current-password" autofocus>
    <button type="submit">UNLOCK</button>
    <div class="err" id="err"></div>
  </form>
</div>
<p class="foot">The dashboard is public. Log in to change settings.</p>
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
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#0A0E13;--bar:#0D1219;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;
  --text:#E7ECF3;--soft:#C9D4E3;--muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--green:#4ADE80;--red:#F87171;
  --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  --mono:"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Consolas,monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:var(--text);font-family:var(--sans);min-height:100vh}
a{color:var(--accent)}
.mono{font-family:var(--mono)}
.eyebrow{font-size:11px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.hint{font-size:13px;color:var(--muted);line-height:1.5}
.top{position:sticky;top:0;z-index:20;background:var(--bar);border-bottom:1px solid var(--line)}
.topin{max-width:1200px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.brand{display:flex;align-items:center;gap:12px;color:var(--text);text-decoration:none;min-width:0}
.tile{width:40px;height:40px;border-radius:10px;background:#172030;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0}
.tile img{width:34px;height:34px;display:block}
.bt{display:flex;flex-direction:column;gap:2px}.bt b{font-size:17px}
.ibtn{width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;border-radius:10px;
      border:1px solid #222D3D;background:var(--panel);color:var(--soft);text-decoration:none;flex-shrink:0}
.ibtn:hover{background:#172030;color:#fff}
main{max-width:1200px;margin:0 auto;padding:20px;display:flex;flex-wrap:wrap;gap:20px;align-items:flex-start}
.colL{flex:1 1 340px;min-width:0;display:flex;flex-direction:column;gap:20px}
.colR{flex:2 1 560px;min-width:0;display:flex;flex-direction:column;gap:20px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:16px}
.clockp{padding:24px;display:flex;flex-direction:column;gap:8px;text-align:center}
.clock{font-family:var(--mono);font-size:56px;font-weight:500;letter-spacing:.03em;line-height:1.05;font-variant-numeric:tabular-nums}
.clockmeta{font-family:var(--mono);font-size:13px;color:var(--muted)}
.statusp{padding:8px 22px 12px}
.shead{display:flex;align-items:center;justify-content:space-between;padding:12px 0}
.srow{display:flex;gap:12px;align-items:flex-start;padding:14px 0;border-top:1px solid var(--line)}
.dot{width:10px;height:10px;border-radius:50%;margin-top:5px;flex-shrink:0;background:#3A4554}
.dot.green{background:var(--green);box-shadow:0 0 0 4px rgba(74,222,128,.14)}
.dot.red{background:var(--red);box-shadow:0 0 0 4px rgba(248,113,113,.16)}
.sbody{display:flex;flex-direction:column;gap:3px;min-width:0;flex:1}
.slabel{font-weight:600}
.status-detail{font-size:13px;color:var(--muted);line-height:1.45;word-break:break-word}
.sbtn{min-height:36px;padding:0 12px;border-radius:8px;border:1px solid var(--line2);background:#172030;color:var(--text);
      font-family:var(--sans);font-size:13px;font-weight:600;cursor:pointer;text-decoration:none;display:inline-flex;align-items:center;gap:6px;flex-shrink:0}
.sbtn:hover{background:#1D2838}
.statline{padding:14px 0 4px;border-top:1px solid var(--line);font-family:var(--mono);font-size:12px;color:var(--muted)}
.card{padding:22px;display:flex;flex-direction:column;gap:14px}
.chead{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;flex-wrap:wrap}
.no-alerts{display:flex;align-items:center;gap:14px;padding:22px;border-radius:12px;background:#0F2418;border:1px solid #1F5236}
.no-alerts b{color:#BBF7D0;display:block}
.no-alerts span{font-size:14px;color:#86C9A0}
.alert-item{border-radius:12px;padding:16px 18px;display:flex;flex-direction:column;gap:10px;color:#fff;margin-bottom:10px}
.alert-item:last-child{margin-bottom:0}
.alert-purple{background:#5B1F8F}.alert-red{background:#7F1D1D}.alert-orange{background:#8A3B0B}
.alert-head{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.alert-title{flex:1;min-width:160px;display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.alert-title b{font-size:18px}
.alert-dmg{font-size:11px;font-weight:700;letter-spacing:.08em;padding:3px 8px;border-radius:999px;border:1px solid rgba(255,255,255,.75)}
.alert-tag{font-size:11px;font-weight:700;letter-spacing:.06em;padding:3px 8px;border-radius:6px;background:rgba(0,0,0,.35);white-space:nowrap}
.alert-sub{font-size:14px;line-height:1.5;opacity:.92}
.alert-actions{display:flex;gap:8px;flex-wrap:wrap}
.abtn{min-height:36px;padding:0 12px;border-radius:8px;border:0;background:rgba(0,0,0,.32);color:#fff;font-family:var(--sans);
      font-size:13px;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;gap:6px;text-decoration:none}
.abtn:hover{background:rgba(0,0,0,.45);color:#fff}
.alert-map{display:none}
.alert-map.show{display:block}
.alert-map .hint{color:#FFE4E4}
.rtw{overflow-x:auto}
.rt{display:grid;grid-template-columns:minmax(200px,2fr) minmax(90px,.8fr) minmax(150px,1.2fr) minmax(150px,1fr);min-width:600px;font-size:14px}
.rt>span{padding:12px 8px 12px 0;border-top:1px solid var(--line)}
.rt>.eyebrow{border-top:0;padding:6px 0 10px}
.rt .ev{display:flex;flex-direction:column;gap:2px}
.rt .vk{font-family:var(--mono);font-size:11px;color:var(--muted)}
.rt .tm{font-family:var(--mono);font-size:13px;color:var(--soft)}
.disp{font-size:11px;font-weight:700;letter-spacing:.06em;padding:3px 8px;border-radius:6px;white-space:nowrap}
.d-ok{background:#14301F;color:#86EFAC}.d-info{background:#1C2430;color:var(--soft)}
.d-warn{background:#2E2A0C;color:#FDE68A}.d-bad{background:#34131A;color:#FCA5A5}
.foot{width:100%;display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px 20px;padding:4px 4px 24px;font-family:var(--mono);font-size:12px;color:var(--muted)}
.foot a{color:var(--muted)}
@media (max-width:560px){.clock{font-size:44px}.card,.clockp{padding:18px}.statusp{padding:6px 18px 10px}}
</style></head>
<body>
<header class="top"><div class="topin">
  <a class="brand" href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">
    <span class="tile"><img src="/icon-192.png" alt=""></span>
    <span class="bt"><b>Weather Watcher</b><span class="eyebrow" style="font-size:10px">NWS alert monitor</span></span>
  </a>
  <a href="/settings" class="ibtn" title="Settings" aria-label="Settings">
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
  </a>
</div></header>

<main>
<div class="colL">
  <section class="panel clockp" aria-label="Clock">
    <span class="clock" id="clock">--:--:--</span>
    <span class="clockmeta">Watcher <span id="tempF">&mdash;</span>&deg;F &middot; Uptime <span id="uptime">&mdash;</span></span>
  </section>

  <section class="panel statusp" aria-label="System status">
    <div class="shead"><span class="eyebrow">System status</span><span class="hint" style="font-size:12px">Live</span></div>
    <div class="srow"><span class="dot" id="wifiDot"></span><div class="sbody"><span class="slabel">Wi-Fi</span><span class="status-detail" id="wifiDetail"></span></div></div>
    <div class="srow"><span class="dot" id="apiDot"></span><div class="sbody"><span class="slabel">NWS API</span><span class="status-detail" id="apiDetail"></span></div></div>
    <div class="srow"><span class="dot" id="linkDot"></span><div class="sbody"><span class="slabel">Controller link</span><span class="status-detail" id="linkDetail"></span></div>
      <button class="sbtn" onclick="testLink()">Test link</button></div>
    <div class="srow"><span class="dot" id="staleDot"></span><div class="sbody"><span class="slabel">Alert data freshness</span><span class="status-detail" id="staleDetail"></span></div></div>
    <div class="statline" id="lastPoll">Last polling attempt: &mdash;</div>
  </section>
</div>

<div class="colR">
  <section class="panel card" aria-label="Current alerts">
    <div class="chead"><span class="eyebrow">Current alerts</span><span class="hint" style="font-size:12px" id="alertCount"></span></div>
    <div id="currentAlerts"><p class="hint">Loading&hellip;</p></div>
  </section>

  <section class="panel card" aria-label="Recent alerts">
    <div class="chead">
      <div><span class="eyebrow">Recent alerts</span><p class="hint" style="margin-top:6px">The last 20 trigger attempts &mdash; activations, skips and failures alike.</p></div>
      <a class="sbtn" href="/alertlog.csv">
        <svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>Export CSV</a>
    </div>
    <div class="rtw" id="recentAlerts"><p class="hint">None yet.</p></div>
  </section>
</div>

<footer class="foot">
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Weather Watcher &middot; v<span id="verNum">&mdash;</span></a>
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a>
</footer>
</main>

<script>
function esc(s){const d=document.createElement('div');d.textContent=s||'';return d.innerHTML;}

let lastCurrentAlertsSig=null;
function renderCurrentAlerts(list){
  // Current Alerts refreshes every second along with everything else, but
  // rebuilding its innerHTML on every tick would collapse any expanded
  // polygon map (WP8) and re-trigger its fetch — skip the rebuild unless
  // the list actually changed.
  const sig=JSON.stringify((list||[]).map(a=>[a.id,a.triggered,a.tag,a.mode]));
  if(sig===lastCurrentAlertsSig)return;
  lastCurrentAlertsSig=sig;
  const el=document.getElementById('currentAlerts');
  document.getElementById('alertCount').textContent=(list&&list.length)?list.length+' active':'None';
  if(!list||!list.length){
    el.innerHTML='<div class="no-alerts"><svg viewBox="0 0 24 24" width="28" height="28" fill="none" stroke="#4ADE80" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M22 11.08V12a10 10 0 1 1-5.93-9.14"/><polyline points="22 4 12 14.01 9 11.01"/></svg><div><b>All clear</b><span>No active alerts for your location.</span></div></div>';
    return;
  }
  el.innerHTML=list.map((a,i)=>
    '<div class="alert-item alert-'+esc(a.color)+'">'+
      '<div class="alert-head"><span class="alert-title"><b>'+esc(a.event)+'</b>'+
        (a.tag?'<span class="alert-dmg">'+esc(a.tag)+'</span>':'')+'</span>'+
        (a.triggered?'<span class="alert-tag">SIREN ACTIVATED'+(a.mode?' &mdash; '+esc(a.mode):'')+'</span>':'')+'</div>'+
      '<div class="alert-sub">'+esc(a.headline)+'</div>'+
      '<div class="alert-actions">'+
        '<a href="'+esc(a.id)+'" target="_blank" rel="noopener" class="abtn">View on NWS &#8599;</a>'+
        '<button class="abtn" onclick="toggleAlertMap(this,'+i+')" aria-expanded="false">Show map</button>'+
      '</div>'+
      '<div class="alert-map" id="amap'+i+'" data-url="'+esc(a.id)+'"></div>'+
    '</div>'
  ).join('');
}

// WP8: a polygon mini-map for one alert, fetched client-side straight from
// its own api.weather.gov feature URL (Accept: application/geo+json) —
// no ESP32-side geometry storage needed. Simple equirectangular
// projection (fine at this zoom level/latitude range); Polygon and
// MultiPolygon both supported. The device's own location dot is fetched
// separately and only rendered for logged-in sessions (Decision C).
async function toggleAlertMap(btn,i){
  const box=document.getElementById('amap'+i);
  if(box.classList.contains('show')){box.classList.remove('show');btn.textContent='Show map';btn.setAttribute('aria-expanded','false');return;}
  box.classList.add('show');btn.textContent='Hide map';btn.setAttribute('aria-expanded','true');
  if(box.dataset.loaded)return;
  box.dataset.loaded='1';
  box.innerHTML='<p class="hint">Loading map&hellip;</p>';
  try{
    const r=await fetch(box.dataset.url,{headers:{'Accept':'application/geo+json'}});
    const feature=await r.json();
    const geom=feature.geometry;
    if(!geom){box.innerHTML='<p class="hint">No polygon available for this alert (county/zone-based).</p>';return;}
    let loc=null;
    try{
      const lr=await fetch('/location-data');
      if(lr.ok)loc=await lr.json();
    }catch(e){}
    box.innerHTML=renderPolygonSvg(geom,loc);
  }catch(e){
    box.innerHTML='<p class="hint">Could not load the map (network or CORS error).</p>';
  }
}
function renderPolygonSvg(geom,loc){
  const polys = geom.type==='Polygon' ? [geom.coordinates] :
                geom.type==='MultiPolygon' ? geom.coordinates : [];
  if(!polys.length)return '<p class="hint">Unsupported geometry type: '+esc(geom.type)+'</p>';
  let minLon=Infinity,maxLon=-Infinity,minLat=Infinity,maxLat=-Infinity;
  polys.forEach(rings=>rings.forEach(ring=>ring.forEach(([lon,lat])=>{
    if(lon<minLon)minLon=lon;if(lon>maxLon)maxLon=lon;
    if(lat<minLat)minLat=lat;if(lat>maxLat)maxLat=lat;
  })));
  if(loc&&loc.lat){
    if(loc.lon<minLon)minLon=loc.lon;if(loc.lon>maxLon)maxLon=loc.lon;
    if(loc.lat<minLat)minLat=loc.lat;if(loc.lat>maxLat)maxLat=loc.lat;
  }
  const refLat=(minLat+maxLat)/2;
  const cosLat=Math.max(0.15,Math.cos(refLat*Math.PI/180));
  const padLon=(maxLon-minLon)*0.12||0.05, padLat=(maxLat-minLat)*0.12||0.05;
  minLon-=padLon;maxLon+=padLon;minLat-=padLat;maxLat+=padLat;
  const W=320,H=180;
  const sx=lon=>((lon-minLon)*cosLat)/((maxLon-minLon)*cosLat||1)*W;
  const sy=lat=>H-((lat-minLat)/((maxLat-minLat)||1))*H;
  const paths=polys.map(rings=>rings.map(ring=>
    'M'+ring.map(([lon,lat])=>sx(lon).toFixed(1)+','+sy(lat).toFixed(1)).join('L')+'Z'
  ).join(' ')).join(' ');
  let dot='';
  if(loc&&loc.lat){
    dot='<circle cx="'+sx(loc.lon).toFixed(1)+'" cy="'+sy(loc.lat).toFixed(1)+'" r="6" fill="#38BDF8" stroke="#fff" stroke-width="2"/>';
  }
  return '<svg viewBox="0 0 '+W+' '+H+'" role="img" aria-label="Warning area map" style="display:block;width:100%;height:auto;background:#0A0E18;border-radius:10px">'+
    '<path d="M0 45h320M0 90h320M0 135h320M80 0v180M160 0v180M240 0v180" stroke="#1A2433" stroke-width="1"/>'+
    '<path d="'+paths+'" fill="rgba(239,68,68,.32)" stroke="#F87171" stroke-width="2"/>'+
    dot+
    '</svg>'+(loc&&loc.lat?'<p class="hint" style="margin-top:6px">Shaded: warning area &middot; Blue dot: this device</p>':'<p class="hint" style="margin-top:6px">Log in to see your exact location on the map.</p>');
}

function dispClass(d){
  if(d==='TRIGGERED'||d==='RETRIED_TRIGGERED') return 'd-ok';
  if(d==='NOTIFY_ONLY'||d==='ALREADY_ACTIVE') return 'd-info';
  if(d==='AUTO_OFF'||d==='BADCMD') return 'd-warn';
  return 'd-bad'; // NO_RESPONSE / GAVE_UP_* / anything else — a real miss
}
function renderRecentAlerts(list){
  const el=document.getElementById('recentAlerts');
  if(!list||!list.length){el.innerHTML='<p class="hint">None yet.</p>';return;}
  el.innerHTML='<div class="rt"><span class="eyebrow">Alert</span><span class="eyebrow">Mode</span><span class="eyebrow">Outcome</span><span class="eyebrow">Time</span>'+
    list.map(r=>
      '<span class="ev"><b>'+esc(r.event)+'</b>'+(r.vtecKey?'<span class="vk">'+esc(r.vtecKey)+'</span>':'')+'</span>'+
      '<span>'+esc(r.mode)+'</span>'+
      '<span><span class="disp '+dispClass(r.disposition)+'">'+esc(r.disposition)+'</span></span>'+
      '<span class="tm">'+esc(r.time)+'</span>'
    ).join('')+'</div>';
}

function fmtUp(s){
  const d=Math.floor(s/86400),h=Math.floor((s%86400)/3600),
        m=Math.floor((s%3600)/60),sc=s%60;
  return (d?d+'d ':'')+String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(sc).padStart(2,'0');
}

function testLink(){
  fetch('/test-link',{method:'POST'});
}

let lastRecentSig=null;
function refresh(){
  fetch('/status-data').then(r=>r.json()).then(d=>{
    document.getElementById('wifiDot').className='dot '+(d.wifiConnected?'green':'red');
    document.getElementById('wifiDetail').textContent=d.wifiDetail;
    document.getElementById('apiDot').className='dot '+(d.apiGreen?'green':'red');
    document.getElementById('apiDetail').textContent=d.apiDetail;
    document.getElementById('linkDot').className='dot'+(d.everTestedLink?(d.linkOk?' green':' red'):'');
    const sxAge=d.lastSxEventAgeSec;
    const sxTxt=d.lastSxEvent?(' — last main-board event: "'+d.lastSxEvent+'" ('+(sxAge<60?sxAge+'s':Math.floor(sxAge/60)+'m')+' ago)'):' — no SX event received yet';
    document.getElementById('linkDetail').textContent=d.linkDetail+(d.sirenLegacyProtocol?' (older siren firmware — update it for trigger confirmations)':'')+sxTxt;
    document.getElementById('staleDot').className='dot '+(d.stale?'red':'green');
    document.getElementById('staleDetail').textContent=d.stale?('Stale — last successful poll '+Math.floor(d.dataAgeSec/60)+'m ago'):'Up to date';
    document.getElementById('clock').textContent=d.currentTime;
    if(d.tempF!=null)document.getElementById('tempF').textContent=d.tempF;
    if(d.uptime!=null)document.getElementById('uptime').textContent=fmtUp(d.uptime);
    document.getElementById('lastPoll').textContent='Last polling attempt: '+d.lastPoll;
    renderCurrentAlerts(d.currentAlerts);
    const rs=JSON.stringify(d.recentAlerts||[]);
    if(rs!==lastRecentSig){lastRecentSig=rs;renderRecentAlerts(d.recentAlerts);}
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
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/manifest.json">
<link rel="apple-touch-icon" href="/icon-192.png">
<link rel="stylesheet" media="print" onload="this.media='all'" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap">
<style>
:root{
  --bg:#0A0E13;--bar:#0D1219;--panel:#111821;--inset:#0B1017;--line:#1E2836;--line2:#2A3646;
  --text:#E7ECF3;--soft:#C9D4E3;--muted:#93A1B5;--faint:#7D8BA0;--accent:#38BDF8;--accent2:#0EA5E9;--green:#4ADE80;--red:#F87171;
  --sans:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  --mono:"JetBrains Mono",ui-monospace,"SF Mono","Cascadia Code",Consolas,monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:var(--text);font-family:var(--sans);min-height:100vh}
a{color:var(--accent)}
.mono{font-family:var(--mono)}
.eyebrow{font-size:11px;font-weight:600;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.top{position:sticky;top:0;z-index:20;background:var(--bar);border-bottom:1px solid var(--line)}
.topin{max-width:1200px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;justify-content:space-between;gap:12px}
.brand{display:flex;align-items:center;gap:14px}
.bt{display:flex;flex-direction:column;gap:2px}.bt b{font-size:17px}
.ibtn{width:44px;height:44px;display:inline-flex;align-items:center;justify-content:center;border-radius:10px;
      border:1px solid #222D3D;background:var(--panel);color:var(--soft);cursor:pointer;text-decoration:none;padding:0}
.ibtn:hover{background:#172030;color:#fff}
.layout{max-width:1200px;margin:0 auto;padding:20px;display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
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
input[type=number]{font-family:var(--mono)}
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
.ws.ok{color:#86EFAC;border-color:#1F5236}
.trow{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 0;border-top:1px solid var(--line)}
.trow label{margin:0;font-size:14px;font-weight:400;color:var(--text);line-height:1.45}
/* Switches */
.trow input[type=checkbox],.mx input[type=checkbox]{appearance:none;-webkit-appearance:none;width:46px;min-height:0;height:28px;padding:0;border:0;
      border-radius:999px;background:#263142;position:relative;cursor:pointer;flex-shrink:0;transition:background .15s}
.trow input[type=checkbox]::after,.mx input[type=checkbox]::after{content:"";position:absolute;top:4px;left:4px;width:20px;height:20px;border-radius:50%;background:var(--muted);transition:left .15s}
.trow input[type=checkbox]:checked,.mx input[type=checkbox]:checked{background:var(--accent2)}
.trow input[type=checkbox]:checked::after,.mx input[type=checkbox]:checked::after{left:22px;background:#fff}
input[type=checkbox]:focus-visible{box-shadow:0 0 0 3px rgba(56,189,248,.35)}
/* Alert-response matrix */
.mxw{overflow-x:auto}
.mx{min-width:640px;display:flex;flex-direction:column}
.mrow{display:grid;grid-template-columns:minmax(200px,1fr) auto 76px;gap:16px;padding:14px 0;border-top:1px solid var(--line);align-items:center}
.mrow.h{border-top:0;padding:0 0 10px;align-items:end}
.mname{display:flex;gap:12px;align-items:flex-start;min-width:0}
.mname .sw{width:10px;height:10px;border-radius:3px;margin-top:5px;flex-shrink:0}
.mname b{display:block;font-size:14px}.mname span{font-size:12px;color:var(--muted)}
.seg{display:inline-flex;border:1px solid var(--line2);border-radius:10px;overflow:hidden;background:var(--inset)}
.seg input{position:absolute;opacity:0;width:1px;height:1px;min-height:0;padding:0}
.seg label{margin:0;min-height:38px;min-width:64px;padding:0 10px;display:inline-flex;align-items:center;justify-content:center;
           font-size:12px;font-weight:700;letter-spacing:.06em;color:var(--muted);cursor:pointer}
.seg label:hover{color:#fff}
.seg input:focus-visible+label{outline:2px solid var(--accent);outline-offset:-2px}
.seg input:checked+label{color:#fff;background:#263142}
.seg input:checked+label[data-v=WAIL]{background:#1D4ED8}
.seg input:checked+label[data-v=ATTACK]{background:#B91C1C}
.seg input:checked+label[data-v=FASTWAIL]{background:#A84A0A}
.mpush{display:flex;justify-content:center}
/* Other-extreme chips */
.chklist{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:8px}
.chkrow{display:flex;align-items:center;gap:10px;min-height:44px;padding:0 12px;border-radius:10px;border:1px solid #222D3D;background:var(--inset)}
.chkrow input{width:18px;height:18px;min-height:0;padding:0;accent-color:var(--accent2);flex-shrink:0;cursor:pointer}
.chkrow label{margin:0;font-size:14px;font-weight:400;color:var(--text);cursor:pointer;flex:1;line-height:44px}
.fw{display:flex;flex-wrap:wrap;gap:12px;align-items:center;padding:18px;border:1px dashed #2F3D52;border-radius:12px;background:var(--inset)}
.fw input[type=file]{position:absolute;width:1px;height:1px;opacity:0}
.fw label{margin:0;display:inline-flex;align-items:center}
#fwName{flex:1;min-width:160px;font-size:14px;color:var(--muted)}
.foot{display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px 20px;padding:8px 4px 24px;font-family:var(--mono);font-size:12px;color:var(--muted)}
.foot a{color:var(--muted)}
.overlay{position:fixed;inset:0;background:rgba(4,6,10,.72);display:none;align-items:center;justify-content:center;padding:16px;z-index:50}
.mbox{width:100%;max-width:360px;padding:24px;display:flex;flex-direction:column;gap:18px;background:#131B26;border:1px solid var(--line);border-radius:16px}
.mgrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.mok,.mcancel{min-height:46px;border-radius:10px;font-weight:700;font-family:var(--sans);cursor:pointer;font-size:14px}
.mok{border:0;background:#B91C1C;color:#fff}
.mcancel{border:1px solid var(--line2);background:#172030;color:var(--text)}
@media (max-width:800px){
  .side{position:static;max-width:none;flex-basis:100%;flex-direction:row;overflow-x:auto;gap:6px;padding-bottom:4px}
  .side .eyebrow{display:none}
  .side a{white-space:nowrap;border:1px solid var(--line);background:var(--panel);min-height:36px;font-size:13px}
}
@media (max-width:560px){.g2,.g3{grid-template-columns:1fr}.card{padding:18px}}
</style></head>
<body>
<header class="top"><div class="topin">
  <div class="brand">
    <a href="/" class="ibtn" title="Back" aria-label="Back to dashboard">
      <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><line x1="19" y1="12" x2="5" y2="12"/><polyline points="12 19 5 12 12 5"/></svg>
    </a>
    <div class="bt"><span class="eyebrow" style="font-size:10px">Weather Watcher</span><b>Settings</b></div>
  </div>
  <button class="ibtn" onclick="showRestart()" title="Restart" aria-label="Restart Weather Watcher">
    <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/></svg>
  </button>
</div></header>

<div class="layout">
<nav class="side" aria-label="Settings sections">
  <span class="eyebrow">Alerts</span>
  <a href="#location">Location &amp; polling</a>
  <a href="#response">Alert response</a>
  <a href="#extreme">Other extreme types</a>
  <a href="#ntfy">Push notifications</a>
  <span class="eyebrow">Device</span>
  <a href="#network">Wi-Fi &amp; network</a>
  <a href="#security">Security</a>
  <a href="#time">Time</a>
  <a href="#firmware">Firmware</a>
</nav>

<main class="content">

<section class="card" id="location">
  <div><h2>Location &amp; polling</h2><p class="desc">Point coordinates, not a county or ZIP &mdash; so a storm-based warning polygon is matched against your exact spot instead of the whole county.</p></div>
  <div class="g2">
    <div><label for="lat">Latitude</label><input type="text" id="lat" class="mono" placeholder="e.g. 35.4676"></div>
    <div><label for="lon">Longitude</label><input type="text" id="lon" class="mono" placeholder="e.g. -97.5164"></div>
    <div><label for="pollSec">Poll every (seconds)</label><input type="number" id="pollSec" min="30" step="1"></div>
    <div><label for="uaContact">Contact for the NWS User-Agent</label><input type="text" id="uaContact" placeholder="e.g. you@example.com"><p class="hint" style="margin-top:6px">Required by api.weather.gov.</p></div>
  </div>
  <div class="actions"><button class="btn save" onclick="saveLocation()">Save location</button></div>
  <div class="msg" id="xm"></div>
</section>

<section class="card" id="response">
  <div><h2>Alert response</h2><p class="desc">For each kind of warning: which Hurricane Controls run mode to request (sent as <code>WX &lt;MODE&gt;</code> over the UART link), and whether to send a push notification. OFF still shows the alert on the dashboard &mdash; it just never sounds the siren.</p></div>
  <div class="mxw"><div class="mx" id="matrix">
    <div class="mrow h"><span class="eyebrow">Warning</span><span class="eyebrow">Siren mode</span><span class="eyebrow" style="text-align:center">Push</span></div>
  </div></div>
  <div class="trow"><label for="repeatUpg">Re-trigger if an alert that already sounded the siren escalates further</label><input type="checkbox" id="repeatUpg" onchange="saveModes()"></div>
  <div class="actions"><button class="btn save" onclick="saveResponse()">Save alert response</button></div>
  <div class="msg" id="mm"></div>
</section>

<section class="card" id="extreme">
  <div><h2>Other extreme types</h2><p class="desc">Which non-tornado, non-thunderstorm warnings may count as an Other Extreme Emergency &mdash; only when NWS marks one Extreme severity, Immediate urgency and Observed certainty. Unchecked types never trigger, however severe. The siren mode and push setting come from the Other Extreme row above.</p></div>
  <div class="chklist" id="otherList"></div>
  <div class="actions"><button class="btn save" onclick="saveModes('mm2')">Save extreme types</button></div>
  <div class="msg" id="mm2"></div>
</section>

<section class="card" id="ntfy">
  <div><h2><svg width="24" height="24" viewBox="0 0 50 50" aria-hidden="true" style="flex-shrink:0;border-radius:5px"><defs><linearGradient id="ntfyBg" x1="0" y1="0" x2="50" y2="50" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#348878"/><stop offset="1" stop-color="#52bca6"/></linearGradient><linearGradient id="ntfyBub" x1="160.722" x2="168.412" y1="128.533" y2="134.326" gradientTransform="matrix(3.74959 0 0 3.74959 -541.79 -387.599)" gradientUnits="userSpaceOnUse"><stop offset="0" stop-color="#348878"/><stop offset="1" stop-color="#56bda8"/></linearGradient></defs><path d="M0 0h50v50H0z" fill="url(#ntfyBg)"/><path d="M88.2 95.309H64.92c-1.601 0-2.91 1.236-2.91 2.746l.022 18.602-.435 2.506 6.231-1.881H88.2c1.6 0 2.91-1.236 2.91-2.747v-16.48c0-1.51-1.31-2.746-2.91-2.746z" fill="url(#ntfyBub)" transform="translate(-51.147 -81.516)"/><path d="M50.4 46.883c-9.168 0-17.023 7.214-17.023 16.387v.007l.09 71.37-2.303 16.992 31.313-8.319h77.841c9.17 0 17.024-7.224 17.024-16.396V63.27c0-9.17-7.85-16.383-17.016-16.387h-.008zm0 11.566h89.926c3.222.004 5.45 2.347 5.45 4.82v63.655c0 2.475-2.232 4.82-5.457 4.82h-79.54l-15.908 4.807.162-.938-.088-72.343c0-2.476 2.23-4.82 5.455-4.82z" fill="#fff" transform="scale(.26458)"/><path d="M62.57 116.77v-1.312l3.28-1.459q.159-.068.306-.102.158-.045.283-.068l.271-.022v-.09q-.136-.012-.271-.046-.125-.023-.283-.057-.147-.045-.306-.113l-3.28-1.459v-1.323l5.068 2.319v1.413z" fill="#fff" transform="matrix(1.45366 0 0 1.72815 -75.122 -171.953)"/><path d="M69.171 117.754h5.43v1.278h-5.43Z" fill="#fff" transform="matrix(1.44935 0 0 1.66414 -74.104 -166.906)"/></svg>Push notifications</h2>
    <p class="desc">Sent through <a href="https://ntfy.sh" target="_blank" rel="noopener">ntfy</a>, self-hosted or ntfy.sh. A <b>supplement</b>, never a replacement: a dead battery, no Wi-Fi or a crashed app can all silently stop them. Which warnings notify is set in the Alert response table.</p></div>
  <div class="trow" style="border-top:0;padding-top:0"><label for="ntfyEnabled"><b>Enable ntfy notifications</b></label><input type="checkbox" id="ntfyEnabled"></div>
  <div class="g2">
    <div><label for="ntfyServer">Server</label><input type="text" id="ntfyServer" placeholder="https://ntfy.sh"></div>
    <div><label for="ntfyTopic">Topic</label><input type="text" id="ntfyTopic" class="mono" placeholder="my-private-topic-name"><p class="hint" style="margin-top:6px">Anyone who knows the topic can read your notifications &mdash; make it long and random.</p></div>
    <div><label for="ntfyToken">Access token</label><input type="password" id="ntfyToken" placeholder="Leave blank to keep existing" autocomplete="new-password"><p class="hint" id="ntfyTokenHint" style="margin-top:6px">No token saved.</p></div>
    <div style="display:flex;align-items:center"><div class="trow" style="border-top:0;width:100%"><label for="ntfyVerifyCert">Verify server certificate (recommended)</label><input type="checkbox" id="ntfyVerifyCert"></div></div>
  </div>
  <div class="sub" style="gap:0">
    <span class="eyebrow" style="margin-bottom:4px">Also notify for</span>
    <div class="trow"><label for="ntfyNotifyErrors">Faults &mdash; NWS poll errors, siren not responding</label><input type="checkbox" id="ntfyNotifyErrors"></div>
    <div class="trow"><label for="ntfyNotifySrcLocal">Siren started from its physical buttons</label><input type="checkbox" id="ntfyNotifySrcLocal"></div>
    <div class="trow"><label for="ntfyNotifySrcWeb">Siren started from the web panel</label><input type="checkbox" id="ntfyNotifySrcWeb"></div>
    <div class="trow"><label for="ntfyNotifySrcMesh">Siren started over Meshtastic</label><input type="checkbox" id="ntfyNotifySrcMesh"></div>
    <div class="trow"><label for="ntfyNotifyStop">Siren stopped or cycle completed</label><input type="checkbox" id="ntfyNotifyStop"></div>
    <div class="trow"><label for="ntfyNotifySystemEvents">Lockout or weather trigger toggled, locked button pressed</label><input type="checkbox" id="ntfyNotifySystemEvents"></div>
    <div class="trow"><label for="ntfyNotifyStatus">Boot announcement and 12-hour status</label><input type="checkbox" id="ntfyNotifyStatus"></div>
  </div>
  <div class="actions"><button class="btn save" onclick="saveNtfy()">Save notification settings</button><button class="btn logout" onclick="testNtfy()">Send test notification</button></div>
  <div class="msg" id="ntfyMsg"></div>
</section>

<section class="card" id="network">
  <div><h2>Wi-Fi &amp; network</h2><p class="desc">How this board joins your network, and the fallback access point it broadcasts when it can't.</p></div>
  <div id="ws" class="ws">Checking&hellip;</div>
  <div class="inl">
    <div><label for="wHost">Device hostname</label><input type="text" id="wHost" placeholder="weather-watcher" maxlength="32"></div>
    <button class="btn save" onclick="saveHostname()">Save hostname</button>
  </div>
  <p class="hint" style="margin-top:-8px">Letters, numbers and hyphens. Also sets the <code>http://&lt;hostname&gt;.local</code> address. Applies after a restart.</p>
  <div class="msg" id="hm"></div>
  <div class="sub">
    <span class="eyebrow">Join a network</span>
    <div class="g2">
      <div><label for="wSSID">Network name (SSID)</label><input type="text" id="wSSID" placeholder="Your Wi-Fi name"></div>
      <div><label for="wPass">Password</label><input type="password" id="wPass" placeholder="Wi-Fi password"></div>
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
    <p class="hint" id="apHint" style="margin-top:-8px">Used to join the &quot;WeatherWatcher&quot; network this board broadcasts when it isn't on your home Wi-Fi. Applies after a restart.</p>
    <div class="msg" id="apm"></div>
  </div>
</section>

<section class="card" id="security">
  <div><h2>Security</h2><p class="desc">The password that unlocks these settings. The dashboard stays public.</p></div>
  <div class="g2">
    <div><label for="p1">New password</label><input type="password" id="p1" autocomplete="new-password"></div>
    <div><label for="p2">Confirm password</label><input type="password" id="p2" autocomplete="new-password"></div>
  </div>
  <div class="actions"><button class="btn save" onclick="savePw()">Change password</button><button class="btn logout" onclick="logout()">Log out</button></div>
  <div class="msg" id="pm"></div>
</section>

<section class="card" id="time">
  <div><h2>Time</h2><p class="desc">Drives the dashboard clock and alert timestamps, and is passed on to Hurricane Controls. Needs internet to sync.</p></div>
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

<section class="card" id="firmware">
  <div><h2>Firmware</h2><p class="desc">Upload a new <code>firmware.bin</code> from <code>weather-watcher/.pio/build/esp32dev/</code>. The board restarts when it finishes.</p></div>
  <div class="fw">
    <label for="fwFile" class="btn logout">Choose .bin file</label>
    <input type="file" id="fwFile" accept=".bin" onchange="document.getElementById('fwName').textContent=this.files[0]?this.files[0].name:'No file chosen'">
    <span id="fwName">No file chosen</span>
    <button class="btn save" onclick="uploadFw()">Upload firmware</button>
  </div>
  <div class="msg" id="fwm"></div>
</section>

<footer class="foot">
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Weather Watcher &middot; v<span id="verNum">&mdash;</span></a>
  <span>Last restart: <span id="rstReason">&mdash;</span></span>
  <a href="https://github.com/awwgeez-its-drew/hurricane-controls" target="_blank" rel="noopener">Created by awwgeez.its.drew &middot; Coded by Claude</a>
</footer>
</main>
</div>

<div class="overlay" id="rmModal">
  <div class="mbox" role="dialog" aria-modal="true" aria-labelledby="rmTitle">
    <b id="rmTitle" style="font-size:18px">Restart the Weather Watcher?</b>
    <div class="mgrid">
      <button class="mcancel" onclick="closeModal()">Cancel</button>
      <button class="mok"     onclick="doRestart()">Restart</button>
    </div>
  </div>
</div>
<script>
function post(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(body)}).then(r=>r.json());}
function msg(id,txt,ok){
  const e=document.getElementById(id);e.textContent=txt;e.className='msg '+(ok?'ok':'er');
  setTimeout(()=>{e.textContent='';e.className='msg';},3500);
}

// One row per alert category: its siren-mode setting (radio group named
// after the old <select> id) and its ntfy category switch.
const RESPONSE_ROWS=[
  ['torEmMode','ntfyCatTornadoEmergency','Tornado Emergency','Catastrophic damage threat','#A855F7'],
  ['torCfMode','ntfyCatTornadoConfirmed','Tornado Warning — confirmed','Observed, or carries a damage tag','#A855F7'],
  ['torUcMode','ntfyCatTornadoUnconfirmed','Tornado Warning — unconfirmed','Radar-indicated, no damage tag','#EF4444'],
  ['svrDeMode','ntfyCatThunderstormDestructive','Severe T-storm — destructive','Destructive damage threat','#EF4444'],
  ['svrCoMode','ntfyCatThunderstormConsiderable','Severe T-storm — considerable','Considerable damage threat','#EF4444'],
  ['svrBaMode','ntfyCatThunderstormBase','Severe T-storm — base','No damage tag','#F97316'],
  ['otherMode','ntfyCatOtherExtreme','Other Extreme Emergency','Only the types checked below','#A855F7'],
];
const MODES=['OFF','WAIL','ATTACK','FASTWAIL'];
(function renderMatrix(){
  document.getElementById('matrix').insertAdjacentHTML('beforeend',RESPONSE_ROWS.map(([sel,cat,name,detail,col])=>
    '<div class="mrow"><div class="mname"><span class="sw" style="background:'+col+'"></span><div><b>'+name+'</b><span>'+detail+'</span></div></div>'+
    '<div class="seg" role="radiogroup" aria-label="'+name+' siren mode">'+
      MODES.map(m=>'<input type="radio" name="'+sel+'" id="'+sel+'_'+m+'" value="'+m+'"><label for="'+sel+'_'+m+'" data-v="'+m+'">'+(m==='FASTWAIL'?'FAST WAIL':m)+'</label>').join('')+
    '</div><div class="mpush"><input type="checkbox" id="'+cat+'" aria-label="Push notification for '+name+'"></div></div>'
  ).join(''));
})();
function setMode(sel,v){const el=document.getElementById(sel+'_'+(v||'OFF'))||document.getElementById(sel+'_OFF');el.checked=true;}
function getMode(sel){const el=document.querySelector('input[name="'+sel+'"]:checked');return el?el.value:'OFF';}

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
  setMode('torUcMode',d.tornadoUnconfirmedMode||'OFF');
  setMode('torCfMode',d.tornadoConfirmedMode||'WAIL');
  setMode('torEmMode',d.tornadoEmergencyMode||'WAIL');
  setMode('svrBaMode',d.thunderstormBaseMode||'OFF');
  setMode('svrCoMode',d.thunderstormConsiderableMode||'WAIL');
  setMode('svrDeMode',d.thunderstormDestructiveMode||'WAIL');
  setMode('otherMode',d.otherExtremeMode||'OFF');
  renderOtherList(d.otherExtremeIncluded);
  document.getElementById('repeatUpg').checked=!!d.repeatOnUpgrade;
  document.getElementById('ntpServer').value=d.ntpServer||'pool.ntp.org';
  document.getElementById('ntpUpdateHours').value=d.ntpUpdateHours||12;
  document.getElementById('timeZone').value=d.timeZone||'EASTERN';
  document.getElementById('autoDst').checked=d.autoDst!==false;
  document.getElementById('verNum').textContent=d.fwVersion||'-';
  document.getElementById('rstReason').textContent=d.resetReason||'-';

  document.getElementById('ntfyEnabled').checked=!!d.ntfyEnabled;
  document.getElementById('ntfyServer').value=d.ntfyServer||'https://ntfy.sh';
  document.getElementById('ntfyTopic').value=d.ntfyTopic||'';
  document.getElementById('ntfyTokenHint').textContent=d.ntfyHasToken?'A token is saved (leave blank to keep it).':'No token saved.';
  document.getElementById('ntfyVerifyCert').checked=d.ntfyVerifyCert!==false;
  RESPONSE_ROWS.forEach(r=>{document.getElementById(r[1]).checked=!!d[r[1]];});
  document.getElementById('ntfyNotifyErrors').checked=!!d.ntfyNotifyErrors;
  document.getElementById('ntfyNotifySrcLocal').checked=!!d.ntfyNotifySrcLocal;
  document.getElementById('ntfyNotifySrcWeb').checked=!!d.ntfyNotifySrcWeb;
  document.getElementById('ntfyNotifySrcMesh').checked=!!d.ntfyNotifySrcMesh;
  document.getElementById('ntfyNotifyStop').checked=!!d.ntfyNotifyStop;
  document.getElementById('ntfyNotifySystemEvents').checked=!!d.ntfyNotifySystemEvents;
  document.getElementById('ntfyNotifyStatus').checked=!!d.ntfyNotifyStatus;
});

fetch('/wifi-data').then(r=>r.json()).then(d=>{
  const el=document.getElementById('ws');
  if(d.connected){el.textContent='Connected: '+d.ssid+' ('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
  document.getElementById('wHost').value=d.hostname||'weather-watcher';
  if(!d.apSecured)document.getElementById('apHint').textContent+=' The access point is currently OPEN (no password).';
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
function modesBody(){
  return {
    tornadoUnconfirmedMode: getMode('torUcMode'),
    tornadoConfirmedMode: getMode('torCfMode'),
    tornadoEmergencyMode: getMode('torEmMode'),
    thunderstormBaseMode: getMode('svrBaMode'),
    thunderstormConsiderableMode: getMode('svrCoMode'),
    thunderstormDestructiveMode: getMode('svrDeMode'),
    otherExtremeMode: getMode('otherMode'),
    otherExtremeIncluded: collectOtherIncluded(),
    repeatOnUpgrade: document.getElementById('repeatUpg').checked,
  };
}
function saveModes(msgId){
  post('/settings-data',modesBody()).then(d=>msg(msgId||'mm',d.ok?'Saved!':'Error',d.ok));
}
// The Alert response table saves the siren modes and the per-category
// ntfy switches together — /settings-data applies whichever keys it gets.
function saveResponse(){
  const b=modesBody();
  RESPONSE_ROWS.forEach(r=>{b[r[1]]=document.getElementById(r[1]).checked;});
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

function saveNtfy(){
  const b={
    ntfyEnabled: document.getElementById('ntfyEnabled').checked,
    ntfyServer: document.getElementById('ntfyServer').value.trim(),
    ntfyTopic: document.getElementById('ntfyTopic').value.trim(),
    ntfyVerifyCert: document.getElementById('ntfyVerifyCert').checked,
    ntfyNotifyErrors: document.getElementById('ntfyNotifyErrors').checked,
    ntfyNotifySrcLocal: document.getElementById('ntfyNotifySrcLocal').checked,
    ntfyNotifySrcWeb: document.getElementById('ntfyNotifySrcWeb').checked,
    ntfyNotifySrcMesh: document.getElementById('ntfyNotifySrcMesh').checked,
    ntfyNotifyStop: document.getElementById('ntfyNotifyStop').checked,
    ntfyNotifySystemEvents: document.getElementById('ntfyNotifySystemEvents').checked,
    ntfyNotifyStatus: document.getElementById('ntfyNotifyStatus').checked,
  };
  RESPONSE_ROWS.forEach(r=>{b[r[1]]=document.getElementById(r[1]).checked;});
  const tok=document.getElementById('ntfyToken').value;
  const saveMain=()=>post('/settings-data',b);
  const saveTok=tok.length?post('/ntfy-token',{token:tok}):Promise.resolve({ok:true});
  Promise.all([saveMain(),saveTok]).then(r=>{
    document.getElementById('ntfyToken').value='';
    msg('ntfyMsg', r.every(x=>x.ok)?'Saved!':'Error', r.every(x=>x.ok));
  });
}
function testNtfy(){
  post('/ntfy-test',{}).then(()=>msg('ntfyMsg','Test queued — check your device in a few seconds',true));
}
function saveApPw(){
  const pw=document.getElementById('apPw').value;
  if(pw.length<8||pw.length>63){msg('apm','Must be 8-63 characters',false);return;}
  if(!confirm('Change the access point password? The board will restart, and anything joined to its network will need the new password.'))return;
  post('/ap-password',{password:pw}).then(d=>msg('apm',d.ok?'Saved — restarting…':'Failed',d.ok));
}
function uploadFw(){
  const f=document.getElementById('fwFile').files[0];
  const el=document.getElementById('fwm');
  if(!f){msg('fwm','Choose a .bin file first',false);return;}
  if(!confirm('Upload '+f.name+' and restart the Weather Watcher?'))return;
  const fd=new FormData();fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest();x.open('POST','/update');
  x.upload.onprogress=e=>{if(e.lengthComputable){el.className='msg';el.textContent='Uploading… '+Math.round(e.loaded*100/e.total)+'%';}};
  x.onload=()=>{let d={};try{d=JSON.parse(x.responseText);}catch(err){}
    if(d.ok){el.className='msg ok';el.textContent='Update installed — restarting…';setTimeout(()=>location.reload(),15000);}
    else{el.className='msg er';el.textContent='Update failed: '+(d.error||('HTTP '+x.status));}};
  x.onerror=()=>{el.className='msg er';el.textContent='Upload failed — connection lost';};
  x.send(fd);
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

    void update() { checkOtaStall(millis()); }

private:
    static constexpr size_t   MAX_BODY     = 1024;   // largest JSON POST body accepted
    static constexpr uint8_t  MAX_SESSIONS = 4;      // oldest login is evicted beyond this
    static constexpr uint32_t OTA_STALL_TIMEOUT_MS = 30000;

    AsyncWebServer   server_{80};

    // Fixed-size session table (mirrors the main board) — replaces an
    // ever-growing std::set that gained a String on every login.
    char     sessions_[MAX_SESSIONS][33] = {};
    uint8_t  nextSession_ = 0;

    // ── Login rate-limiting (mirrors the main board's single-user approach) ──
    uint8_t  failedLoginAttempts_ = 0;
    uint32_t loginLockoutUntil_   = 0;   // 0 = not locked out
    static constexpr uint8_t  MAX_LOGIN_ATTEMPTS = 5;
    static constexpr uint32_t LOGIN_LOCKOUT_MS   = 30000;

    // ── OTA (per-upload result in req->_tempObject; the request frees it) ────
    struct OtaJob {
        bool owner;
        bool ok;
        char err[64];
    };
    volatile bool     otaRunning_      = false;
    volatile uint32_t otaLastDataMs_   = 0;
    volatile uint32_t otaStartMs_      = 0;
    volatile uint32_t otaBytesWritten_ = 0;

    // ── Auth helpers ─────────────────────────────────────────────────────────

    static void generateToken(char* out) {
        for (int i = 0; i < 4; i++) snprintf(out + i * 8, 9, "%08x", (unsigned)esp_random());
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

    static void unauth(AsyncWebServerRequest* req) {
        req->send(401, "application/json", "{\"error\":\"unauth\"}");
    }

    // Streams a page straight out of flash — the const char* overload of
    // send() copies the whole page into a heap String on every request.
    static void sendPage(AsyncWebServerRequest* req, const char* page, const char* type = "text/html") {
        req->send(200, type, (const uint8_t*)page, strlen(page));
    }

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
        Serial.printf("OTA upload stalled - aborted (%u bytes written, %ldms since last data, %ldms total)\n",
            (unsigned)otaBytesWritten_, (long)(int32_t)(now - otaLastDataMs_), (long)(int32_t)(now - otaStartMs_));
    }

    void otaFail(OtaJob* job, const char* stage, const char* err) {
        Serial.printf("OTA %s failed: %s (%u bytes written)\n", stage, err, (unsigned)otaBytesWritten_);
        strlcpy(job->err, err, sizeof(job->err));
        if (Update.isRunning()) Update.abort();
    }

    static void clampFloat(JsonDocument& doc, const char* key, float& field, float lo, float hi) {
        if (!doc[key].is<double>()) return;
        float v = doc[key].as<float>();
        field = v < lo ? lo : v > hi ? hi : v;
    }

    static void clampUInt(JsonDocument& doc, const char* key, uint32_t& field, uint32_t lo, uint32_t hi) {
        if (!doc[key].is<double>()) return;
        double v = doc[key].as<double>();
        field = (uint32_t)(v < lo ? lo : v > hi ? hi : v);
    }

    // Trigger modes are only ever one of the four tokens the main board understands.
    static void applyMode(JsonDocument& doc, const char* key, char* field, size_t size) {
        if (!doc[key].is<const char*>()) return;
        const char* m = doc[key].as<const char*>();
        if (!strcmp(m, "OFF") || !strcmp(m, "WAIL") || !strcmp(m, "ATTACK") || !strcmp(m, "FASTWAIL"))
            strlcpy(field, m, size);
    }

    void redirectLogin(AsyncWebServerRequest* req) {
        AsyncWebServerResponse* r = req->beginResponse(302);
        r->addHeader("Location", "/login");
        req->send(r);
    }

    // Bodies over MAX_BODY are dropped without allocating, so a bogus
    // Content-Length (even on the unauthenticated login route) can't
    // exhaust the heap.
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

    // ── Route setup ──────────────────────────────────────────────────────────

    void setupRoutes() {
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
        server_.on("/dashboard-hero.png", HTTP_GET, [](AsyncWebServerRequest* req) {
            AsyncWebServerResponse* r = req->beginResponse(200, "image/png", DASHBOARD_HERO_PNG, DASHBOARD_HERO_PNG_LEN);
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

        server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // Public dashboard — status/alert awareness needs no login; only
            // Settings and anything that changes device state is gated.
            sendPage(req, DASHBOARD_HTML);
        });

        server_.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { redirectLogin(req); return; }
            sendPage(req, SETTINGS_HTML);
        });

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
                    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", "{\"ok\":true}");
                    resp->addHeader("Set-Cookie", String("sid=") + token + "; Path=/; HttpOnly; SameSite=Strict");
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
                        clearSessions();
                        ok = true;
                    }
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            },
            nullptr, bodyAccumulator
        );

        // Write-only, same pattern as /password — never echoed back by
        // /settings-data, never logged (ground rule 4).
        server_.on("/ntfy-token", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                if (req->_tempObject) {
                    JsonDocument doc;
                    deserializeJson(doc, (const char*)req->_tempObject);
                    free(req->_tempObject);
                    req->_tempObject = nullptr;
                    const char* tok = doc["token"] | "";
                    CtrlLock lock;
                    strlcpy(settingsMgr.s.ntfyToken, tok, sizeof(settingsMgr.s.ntfyToken));
                    settingsMgr.save();
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/ntfy-test", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            ntfy.notify(3, "Test notification", "This is a test push from your Weather Watcher board.");
            req->send(200, "application/json", "{\"ok\":true}");
        });

        server_.on("/ntfy-status", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            JsonDocument doc;
            doc["queued"] = ntfy.queued();
            doc["lastOk"] = ntfy.lastSendOk();
            doc["lastDetail"] = ntfy.lastSendDetail();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        server_.on("/status-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            // Public — feeds the unauthenticated dashboard.
            req->send(200, "application/json", buildStatusJson());
        });

        // WP8's polygon mini-map location dot — gated to logged-in sessions
        // (Decision C). The polygon itself comes straight from NWS's public
        // API fetched client-side; only this board's exact coordinates are
        // login-gated.
        server_.on("/location-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            JsonDocument doc;
            doc["lat"] = settingsMgr.s.latitude;
            doc["lon"] = settingsMgr.s.longitude;
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        server_.on("/settings-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            req->send(200, "application/json", buildSettingsJson());
        });

        // Full persisted alert-log export (auth required) — streams the
        // whole LittleFS-backed history, not just the dashboard's last 20.
        server_.on("/alertlog.csv", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            AsyncResponseStream* r = req->beginResponseStream("text/csv");
            r->addHeader("Content-Disposition", "attachment; filename=alertlog.csv");
            writeAlertLogCsv(*r);
            req->send(r);
        });

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
                    // Range-checked: a 0-second poll interval would hammer
                    // api.weather.gov (and get this device rate-limited), and
                    // ntpUpdateHours * 3600000 overflows above ~1193 hours.
                    clampFloat(doc, "latitude",  s.latitude,  -90.0f,  90.0f);
                    clampFloat(doc, "longitude", s.longitude, -180.0f, 180.0f);
                    clampUInt(doc, "pollIntervalSec", s.pollIntervalSec, 30, 3600);
                    if (doc["userAgentContact"].is<const char*>())
                        strlcpy(s.userAgentContact, doc["userAgentContact"].as<const char*>(), sizeof(s.userAgentContact));
                    applyMode(doc, "tornadoUnconfirmedMode", s.tornadoUnconfirmedMode, sizeof(s.tornadoUnconfirmedMode));
                    applyMode(doc, "tornadoConfirmedMode", s.tornadoConfirmedMode, sizeof(s.tornadoConfirmedMode));
                    applyMode(doc, "tornadoEmergencyMode", s.tornadoEmergencyMode, sizeof(s.tornadoEmergencyMode));
                    applyMode(doc, "thunderstormBaseMode", s.thunderstormBaseMode, sizeof(s.thunderstormBaseMode));
                    applyMode(doc, "thunderstormConsiderableMode", s.thunderstormConsiderableMode, sizeof(s.thunderstormConsiderableMode));
                    applyMode(doc, "thunderstormDestructiveMode", s.thunderstormDestructiveMode, sizeof(s.thunderstormDestructiveMode));
                    applyMode(doc, "otherExtremeMode", s.otherExtremeMode, sizeof(s.otherExtremeMode));
                    if (doc["otherExtremeIncluded"].is<const char*>())
                        strlcpy(s.otherExtremeIncluded, doc["otherExtremeIncluded"].as<const char*>(), sizeof(s.otherExtremeIncluded));
                    if (doc["repeatOnUpgrade"].is<bool>()) s.repeatOnUpgrade = doc["repeatOnUpgrade"].as<bool>();
                    if (doc["ntpServer"].is<const char*>())
                        strlcpy(s.ntpServer, doc["ntpServer"].as<const char*>(), sizeof(s.ntpServer));
                    clampUInt(doc, "ntpUpdateHours", s.ntpUpdateHours, 1, 168);
                    if (doc["timeZone"].is<const char*>())
                        strlcpy(s.timeZone, doc["timeZone"].as<const char*>(), sizeof(s.timeZone));
                    if (doc["autoDst"].is<bool>()) s.autoDst = doc["autoDst"].as<bool>();

                    if (doc["ntfyEnabled"].is<bool>()) s.ntfyEnabled = doc["ntfyEnabled"].as<bool>();
                    if (doc["ntfyServer"].is<const char*>())
                        strlcpy(s.ntfyServer, doc["ntfyServer"].as<const char*>(), sizeof(s.ntfyServer));
                    if (doc["ntfyTopic"].is<const char*>())
                        strlcpy(s.ntfyTopic, doc["ntfyTopic"].as<const char*>(), sizeof(s.ntfyTopic));
                    if (doc["ntfyVerifyCert"].is<bool>()) s.ntfyVerifyCert = doc["ntfyVerifyCert"].as<bool>();
                    if (doc["ntfyCatThunderstormBase"].is<bool>()) s.ntfyCatThunderstormBase = doc["ntfyCatThunderstormBase"].as<bool>();
                    if (doc["ntfyCatOtherExtreme"].is<bool>()) s.ntfyCatOtherExtreme = doc["ntfyCatOtherExtreme"].as<bool>();
                    if (doc["ntfyCatThunderstormConsiderable"].is<bool>()) s.ntfyCatThunderstormConsiderable = doc["ntfyCatThunderstormConsiderable"].as<bool>();
                    if (doc["ntfyCatTornadoUnconfirmed"].is<bool>()) s.ntfyCatTornadoUnconfirmed = doc["ntfyCatTornadoUnconfirmed"].as<bool>();
                    if (doc["ntfyCatThunderstormDestructive"].is<bool>()) s.ntfyCatThunderstormDestructive = doc["ntfyCatThunderstormDestructive"].as<bool>();
                    if (doc["ntfyCatTornadoConfirmed"].is<bool>()) s.ntfyCatTornadoConfirmed = doc["ntfyCatTornadoConfirmed"].as<bool>();
                    if (doc["ntfyCatTornadoEmergency"].is<bool>()) s.ntfyCatTornadoEmergency = doc["ntfyCatTornadoEmergency"].as<bool>();
                    if (doc["ntfyNotifyErrors"].is<bool>()) s.ntfyNotifyErrors = doc["ntfyNotifyErrors"].as<bool>();
                    if (doc["ntfyNotifySrcLocal"].is<bool>()) s.ntfyNotifySrcLocal = doc["ntfyNotifySrcLocal"].as<bool>();
                    if (doc["ntfyNotifySrcWeb"].is<bool>()) s.ntfyNotifySrcWeb = doc["ntfyNotifySrcWeb"].as<bool>();
                    if (doc["ntfyNotifySrcMesh"].is<bool>()) s.ntfyNotifySrcMesh = doc["ntfyNotifySrcMesh"].as<bool>();
                    if (doc["ntfyNotifyStop"].is<bool>()) s.ntfyNotifyStop = doc["ntfyNotifyStop"].as<bool>();
                    if (doc["ntfyNotifySystemEvents"].is<bool>()) s.ntfyNotifySystemEvents = doc["ntfyNotifySystemEvents"].as<bool>();
                    if (doc["ntfyNotifyStatus"].is<bool>()) s.ntfyNotifyStatus = doc["ntfyNotifyStatus"].as<bool>();

                    settingsMgr.save();
                    applyTimeConfig();
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/wifi-data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
            JsonDocument doc;
            doc["mode"]      = (wifiMgr.getMode() == WiFiManager::Mode::STA) ? "sta" : "ap";
            doc["connected"] = wifiMgr.isConnected();
            doc["ssid"]      = wifiMgr.getSSID();
            doc["ip"]        = wifiMgr.getIP();
            doc["hostname"]  = wifiMgr.getHostname();
            doc["apSecured"] = wifiMgr.apSecured();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

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

        // OTA firmware upload (auth required)
        server_.on("/update", HTTP_POST,
            [this](AsyncWebServerRequest* req) {
                if (!isAuthed(req)) { unauth(req); return; }
                OtaJob* job = (OtaJob*)req->_tempObject;
                bool ok = job && job->ok;
                if (job && job->owner && !ok) {
                    if (Update.isRunning()) Update.abort();
                    otaRunning_ = false;
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
                    if (otaRunning_) { otaFail(job, "auth", "Another update is already in progress"); return; }
                    otaRunning_      = true;
                    job->owner       = true;
                    otaStartMs_      = millis();
                    otaLastDataMs_   = otaStartMs_;
                    otaBytesWritten_ = 0;
                    Serial.printf("OTA upload started (free heap: %u bytes, content-length: %u bytes)\n",
                        (unsigned)ESP.getFreeHeap(), (unsigned)req->contentLength());
                    // A known size (even just the multipart request's
                    // Content-Length, a bit larger than the true firmware
                    // size) lets IDF erase only what's needed instead of the
                    // whole OTA partition up front.
                    if (!Update.begin(req->contentLength(), U_FLASH)) { otaFail(job, "begin", Update.errorString()); return; }
                }
                if (!job || !job->owner || job->err[0]) return;
                otaLastDataMs_ = millis();
                if (len && Update.write(data, len) != len) { otaFail(job, "write", Update.errorString()); return; }
                otaBytesWritten_ += len;
                if (final) {
                    if (Update.end(true)) {
                        job->ok = true;
                        Serial.printf("OTA upload complete (%u bytes) - restarting\n", (unsigned)(index + len));
                        wifiMgr.scheduleRestart(1500);
                    } else {
                        otaFail(job, "end", Update.errorString());
                    }
                }
            }
        );

        server_.on("/restart", HTTP_POST, [this](AsyncWebServerRequest* req) {
            if (!isAuthed(req)) { unauth(req); return; }
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
        CtrlLock lock;
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

        doc["ntfyEnabled"]    = s.ntfyEnabled;
        doc["ntfyServer"]     = s.ntfyServer;
        doc["ntfyTopic"]      = s.ntfyTopic;
        doc["ntfyHasToken"]   = s.ntfyToken[0] != '\0'; // never the token itself (ground rule 4)
        doc["ntfyVerifyCert"] = s.ntfyVerifyCert;
        doc["ntfyCatThunderstormBase"]         = s.ntfyCatThunderstormBase;
        doc["ntfyCatOtherExtreme"]             = s.ntfyCatOtherExtreme;
        doc["ntfyCatThunderstormConsiderable"] = s.ntfyCatThunderstormConsiderable;
        doc["ntfyCatTornadoUnconfirmed"]       = s.ntfyCatTornadoUnconfirmed;
        doc["ntfyCatThunderstormDestructive"]  = s.ntfyCatThunderstormDestructive;
        doc["ntfyCatTornadoConfirmed"]         = s.ntfyCatTornadoConfirmed;
        doc["ntfyCatTornadoEmergency"]         = s.ntfyCatTornadoEmergency;
        doc["ntfyNotifyErrors"]   = s.ntfyNotifyErrors;
        doc["ntfyNotifySrcLocal"] = s.ntfyNotifySrcLocal;
        doc["ntfyNotifySrcWeb"]   = s.ntfyNotifySrcWeb;
        doc["ntfyNotifySrcMesh"]  = s.ntfyNotifySrcMesh;
        doc["ntfyNotifyStop"]         = s.ntfyNotifyStop;
        doc["ntfyNotifySystemEvents"] = s.ntfyNotifySystemEvents;
        doc["ntfyNotifyStatus"]       = s.ntfyNotifyStatus;

        doc["resetReason"]      = resetReasonName();
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

    // UTC ISO-8601 for the exported CSV — no local-time/DST ambiguity in a
    // file, and no coordinates/names anywhere in it (ground rule 5).
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
    static String csvEscape(const char* s) {
        String out(s);
        out.replace("\"", "\"\"");
        return out;
    }

    // Streams the full persisted alert log, line-by-line — bounded memory
    // regardless of file size. A torn trailing line (power loss mid-write)
    // is skipped rather than aborting the export.
    static void writeAlertLogCsv(Print& out) {
        out.println("seq,time_utc,vtec_key,event,category,siren_mode,disposition,attempts,escalation,notified,detail");
        File f = alertLog.openForExport();
        if (!f) return;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            if (!line.length()) continue;
            JsonDocument doc;
            if (deserializeJson(doc, line)) continue;
            uint32_t seq  = doc["seq"] | 0;
            uint32_t ep   = doc["ep"]  | 0;
            const char* vk   = doc["vk"]   | "";
            const char* ev   = doc["ev"]   | "";
            uint8_t cat      = doc["cat"]  | 0;
            const char* mode = doc["mode"] | "";
            const char* disp = doc["disp"] | "";
            uint8_t att      = doc["att"]  | 0;
            bool esc         = doc["esc"]  | false;
            bool ntf         = doc["ntf"]  | false;
            const char* det  = doc["det"]  | "";
            out.printf("%u,%s,%s,\"%s\",%u,%s,%s,%u,%s,%s,\"%s\"\n",
                seq, isoUtc(ep).c_str(), vk, csvEscape(ev).c_str(), cat, mode, disp, att,
                esc ? "true" : "false", ntf ? "true" : "false", csvEscape(det).c_str());
        }
        f.close();
    }

    // Public, polled every second — reads the alert lists poll() rewrites on
    // the loop task, so it holds the same lock.
    // Damage-threat pill text for the dashboard. TORNADO_CONFIRMED covers both
    // "observed" and a sub-catastrophic damage tag, so it reads CONFIRMED.
    static const char* categoryTag(AlertCategory c) {
        switch (c) {
        case AlertCategory::TORNADO_EMERGENCY:         return "CATASTROPHIC";
        case AlertCategory::TORNADO_CONFIRMED:         return "CONFIRMED";
        case AlertCategory::THUNDERSTORM_DESTRUCTIVE:  return "DESTRUCTIVE";
        case AlertCategory::THUNDERSTORM_CONSIDERABLE: return "CONSIDERABLE";
        default:                                       return "";
        }
    }

    static String buildStatusJson() {
        CtrlLock lock;
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
        doc["sirenLegacyProtocol"] = nwsClient.sirenLegacyProtocol();

        // Diagnostic: the raw text of the most recent "SX ..." line
        // received from the main board, regardless of which notification
        // toggles are on — lets you confirm what's actually arriving over
        // the wire straight from the dashboard, no serial connection
        // needed. -1 sentinel for "nothing received yet".
        doc["lastSxEvent"] = nwsClient.lastSxEvent();
        doc["lastSxEventAgeSec"] = nwsClient.lastSxEventMs()
            ? (int32_t)((millis() - nwsClient.lastSxEventMs()) / 1000) : -1;

        doc["stale"]       = nwsClient.stale();
        doc["dataAgeSec"]  = nwsClient.dataAgeSec();

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
            o["tag"]       = categoryTag(a.category);
            o["triggered"] = a.triggeredSiren;
            if (a.triggeredSiren) {
                // The mode actually sent, from the most recent successful
                // trigger logged for this warning (recent(0) is newest).
                for (uint8_t j = 0; j < alertLog.count(); j++) {
                    const AlertLogEntry& r = alertLog.recent(j);
                    if (strcmp(r.vtecKey, a.vtecKey) == 0 && strstr(r.disposition, "TRIGGERED")) {
                        o["mode"] = r.sirenMode;
                        break;
                    }
                }
            }
        }

        // Full persisted disposition history (WP3's alertlog.h) — up to the
        // last 20 queue outcomes, not just successful triggers, so AUTO_OFF/
        // NO_RESPONSE/GAVE_UP_* show up here too for troubleshooting.
        JsonArray rec = doc["recentAlerts"].to<JsonArray>();
        for (uint8_t i = 0; i < alertLog.count(); i++) {
            const AlertLogEntry& r = alertLog.recent(i);
            JsonObject o = rec.add<JsonObject>();
            o["event"]       = r.event;
            o["mode"]        = r.sirenMode;
            o["disposition"] = r.disposition;
            o["vtecKey"]     = r.vtecKey;
            o["time"]        = (r.epoch > 0) ? formatEpoch(r.epoch, "%Y-%m-%d %H:%M:%S") : String("Unknown time");
        }

        String out;
        serializeJson(doc, out);
        return out;
    }
};

extern WeatherWebUI webUI;
