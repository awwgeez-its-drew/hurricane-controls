'use strict';
// Behavioural mock of both boards, written from src/webserver.h and
// weather-watcher/src/webserver.h. Used by the test-suite and by
// `npm run mock` so the dashboard can be developed/demoed with no hardware.
//
// It is NOT the firmware: timing is simplified. It does reproduce the parts
// the dashboard depends on: cookie sessions (max 4, oldest evicted), the
// global 5-strike/30 s login lockout, /cmd rules incl. TEST MODE blocking,
// keep-alive failsafes (manual 2 s, test component 2 s), the 500 ms WebSocket
// status push, OTA refusal while running, and CSV exports.

const http = require('http');
const crypto = require('crypto');
const { WebSocketServer } = require('ws');

function makeAuth(getPassword, opts = {}) {
  const sessions = [];
  let failed = 0;
  let lockUntil = 0;
  const maxSessions = 4;
  const lockMs = opts.lockMs || 30000;
  return {
    sessions,
    tokenOf(req) {
      const m = /(?:^|;\s*)sid=([0-9a-f]{32})/.exec(req.headers.cookie || '');
      return m ? m[1] : '';
    },
    authed(req) { const t = this.tokenOf(req); return !!t && sessions.includes(t); },
    login(pw) {
      const now = Date.now();
      if (lockUntil && now >= lockUntil) lockUntil = 0;
      if (lockUntil) return { ok: false, locked: true, retryAfter: Math.ceil((lockUntil - now) / 1000) };
      if (pw === getPassword()) {
        failed = 0;
        const tok = crypto.randomBytes(16).toString('hex');
        sessions.push(tok);
        while (sessions.length > maxSessions) sessions.shift();
        return { ok: true, tok };
      }
      if (++failed >= 5) { lockUntil = now + lockMs; failed = 0; }
      return { ok: false };
    },
    logout(req) { const t = this.tokenOf(req); const i = sessions.indexOf(t); if (i >= 0) sessions.splice(i, 1); },
    clear() { sessions.length = 0; },
  };
}

function readBody(req) {
  return new Promise((res) => { const c = []; req.on('data', (d) => c.push(d)); req.on('end', () => res(Buffer.concat(c))); });
}
const send = (res, code, obj, extra = {}) => {
  const body = typeof obj === 'string' ? obj : JSON.stringify(obj);
  res.writeHead(code, { 'Content-Type': typeof obj === 'string' ? 'text/plain' : 'application/json', ...extra });
  res.end(body);
};

// ── Siren ─────────────────────────────────────────────────────────────────
function createSiren({ port = 0, password = 'Siren123!', lockMs } = {}) {
  const S = {
    password,
    mode: 'idle', runMode: 'idle', startedAt: 0, durMs: 0,
    testMode: false, locked: false,
    relays: [0, 0, 0], held: [false, false, false], kaTs: [0, 0, 0], manualKa: 0,
    log: [], totalRuns: 3, totalSecs: 540, restarted: 0, ota: null,
    settings: {
      chopperDelay: 1000, blowerDelay: 2000, rotatorDelay: 500, wailDuration: 180000, attackDuration: 120000,
      attackOnTime: 5000, attackOffTime: 3000, fastWailDuration: 120000, fastWailOnTime: 4000, fastWailOffTime: 2000,
      stopBlowerDelay: 1000, stopChopperDelay: 0, stopRotDelay: 3000, growlBlowerTime: 3000, growlRotatorTime: 3000,
      growlChopperTime: 3000, longPressMs: 800, buttonDebounceMs: 50, meshWhitelist: 'ABCD', meshWhitelistEnabled: true,
      meshPassword: 'secret', weatherAutoTriggerEnabled: true, meshNotifyActivation: true, meshNotifySystemEvents: true,
      meshNotifyWwHealth: false, meshNotifyStatus: false, meshNotifyWatches: false, ntpServer: 'pool.ntp.org',
      ntpUpdateHours: 12, timeZone: 'CENTRAL', autoDst: true, statusTime1: '08:00', statusTime2: '', statusTime3: '',
      statusIncludeMode: true, statusIncludeLock: true, statusIncludeUptime: true, statusIncludeCpuTemp: true,
      statusIncludeWwHealth: true, statusIncludeWifi: true, statusIncludeTime: true, statusIncludeLastRun: true,
      statusIncludeAlerts: true, wwEverReported: true, wwOk: true, wwPending: false, wwDetail: 'OK', wwLost: false,
      wwLastHeardSec: 4, resetReason: 'Power on', fwVersion: '2.0.2',
    },
    wifi: { mode: 'sta', connected: true, ssid: 'HomeNet', ip: '127.0.0.1', hostname: 'hurricane', apSecured: true },
    alerts: [],
    calls: [],
  };
  const auth = makeAuth(() => S.password, { lockMs });
  const t0 = Date.now();
  const stateName = () => S.mode;
  const status = () => {
    const el = S.startedAt ? Math.floor((Date.now() - S.startedAt) / 1000) : 0;
    return {
      mode: S.mode, runMode: S.runMode, uptime: Math.floor((Date.now() - t0) / 1000), relays: S.relays.slice(),
      elapsed: el, remaining: S.durMs ? Math.max(0, Math.floor(S.durMs / 1000) - el) : 0, hasRemaining: !!S.durMs,
      btnLocked: S.locked, testMode: S.testMode, lockAutoExpire: S.testMode, lockRemaining: S.testMode ? 540 : 0, tempF: 118,
      currentTime: new Date().toTimeString().slice(0, 8),
      health: { overall: 'ok', wifi: true, ww: 'OK', freeHeapKB: 140, otaActive: !!S.ota,
        mesh: { rx: 120, ping: -1, tx: 300, from: 'abcd' } },
    };
  };
  const runStart = (m, dur) => {
    if (S.mode !== 'idle') return;
    S.mode = m; S.runMode = m; S.startedAt = Date.now(); S.durMs = dur;
    S.relays = [1, 1, 1];
    if (dur) S._runTimer = setTimeout(() => runEnd(0, 0), dur);   // manual has no duration
  };
  const runEnd = (end, stopSrc) => {
    if (S.mode === 'idle') return;
    clearTimeout(S._runTimer);
    S.log.unshift({ q: ++S.totalRuns, e: Math.floor(Date.now() / 1000), u: 10, d: Math.max(1, Math.round((Date.now() - S.startedAt) / 1000)),
      m: { wail: 1, attack: 2, fastwail: 3, manual: 4, growl: 5 }[S.runMode] || 0, s: 1, x: end, ss: stopSrc });
    S.mode = 'idle'; S.runMode = 'idle'; S.startedAt = 0; S.durMs = 0; S.relays = [0, 0, 0]; S.manualKa = 0;
  };
  const failsafe = setInterval(() => {
    if (S.runMode === 'manual' && S.manualKa && Date.now() - S.manualKa > 2000) runEnd(2, 1);
    for (let i = 0; i < 3; i++) if (S.held[i] && Date.now() - S.kaTs[i] > 2000) { S.held[i] = false; S.relays[i] = 0; }
  }, 200);

  const server = http.createServer(async (req, res) => {
    const url = req.url.split('?')[0];
    S.calls.push(req.method + ' ' + url);
    const need = () => { if (!auth.authed(req)) { send(res, 401, { error: 'unauth' }); return false; } return true; };
    try {
      if (url === '/auth/login' && req.method === 'POST') {
        const b = JSON.parse((await readBody(req)).toString() || '{}');
        const r = auth.login(b.password);
        if (r.ok) return send(res, 200, { ok: true }, { 'Set-Cookie': `sid=${r.tok}; Path=/; HttpOnly; SameSite=Strict` });
        return send(res, 200, r);
      }
      if (url === '/auth/logout') { auth.logout(req); return send(res, 200, { ok: true }, { 'Set-Cookie': 'sid=; Path=/; Max-Age=0' }); }
      if (url === '/pub-status') return send(res, 200, { s: S.mode });
      if (url === '/status' && req.method === 'GET') return need() && send(res, 200, status());
      if (url === '/alerts') return need() && send(res, 200, { receivedMs: 1000, uptimeMs: 2000, nowEpoch: Math.floor(Date.now() / 1000), alerts: S.alerts });
      if (url === '/settings-data' && req.method === 'GET') return need() && send(res, 200, S.settings);
      if (url === '/settings-data' && req.method === 'POST') {
        if (!need()) return;
        const b = JSON.parse((await readBody(req)).toString());
        S.lastSettingsPost = b;
        Object.assign(S.settings, b);
        return send(res, 200, { ok: true });
      }
      if (url === '/wifi-data' && req.method === 'GET') return need() && send(res, 200, S.wifi);
      if (url === '/wifi-data' && req.method === 'POST') { if (!need()) return; S.lastWifiPost = JSON.parse((await readBody(req)).toString()); return send(res, 200, { ok: true }); }
      if (url === '/log') return need() && send(res, 200, { runs: S.totalRuns, secs: S.totalSecs, uptime: Math.floor((Date.now() - t0) / 1000), entries: S.log.slice(0, 20) });
      if (url === '/log.csv') {
        if (!need()) return;
        res.writeHead(200, { 'Content-Type': 'text/csv' });
        return res.end('seq,start_utc,start_uptime_sec,duration_sec,mode,source,end,stop_source,alert_type,alert_id\n1,2026-01-01T00:00:00Z,5,60,WAIL,WEB,COMPLETED,LOCAL,"",""\n');
      }
      if (url === '/password' && req.method === 'POST') {
        if (!need()) return;
        const b = JSON.parse((await readBody(req)).toString());
        const ok = /^(?=.*[A-Z])(?=.*[a-z])(?=.*\d)(?=.*[^A-Za-z0-9]).{8,}$/.test(b.password || '');
        if (ok) { S.password = b.password; auth.clear(); }
        return send(res, 200, { ok });
      }
      if (url === '/ap-password' && req.method === 'POST') { if (!need()) return; const b = JSON.parse((await readBody(req)).toString()); return send(res, 200, { ok: (b.password || '').length >= 8 }); }
      if (url === '/restart' && req.method === 'POST') { if (!need()) return; S.restarted++; return send(res, 200, { ok: true }); }
      if (url === '/update' && req.method === 'POST') {
        if (!need()) return;
        const body = await readBody(req);
        if (S.mode !== 'idle') return send(res, 200, { ok: false, error: 'Siren is running - stop it first' });
        if (S.testMode) return send(res, 200, { ok: false, error: 'Turn TEST MODE off first' });
        S.ota = { bytes: body.length, head: body.subarray(0, 200).toString('latin1') };
        S.restarted++;
        return send(res, 200, { ok: true });
      }
      if (url === '/cmd' && req.method === 'POST') {
        if (!need()) return;
        const b = JSON.parse((await readBody(req)).toString());
        let ok = true;
        const blockable = ['wail', 'attack', 'fastwail', 'manual', 'growl', 'stop'];
        if (S.testMode && blockable.includes(b.mode)) ok = false;
        else if (b.mode === 'wail') runStart('wail', S.settings.wailDuration);
        else if (b.mode === 'attack') runStart('attack', S.settings.attackDuration);
        else if (b.mode === 'fastwail') runStart('fastwail', S.settings.fastWailDuration);
        else if (b.mode === 'growl') runStart('growl', 9000);
        else if (b.mode === 'manual') { runStart('manual', 0); S.manualKa = Date.now(); }
        else if (b.mode === 'manual-ka') S.manualKa = Date.now();
        else if (b.mode === 'stop') runEnd(1, 1);
        else if (b.mode === 'btn-lock') S.locked = !S.locked;
        else if (b.mode === 'test-lock') { S.testMode = !!b.on; if (!b.on) { S.relays = [0, 0, 0]; S.held = [false, false, false]; } }
        else if (b.mode === 'test') {
          ok = S.testMode && S.mode === 'idle';
          const i = ['chopper', 'blower', 'rotator'].indexOf(b.component);
          if (ok && i >= 0) { S.relays[i] = b.on ? 1 : 0; S.held[i] = !!b.on; S.kaTs[i] = Date.now(); }
        } else if (b.mode === 'test-ka') {
          const i = ['chopper', 'blower', 'rotator'].indexOf(b.component);
          if (i >= 0 && S.held[i]) S.kaTs[i] = Date.now();
        }
        return send(res, 200, { ok });
      }
      send(res, 302, '', { Location: '/' });
    } catch (e) { send(res, 500, { error: String(e) }); }
  });

  const wss = new WebSocketServer({ noServer: true });
  server.on('upgrade', (req, sock, head) => {
    if (req.url !== '/ws' || !auth.authed(req)) { sock.write('HTTP/1.1 401 Unauthorized\r\n\r\n'); sock.destroy(); return; }
    wss.handleUpgrade(req, sock, head, (ws) => wss.emit('connection', ws));
  });
  const push = setInterval(() => { const m = JSON.stringify(status()); wss.clients.forEach((c) => { if (c.readyState === 1) c.send(m); }); }, 500);

  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve({
    S, auth, port: server.address().port, host: '127.0.0.1:' + server.address().port,
    close() { clearInterval(push); clearInterval(failsafe); clearTimeout(S._runTimer); wss.clients.forEach((c) => c.terminate()); server.closeAllConnections?.(); server.close(); },
  })));
}

// ── Weather Watcher ───────────────────────────────────────────────────────
function createWW({ port = 0, password = 'Weather123!', lockMs } = {}) {
  const S = {
    password,
    settings: {
      latitude: 35.1, longitude: -97.4, pollIntervalSec: 60, tornadoUnconfirmedMode: 'WAIL', tornadoConfirmedMode: 'ATTACK',
      tornadoEmergencyMode: 'ATTACK', thunderstormBaseMode: 'OFF', thunderstormConsiderableMode: 'OFF',
      thunderstormDestructiveMode: 'WAIL', otherExtremeMode: 'OFF', otherExtremeIncluded: 'FIRE,HAZMAT', userAgentContact: 'me@example.com',
      repeatOnUpgrade: false, ntpServer: 'pool.ntp.org', ntpUpdateHours: 12, timeZone: 'CENTRAL', autoDst: true,
      ntfyEnabled: true, ntfyServer: 'https://ntfy.sh', ntfyTopic: 'mytopic', ntfyHasToken: false, ntfyVerifyCert: true,
      ntfyPriThunderstormBase: 3, ntfyPriOtherExtreme: 5, ntfyPriTornadoWatch: 4, ntfyPriSevereTstormWatch: 3,
      ntfyPriThunderstormConsiderable: 4, ntfyPriTornadoUnconfirmed: 5, ntfyPriThunderstormDestructive: 5,
      ntfyPriTornadoConfirmed: 5, ntfyPriTornadoEmergency: 5, ntfyPriFaults: 4, ntfyPriActivations: 5, ntfyPriStop: 3,
      ntfyPriLockedPress: 3, ntfyPriBoot: 2, ntfyPriScheduledStatus: 2, resetReason: 'Power on', fwVersion: '2.0.2',
    },
    wifi: { mode: 'sta', connected: true, ssid: 'HomeNet', ip: '127.0.0.1', hostname: 'weather-watcher', apSecured: true },
    currentAlerts: [
      { id: 'a1', event: 'Tornado Warning', headline: 'Tornado Warning issued', areaDesc: 'Cleveland, OK', exp: Math.floor(Date.now() / 1000) + 1800,
        color: 'red', tag: 'CONFIRMED', triggered: true, mode: 'ATTACK' },
    ],
    recent: [{ event: 'Tornado Warning', mode: 'ATTACK', disposition: 'TRIGGERED', vtecKey: 'K1', time: '2026-01-01 12:00:00' }],
    linkTests: 0, ntfyTests: 0, restarted: 0, calls: [],
  };
  const auth = makeAuth(() => S.password, { lockMs });
  const t0 = Date.now();
  const status = () => ({
    fwVersion: '2.0.2', wifiConnected: true, wifiDetail: 'Connected: HomeNet (127.0.0.1)', apiGreen: true, apiDetail: 'Status OK',
    everTestedLink: true, linkOk: true, linkDetail: 'Controller replied', sirenLegacyProtocol: false, lastSxEvent: 'SX BOOT', lastSxEventAgeSec: 30,
    stale: false, dataAgeSec: 12, currentTime: new Date().toTimeString().slice(0, 8), uptime: Math.floor((Date.now() - t0) / 1000), tempF: 110,
    lastPoll: '2026-01-01 12:00:00', currentAlerts: S.currentAlerts, recentAlerts: S.recent,
  });
  const server = http.createServer(async (req, res) => {
    const url = req.url.split('?')[0];
    S.calls.push(req.method + ' ' + url);
    const need = () => { if (!auth.authed(req)) { send(res, 401, { error: 'unauth' }); return false; } return true; };
    try {
      if (url === '/auth/login' && req.method === 'POST') {
        const b = JSON.parse((await readBody(req)).toString() || '{}');
        const r = auth.login(b.password);
        if (r.ok) return send(res, 200, { ok: true }, { 'Set-Cookie': `sid=${r.tok}; Path=/; HttpOnly; SameSite=Strict` });
        return send(res, 200, r);
      }
      if (url === '/auth/logout') { auth.logout(req); return send(res, 200, { ok: true }); }
      if (url === '/status-data') return send(res, 200, status());
      if (url === '/test-link' && req.method === 'POST') { S.linkTests++; return send(res, 200, { ok: true }); }
      if (url === '/ntfy-test' && req.method === 'POST') { if (!need()) return; S.ntfyTests++; return send(res, 200, { ok: true }); }
      if (url === '/ntfy-status') return need() && send(res, 200, { queued: 0, lastOk: true, lastDetail: 'sent' });
      if (url === '/ntfy-token' && req.method === 'POST') { if (!need()) return; S.ntfyToken = JSON.parse((await readBody(req)).toString()).token; S.settings.ntfyHasToken = !!S.ntfyToken; return send(res, 200, { ok: true }); }
      if (url === '/location-data') return need() && send(res, 200, { lat: S.settings.latitude, lon: S.settings.longitude });
      if (url === '/settings-data' && req.method === 'GET') return need() && send(res, 200, S.settings);
      if (url === '/settings-data' && req.method === 'POST') { if (!need()) return; const b = JSON.parse((await readBody(req)).toString()); S.lastSettingsPost = b; Object.assign(S.settings, b); return send(res, 200, { ok: true }); }
      if (url === '/wifi-data' && req.method === 'GET') return need() && send(res, 200, S.wifi);
      if (url === '/wifi-data' && req.method === 'POST') { if (!need()) return; S.lastWifiPost = JSON.parse((await readBody(req)).toString()); return send(res, 200, { ok: true }); }
      if (url === '/alertlog.csv') { if (!need()) return; res.writeHead(200, { 'Content-Type': 'text/csv' }); return res.end('seq,time_utc\n1,2026-01-01T12:00:00Z\n'); }
      if (url === '/password' && req.method === 'POST') {
        if (!need()) return;
        const b = JSON.parse((await readBody(req)).toString());
        const ok = /^(?=.*[A-Z])(?=.*[a-z])(?=.*\d)(?=.*[^A-Za-z0-9]).{8,}$/.test(b.password || '');
        if (ok) { S.password = b.password; auth.clear(); }
        return send(res, 200, { ok });
      }
      if (url === '/ap-password' && req.method === 'POST') { if (!need()) return; const b = JSON.parse((await readBody(req)).toString()); return send(res, 200, { ok: (b.password || '').length >= 8 }); }
      if (url === '/restart' && req.method === 'POST') { if (!need()) return; S.restarted++; return send(res, 200, { ok: true }); }
      if (url === '/update' && req.method === 'POST') { if (!need()) return; const body = await readBody(req); S.ota = { bytes: body.length }; S.restarted++; return send(res, 200, { ok: true }); }
      send(res, 302, '', { Location: '/' });
    } catch (e) { send(res, 500, { error: String(e) }); }
  });
  return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve({
    S, auth, port: server.address().port, host: '127.0.0.1:' + server.address().port,
    close() { server.closeAllConnections?.(); server.close(); },
  })));
}

module.exports = { createSiren, createWW };

if (require.main === module) {
  (async () => {
    const s = await createSiren({ port: 18080 });
    const w = await createWW({ port: 18081 });
    console.log(`Mock siren          127.0.0.1:${s.port}  password: Siren123!`);
    console.log(`Mock weather watcher 127.0.0.1:${w.port}  password: Weather123!`);
  })();
}
