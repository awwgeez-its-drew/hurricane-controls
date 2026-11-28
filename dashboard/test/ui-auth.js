'use strict';
// Windows sign-in gate, end to end in the real Electron app. The Windows dialog
// itself can't run here, so HC_AUTH_MOCK stands in for it (ignored when packaged).
const { _electron: electron } = require('playwright');
const path = require('path'), fs = require('fs'), os = require('os'), assert = require('assert');
const { createSiren, createWW } = require('./mock-boards');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function launch(mode) {
  const siren = await createSiren(), ww = await createWW();
  const ud = fs.mkdtempSync(path.join(os.tmpdir(), 'hc-auth-'));
  const b64 = (s) => 'plain:' + Buffer.from(s).toString('base64');
  fs.writeFileSync(path.join(ud, 'config.json'), JSON.stringify({ siren: { host: siren.host, password: b64('Siren123!') }, ww: { host: ww.host, password: b64('Weather123!') } }));
  const app = await electron.launch({ executablePath: require('electron'), args: [path.join(__dirname, '..'), '--no-sandbox', '--user-data-dir=' + ud], env: { ...process.env, HC_AUTH_MOCK: mode } });
  const page = await app.firstWindow();
  await page.setViewportSize({ width: 1360, height: 860 });
  const until = async (fn, ms = 6000) => { const e = Date.now() + ms; while (Date.now() < e) { if (await page.evaluate(fn)) return; await sleep(50); } throw new Error('timeout: ' + String(fn).slice(0, 100)); };
  await until(() => document.querySelector('#topSiren').textContent.includes('online'));
  const prompts = () => app.evaluate(() => global.__hcGate.mockPrompts || 0);
  const hold = async (sel, ms) => { const b = await page.locator(sel).first().boundingBox(); await page.mouse.move(b.x + b.width / 2, b.y + b.height / 2); await page.mouse.down(); await sleep(ms); await page.mouse.up(); };
  return { siren, ww, app, page, until, prompts, hold, close: async () => { await app.close(); siren.close(); ww.close(); } };
}

let fails = 0;
const t = async (name, fn) => { try { await fn(); console.log('  ok   ' + name); } catch (e) { fails++; console.log('  FAIL ' + name + '\n       ' + (e.message || e)); } };

(async () => {
  console.log('sign-in DENIED / cancelled');
  let c = await launch('deny');
  await t('shows a Locked chip', async () => { assert.match(await c.page.locator('#authChip').innerText(), /Locked/); });
  await t('hold WAIL: prompts, is refused, siren does NOT start', async () => {
    await c.hold('.modes .hold.wail', 1300); await sleep(800);
    assert.strictEqual(c.siren.S.mode, 'idle'); assert.strictEqual(await c.prompts(), 1);
    assert.ok(await c.page.locator('.toast:has-text("cancelled")').count());
  });
  await t('MANUAL press while locked prompts and does not sound', async () => {
    await c.page.click('#nav button[data-page=siren]');
    const b = await c.page.locator('.page.on .hold.manual').boundingBox();
    await c.page.mouse.move(b.x + 20, b.y + 20); await c.page.mouse.down(); await sleep(600); await c.page.mouse.up();
    assert.strictEqual(c.siren.S.mode, 'idle'); assert.strictEqual(await c.prompts(), 2);
  });
  await t('settings save is refused (nothing POSTed)', async () => {
    await c.page.click('.page.on .tabs button:has-text("Settings")');
    await c.page.waitForSelector('.page.on .fgrid input');
    await c.page.locator('.page.on .field:has(label:text-is("Duration (s)")) input').first().fill('77');
    c.siren.S.lastSettingsPost = null;
    await c.page.click('.page.on button:has-text("Save changes")');
    await sleep(700);
    assert.strictEqual(c.siren.S.lastSettingsPost, null);
    assert.ok(await c.page.locator('.page.on .msg.er:has-text("cancelled")').count());
  });
  await t('reading data still works while locked (status, log)', async () => {
    await c.page.click('.page.on .tabs button:has-text("Run log")');
    await c.page.waitForSelector('.page.on table, .page.on .empty');
  });
  await c.close();

  console.log('sign-in ALLOWED');
  c = await launch('allow');
  await t('hold WAIL: one prompt, then the siren starts; chip shows Unlocked', async () => {
    await c.hold('.modes .hold.wail', 1300);
    await c.until(() => document.querySelector('.hero .mode').textContent === 'Wail');
    assert.strictEqual(c.siren.S.mode, 'wail'); assert.strictEqual(await c.prompts(), 1);
    assert.match(await c.page.locator('#authChip').innerText(), /Unlocked/);
  });
  await t('STOP works while LOCKED, with no prompt', async () => {
    await c.page.click('#authChip');                                    // lock now
    await c.until(() => document.querySelector('#authChip').textContent.includes('Locked'));
    await c.hold('#gStop', 700);
    await c.until(() => document.querySelector('.hero .mode').textContent === 'Idle');
    assert.strictEqual(c.siren.S.mode, 'idle'); assert.strictEqual(await c.prompts(), 1, 'no new prompt for STOP');
  });
  await t('within the unlock window a settings save needs no second prompt', async () => {
    await c.page.click('#authChip');                                    // unlock (prompt #2)
    await c.until(() => document.querySelector('#authChip').textContent.includes('Unlocked'));
    const n = await c.prompts();
    await c.page.click('#nav button[data-page=siren]');
    await c.page.click('.page.on .tabs button:has-text("Settings")');
    await c.page.waitForSelector('.page.on .fgrid input');
    await c.page.locator('.page.on .field:has(label:text-is("Duration (s)")) input').first().fill('77');
    await c.page.click('.page.on button:has-text("Save changes")');
    await c.page.waitForSelector('.toast:has-text("Settings saved")');
    assert.deepStrictEqual(c.siren.S.lastSettingsPost, { wailDuration: 77000 }); assert.strictEqual(await c.prompts(), n);
  });
  await t('the unlock expires: after the window closes the next action prompts again', async () => {
    await c.app.evaluate(() => { global.__hcGate.until = Date.now() - 1; });
    const n = await c.prompts();
    await c.page.click('#nav button[data-page=ww]');
    await c.page.click('.page.on .tabs button:has-text("Settings")');
    const sel = c.page.locator('.page.on .field:has(label:text-is("Tornado Warning - radar indicated")) select');
    await sel.waitFor(); await sel.selectOption('ATTACK');
    c.ww.S.lastSettingsPost = null;
    await c.page.click('.page.on button:has-text("Save changes")');
    for (let i = 0; i < 100 && !c.ww.S.lastSettingsPost; i++) await sleep(50);   // a fresh save, not an old toast
    assert.deepStrictEqual(c.ww.S.lastSettingsPost, { tornadoUnconfirmedMode: 'ATTACK' });
    assert.strictEqual(await c.prompts(), n + 1);
  });
  await t('turning protection OFF needs sign-in, then nothing prompts', async () => {
    await c.app.evaluate(() => { global.__hcGate.until = 0; });
    await c.page.click('#nav button[data-page=system]');
    const n = await c.prompts();
    await c.page.locator('#reqAuth').uncheck();
    await c.until(() => document.querySelector('#authChip').hidden === true);
    assert.strictEqual(await c.prompts(), n + 1);
    await c.page.click('#nav button[data-page=overview]');
    await c.hold('.modes .hold.wail', 1300);
    await c.until(() => document.querySelector('.hero .mode').textContent === 'Wail');
    assert.strictEqual(await c.prompts(), n + 1, 'no prompt once protection is off');
  });
  await c.close();
  console.log(fails ? `\n${fails} FAILED` : '\nall sign-in checks passed');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
