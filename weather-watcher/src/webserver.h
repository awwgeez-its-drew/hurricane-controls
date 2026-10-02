#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "settings.h"
#include "wifi_manager.h"

// Minimal, unauthenticated settings UI for the Weather Watcher board. Unlike
// the main Hurricane Controls board, this device doesn't drive any hardware
// of consequence (no relays, no siren) — it only ever sends a short text
// command to the main board over a dedicated wire — so a login system
// wasn't judged worth the added complexity. It's meant to live on a trusted
// home network, same as the main board's own open WiFi AP (see that
// project's README "Network security note").
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Watcher</title>
<style>
:root{--bg:#848482;--surface:#0e1320;--cyan:#00d4ff;--green:#4ade80}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg)}
body{color:#eaeaea;font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;min-height:100vh}
.navbar{position:fixed;top:0;left:0;right:0;height:56px;background:var(--surface);
        border-bottom:1px solid #1b2438;display:flex;align-items:center;padding:0 16px}
.navbar h1{font-size:1.05rem;color:var(--cyan)}
.content{max-width:480px;margin:0 auto;padding:76px 16px 40px}
.card{background:var(--surface);border-radius:4px;padding:20px;margin-bottom:14px;
      box-shadow:0 1px 3px rgba(0,0,0,.35)}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:1px;color:#9aa3af;margin-bottom:14px}
.hint{font-size:.78rem;color:#9aa3af;margin-bottom:12px}
.row{margin-bottom:10px}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px 16px}
label{font-size:.78rem;color:#9aa3af;display:block;margin-bottom:3px}
input,select{width:100%;background:#0a0e18;color:#eaeaea;border:1px solid #2d2d4e;
      border-radius:4px;padding:9px 11px;font-size:.9rem;font-family:inherit}
input:focus,select:focus{outline:none;border-color:var(--cyan)}
.btn{width:100%;margin-top:12px;padding:12px;font-weight:700;border:none;
     border-radius:4px;cursor:pointer;font-size:.95rem;letter-spacing:.5px;font-family:inherit}
.btn:active{opacity:.8}
.save{background:var(--green);color:#04220f}
.danger{background:#7f1d1d;color:#fff;margin-top:6px}
.msg{font-size:.8rem;margin-top:6px;min-height:1.1em;text-align:center}
.ok{color:var(--green)}.er{color:#f87171}
.ws{font-size:.8rem;padding:9px 12px;background:#0a0e18;border-radius:4px;
    margin-bottom:12px;color:#9aa3af;border-left:3px solid transparent}
.ws.ok{color:var(--green);border-left-color:var(--green)}
.ver{text-align:center;color:#c9cdd3;font-size:.72rem;margin:6px 0 4px;padding:8px;
     background:var(--surface);border-radius:4px}
</style></head>
<body>
<div class="navbar"><h1>Weather Watcher</h1></div>
<div class="content">

<div class="card">
  <h2>Wi-Fi</h2>
  <div class="ws" id="ws">Checking...</div>
  <div class="row"><label>Network SSID</label><input type="text" id="wSSID" placeholder="Your WiFi name"></div>
  <div class="row"><label>Password</label><input type="password" id="wPass" placeholder="WiFi password"></div>
  <button class="btn save" onclick="saveWifi()">Connect to Network</button>
  <button class="btn danger" onclick="clearWifi()">Use AP Mode Only</button>
  <div class="msg" id="wm"></div>
</div>

<div class="card">
  <h2>Alert Location</h2>
  <p class="hint">Point coordinates, not a county/zip — this is what lets a storm-based warning polygon be matched precisely instead of your whole county.</p>
  <div class="grid2">
    <div><label>Latitude</label><input type="text" id="lat" placeholder="e.g. 35.4676"></div>
    <div><label>Longitude</label><input type="text" id="lon" placeholder="e.g. -97.5164"></div>
  </div>
  <div class="row"><label>Poll interval (seconds)</label><input type="number" id="pollSec" min="30" step="1"></div>
  <div class="row"><label>User-Agent contact (required by the NWS API — your email or site)</label>
    <input type="text" id="uaContact" placeholder="e.g. you@example.com"></div>
  <button class="btn save" onclick="saveWx()">Save Alert Settings</button>
  <div class="msg" id="xm"></div>
</div>

<div class="card">
  <h2>Trigger Modes</h2>
  <p class="hint">Which Hurricane Controls run mode to request for each qualifying alert type. Sent as "WX &lt;MODE&gt;" over the dedicated UART link.</p>
  <div class="row"><label>Confirmed/PDS/Emergency Tornado Warning</label>
    <select id="torMode"><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <div class="row"><label>Considerable/Destructive Severe T-storm Warning</label>
    <select id="tsMode"><option>WAIL</option><option>ATTACK</option><option>FASTWAIL</option></select></div>
  <button class="btn save" onclick="saveModes()">Save Trigger Modes</button>
  <div class="msg" id="mm"></div>
</div>

<div class="ver mono">Weather Watcher &middot; v<span id="verNum">&mdash;</span></div>

</div>
<script>
function post(url,body){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},
  body:JSON.stringify(body)}).then(r=>r.json());}
function msg(id,txt,ok){const e=document.getElementById(id);e.textContent=txt;e.className='msg '+(ok?'ok':'er');}

fetch('/settings-data').then(r=>r.json()).then(d=>{
  document.getElementById('lat').value=d.latitude||'';
  document.getElementById('lon').value=d.longitude||'';
  document.getElementById('pollSec').value=d.pollIntervalSec||120;
  document.getElementById('uaContact').value=d.userAgentContact||'';
  document.getElementById('torMode').value=d.tornadoMode||'WAIL';
  document.getElementById('tsMode').value=d.thunderstormMode||'WAIL';
  document.getElementById('verNum').textContent=d.fwVersion||'-';
});

fetch('/wifi-data').then(r=>r.json()).then(d=>{
  const el=document.getElementById('ws');
  if(d.connected){el.textContent='Connected: '+d.ssid+' ('+d.ip+')';el.className='ws ok';}
  else if(d.ssid){el.textContent='Not connected — last: '+d.ssid;el.className='ws';}
  else{el.textContent='AP mode — '+d.ip;el.className='ws';}
  if(d.ssid)document.getElementById('wSSID').value=d.ssid;
});

function saveWifi(){
  const ssid=document.getElementById('wSSID').value.trim();
  const pass=document.getElementById('wPass').value;
  post('/wifi-data',{ssid,pass}).then(()=>msg('wm','Saved — rebooting...',true));
}
function clearWifi(){
  post('/wifi-data',{clear:true}).then(()=>msg('wm','Cleared — rebooting into AP mode...',true));
}
function saveWx(){
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
    tornadoMode: document.getElementById('torMode').value,
    thunderstormMode: document.getElementById('tsMode').value,
  };
  post('/settings-data',b).then(d=>msg('mm',d.ok?'Saved!':'Error',d.ok));
}
</script>
</body></html>
)rawliteral";

class WeatherWebUI {
public:
    void begin() {
        server_.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "text/html", INDEX_HTML);
        });

        server_.on("/settings-data", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(200, "application/json", buildSettingsJson());
        });

        server_.on("/settings-data", HTTP_POST,
            [](AsyncWebServerRequest* req) {
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
                    if (doc["tornadoMode"].is<const char*>())
                        strlcpy(s.tornadoMode, doc["tornadoMode"].as<const char*>(), sizeof(s.tornadoMode));
                    if (doc["thunderstormMode"].is<const char*>())
                        strlcpy(s.thunderstormMode, doc["thunderstormMode"].as<const char*>(), sizeof(s.thunderstormMode));
                    settingsMgr.save();
                }
                req->send(200, "application/json", "{\"ok\":true}");
            },
            nullptr, bodyAccumulator
        );

        server_.on("/wifi-data", HTTP_GET, [](AsyncWebServerRequest* req) {
            JsonDocument doc;
            doc["mode"]      = (wifiMgr.getMode() == WiFiManager::Mode::STA) ? "sta" : "ap";
            doc["connected"] = wifiMgr.isConnected();
            doc["ssid"]      = wifiMgr.getSSID();
            doc["ip"]        = wifiMgr.getIP();
            String out;
            serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        server_.on("/wifi-data", HTTP_POST,
            [](AsyncWebServerRequest* req) {
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

        server_.begin();
    }

    void update() {}

private:
    AsyncWebServer server_{80};

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

    static String buildSettingsJson() {
        const Settings& s = settingsMgr.s;
        JsonDocument doc;
        doc["latitude"]         = s.latitude;
        doc["longitude"]        = s.longitude;
        doc["pollIntervalSec"]  = s.pollIntervalSec;
        doc["tornadoMode"]      = s.tornadoMode;
        doc["thunderstormMode"] = s.thunderstormMode;
        doc["userAgentContact"] = s.userAgentContact;
        doc["fwVersion"]        = FW_VERSION;
        String out;
        serializeJson(doc, out);
        return out;
    }
};

extern WeatherWebUI webUI;
