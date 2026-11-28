'use strict';
// Talks to one Hurricane Controls board (siren controller or Weather Watcher)
// over its existing HTTP API. Runs in Electron's main process, so there is no
// CORS and the session cookie is handled by hand.
//
// Design notes (all driven by how the firmware behaves):
//  * Login failures count against a *global* 5-strike / 30 s lockout on the
//    board, which also locks out anyone using the phone UI. So a wrong
//    password is never retried automatically - the board goes to "auth-error"
//    until the user fixes the password and hits Connect.
//  * The board keeps only 4 sessions and evicts the oldest, so we log in once,
//    reuse the cookie, and only log in again on a 401.
//  * ESP32 sockets are scarce: one request at a time per board, no keep-alive.

const http = require('http');
const { EventEmitter } = require('events');
const crypto = require('crypto');

const REQ_TIMEOUT_MS = 5000;

function normalizeHost(h) {
  let s = String(h || '').trim();
  s = s.replace(/^[a-z]+:\/\//i, '').replace(/\/.*$/, '');
  return s;
}

function parseHostPort(host) {
  const m = /^(\[[^\]]+\]|[^:]+)(?::(\d+))?$/.exec(host);
  if (!m) throw new Error('Invalid address: ' + host);
  return { hostname: m[1].replace(/^\[|\]$/g, ''), port: m[2] ? parseInt(m[2], 10) : 80 };
}

class BoardClient extends EventEmitter {
  /**
   * @param {{id:string, kind:'siren'|'ww', host:string, password:string}} cfg
   */
  constructor(cfg) {
    super();
    this.id = cfg.id;
    this.kind = cfg.kind;
    this.host = normalizeHost(cfg.host);
    this.password = cfg.password || '';
    this.cookie = '';
    this.state = { conn: this.host ? 'connecting' : 'unconfigured', error: '', lastOk: 0 };
    this.data = {};            // live data pushed to the UI
    this._queue = Promise.resolve();
    this._pending = 0;
    this._timers = new Set();
    this._stopped = true;
    this._lockedUntil = 0;
    this._authFailed = false;
    this._ws = null;
    this._failCount = 0;
  }

  // ── state helpers ────────────────────────────────────────────────────────
  _setConn(conn, error = '') {
    const changed = this.state.conn !== conn || this.state.error !== error;
    this.state.conn = conn;
    this.state.error = error;
    if (conn === 'online') this.state.lastOk = Date.now();
    if (changed) this.emit('change');
  }

  snapshot() {
    return { id: this.id, kind: this.kind, host: this.host, ...this.state, data: this.data };
  }

  // ── low-level HTTP ───────────────────────────────────────────────────────
  _rawRequest(method, path, { body, headers = {}, timeout = REQ_TIMEOUT_MS, onUploadProgress } = {}) {
    return new Promise((resolve, reject) => {
      let hp;
      try { hp = parseHostPort(this.host); } catch (e) { reject(e); return; }
      const h = { Connection: 'close', ...headers };
      if (this.cookie) h.Cookie = this.cookie;
      let payload = null;
      if (body !== undefined && body !== null) {
        payload = Buffer.isBuffer(body) ? body : Buffer.from(typeof body === 'string' ? body : JSON.stringify(body));
        if (!h['Content-Type']) h['Content-Type'] = 'application/json';
        h['Content-Length'] = payload.length;
      }
      const req = http.request({
        host: hp.hostname, port: hp.port, method, path, headers: h, agent: false,
      }, (res) => {
        const chunks = [];
        res.on('data', (c) => chunks.push(c));
        res.on('end', () => {
          const setCookie = res.headers['set-cookie'];
          if (setCookie) {
            for (const c of setCookie) {
              const m = /^sid=([^;]*)/.exec(c);
              if (m) this.cookie = m[1] ? 'sid=' + m[1] : '';
            }
          }
          resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) });
        });
        res.on('error', reject);
      });
      req.setTimeout(timeout, () => req.destroy(new Error('Timed out')));
      req.on('error', reject);
      if (payload) {
        if (onUploadProgress) {
          // Write in slices so we can report progress. 4 KB slices keep the
          // ESP32's receive window happy.
          let sent = 0;
          const total = payload.length;
          const SLICE = 4096;
          const pump = () => {
            while (sent < total) {
              const end = Math.min(sent + SLICE, total);
              const ok = req.write(payload.subarray(sent, end));
              sent = end;
              onUploadProgress(sent, total);
              if (!ok) { req.once('drain', pump); return; }
            }
            req.end();
          };
          pump();
        } else {
          req.end(payload);
        }
      } else {
        req.end();
      }
    });
  }

  // ── auth ─────────────────────────────────────────────────────────────────
  async login() {
    if (Date.now() < this._lockedUntil) {
      const s = Math.ceil((this._lockedUntil - Date.now()) / 1000);
      const err = new Error(`Board is locked out after failed logins - retry in ${s}s`);
      err.code = 'LOCKED';
      throw err;
    }
    if (this._authFailed) {
      const err = new Error('Wrong password - update it in Connections');
      err.code = 'AUTH';
      throw err;
    }
    this.cookie = '';
    const res = await this._rawRequest('POST', '/auth/login', { body: { password: this.password } });
    let j = {};
    try { j = JSON.parse(res.body.toString('utf8')); } catch (_) { /* fallthrough */ }
    if (j.ok && this.cookie) {
      this._authFailed = false;
      return true;
    }
    if (j.locked) {
      this._lockedUntil = Date.now() + (j.retryAfter || 30) * 1000 + 500;
      const err = new Error(`Board is locked out after failed logins - retry in ${j.retryAfter || 30}s`);
      err.code = 'LOCKED';
      throw err;
    }
    // Plain wrong password. Do NOT retry automatically.
    this._authFailed = true;
    const err = new Error('Wrong password - update it in Connections');
    err.code = 'AUTH';
    throw err;
  }

  /** Serialised, authenticated request. Returns {status, json, body}. */
  request(method, path, opts = {}) {
    const run = async () => {
      let res = await this._rawRequest(method, path, opts);
      if (res.status === 401 || (res.status === 302 && /\/login/.test(res.headers.location || ''))) {
        await this.login();
        res = await this._rawRequest(method, path, opts);
        if (res.status === 401) {
          const err = new Error('Not authorised');
          err.code = 'AUTH';
          throw err;
        }
      }
      let json;
      const ct = String(res.headers['content-type'] || '');
      if (ct.includes('json')) { try { json = JSON.parse(res.body.toString('utf8')); } catch (_) { /* ignore */ } }
      return { status: res.status, json, body: res.body, headers: res.headers };
    };
    // STOP (and anything else marked priority) must never wait behind a slow
    // settings load: it goes out immediately on its own socket.
    if (opts.priority) return run();
    // Keep-alives are worthless late: if the board is already busy, skip
    // rather than queue a burst that fires all at once when it recovers.
    if (opts.skipIfBusy && this._pending > 0) return Promise.resolve({ status: 0, skipped: true });
    // One request at a time per board.
    this._pending++;
    const p = this._queue.then(run, run);
    this._queue = p.catch(() => {});
    const done = () => { this._pending--; };
    p.then(done, done);
    return p;
  }

  /** Request that has to work even before we have credentials (public routes). */
  async requestPublic(method, path, opts = {}) {
    const p = this._queue.then(() => this._rawRequest(method, path, opts));
    this._queue = p.catch(() => {});
    const res = await p;
    let json;
    try { json = JSON.parse(res.body.toString('utf8')); } catch (_) { /* ignore */ }
    return { status: res.status, json, body: res.body };
  }

  // ── multipart OTA upload ─────────────────────────────────────────────────
  async uploadFirmware(buf, filename, onProgress) {
    const boundary = '----hc' + crypto.randomBytes(8).toString('hex');
    const head = Buffer.from(
      `--${boundary}\r\nContent-Disposition: form-data; name="firmware"; filename="${filename.replace(/"/g, '')}"\r\n` +
      'Content-Type: application/octet-stream\r\n\r\n');
    const tail = Buffer.from(`\r\n--${boundary}--\r\n`);
    const body = Buffer.concat([head, buf, tail]);
    // Make sure we hold a valid session first so the upload isn't rejected
    // mid-stream with an unreadable 401.
    if (!this.cookie) await this.login();
    this.stopPolling();   // free the board's sockets for the upload
    const run = async () => {
      const res = await this._rawRequest('POST', '/update', {
        body,
        headers: { 'Content-Type': 'multipart/form-data; boundary=' + boundary },
        timeout: 120000,
        onUploadProgress: onProgress,
      });
      let j = {};
      try { j = JSON.parse(res.body.toString('utf8')); } catch (_) { /* ignore */ }
      return j;
    };
    const p = this._queue.then(run, run);
    this._queue = p.catch(() => {});
    return p;
  }

  // ── lifecycle ────────────────────────────────────────────────────────────
  _after(ms, fn) {
    const t = setTimeout(() => { this._timers.delete(t); if (!this._stopped) fn(); }, ms);
    this._timers.add(t);
    return t;
  }

  startPolling() {
    this._stopped = false;
    if (!this.host) { this._setConn('unconfigured'); return; }
    if (this.kind === 'siren') this._sirenLoop(); else this._wwLoop();
    this._alertsLoop();
  }

  stopPolling() {
    this._stopped = true;
    for (const t of this._timers) clearTimeout(t);
    this._timers.clear();
    if (this._ws) { try { this._ws.removeAllListeners(); this._ws.terminate(); } catch (_) { /* ignore */ } this._ws = null; }
  }

  /** Forget a previous bad-password / lockout verdict (user pressed Connect). */
  resetAuth() { this._authFailed = false; this._lockedUntil = 0; this.cookie = ''; }

  _handleError(e) {
    this._failCount++;
    if (e.code === 'AUTH') { this._setConn('auth-error', e.message); return 'auth'; }
    if (e.code === 'LOCKED') { this._setConn('locked', e.message); return 'locked'; }
    this._setConn('offline', friendly(e));
    return 'net';
  }

  // Siren: live status over the authenticated WebSocket, HTTP poll fallback.
  async _sirenLoop() {
    if (this._stopped) return;
    try {
      if (!this.cookie) await this.login();
      this._openWs();
    } catch (e) {
      const kind = this._handleError(e);
      if (kind === 'auth') return;                       // wait for the user
      const wait = kind === 'locked' ? Math.max(1000, this._lockedUntil - Date.now()) : backoff(this._failCount);
      this._after(wait, () => this._sirenLoop());
    }
  }

  _openWs() {
    if (this._stopped || this._ws) return;
    const WebSocket = require('ws');
    const hp = parseHostPort(this.host);
    const ws = new WebSocket(`ws://${hp.hostname}:${hp.port}/ws`, { headers: { Cookie: this.cookie }, handshakeTimeout: 5000 });
    this._ws = ws;
    let gotMsg = false;
    // If the socket is open but silent, the board's stalled - poll instead.
    const watchdog = setInterval(() => {
      if (Date.now() - (this._lastWsMsg || 0) > 4000 && ws.readyState === 1) { try { ws.terminate(); } catch (_) { /* ignore */ } }
    }, 2000);
    ws.on('message', (m) => {
      this._lastWsMsg = Date.now();
      try {
        this.data.status = JSON.parse(m.toString());
        this._failCount = 0;
        gotMsg = true;
        this._setConn('online');
        this.emit('status');
      } catch (_) { /* ignore garbage */ }
    });
    const done = () => {
      clearInterval(watchdog);
      if (this._ws !== ws) return;
      this._ws = null;
      if (this._stopped) return;
      // One HTTP poll tells us why it dropped (401 = session evicted/password changed).
      this.request('GET', '/status').then((r) => {
        if (r.json) { this.data.status = r.json; this._setConn('online'); this.emit('status'); }
        this._after(gotMsg ? 1500 : 3000, () => this._sirenLoop());
      }).catch((e) => {
        const kind = this._handleError(e);
        if (kind === 'auth') return;
        this._after(kind === 'locked' ? Math.max(1000, this._lockedUntil - Date.now()) : backoff(this._failCount), () => this._sirenLoop());
      });
    };
    ws.on('close', done);
    ws.on('error', () => { /* 'close' follows */ });
    ws.on('unexpected-response', (_req, res) => { if (res.statusCode === 401) this.cookie = ''; try { ws.terminate(); } catch (_) { /* ignore */ } });
  }

  // Weather Watcher: /status-data is public, so monitoring never needs the password.
  async _wwLoop() {
    if (this._stopped) return;
    try {
      const r = await this.requestPublic('GET', '/status-data', { timeout: 4000 });
      if (r.status === 200 && r.json) {
        this.data.status = r.json;
        this._failCount = 0;
        this._setConn('online');
        this.emit('status');
      } else {
        throw new Error('HTTP ' + r.status);
      }
      this._after(1000, () => this._wwLoop());
    } catch (e) {
      this._handleError(e);
      this._after(backoff(this._failCount), () => this._wwLoop());
    }
  }

  // Siren /alerts (what the Weather Watcher has told the siren) - slow poll.
  async _alertsLoop() {
    if (this._stopped || this.kind !== 'siren') return;
    if (this.state.conn === 'online') {
      try {
        const r = await this.request('GET', '/alerts');
        if (r.json) { this.data.alerts = r.json; this.emit('status'); }
      } catch (_) { /* surfaced by the main loop */ }
    }
    this._after(5000, () => this._alertsLoop());
  }

  async logout() {
    if (!this.cookie) return;
    try { await this._rawRequest('POST', '/auth/logout', { timeout: 1500 }); } catch (_) { /* best effort */ }
    this.cookie = '';
  }
}

function backoff(n) { return Math.min(15000, 1500 * Math.max(1, n)); }

function friendly(e) {
  const c = e && e.code;
  if (c === 'ECONNREFUSED') return 'Connection refused';
  if (c === 'EHOSTUNREACH' || c === 'ENETUNREACH') return 'Host unreachable';
  if (c === 'ENOTFOUND' || c === 'EAI_AGAIN') return 'Address not found';
  if (c === 'ECONNRESET') return 'Connection reset';
  return (e && e.message) || 'Unreachable';
}

module.exports = { BoardClient, normalizeHost };
