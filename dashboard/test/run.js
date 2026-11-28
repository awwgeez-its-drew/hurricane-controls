'use strict';
const assert = require('assert');
const { createSiren, createWW } = require('./mock-boards');
const { BoardClient } = require('../lib/board-client');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (fn, ms = 4000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await fn()) return true; await sleep(30); } return false; };

let pass = 0;
async function t(name, fn) {
  try { await fn(); pass++; console.log('  ok   ' + name); }
  catch (e) { console.log('  FAIL ' + name + '\n       ' + (e.stack || e)); process.exitCode = 1; }
}

(async () => {
  const siren = await createSiren({ lockMs: 1500 });
  const ww = await createWW();
  const mk = (kind, b, pw) => new BoardClient({ id: kind, kind, host: b.host, password: pw });

  console.log('Siren client');
  await t('logs in, receives live WebSocket status', async () => {
    const c = mk('siren', siren, 'Siren123!');
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'online' && c.data.status));
    assert.strictEqual(c.data.status.mode, 'idle');
    assert.strictEqual(siren.auth.sessions.length, 1, 'exactly one session used');
    c.stopPolling(); await c.logout();
    assert.strictEqual(siren.auth.sessions.length, 0, 'logout removes the session');
  });

  await t('wrong password: marks auth-error and does NOT keep retrying (global lockout protection)', async () => {
    const c = mk('siren', siren, 'nope');
    const before = siren.S.calls.filter((x) => x === 'POST /auth/login').length;
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'auth-error'));
    await sleep(1500);
    const after = siren.S.calls.filter((x) => x === 'POST /auth/login').length;
    assert.strictEqual(after - before, 1, 'one login attempt only, got ' + (after - before));
    c.stopPolling();
  });

  await t('reports lockout and recovers after it expires (resetAuth + correct password)', async () => {
    for (let i = 0; i < 5; i++) siren.auth.login('bad');           // trip the lockout the way a phone user would
    const c = mk('siren', siren, 'Siren123!');
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'locked'), 'locked state');
    assert.ok(await until(() => c.state.conn === 'online', 5000), 'recovers on its own');
    c.stopPolling(); await c.logout();
  });

  await t('re-logs-in transparently when the session is evicted', async () => {
    const c = mk('siren', siren, 'Siren123!');
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'online'));
    siren.auth.clear();                                              // e.g. password changed elsewhere
    const r = await c.request('GET', '/settings-data');
    assert.strictEqual(r.status, 200);
    assert.strictEqual(r.json.fwVersion, '2.0.2');
    c.stopPolling(); await c.logout();
  });

  await t('/cmd: wail runs, stop ends it; log entry recorded', async () => {
    const c = mk('siren', siren, 'Siren123!');
    c.startPolling();
    assert.ok(await until(() => c.data.status));
    assert.deepStrictEqual((await c.request('POST', '/cmd', { body: { mode: 'wail' } })).json, { ok: true });
    assert.ok(await until(() => c.data.status.mode === 'wail' && c.data.status.relays[0] === 1));
    await c.request('POST', '/cmd', { body: { mode: 'stop' } });
    assert.ok(await until(() => c.data.status.mode === 'idle'));
    const log = (await c.request('GET', '/log')).json;
    assert.ok(log.entries.length >= 1);
    c.stopPolling(); await c.logout();
  });

  await t('TEST MODE blocks run modes and STOP, allows components', async () => {
    const c = mk('siren', siren, 'Siren123!');
    await c.request('POST', '/cmd', { body: { mode: 'test-lock', on: true } });
    assert.strictEqual((await c.request('POST', '/cmd', { body: { mode: 'wail' } })).json.ok, false);
    assert.strictEqual((await c.request('POST', '/cmd', { body: { mode: 'test', component: 'blower', on: true } })).json.ok, true);
    assert.strictEqual(siren.S.relays[1], 1);
    await sleep(2500);                                               // no keep-alive => board switches it off
    assert.strictEqual(siren.S.relays[1], 0, 'keep-alive failsafe');
    await c.request('POST', '/cmd', { body: { mode: 'test-lock', on: false } });
    await c.logout();
  });

  await t('priority requests bypass the queue; keep-alives are skipped when busy', async () => {
    const c = mk('siren', siren, 'Siren123!');
    await c.request('GET', '/status');
    const slow = c.request('GET', '/log');                       // occupies the queue
    const ka = await c.request('POST', '/cmd', { body: { mode: 'manual-ka' }, skipIfBusy: true });
    assert.strictEqual(ka.skipped, true, 'keep-alive skipped while busy');
    const stop = await c.request('POST', '/cmd', { body: { mode: 'stop' }, priority: true });
    assert.strictEqual(stop.status, 200);
    await slow;
    const ka2 = await c.request('POST', '/cmd', { body: { mode: 'manual-ka' }, skipIfBusy: true });
    assert.strictEqual(ka2.status, 200, 'sent when idle');
    await c.logout();
  });

  await t('settings POST only carries what we send', async () => {
    const c = mk('siren', siren, 'Siren123!');
    await c.request('POST', '/settings-data', { body: { wailDuration: 90000 } });
    assert.deepStrictEqual(siren.S.lastSettingsPost, { wailDuration: 90000 });
    assert.strictEqual((await c.request('GET', '/settings-data')).json.wailDuration, 90000);
    await c.logout();
  });

  await t('OTA upload sends multipart with the file, reports progress, refused while running', async () => {
    const c = mk('siren', siren, 'Siren123!');
    const fw = Buffer.alloc(50000, 7);
    let last = 0;
    const r = await c.uploadFirmware(fw, 'firmware.bin', (s) => { last = s; });
    assert.strictEqual(r.ok, true);
    assert.ok(last >= fw.length);
    assert.ok(siren.S.ota.bytes >= fw.length);
    assert.ok(/name="firmware"; filename="firmware.bin"/.test(siren.S.ota.head));
    siren.S.ota = null;
    await c.request('POST', '/cmd', { body: { mode: 'wail' } });
    const r2 = await c.uploadFirmware(fw, 'firmware.bin');
    assert.strictEqual(r2.ok, false);
    assert.match(r2.error, /running/);
    await c.request('POST', '/cmd', { body: { mode: 'stop' } });
    await c.logout();
  });

  console.log('Weather Watcher client');
  await t('monitors via public /status-data WITHOUT logging in', async () => {
    const c = mk('ww', ww, '');
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'online' && c.data.status));
    assert.strictEqual(c.data.status.currentAlerts[0].event, 'Tornado Warning');
    assert.ok(!ww.S.calls.includes('POST /auth/login'), 'no login needed for monitoring');
    c.stopPolling();
  });

  await t('settings need a login, handled on demand', async () => {
    const c = mk('ww', ww, 'Weather123!');
    const r = await c.request('GET', '/settings-data');
    assert.strictEqual(r.json.latitude, 35.1);
    await c.request('POST', '/settings-data', { body: { tornadoUnconfirmedMode: 'ATTACK' } });
    assert.strictEqual(ww.S.lastSettingsPost.tornadoUnconfirmedMode, 'ATTACK');
    await c.logout();
  });

  await t('reports offline then recovers', async () => {
    const tmp = await createWW({ port: 0 });
    const port = tmp.port;
    const c = new BoardClient({ id: 'ww', kind: 'ww', host: '127.0.0.1:' + port, password: '' });
    c.startPolling();
    assert.ok(await until(() => c.state.conn === 'online'));
    tmp.close();
    assert.ok(await until(() => c.state.conn === 'offline', 6000));
    const again = await createWW({ port });
    assert.ok(await until(() => c.state.conn === 'online', 8000));
    c.stopPolling(); again.close();
  });

  await t('host normalisation: scheme and path are stripped', async () => {
    const { normalizeHost } = require('../lib/board-client');
    assert.strictEqual(normalizeHost(' http://192.168.1.50/ '), '192.168.1.50');
    assert.strictEqual(normalizeHost('hurricane.local/settings'), 'hurricane.local');
    assert.strictEqual(normalizeHost('10.0.0.2:8080'), '10.0.0.2:8080');
  });

  console.log('Windows sign-in gate');
  const { AuthGate, protectedReason, UNLOCK_MS } = require('../lib/auth-gate');
  const winauth = require('../lib/winauth');
  await t('protects starting/config, never STOP or keep-alives or reads', async () => {
    const P = (id, m, p, b) => protectedReason(id, m, p, b);
    for (const mode of ['wail', 'attack', 'fastwail', 'manual', 'growl', 'btn-lock']) assert.ok(P('siren', 'POST', '/cmd', { mode }), mode);
    assert.ok(P('siren', 'POST', '/cmd', { mode: 'test-lock', on: true }));
    assert.ok(P('siren', 'POST', '/cmd', { mode: 'test', component: 'blower', on: true }));
    for (const path of ['/settings-data', '/password', '/ap-password', '/wifi-data', '/ntfy-token', '/restart']) {
      assert.ok(P('siren', 'POST', path, {}), path); assert.ok(P('ww', 'POST', path, {}), 'ww ' + path);
    }
    // must stay unlocked: a prompt can never delay stopping the siren
    assert.strictEqual(P('siren', 'POST', '/cmd', { mode: 'stop' }), null);
    assert.strictEqual(P('siren', 'POST', '/cmd', { mode: 'manual-ka' }), null);
    assert.strictEqual(P('siren', 'POST', '/cmd', { mode: 'test-ka', component: 'blower' }), null);
    assert.strictEqual(P('siren', 'POST', '/cmd', { mode: 'test', component: 'blower', on: false }), null);
    assert.strictEqual(P('siren', 'POST', '/cmd', { mode: 'test-lock', on: false }), null);
    for (const path of ['/status', '/settings-data', '/log', '/alerts', '/wifi-data']) assert.strictEqual(P('siren', 'GET', path), null, path);
    assert.strictEqual(P('ww', 'POST', '/test-link'), null);
    assert.strictEqual(P('ww', 'POST', '/ntfy-test'), null);
    // look-alike / odd bodies are not waved through as "stop"
    assert.ok(P('siren', 'POST', '/cmd', { mode: 'wail', extra: 'stop' }));
    assert.strictEqual(P('siren', 'POST', '/cmd', null), null);
  });
  await t('one sign-in unlocks for 5 minutes, then re-locks', async () => {
    let now = 1000, prompts = 0;
    const g = new AuthGate({ required: () => true, now: () => now, verify: async () => { prompts++; return { ok: true }; } });
    assert.strictEqual(g.unlocked, false);
    assert.deepStrictEqual(await g.ensure('x'), { ok: true });
    now += UNLOCK_MS - 1; assert.deepStrictEqual(await g.ensure('x'), { ok: true }); assert.strictEqual(prompts, 1);
    now += 2; assert.strictEqual(g.unlocked, false);
    await g.ensure('x'); assert.strictEqual(prompts, 2);
    g.lock(); assert.strictEqual(g.unlocked, false);
  });
  await t('simultaneous protected requests share a single prompt', async () => {
    let prompts = 0;
    const g = new AuthGate({ required: () => true, verify: async () => { prompts++; await sleep(60); return { ok: true }; } });
    const rs = await Promise.all([g.ensure('a'), g.ensure('b'), g.ensure('c')]);
    assert.ok(rs.every((r) => r.ok)); assert.strictEqual(prompts, 1);
  });
  await t('cancel / deny / a throwing prompt all fail CLOSED and do not unlock', async () => {
    for (const v of [async () => ({ ok: false, reason: 'cancelled' }), async () => ({ ok: false }), async () => { throw new Error('boom'); }, async () => undefined]) {
      const g = new AuthGate({ required: () => true, verify: v });
      const r = await g.ensure('x'); assert.strictEqual(r.ok, false); assert.ok(r.message); assert.strictEqual(g.unlocked, false);
    }
  });
  await t('not required => never prompts', async () => {
    const g = new AuthGate({ required: () => false, verify: async () => { throw new Error('should not be called'); } });
    assert.deepStrictEqual(await g.ensure('x'), { ok: true });
  });
  await t('PowerShell helper: strings are quoted (no injection) and results map correctly', async () => {
    const sc = winauth.buildScript(4242, "Cap'tion", "evil'; Remove-Item C:\\x #");
    assert.ok(sc.includes("'Cap''tion'") && sc.includes("'evil''; Remove-Item C:\\x #'"));
    assert.ok(sc.includes('[long]4242'));
    assert.ok(/\r\n'@\r\n/.test(sc), 'here-string terminator on its own line');
    assert.strictEqual(winauth.buildScript('1; calc', 'a', 'b').includes('calc'), false, 'hwnd is numeric-only');
    assert.deepStrictEqual(winauth.interpret('OK\r\n'), { ok: true });
    assert.strictEqual(winauth.interpret('CANCEL').reason, 'cancelled');
    assert.strictEqual(winauth.interpret('FAIL').reason, 'denied');
    assert.strictEqual(winauth.interpret('UNSUPPORTED').reason, 'unsupported');
    assert.strictEqual(winauth.interpret('ERR:Add-Type blocked').reason, 'unavailable');
    assert.strictEqual(winauth.interpret('').ok, false);
    assert.strictEqual((await winauth.verify({ hwnd: 0, caption: 'c', message: 'm' })).reason, process.platform === 'win32' ? (await winauth.verify({ hwnd: 0, caption: 'c', message: 'm', timeoutMs: 1 })).reason : 'unavailable');
  });

  await t('Windows Hello flow: used first, final on cancel/fail, falls back to password only when unusable', async () => {
    const mk = (helloOut, pwOut) => { const calls = []; return { calls, run: async (script) => { const hello = script.includes('UserConsentVerifier'); calls.push(hello ? 'hello' : 'password'); const v = hello ? helloOut : pwOut; if (v instanceof Error) throw v; return v; } }; };
    const base = { hwnd: 1, caption: 'c', message: 'm' };
    let m = mk('HELLO:Verified\r\n', 'OK');
    assert.deepStrictEqual(await winauth.verify(base, m.run), { ok: true }); assert.deepStrictEqual(m.calls, ['hello']);
    m = mk('HELLO:Canceled', 'OK');
    let r = await winauth.verify(base, m.run); assert.strictEqual(r.ok, false); assert.strictEqual(r.reason, 'cancelled'); assert.deepStrictEqual(m.calls, ['hello'], 'cancel must NOT fall through to the password box');
    m = mk('HELLO:RetriesExhausted', 'OK');
    r = await winauth.verify(base, m.run); assert.strictEqual(r.ok, false); assert.deepStrictEqual(m.calls, ['hello']);
    for (const out of ['HELLO:NA:NotConfiguredForUser', 'HELLO:NA:DeviceNotPresent', 'HELLO:NotConfiguredForUser', 'HELLO:ERR:projection failed', '', new Error('spawn failed')]) {
      m = mk(out, 'OK'); r = await winauth.verify(base, m.run);
      assert.deepStrictEqual(r, { ok: true }, 'fallback for ' + out); assert.deepStrictEqual(m.calls, ['hello', 'password']);
    }
    m = mk('HELLO:Verified', 'OK'); r = await winauth.verify({ ...base, method: 'password' }, m.run);
    assert.deepStrictEqual(m.calls, ['password'], 'password-only never opens Hello');
    m = mk('HELLO:NA:x', 'CANCEL'); r = await winauth.verify(base, m.run); assert.strictEqual(r.reason, 'cancelled');
    m = mk('HELLO:NA:x', new Error('blocked')); r = await winauth.verify(base, m.run); assert.strictEqual(r.ok, false); assert.strictEqual(r.reason, 'unavailable');
    const hs = winauth.buildHelloScript("it's \"quoted\" $x"); assert.ok(hs.includes("'it''s \"quoted\" $x'"));
  });

  siren.close(); ww.close();
  console.log(`\n${pass} passed${process.exitCode ? ', SOME FAILED' : ''}`);
  setTimeout(() => process.exit(process.exitCode || 0), 100);
})();
