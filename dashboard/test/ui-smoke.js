'use strict';
// End-to-end: real Electron app + mock boards, driven with Playwright.
// Run:  xvfb-run -a node test/ui-smoke.js        (see package.json "test:ui")
const { _electron: electron } = require('playwright');
const path = require('path');
const fs = require('fs');
const os = require('os');
const assert = require('assert');
const { createSiren, createWW } = require('./mock-boards');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const shotDir = process.env.SHOTS || '';

(async () => {
  const siren = await createSiren();
  const ww = await createWW();
  const ud = fs.mkdtempSync(path.join(os.tmpdir(), 'hc-ud-'));
  const b64 = (s) => 'plain:' + Buffer.from(s).toString('base64');
  fs.writeFileSync(path.join(ud, 'config.json'), JSON.stringify({
    siren: { host: siren.host, password: b64('Siren123!') }, ww: { host: ww.host, password: b64('Weather123!') },
  }));
  const app = await electron.launch({
    executablePath: require('electron'),
    args: [path.join(__dirname, '..'), '--no-sandbox', '--user-data-dir=' + ud],
  });
  const page = await app.firstWindow();
  const errors = [];
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  await page.setViewportSize({ width: 1360, height: 860 });
  // Playwright's waitForFunction string-evals in the page, which the app's strict
  // CSP (no unsafe-eval) rightly blocks - poll with evaluate() instead.
  const waitFn = async (fn, _arg, opts = {}) => {
    const end = Date.now() + (opts.timeout || 5000);
    for (;;) {
      if (await page.evaluate(fn)) return;
      if (Date.now() > end) throw new Error('waitFn timed out: ' + String(fn).slice(0, 120));
      await sleep(50);
    }
  };
  let fails = 0;
  const t = async (name, fn) => { try { await fn(); console.log('  ok   ' + name); } catch (e) { fails++; console.log('  FAIL ' + name + '\n       ' + (e.message || e)); } };
  const shot = async (n) => { if (shotDir) await page.screenshot({ path: path.join(shotDir, n + '.png') }); };
  const hold = async (sel, ms) => {
    const b = await page.locator(sel).first().boundingBox();
    await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2);
    await page.mouse.down(); await sleep(ms); await page.mouse.up();
  };

  await t('both boards come online', async () => {
    await waitFn(() => document.querySelector('#topSiren').textContent.includes('online') && document.querySelector('#topWw').textContent.includes('online'), null, { timeout: 10000 });
  });
  await t('overview shows idle siren + active tornado warning', async () => {
    await page.waitForSelector('.hero .mode:has-text("Idle")');
    assert.ok(await page.locator('#topAlerts .alert:has-text("Tornado Warning")').count());
    await shot('overview');
  });

  await t('a short press does NOT start the siren (hold-to-confirm)', async () => {
    await hold('.modes .hold.wail >> visible=true', 300);
    await sleep(700);
    assert.strictEqual(siren.S.mode, 'idle');
  });
  await t('holding WAIL for 1 s starts it; relays and timer reflect it', async () => {
    await hold('.modes .hold.wail >> visible=true', 1300);
    await page.waitForSelector('.hero .mode:has-text("Wail")', { timeout: 4000 });
    assert.strictEqual(siren.S.mode, 'wail');
    await page.waitForSelector('.relay.on >> nth=0');
    await shot('running');
  });
  await t('global STOP: short press does nothing, 0.4 s hold stops', async () => {
    await hold('#gStop', 150); await sleep(500);
    assert.strictEqual(siren.S.mode, 'wail');
    await hold('#gStop', 700);
    await waitFn(() => document.querySelector('.hero .mode').textContent === 'Idle', null, { timeout: 4000 });
    assert.strictEqual(siren.S.mode, 'idle');
  });

  await t('Siren page: MANUAL sounds only while held, keep-alives flow, release stops', async () => {
    await page.click('#nav button[data-page=siren]');
    const b = page.locator('.page.on .hold.manual');
    const bb = await b.boundingBox();
    await page.mouse.move(bb.x + 20, bb.y + 20); await page.mouse.down();
    await waitFn(() => document.querySelector('.page.on .hero .mode').textContent === 'Manual', null, { timeout: 4000 });
    const before = siren.S.calls.filter((c) => c === 'POST /cmd').length;
    await sleep(1800);
    const ka = siren.S.calls.filter((c) => c === 'POST /cmd').length - before;
    assert.ok(ka >= 2, 'keep-alives sent: ' + ka);
    assert.strictEqual(siren.S.mode, 'manual', 'still sounding while held (no failsafe trip)');
    await page.mouse.up();
    await waitFn(() => document.querySelector('.page.on .hero .mode').textContent === 'Idle', null, { timeout: 4000 });
    assert.strictEqual(siren.S.mode, 'idle');
  });
  await t('window blur while holding MANUAL releases it', async () => {
    const b = page.locator('.page.on .hold.manual'); const bb = await b.boundingBox();
    await page.mouse.move(bb.x + 20, bb.y + 20); await page.mouse.down();
    await waitFn(() => document.querySelector('.page.on .hero .mode').textContent === 'Manual', null, { timeout: 4000 });
    await page.evaluate(() => window.dispatchEvent(new Event('blur')));
    await waitFn(() => document.querySelector('.page.on .hero .mode').textContent === 'Idle', null, { timeout: 4000 });
    await page.mouse.up();
  });

  await t('Component test: needs TEST MODE; hold energises one relay; release de-energises', async () => {
    await page.click('.page.on .tabs button:has-text("Component test")');
    assert.ok(await page.locator('.page.on .comp >> nth=0').isDisabled(), 'disabled without test mode');
    await page.click('.page.on button:has-text("Turn TEST MODE on")');
    await page.click('#modal button:has-text("Turn on")');
    await page.waitForSelector('.page.on .tm-banner:visible');
    assert.strictEqual(siren.S.testMode, true);
    const c = page.locator('.page.on .comp >> nth=1'); const bb = await c.boundingBox();
    await page.mouse.move(bb.x + 20, bb.y + 20); await page.mouse.down();
    await sleep(1500);
    assert.deepStrictEqual(siren.S.relays, [0, 1, 0]);
    await page.mouse.up(); await sleep(500);
    assert.deepStrictEqual(siren.S.relays, [0, 0, 0]);
    await shot('test-mode');
    await page.click('.page.on button:has-text("Turn TEST MODE off")');
    await waitFn(() => !document.querySelector('.page.on .tm-banner') || document.querySelector('.page.on .tm-banner').hidden);
  });

  await t('Run log tab lists the runs', async () => {
    await page.click('.page.on .tabs button:has-text("Run log")');
    await page.waitForSelector('.page.on table tbody tr');
    assert.ok(await page.locator('.page.on table tbody tr').count() >= 1);
    await shot('runlog');
  });

  await t('Settings: editing one value POSTs only that value (in ms)', async () => {
    await page.click('.page.on .tabs button:has-text("Settings")');
    await page.waitForSelector('.page.on .fgrid input');
    const inp = page.locator('.page.on .field:has(label:text-is("Duration (s)")) input').first();
    await inp.fill('90');
    await page.click('.page.on button:has-text("Save changes")');
    await page.waitForSelector('.toast:has-text("Settings saved")');
    assert.deepStrictEqual(siren.S.lastSettingsPost, { wailDuration: 90000 });
    await shot('siren-settings');
  });
  await t('Settings: out-of-range value is blocked client-side', async () => {
    const inp = page.locator('.page.on .field:has(label:text-is("Duration (s)")) input').first();
    siren.S.lastSettingsPost = null;
    await inp.fill('0.2');
    await page.click('.page.on button:has-text("Save changes")');
    await sleep(300);
    assert.strictEqual(siren.S.lastSettingsPost, null);
    assert.ok(await page.locator('.page.on .field.err').count());
    await inp.fill('90'); // restore
  });

  await t('Weather Watcher page: status, alerts, siren-mode setting save', async () => {
    await page.click('#nav button[data-page=ww]');
    await page.waitForSelector('.page.on .alert:has-text("Tornado Warning")');
    await page.waitForSelector('.page.on td:has-text("TRIGGERED")');
    await shot('ww-status');
    await page.click('.page.on .tabs button:has-text("Settings")');
    const sel = page.locator('.page.on .field:has(label:text-is("Tornado Warning - radar indicated")) select');
    await sel.waitFor();
    await sel.selectOption('FASTWAIL');
    await page.click('.page.on button:has-text("Save changes")');
    await page.waitForSelector('.toast:has-text("Settings saved")');
    assert.deepStrictEqual(ww.S.lastSettingsPost, { tornadoUnconfirmedMode: 'FASTWAIL' });
    await shot('ww-settings');
  });
  await t('WW: other-extreme checklist round-trips', async () => {
    const box = page.locator('.page.on .checks input[value=EVAC]');
    await box.check();
    await page.click('.page.on button:has-text("Save changes")');
    await page.waitForSelector('.toast:has-text("Settings saved") >> nth=-1');
    assert.strictEqual(ww.S.lastSettingsPost.otherExtremeIncluded, 'FIRE,HAZMAT,EVAC');
  });
  await t('WW: ntfy priorities are sent as numbers', async () => {
    await page.click('.page.on .tabs button:has-text("Settings")');
    const sel = page.locator('.page.on .field:has(label:text-is("Siren stopped")) select');
    await sel.selectOption('5');
    await page.click('.page.on button:has-text("Save changes")');
    await sleep(500);
    assert.strictEqual(ww.S.lastSettingsPost.ntfyPriStop, 5);
  });

  await t('Connections page: wrong password is reported, not retried', async () => {
    await page.click('#nav button[data-page=system]');
    const pw = page.locator('.page.on input[type=password] >> nth=0');
    await pw.fill('Wrong123!');
    const logins = () => siren.S.calls.filter((c) => c === 'POST /auth/login').length;
    const before = logins();
    await page.locator('.page.on button:has-text("Save & connect") >> nth=0').click();
    await waitFn(() => document.querySelector('#topSiren').textContent.includes('wrong password'), null, { timeout: 8000 });
    await sleep(2500);
    assert.ok(logins() - before <= 2, 'login attempts after bad password: ' + (logins() - before));
    await shot('connections-badpw');
    await pw.fill('Siren123!');
    await page.locator('.page.on button:has-text("Save & connect") >> nth=0').click();
    await waitFn(() => document.querySelector('#topSiren').textContent.includes('online'), null, { timeout: 8000 });
  });

  await t('Restart board action (with confirm)', async () => {
    await page.locator('.page.on button:has-text("Restart board") >> nth=1').click();
    await page.click('#modal button:has-text("Restart")');
    await page.waitForSelector('.toast:has-text("Restarting")');
    assert.strictEqual(ww.S.restarted, 1);
  });

  await t('no renderer errors', async () => { assert.deepStrictEqual(errors, []); });

  await app.close();
  siren.close(); ww.close();
  console.log(fails ? `\n${fails} FAILED` : '\nall UI checks passed');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
