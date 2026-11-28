'use strict';
const { app, BrowserWindow, ipcMain, dialog, safeStorage, shell, Menu } = require('electron');
const path = require('path');
const fs = require('fs');
const { BoardClient, normalizeHost } = require('./lib/board-client');
const { AuthGate, protectedReason } = require('./lib/auth-gate');
const winauth = require('./lib/winauth');

// ── config ──────────────────────────────────────────────────────────────────
// Passwords are encrypted with the OS keystore (Windows DPAPI) when available.
const BOARDS = {
  siren: { kind: 'siren', label: 'Siren Controller' },
  ww: { kind: 'ww', label: 'Weather Watcher' },
};
let cfgPath;
let cfg = { siren: { host: '', password: '' }, ww: { host: '', password: '' }, security: { requireAuth: true } };

function encode(pw) {
  if (!pw) return '';
  if (safeStorage.isEncryptionAvailable()) return 'enc:' + safeStorage.encryptString(pw).toString('base64');
  return 'plain:' + Buffer.from(pw, 'utf8').toString('base64');
}
function decode(v) {
  if (!v) return '';
  try {
    if (v.startsWith('enc:')) return safeStorage.decryptString(Buffer.from(v.slice(4), 'base64'));
    if (v.startsWith('plain:')) return Buffer.from(v.slice(6), 'base64').toString('utf8');
  } catch (_) { /* unreadable (different Windows user): treat as unset */ }
  return '';
}
function loadCfg() {
  try {
    const raw = JSON.parse(fs.readFileSync(cfgPath, 'utf8'));
    for (const id of Object.keys(BOARDS)) {
      if (raw[id]) cfg[id] = { host: normalizeHost(raw[id].host), password: decode(raw[id].password) };
    }
    if (raw.security && typeof raw.security.requireAuth === 'boolean') cfg.security.requireAuth = raw.security.requireAuth;
  } catch (_) { /* first run */ }
}
function saveCfg() {
  const out = {};
  for (const id of Object.keys(BOARDS)) out[id] = { host: cfg[id].host, password: encode(cfg[id].password) };
  out.security = { requireAuth: cfg.security.requireAuth };
  fs.mkdirSync(path.dirname(cfgPath), { recursive: true });
  fs.writeFileSync(cfgPath, JSON.stringify(out, null, 2));
}

// ── board clients ───────────────────────────────────────────────────────────
const clients = {};
let win = null;
let pushTimer = null;

// ── Windows sign-in gate ────────────────────────────────────────────────────
// Test hook (never honoured in a packaged build): HC_AUTH_MOCK=allow|deny.
const authMock = () => (!app.isPackaged && process.env.HC_AUTH_MOCK) || '';
const authAvailable = () => process.platform === 'win32' || !!authMock();
const gate = new AuthGate({
  required: () => cfg.security.requireAuth && authAvailable(),
  verify: async (reason) => {
    const m = authMock();
    if (m) {
      await new Promise((r) => setTimeout(r, 150));
      gate.mockPrompts = (gate.mockPrompts || 0) + 1;
      return m === 'allow' ? { ok: true } : { ok: false, reason: 'cancelled', message: 'Windows sign-in was cancelled - nothing was sent' };
    }
    const hwnd = win && !win.isDestroyed() ? Number(win.getNativeWindowHandle().readBigUInt64LE(0)) : 0;
    return winauth.verify({ hwnd, caption: 'Hurricane Controls Dashboard', message: 'Sign in with your Windows account to ' + reason + '.' });
  },
});
gate.onChange = () => schedulePush();
if (authMock()) global.__hcGate = gate;   // test hook only (HC_AUTH_MOCK is ignored when packaged)
const denied = (r) => ({ status: 0, error: r.message, code: 'AUTH_DENIED' });

function snapshotAll() {
  const out = {};
  for (const id of Object.keys(clients)) out[id] = clients[id].snapshot();
  out.auth = { ...gate.snapshot(), available: authAvailable(), enabled: cfg.security.requireAuth };
  return out;
}
function schedulePush() {
  if (pushTimer) return;
  pushTimer = setTimeout(() => {
    pushTimer = null;
    if (win && !win.isDestroyed()) win.webContents.send('state', snapshotAll());
  }, 120);
}
function makeClient(id) {
  const c = new BoardClient({ id, kind: BOARDS[id].kind, host: cfg[id].host, password: cfg[id].password });
  c.on('change', schedulePush);
  c.on('status', schedulePush);
  clients[id] = c;
  return c;
}
function startAll() { for (const id of Object.keys(BOARDS)) makeClient(id).startPolling(); }

// Only the routes the two firmwares actually expose.
const PATHS = {
  siren: /^\/(status|alerts|settings-data|wifi-data|log|cmd|password|ap-password|restart|pub-status)$/,
  ww: /^\/(status-data|settings-data|wifi-data|ntfy-status|ntfy-test|ntfy-token|location-data|test-link|password|ap-password|restart)$/,
};

// ── IPC ─────────────────────────────────────────────────────────────────────
function ipc() {
  ipcMain.handle('state:get', () => snapshotAll());

  ipcMain.handle('cfg:get', () => {
    const out = {};
    for (const id of Object.keys(BOARDS)) out[id] = { host: cfg[id].host, hasPassword: !!cfg[id].password };
    out.encrypted = safeStorage.isEncryptionAvailable();
    return out;
  });

  ipcMain.handle('cfg:save', async (_e, { id, host, password }) => {
    if (!BOARDS[id]) throw new Error('unknown board');
    // First-time setup (nothing saved yet) must work; changing an existing
    // connection is a configuration change and needs the sign-in.
    if (cfg[id].host || cfg[id].password) {
      const g = await gate.ensure('change the saved connection');
      if (!g.ok) return { ok: false, error: g.message };
    }
    cfg[id].host = normalizeHost(host);
    if (typeof password === 'string' && password !== '') cfg[id].password = password;   // blank = keep
    saveCfg();
    const old = clients[id];
    if (old) { old.stopPolling(); old.logout(); }
    makeClient(id).startPolling();
    schedulePush();
    return { ok: true };
  });

  // Try credentials without saving them. Explicit user action only - a wrong
  // password counts toward the board's shared login lockout.
  ipcMain.handle('cfg:test', async (_e, { id, host, password }) => {
    if (!BOARDS[id]) throw new Error('unknown board');
    const c = new BoardClient({ id, kind: BOARDS[id].kind, host, password: password || cfg[id].password });
    try {
      if (!c.host) return { ok: false, message: 'Enter the board address first' };
      const pub = await c.requestPublic('GET', id === 'siren' ? '/pub-status' : '/status-data', { timeout: 4000 });
      if (pub.status !== 200) return { ok: false, message: 'Board answered HTTP ' + pub.status + ' - is this the right device?' };
      if (!c.password) return { ok: true, message: 'Board reachable. Enter its password to finish.' };
      await c.login();
      const s = await c.request('GET', '/settings-data');
      await c.logout();
      return { ok: true, message: 'Connected and signed in' + (s.json && s.json.fwVersion ? ' (firmware ' + s.json.fwVersion + ')' : '') };
    } catch (e) {
      return { ok: false, message: e.code === 'AUTH' ? 'Reachable, but the password was rejected' : e.message };
    }
  });

  ipcMain.handle('board:reconnect', (_e, id) => {
    const old = clients[id];
    if (!old) return false;
    old.stopPolling();
    old.resetAuth();
    makeClient(id).startPolling();
    return true;
  });

  ipcMain.handle('board:req', async (_e, { id, method, path: p, body, priority, skipIfBusy }) => {
    const c = clients[id];
    if (!c) throw new Error('unknown board');
    if (!PATHS[id].test(p) || !/^(GET|POST)$/.test(method)) throw new Error('route not allowed: ' + method + ' ' + p);
    if (!c.host) return { status: 0, error: 'Board address not set' };
    const why = protectedReason(id, method, p, body);
    if (why) {
      const g = await gate.ensure(why);
      if (!g.ok) return denied(g);
    }
    try {
      const r = await c.request(method, p, { body, priority, skipIfBusy });
      if (r.skipped) return { status: 0, skipped: true };
      return { status: r.status, json: r.json };
    } catch (e) {
      return { status: 0, error: e.message, code: e.code };
    }
  });

  ipcMain.handle('board:csv', async (_e, id) => {
    const c = clients[id];
    const p = id === 'siren' ? '/log.csv' : '/alertlog.csv';
    const def = id === 'siren' ? 'siren-runlog.csv' : 'weather-watcher-alertlog.csv';
    try {
      const r = await c.request('GET', p, { timeout: 30000 });
      if (r.status !== 200) return { ok: false, error: 'HTTP ' + r.status };
      const res = await dialog.showSaveDialog(win, { defaultPath: def, filters: [{ name: 'CSV', extensions: ['csv'] }] });
      if (res.canceled || !res.filePath) return { ok: false, canceled: true };
      fs.writeFileSync(res.filePath, r.body);
      return { ok: true, path: res.filePath };
    } catch (e) { return { ok: false, error: e.message }; }
  });

  ipcMain.handle('board:ota', async (_e, id) => {
    const c = clients[id];
    const g = await gate.ensure('update the firmware');
    if (!g.ok) return { ok: false, error: g.message };
    const pick = await dialog.showOpenDialog(win, {
      title: `Choose firmware (.bin) for the ${BOARDS[id].label}`,
      filters: [{ name: 'Firmware', extensions: ['bin'] }], properties: ['openFile'],
    });
    if (pick.canceled || !pick.filePaths[0]) return { ok: false, canceled: true };
    const file = pick.filePaths[0];
    const buf = fs.readFileSync(file);
    if (buf.length < 100000 || buf[0] !== 0xE9) {
      const r = await dialog.showMessageBox(win, {
        type: 'warning', buttons: ['Cancel', 'Upload anyway'], defaultId: 0, cancelId: 0,
        message: 'This does not look like an ESP32 firmware image',
        detail: `${path.basename(file)} is ${buf.length} bytes and ${buf[0] === 0xE9 ? 'looks ok' : 'does not start with the ESP32 image marker (0xE9)'}. ` +
                'Make sure you picked firmware.bin from .pio/build/…, not bootloader.bin or partitions.bin.',
      });
      if (r.response !== 1) return { ok: false, canceled: true };
    }
    const confirm = await dialog.showMessageBox(win, {
      type: 'question', buttons: ['Cancel', 'Upload and restart the board'], defaultId: 0, cancelId: 0,
      message: `Upload ${path.basename(file)} to the ${BOARDS[id].label}?`,
      detail: 'The board restarts when the update finishes. The siren must be idle and out of TEST MODE.',
    });
    if (confirm.response !== 1) return { ok: false, canceled: true };
    try {
      const j = await c.uploadFirmware(buf, path.basename(file), (sent, total) => {
        if (win && !win.isDestroyed()) win.webContents.send('ota:progress', { id, sent, total });
      });
      return j.ok ? { ok: true } : { ok: false, error: j.error || 'The board rejected the update' };
    } catch (e) {
      return { ok: false, error: e.message };
    } finally {
      // Give the board time to reboot, then resume monitoring.
      setTimeout(() => { c.resetAuth(); c.startPolling(); }, 12000);
    }
  });

  ipcMain.handle('auth:unlock', async () => { const g = await gate.ensure('unlock the dashboard controls'); return g.ok ? { ok: true } : { ok: false, error: g.message }; });
  ipcMain.handle('auth:lock', () => { gate.lock(); return true; });
  ipcMain.handle('auth:setRequired', async (_e, on) => {
    // Turning protection OFF is itself protected; turning it on is not.
    if (!on && gate.isRequired()) { const g = await gate.ensure('turn off Windows sign-in protection'); if (!g.ok) return { ok: false, error: g.message }; }
    cfg.security.requireAuth = !!on; saveCfg(); gate.lock(); schedulePush();
    return { ok: true };
  });

  ipcMain.handle('app:info', () => ({ version: app.getVersion(), encrypted: safeStorage.isEncryptionAvailable() }));
}

// ── window ──────────────────────────────────────────────────────────────────
function createWindow() {
  win = new BrowserWindow({
    width: 1360, height: 860, minWidth: 1040, minHeight: 640,
    backgroundColor: '#0e1116',
    title: 'Hurricane Controls Dashboard',
    icon: path.join(__dirname, 'renderer', 'siren.png'),
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true, nodeIntegration: false, sandbox: true,
      backgroundThrottling: false,      // keep-alives must not be throttled when covered
    },
  });
  Menu.setApplicationMenu(null);
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  win.webContents.setWindowOpenHandler(({ url }) => { if (/^https:\/\/(www\.)?weather\.gov/.test(url)) shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('will-navigate', (e) => e.preventDefault());
  win.on('closed', () => { win = null; });
}

if (!app.requestSingleInstanceLock()) { app.quit(); } else {
  app.on('second-instance', () => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); } });
  app.whenReady().then(() => {
    cfgPath = path.join(app.getPath('userData'), 'config.json');
    loadCfg();
    ipc();
    startAll();
    createWindow();
  });
  app.on('window-all-closed', () => app.quit());
  let quitting = false;
  app.on('before-quit', (e) => {
    if (quitting) return;
    quitting = true;
    e.preventDefault();
    // Free our session slots on the boards (they hold only 4 in total).
    Promise.race([
      Promise.all(Object.values(clients).map((c) => { c.stopPolling(); return c.logout(); })),
      new Promise((r) => setTimeout(r, 2000)),
    ]).finally(() => app.exit(0));
  });
}
