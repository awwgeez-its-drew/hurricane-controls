'use strict';
// Page builders. Each returns {node, render(state), onShow?(), onHide?()}.
// `state` is { siren: snapshot, ww: snapshot } pushed from the main process.

const ML = { idle: 'Idle', seq: 'Starting', wail: 'Wail', attack: 'Attack', fastwail: 'Fast Wail', manual: 'Manual', growl: 'Growl Test', stop: 'Stopping', test: 'Test Mode', offline: 'No signal' };
const LM = ['—', 'Wail', 'Attack', 'Fast Wail', 'Manual', 'Growl Test'];
const LS = ['Local', 'Web', 'Mesh', 'NWS Alert', 'Auto'];
const CONN_TEXT = {
  online: 'online', connecting: 'connecting…', offline: 'offline', 'auth-error': 'wrong password',
  locked: 'locked out', unconfigured: 'not set up',
};

function sirenKey(d) {
  const st = d.mode || 'idle', rm = d.runMode || 'idle';
  if (d.testMode && st === 'idle') return 'test';
  if (st === 'idle') return 'idle';
  if (st === 'stop') return 'stop';
  if (rm !== 'idle') return rm;
  return st.startsWith('attack') ? 'attack' : st.startsWith('fastwail') ? 'fastwail' : st;
}
const sirenOnline = (s) => s && s.conn === 'online' && s.data && s.data.status;
const connDot = (s) => (!s ? '' : s.conn === 'online' ? 'ok' : s.conn === 'connecting' ? 'warn pulse' : s.conn === 'unconfigured' ? '' : 'bad');
const chipEl = (dot, text) => el('span', { class: 'chip' }, el('i', { class: 'dot ' + dot }), text);

// ── commands ────────────────────────────────────────────────────────────────
async function sirenCmd(body, opts) {
  const r = await hc.req('siren', 'POST', '/cmd', body, opts);
  if (r.skipped) return true;
  if (r.status !== 200 || !r.json) { toast(r.error || 'Siren did not answer', 'er'); return false; }
  if (!r.json.ok) {
    toast(body.mode === 'stop' ? 'Refused: software STOP is disabled in TEST MODE - use the hardware E-stop'
      : body.mode === 'test' ? 'Turn TEST MODE on first (and the siren must be idle)'
        : 'The siren refused that command (TEST MODE on, or not idle)', 'er');
    return false;
  }
  return true;
}
const doStop = () => { claxon(); return sirenCmd({ mode: 'stop' }, { priority: true }); };
const doStart = (mode) => { claxon(); return sirenCmd({ mode }); };

// ── shared building blocks ──────────────────────────────────────────────────
function alertBlock(a, { showSub = true } = {}) {
  const color = a.color || '';
  return el('div', { class: 'alert ' + color },
    el('b', { text: a.event || 'Alert' }),
    a.tag ? el('span', { class: 'tag', text: a.tag }) : null,
    a.exp ? el('span', { class: 'hint', text: fmtUntil(a.exp) }) : null,
    a.triggered || a.activatedThis ? el('span', { class: 'act', text: 'SIREN ACTIVATED' + ((a.mode || a.actMode) ? ' - ' + (a.mode || a.actMode) : '') + (a.actTime ? ' at ' + a.actTime : '') }) : null,
    showSub && (a.areaDesc || a.headline) ? el('span', { class: 'sub', text: [a.headline, a.areaDesc].filter(Boolean).join(' - ') }) : null);
}

function sirenHero() {
  const lbl = el('div', { class: 'lbl', text: 'Siren controller' });
  const mode = el('div', { class: 'mode' });
  const timerLbl = el('div', { class: 'lbl', text: 'Remaining' });
  const timer = el('div', { class: 'timer' });
  const sub = el('div', { class: 'hint' });
  const hero = el('div', { class: 'hero' }, el('div', null, lbl, mode), el('div', { class: 'spacer' }), el('div', null, timerLbl, timer, sub));
  const relays = ['Chopper', 'Blower', 'Rotator'].map((n) => ({ n, box: el('div', { class: 'relay' }, el('b', { text: n }), el('span', { text: 'OFF' })) }));
  const rrow = el('div', { class: 'relays' }, relays.map((r) => r.box));
  const flags = el('div', { class: 'row' });
  return {
    node: el('div', { class: 'stack' }, hero, rrow, flags),
    parts: { hero, relays: rrow, flags },
    render(s) {
      const d = sirenOnline(s) ? s.data.status : null;
      if (!d) {
        hero.dataset.m = 'offline'; mode.textContent = s && s.conn === 'connecting' ? 'Connecting…' : 'No signal';
        timer.textContent = '--:--'; sub.textContent = s ? (s.error || CONN_TEXT[s.conn] || '') : '';
        relays.forEach((r) => { r.box.classList.remove('on'); r.box.lastChild.textContent = '-'; });
        flags.replaceChildren(); return;
      }
      const key = sirenKey(d);
      hero.dataset.m = key; mode.textContent = ML[key] || key;
      const st = d.mode;
      if (key === 'test') { timerLbl.textContent = 'Remaining'; timer.textContent = '--:--'; sub.textContent = 'Software STOP disabled - use the hardware E-stop'; }
      else if (st === 'idle') { timerLbl.textContent = 'Remaining'; timer.textContent = '--:--'; sub.textContent = 'Ready'; }
      else if (st === 'seq') { timerLbl.textContent = 'Remaining'; timer.textContent = '--:--'; sub.textContent = 'Starting…'; }
      else if (st === 'stop') { timerLbl.textContent = 'Elapsed'; timer.textContent = fmtClock(d.elapsed); sub.textContent = 'Stopping…'; }
      else if (d.hasRemaining) { timerLbl.textContent = 'Remaining'; timer.textContent = fmtClock(d.remaining); sub.textContent = fmtClock(d.elapsed) + ' elapsed'; }
      else { timerLbl.textContent = 'Elapsed'; timer.textContent = fmtClock(d.elapsed); sub.textContent = d.runMode === 'manual' ? 'Sounding while held' : ''; }
      (d.relays || [0, 0, 0]).forEach((v, i) => { relays[i].box.classList.toggle('on', !!v); relays[i].box.lastChild.textContent = v ? 'ON' : 'OFF'; });
      const h = d.health || {};
      flags.replaceChildren(...[
        chipEl(d.btnLocked ? 'warn' : 'ok', d.btnLocked ? 'Panel buttons locked' : 'Panel buttons active'),
        chipEl(h.wifi ? 'ok' : 'bad', 'Wi-Fi ' + (h.wifi ? 'connected' : 'down')),
        chipEl(h.ww === 'OK' ? 'ok' : (h.ww === 'ERROR' || h.ww === 'LOST') ? 'bad' : h.ww === 'STARTING' || h.ww === 'N/A' ? 'warn' : '', 'Weather Watcher ' + ({ OFF: 'off', 'N/A': 'not connected', LOST: 'LOST', STARTING: 'starting', OK: 'OK', ERROR: 'ERROR' }[h.ww] || h.ww || '?')),
        d.currentTime ? chipEl('', 'Clock ' + d.currentTime) : null,
        d.tempF != null ? chipEl('', 'CPU ' + d.tempF + '°F') : null,
        h.otaActive ? chipEl('warn', 'Firmware update in progress') : null].filter(Boolean));
    },
  };
}

// Start/stop controls. `full` adds Manual, Growl and the panel-lock toggle.
function sirenModes({ full, big }) {
  const mk = (cls, label, hint) => el('button', { class: 'hold ' + cls, type: 'button' }, el('i', { class: 'hfill' }), el('span', { text: label }), el('small', { text: hint }));
  const bWail = mk('wail', 'WAIL', 'hold 1 s to start'), bAttack = mk('attack', 'ATTACK', 'hold 1 s to start'), bFast = mk('fastwail', 'FAST WAIL', 'hold 1 s to start');
  const bStop = mk('stop', 'STOP', 'hold 0.4 s to stop');
  armHold(bWail, 1000, () => doStart('wail')); armHold(bAttack, 1000, () => doStart('attack')); armHold(bFast, 1000, () => doStart('fastwail'));
  armHold(bStop, 400, doStop);
  const grid = el('div', { class: 'modes' + (big ? ' big' : '') }, bWail, bAttack, bFast);
  const bMan = mk('manual', 'MANUAL', 'press and hold to sound');
  const bGrowl = mk('growl', 'GROWL TEST', 'hold 1 s');
  const bLock = el('button', { class: 'btn', type: 'button', text: 'Lock panel buttons' });
  if (full) {
    armHold(bGrowl, 1000, () => doStart('growl'));
    armMomentary(bMan, {
      // If locked, sign in FIRST (a prompt mid-hold would steal focus and release the button).
      down: async () => { if (authLocked()) { await unlockInteractive(); return false; } claxon(); return sirenCmd({ mode: 'manual' }); },
      ka: () => sirenCmd({ mode: 'manual-ka' }, { skipIfBusy: true }),
      up: () => sirenCmd({ mode: 'stop' }, { priority: true }),
    });
    grid.append(bMan, bGrowl);
    bLock.addEventListener('click', () => sirenCmd({ mode: 'btn-lock' }));
  }
  grid.append(bStop);
  const note = el('p', { class: 'hint' });
  return {
    node: el('div', { class: 'stack' }, grid, full ? el('div', { class: 'row' }, bLock, el('span', { class: 'hint', text: 'Locks/unlocks the physical WAIL / ATTACK / STOP buttons on the siren.' })) : null, note),
    render(s) {
      const d = sirenOnline(s) ? s.data.status : null;
      const idle = d && d.mode === 'idle' && !d.testMode;
      for (const b of [bWail, bAttack, bFast, bGrowl]) b.disabled = !idle;
      // Never disable MANUAL while it is the running mode: the button being held
      // owns the pointer capture, and disabling it would fire lostpointercapture
      // and cut the siren off mid-hold.
      bMan.disabled = !(idle || (d && d.runMode === 'manual'));
      bStop.disabled = !d || (d.mode === 'idle' && !d.testMode);
      bLock.disabled = !d; bLock.textContent = d && d.btnLocked ? 'Unlock panel buttons' : 'Lock panel buttons';
      note.textContent = !d ? 'Controls are disabled until the siren controller is online.'
        : d.testMode ? 'TEST MODE is on: run modes and software STOP are disabled by the siren itself.' : '';
    },
  };
}

function wwSummary({ full }) {
  const chips = el('div', { class: 'row' });
  const alerts = el('div', { class: 'stack' });
  const detail = el('dl', { class: 'kv' });
  const recent = el('div');
  return {
    node: el('div', { class: 'stack' }, chips, alerts, full ? detail : null, full ? recent : null),
    render(s) {
      const d = s && s.data && s.data.status;
      if (!d) {
        chips.replaceChildren(chipEl(connDot(s), 'Weather Watcher ' + (s ? (s.error || CONN_TEXT[s.conn]) : '')));
        alerts.replaceChildren(el('p', { class: 'empty', text: 'No data yet.' })); detail.replaceChildren(); recent.replaceChildren(); return;
      }
      chips.replaceChildren(
        chipEl(d.wifiConnected ? 'ok' : 'bad', 'Wi-Fi ' + (d.wifiConnected ? 'connected' : 'down')),
        chipEl(d.apiGreen ? 'ok' : 'bad', 'NWS API ' + (d.apiGreen ? 'OK' : 'problem')),
        chipEl(!d.everTestedLink ? 'warn' : d.linkOk ? 'ok' : 'bad', 'Siren link ' + (!d.everTestedLink ? 'untested' : d.linkOk ? 'OK' : 'FAILED')),
        chipEl(d.stale ? 'warn' : 'ok', d.stale ? 'Alert data stale' : 'Alert data fresh'),
        chipEl('', 'Last poll ' + d.lastPoll));
      const list = d.currentAlerts || [];
      if (full) alerts.replaceChildren(...(list.length ? list.map((a) => alertBlock(a)) : [el('p', { class: 'empty', text: 'No active warnings or watches for this location.' })]));
      else alerts.replaceChildren(el('p', { class: 'empty', text: list.length ? list.length + ' active - see the banner above.' : 'No active warnings or watches.' }));
      if (full) {
        const rows = [['API status', d.apiDetail], ['Siren link', d.linkDetail], ['Wi-Fi', d.wifiDetail], ['Last message from siren', d.lastSxEvent ? d.lastSxEvent + (d.lastSxEventAgeSec >= 0 ? ' (' + fmtAgo(d.lastSxEventAgeSec) + ')' : '') : 'none yet'],
          ['Clock', d.currentTime], ['Uptime', fmtUptime(d.uptime)], ['CPU temperature', d.tempF + '°F'], ['Firmware', d.fwVersion]];
        detail.replaceChildren(...rows.flatMap(([k, v]) => [el('dt', { text: k }), el('dd', { text: v == null ? '-' : String(v) })]));
        const rec = d.recentAlerts || [];
        recent.replaceChildren(el('h3', { text: 'Recent alert decisions' }), rec.length
          ? el('div', { class: 'scroll' }, el('table', null, el('thead', null, el('tr', null, ['Time', 'Alert', 'Siren mode', 'Outcome'].map((h) => el('th', { text: h })))),
            el('tbody', null, rec.map((r) => el('tr', null, el('td', { text: r.time }), el('td', { text: r.event }), el('td', { text: r.mode }), el('td', { text: r.disposition }))))))
          : el('p', { class: 'empty', text: 'Nothing logged yet.' }));
      }
    },
  };
}

// ── Warnings & watches banner ───────────────────────────────────────────────
const SEV_RANK = { purple: 4, red: 3, orange: 2, yellow: 1, '': 0 };
const topSeverity = (list) => list.reduce((m, a) => (SEV_RANK[a.color || ''] > SEV_RANK[m] ? (a.color || '') : m), '');
function warnBanner() {
  const node = el('section', { class: 'warnbanner none', role: 'region', 'aria-label': 'Active warnings and watches', 'aria-live': 'polite' });
  return {
    node,
    render(w) {
      const d = w && w.data && w.data.status;
      node.removeAttribute('data-sev');
      if (!d || w.conn !== 'online') {
        node.className = 'warnbanner unknown';
        node.replaceChildren(el('div', { class: 'wb-head', text: 'Weather Watcher ' + (w ? (CONN_TEXT[w.conn] || 'offline') : 'not connected') + ' - alert status unknown. Do not assume there are no warnings.' }));
        return;
      }
      const list = (d.currentAlerts || []).slice().sort((a, b) => SEV_RANK[b.color || ''] - SEV_RANK[a.color || '']);
      if (!list.length) {
        node.className = 'warnbanner none';
        node.replaceChildren(el('div', { class: 'wb-head', text: 'No active warnings or watches for your location' + (d.stale ? ' (alert data is stale)' : '') }));
        return;
      }
      const warnings = list.filter((a) => !/watch/i.test(a.event || '')).length;
      const watches = list.length - warnings;
      node.className = 'warnbanner';
      node.dataset.sev = topSeverity(list);
      node.replaceChildren(
        el('div', { class: 'wb-head' }, 'Active ' + [warnings && warnings + (warnings === 1 ? ' warning' : ' warnings'), watches && watches + (watches === 1 ? ' watch' : ' watches')].filter(Boolean).join(' and '),
          d.stale ? el('span', { text: '· data stale' }) : null),
        el('div', { class: 'wb-body' }, list.map((a) => el('div', { class: 'wrow ' + (a.color || '') },
          el('span', { class: 'kind', text: /watch/i.test(a.event || '') ? 'WATCH' : 'WARNING' }),
          el('span', { class: 'ev', text: a.event || 'Alert' }),
          a.tag ? el('span', { class: 'tag', text: a.tag }) : null,
          a.exp ? el('span', { class: 'until', text: fmtUntil(a.exp) }) : null,
          a.triggered ? el('span', { class: 'act', text: 'SIREN ACTIVATED' + (a.mode ? ' - ' + a.mode : '') }) : null,
          a.areaDesc ? el('span', { class: 'area', text: a.areaDesc }) : null))));
    },
  };
}

// ── Overview ────────────────────────────────────────────────────────────────
function pageOverview(go) {
  const hero = sirenHero(), modes = sirenModes({ full: false, big: true }), ww = wwSummary({ full: false }), wb = warnBanner();
  const welcome = el('div', { class: 'banner warn', hidden: true },
    el('b', { text: 'Welcome. ' }), 'Enter each board\'s address and password to get started. ',
    el('button', { class: 'btn small', text: 'Open Connections', onclick: () => go('system') }));
  const sirenIssue = el('div', { class: 'banner bad', hidden: true });
  const wwIssue = el('div', { class: 'banner bad', hidden: true });
  const node = el('div', { class: 'page' },
    welcome,
    wb.node,
    el('div', { class: 'grid ov-grid' },
      el('section', { class: 'card stack' }, el('header', null, el('h2', { text: 'Siren Controller' }), el('button', { class: 'btn small', text: 'Open full controls', onclick: () => go('siren') })), sirenIssue, hero.parts.hero, modes.node, hero.parts.relays, hero.parts.flags),
      el('section', { class: 'card stack' }, el('header', null, el('h2', { text: 'Weather Watcher' }), el('button', { class: 'btn small', text: 'Open details', onclick: () => go('ww') })), wwIssue, ww.node)));
  const issue = (box, name, s) => {
    const bad = s && ['offline', 'auth-error', 'locked'].includes(s.conn);
    box.hidden = !bad;
    if (!bad) return;
    box.replaceChildren(el('b', { text: name + ': ' + CONN_TEXT[s.conn] + '. ' }), s.error || '',
      ' ', el('button', { class: 'btn small', text: s.conn === 'offline' ? 'Retry now' : 'Reconnect', onclick: () => hc.reconnect(s.id) }),
      s.conn === 'auth-error' ? el('button', { class: 'btn small', text: 'Fix password', onclick: () => go('system') }) : null);
  };
  return {
    node,
    render(st) {
      welcome.hidden = !!(st.siren && st.siren.host) || !!(st.ww && st.ww.host);
      issue(sirenIssue, 'Siren Controller', st.siren); issue(wwIssue, 'Weather Watcher', st.ww);
      hero.render(st.siren); modes.render(st.siren); ww.render(st.ww); wb.render(st.ww);
    },
  };
}

// ── Siren page ──────────────────────────────────────────────────────────────
function pageSiren() {
  const hero = sirenHero(), modes = sirenModes({ full: true });
  const tabs = tabBar(['Control', 'Component test', 'Run log', 'Settings']);

  // Component test
  const tmBtn = el('button', { class: 'btn', type: 'button', text: 'Turn TEST MODE on' });
  const tmBanner = el('div', { class: 'tm-banner', hidden: true });
  const comps = ['chopper', 'blower', 'rotator'].map((c) => {
    const b = el('button', { class: 'hold comp', type: 'button' }, el('span', { text: c.toUpperCase() }), el('small', { text: 'press and hold' }));
    armMomentary(b, {
      down: async () => { if (authLocked()) { await unlockInteractive(); return false; } return sirenCmd({ mode: 'test', component: c, on: true }); },
      ka: () => sirenCmd({ mode: 'test-ka', component: c }, { skipIfBusy: true }),
      up: () => sirenCmd({ mode: 'test', component: c, on: false }, { priority: true }),
    });
    return { c, b };
  });
  const relayState = el('p', { class: 'hint' });
  tmBtn.addEventListener('click', async () => {
    const on = !(tmBtn.dataset.on === '1');
    if (on && !(await confirmBox('Turn TEST MODE on?',
      'TEST MODE lets you run the chopper, blower and rotator individually. While it is on, the siren itself disables run modes AND the software STOP button. ' +
      'Make sure your hardware E-stop works and is within reach. TEST MODE switches itself off after about 3 minutes.', 'Turn on', true))) return;
    sirenCmd({ mode: 'test-lock', on });
  });
  const testTab = el('div', { class: 'stack' },
    el('div', { class: 'card stack' }, el('header', null, el('h2', { text: 'Component test' }), tmBtn), tmBanner,
      el('p', { class: 'hint', text: 'Each component stays on only while you hold the button. If this app loses contact for ~2 s, the board switches it off by itself.' }),
      el('div', { class: 'comps' }, comps.map((x) => x.b)), relayState));

  // Run log
  const logBody = el('div'); const totals = el('div', { class: 'row' });
  const csvBtn = el('button', { class: 'btn', text: 'Export full log (CSV)', onclick: () => exportCsv('siren') });
  const logTab = el('div', { class: 'card stack' }, el('header', null, el('h2', { text: 'Run log' }), csvBtn), totals, logBody);
  async function loadLog() {
    const r = await hc.req('siren', 'GET', '/log');
    if (r.status !== 200 || !r.json) { logBody.replaceChildren(el('p', { class: 'empty', text: r.error || 'Could not load the run log.' })); return; }
    const d = r.json;
    totals.replaceChildren(chipEl('', d.runs + ' runs in total'), chipEl('', (d.secs / 3600).toFixed(1) + ' hours of sounding'));
    if (!d.entries.length) { logBody.replaceChildren(el('p', { class: 'empty', text: 'No runs since the last restart. (The CSV export has the full history.)' })); return; }
    logBody.replaceChildren(el('div', { class: 'scroll' }, el('table', null,
      el('thead', null, el('tr', null, ['When', 'Mode', 'Source', 'Length', 'Ended', 'Alert'].map((h) => el('th', { text: h })))),
      el('tbody', null, d.entries.map((e) => {
        const when = e.e ? new Date(e.e * 1000).toLocaleString() : fmtAgo(d.uptime - e.u);
        const end = e.x === 0 ? 'completed' : e.x === 2 ? 'failsafe stop' : e.x === 3 ? 'interrupted (power loss)' : 'stopped (' + (LS[e.ss] || '?') + ')';
        return el('tr', null, el('td', { text: when }), el('td', { text: LM[e.m] || '?' }), el('td', { text: LS[e.s] || '?' }), el('td', { text: fmtDur(e.d) }),
          el('td', { class: e.x === 0 ? 'ok' : e.x === 3 ? 'bad' : '', text: end }), el('td', { text: e.at ? e.at + (e.ai ? ' (' + e.ai + ')' : '') : '' }));
      })))));
  }

  const ed = settingsEditor('siren', SIREN_SCHEMA);
  const settingsTab = el('div', { class: 'card' }, el('header', null, el('h2', { text: 'Siren settings' }), el('span', { class: 'hint', text: 'Only changed values are sent to the board.' })), ed.node);

  tabs.panes([
    el('div', { class: 'stack' }, el('section', { class: 'card stack' }, hero.node, modes.node),
      el('p', { class: 'hint', text: 'This controls a mechanical siren that can injure. Keep your independent hardware E-stop within reach. See the Safety section of the project README.' })),
    testTab, logTab, settingsTab]);
  let logTimer = null;
  tabs.onSelect = (i) => {
    clearInterval(logTimer);
    if (i === 2) { loadLog(); logTimer = setInterval(loadLog, 5000); }
    if (i === 3) ed.load();
  };
  let lastTest = null;
  return {
    node: el('div', { class: 'page' }, tabs.node),
    render(st) {
      const s = st.siren;
      hero.render(s); modes.render(s);
      const d = sirenOnline(s) ? s.data.status : null;
      tmBtn.disabled = !d || (d.mode !== 'idle');
      tmBtn.dataset.on = d && d.testMode ? '1' : '0';
      tmBtn.textContent = d && d.testMode ? 'Turn TEST MODE off' : 'Turn TEST MODE on';
      tmBanner.hidden = !(d && d.testMode);
      if (d && d.testMode) {
        const rem = d.lockRemaining || 0;
        tmBanner.textContent = 'TEST MODE is ON - software STOP is disabled. ' + (d.lockAutoExpire ? 'Turns off automatically in ' + fmtClock(rem) + '.' : '');
      }
      for (const { c, b } of comps) { b.disabled = !(d && d.testMode && d.mode === 'idle'); }
      relayState.textContent = d ? 'Relays: chopper ' + (d.relays[0] ? 'ON' : 'off') + ' · blower ' + (d.relays[1] ? 'ON' : 'off') + ' · rotator ' + (d.relays[2] ? 'ON' : 'off') : '';
      // Refresh the log promptly when a run finishes.
      const key = d ? d.mode : null;
      if (lastTest === 'run' && key === 'idle' && tabs.current === 2) loadLog();
      lastTest = key && key !== 'idle' ? 'run' : 'idle';
    },
    onShow() { if (tabs.current === 3) ed.load(); },
    onHide() { clearInterval(logTimer); comps.forEach(({ b }) => b.dispatchEvent(new Event('lostpointercapture'))); },
  };
}

// ── Weather Watcher page ────────────────────────────────────────────────────
function pageWW() {
  const sum = wwSummary({ full: true });
  const tabs = tabBar(['Status', 'Settings', 'Notifications']);

  const linkBtn = el('button', { class: 'btn', text: 'Test siren link', onclick: async () => {
    const r = await hc.req('ww', 'POST', '/test-link');
    toast(r.status === 200 ? 'Link test started - result appears in Status' : (r.error || 'Failed'), r.status === 200 ? 'ok' : 'er');
  } });
  const csvBtn = el('button', { class: 'btn', text: 'Export alert log (CSV)', onclick: () => exportCsv('ww') });

  const ed = settingsEditor('ww', WW_SCHEMA, { onLoaded: (j) => { tokenState.textContent = j.ntfyHasToken ? 'A token is stored on the board.' : 'No token stored.'; } });
  const tokenIn = el('input', { type: 'password', autocomplete: 'off', placeholder: 'New access token' });
  const tokenState = el('span', { class: 'hint' });
  const ntfyOut = el('span', { class: 'msg' });
  const notifTab = el('div', { class: 'stack' },
    el('div', { class: 'card stack' }, el('header', null, el('h2', { text: 'ntfy access token' })),
      el('p', { class: 'hint', text: 'Only needed for a protected topic. The board never sends the token back, so it can only be replaced or cleared here.' }),
      el('div', { class: 'row' }, el('div', { class: 'field', style: 'min-width:280px' }, tokenIn),
        el('button', { class: 'btn', text: 'Save token', onclick: async () => {
          if (!tokenIn.value) return toast('Enter a token first (or use Clear token)', 'er');
          const r = await hc.req('ww', 'POST', '/ntfy-token', { token: tokenIn.value });
          if (r.status === 200) { tokenIn.value = ''; toast('Token saved', 'ok'); ed.load(); } else toast(r.error || 'Failed', 'er');
        } }),
        el('button', { class: 'btn', text: 'Clear token', onclick: async () => {
          if (!(await confirmBox('Clear the stored ntfy token?', 'Notifications to a protected topic will stop until a new token is saved.', 'Clear', true))) return;
          const r = await hc.req('ww', 'POST', '/ntfy-token', { token: '' });
          if (r.status === 200) { toast('Token cleared', 'ok'); ed.load(); } else toast(r.error || 'Failed', 'er');
        } }), tokenState)),
    el('div', { class: 'card stack' }, el('header', null, el('h2', { text: 'Send a test notification' })),
      el('div', { class: 'row' }, el('button', { class: 'btn', text: 'Send test push', onclick: async () => {
        ntfyOut.className = 'msg'; ntfyOut.textContent = 'Sending…';
        const r = await hc.req('ww', 'POST', '/ntfy-test');
        if (r.status !== 200) { ntfyOut.className = 'msg er'; ntfyOut.textContent = r.error || 'Failed'; return; }
        await sleep(3500);
        const s = await hc.req('ww', 'GET', '/ntfy-status');
        const j = s.json || {};
        ntfyOut.className = 'msg ' + (j.lastOk ? 'ok' : 'er');
        ntfyOut.textContent = j.lastOk ? 'Delivered: ' + (j.lastDetail || 'ok') : 'Not delivered: ' + (j.lastDetail || 'unknown') + (j.queued ? ' (' + j.queued + ' queued)' : '');
      } }), ntfyOut)),
    el('p', { class: 'hint', text: 'Notification settings (server, topic, priorities) are in the Settings tab.' }));

  tabs.panes([
    el('div', { class: 'stack' }, el('section', { class: 'card stack' }, el('header', null, el('h2', { text: 'Weather Watcher status' }), el('div', { class: 'row' }, linkBtn, csvBtn)), sum.node)),
    el('div', { class: 'card' }, el('header', null, el('h2', { text: 'Weather Watcher settings' }), el('span', { class: 'hint', text: 'Only changed values are sent to the board.' })), ed.node),
    notifTab]);
  tabs.onSelect = (i) => { if (i === 1 || i === 2) ed.load(); };
  return {
    node: el('div', { class: 'page' }, tabs.node),
    render(st) { sum.render(st.ww); },
    onShow() { if (tabs.current !== 0) ed.load(); },
  };
}

// ── Connections & System ────────────────────────────────────────────────────
function pageSystem() {
  const cards = {};
  const mkCard = (id, title, defaultHint) => {
    const host = el('input', { type: 'text', placeholder: 'e.g. 192.168.1.50 or hurricane.local', autocomplete: 'off' });
    const pw = el('input', { type: 'password', placeholder: defaultHint, autocomplete: 'off' });
    const out = el('span', { class: 'msg' });
    const status = el('span', { class: 'chip' });
    const info = el('dl', { class: 'kv' });
    const otaBar = el('progress', { max: '100', value: '0', hidden: true });
    const otaMsg = el('span', { class: 'msg' });
    let wifi = null, fw = null, lastHost = '';
    const board = el('span', { text: title });

    const test = el('button', { class: 'btn', text: 'Test connection', onclick: async () => {
      out.className = 'msg'; out.textContent = 'Testing…';
      const r = await hc.cfgTest({ id, host: host.value, password: pw.value });
      out.className = 'msg ' + (r.ok ? 'ok' : 'er'); out.textContent = r.message;
    } });
    const save = el('button', { class: 'btn primary', text: 'Save & connect', onclick: async () => {
      const r = await hc.cfgSave({ id, host: host.value, password: pw.value });
      if (!r.ok) { out.className = 'msg er'; out.textContent = r.error; return; }
      pw.value = ''; pw.placeholder = '(saved - leave blank to keep)';
      out.className = 'msg ok'; out.textContent = 'Saved. Connecting…';
    } });

    async function loadInfo() {
      if (!lastOnline(id)) { info.replaceChildren(); return; }
      const [w, s] = await Promise.all([hc.req(id, 'GET', '/wifi-data'), hc.req(id, 'GET', '/settings-data')]);
      wifi = w.json; fw = s.json;
      const rows = [];
      if (fw) rows.push(['Firmware', fw.fwVersion], ['Last reset', fw.resetReason]);
      if (wifi) rows.push(['Wi-Fi network', wifi.ssid || '-'], ['IP address', wifi.ip], ['Hostname', wifi.hostname], ['Mode', wifi.mode === 'sta' ? 'Joined your network' : 'Own access point'], ['Access point', wifi.apSecured ? 'Password protected' : 'OPEN (set a password)']);
      info.replaceChildren(...rows.flatMap(([k, v]) => [el('dt', { text: k }), el('dd', { text: String(v) })]));
    }

    const actions = el('div', { class: 'row' },
      el('button', { class: 'btn', text: 'Update firmware…', onclick: async () => {
        otaBar.hidden = false; otaBar.value = 0; otaMsg.className = 'msg'; otaMsg.textContent = '';
        const r = await hc.ota(id);
        otaBar.hidden = true;
        if (r.canceled) return;
        otaMsg.className = 'msg ' + (r.ok ? 'ok' : 'er');
        otaMsg.textContent = r.ok ? 'Update installed - the board is restarting…' : 'Update failed: ' + r.error;
      } }),
      el('button', { class: 'btn', text: 'Restart board', onclick: async () => {
        if (!(await confirmBox('Restart the ' + title + '?', id === 'siren' ? 'The siren must be idle. If it is running, a restart may stop it abruptly.' : 'Alert monitoring pauses for a minute.', 'Restart', true))) return;
        const r = await hc.req(id, 'POST', '/restart');
        toast(r.status === 200 ? 'Restarting…' : (r.error || 'Failed'), r.status === 200 ? 'ok' : 'er');
      } }),
      el('button', { class: 'btn', text: 'Change login password…', onclick: async () => {
        const p = await askPassword('New web password for the ' + title, '8+ characters with upper, lower, digit and a symbol. This is the password the dashboard and the board\'s own web page both use.');
        if (p == null) return;
        if (!strongPw(p)) return toast('Needs 8+ characters with upper-case, lower-case, a digit and a symbol', 'er');
        const r = await hc.req(id, 'POST', '/password', { password: p });
        if (r.json && r.json.ok) { await hc.cfgSave({ id, host: host.value || lastHost, password: p }); toast('Password changed - everyone else must sign in again', 'ok'); }
        else toast(r.error || 'The board rejected that password', 'er');
      } }),
      el('button', { class: 'btn', text: 'Change access-point password…', onclick: async () => {
        const p = await askPassword('New Wi-Fi access-point password', 'The board\'s own fallback Wi-Fi network. 8-63 characters. The board restarts.');
        if (p == null) return;
        if (p.length < 8 || p.length > 63) return toast('Must be 8 to 63 characters', 'er');
        const r = await hc.req(id, 'POST', '/ap-password', { password: p });
        toast(r.json && r.json.ok ? 'Saved - the board is restarting' : (r.error || 'Rejected'), r.json && r.json.ok ? 'ok' : 'er');
      } }),
      el('button', { class: 'btn', text: 'Rename device…', onclick: () => renameDialog() }),
      el('button', { class: 'btn', text: 'Join another Wi-Fi network…', onclick: () => wifiDialog() }));

    async function renameDialog() {
      const inp = el('input', { type: 'text', value: (wifi && wifi.hostname) || '', maxlength: '32' });
      const r = await modal({ title: 'Rename the ' + title, body: el('div', { class: 'stack' }, el('p', { class: 'hint', text: 'Letters, digits and hyphens, 1-32 characters. The board restarts and is then reachable as <name>.local.' }), el('div', { class: 'field' }, el('label', { text: 'Hostname' }), inp)),
        buttons: [{ label: 'Cancel', value: false }, { label: 'Rename & restart', value: true, cls: 'primary' }] });
      if (!r) return;
      const name = inp.value.trim();
      if (!/^(?!-)[A-Za-z0-9-]{1,32}(?<!-)$/.test(name)) return toast('Invalid hostname', 'er');
      const oldName = wifi && wifi.hostname;
      const res = await hc.req(id, 'POST', '/wifi-data', { hostname: name });
      if (res.json && res.json.ok) {
        toast('Renamed - restarting', 'ok');
        if (oldName && host.value.toLowerCase() === (oldName + '.local').toLowerCase()) { host.value = name + '.local'; await hc.cfgSave({ id, host: host.value }); }
      } else toast(res.error || 'Rejected', 'er');
    }
    async function wifiDialog() {
      const ssid = el('input', { type: 'text', maxlength: '32' }), pass = el('input', { type: 'password', autocomplete: 'off' });
      const r = await modal({ title: 'Join a different Wi-Fi network', body: el('div', { class: 'stack' },
        el('p', { class: 'banner warn', text: 'The board restarts and leaves its current network. If these details are wrong it falls back to its own access point and this dashboard will lose it until you fix the connection (new IP address, or connect to its access point).' }),
        el('div', { class: 'field' }, el('label', { text: 'Network name (SSID)' }), ssid), el('div', { class: 'field' }, el('label', { text: 'Password' }), pass)),
        buttons: [{ label: 'Cancel', value: false }, { label: 'Forget Wi-Fi (use access point)', value: 'clear', cls: 'danger' }, { label: 'Join & restart', value: true, cls: 'primary' }] });
      if (!r) return;
      const body = r === 'clear' ? { clear: true } : { ssid: ssid.value, pass: pass.value };
      if (r !== 'clear' && !ssid.value) return toast('Enter a network name', 'er');
      const res = await hc.req(id, 'POST', '/wifi-data', body);
      toast(res.json && res.json.ok ? 'Saved - the board is restarting' : (res.error || 'Rejected'), res.json && res.json.ok ? 'ok' : 'er');
    }

    const node = el('section', { class: 'card stack' },
      el('header', null, el('h2', null, board), status),
      el('div', { class: 'fgrid' }, el('div', { class: 'field' }, el('label', { text: 'Address' }), host), el('div', { class: 'field' }, el('label', { text: 'Web password' }), pw)),
      el('div', { class: 'row' }, test, save, el('button', { class: 'btn', text: 'Reconnect', onclick: () => hc.reconnect(id) }), out),
      el('h3', { text: 'Device' }), info,
      el('h3', { text: 'Maintenance' }), actions, otaBar, otaMsg);
    cards[id] = { node, host, pw, status, loadInfo, otaBar, otaMsg, setHost(v) { lastHost = v; } };
    return node;
  };

  const lastState = { siren: null, ww: null };
  const lastOnline = (id) => lastState[id] && lastState[id].conn === 'online';
  const encNote = el('p', { class: 'hint' });
  const reqBox = el('input', { type: 'checkbox', id: 'reqAuth' });
  const secMsg = el('p', { class: 'hint' });
  const lockBtn = el('button', { class: 'btn', text: 'Sign in / lock now' });
  const methodSel = el('select', { id: 'authMethod' }, el('option', { value: 'auto', text: 'Windows Hello / PIN if set up, otherwise password (recommended)' }), el('option', { value: 'password', text: 'Windows password only' }));
  methodSel.addEventListener('change', () => hc.setAuthMethod(methodSel.value));
  reqBox.addEventListener('change', async () => {
    const want = reqBox.checked;
    const r = await hc.setAuthRequired(want);
    if (!r.ok) { reqBox.checked = !want; toast(r.error, 'er'); } else toast(want ? 'Windows sign-in is now required' : 'Windows sign-in turned off', 'ok');
  });
  lockBtn.addEventListener('click', () => { if (authLocked()) unlockInteractive(); else hc.lock(); });
  const secCard = el('section', { class: 'card stack' }, el('header', null, el('h2', { text: 'Security' })),
    el('div', { class: 'field check' }, reqBox, el('div', null, el('label', { for: 'reqAuth', text: 'Require Windows sign-in to start the siren or change settings' }),
      el('div', { class: 'sub', text: 'One sign-in unlocks the controls for 5 minutes. STOP and viewing status never need it.' }))),
    el('div', { class: 'field', style: 'max-width:520px' }, el('label', { for: 'authMethod', text: 'Sign-in method' }), methodSel),
    el('p', { class: 'hint', text: 'Requires sign-in: starting any run mode, TEST MODE, panel lock, settings, passwords, Wi-Fi, firmware, restart, and editing the saved connections above.' }),
    el('div', { class: 'row' }, lockBtn), secMsg);
  const node = el('div', { class: 'page' }, el('div', { class: 'grid g2' },
    mkCard('siren', 'Siren Controller', 'default Siren123! - change it'),
    mkCard('ww', 'Weather Watcher', 'default Weather123! - change it')), secCard, encNote);
  hc.onOta(({ id, sent, total }) => { const c = cards[id]; if (c) { c.otaBar.hidden = false; c.otaBar.value = Math.round(sent * 100 / total); } });
  let loaded = false;
  async function loadCfg() {
    const c = await hc.cfgGet();
    for (const id of ['siren', 'ww']) {
      if (document.activeElement !== cards[id].host) cards[id].host.value = c[id].host;
      cards[id].setHost(c[id].host);
      cards[id].pw.placeholder = c[id].hasPassword ? '(saved - leave blank to keep)' : cards[id].pw.placeholder;
    }
    encNote.textContent = c.encrypted ? 'Passwords are stored encrypted for your Windows account.' : 'Note: OS encryption is unavailable here, so saved passwords are stored obfuscated, not encrypted.';
    loaded = true;
  }
  return {
    node,
    render(st) {
      if (st.auth) {
        reqBox.checked = st.auth.enabled; reqBox.disabled = !st.auth.available;
        secMsg.textContent = st.auth.available ? (st.auth.required ? (authLocked() ? 'Currently locked.' : 'Currently unlocked.') : 'Protection is off.') : 'Windows sign-in is only available on Windows - protection is inactive here.';
        lockBtn.disabled = !st.auth.required;
        if (document.activeElement !== methodSel) methodSel.value = st.auth.method || 'auto';
        methodSel.disabled = !st.auth.available;
      }
      for (const id of ['siren', 'ww']) {
        const s = st[id], was = lastState[id] && lastState[id].conn;
        lastState[id] = s;
        cards[id].status.replaceChildren(el('i', { class: 'dot ' + connDot(s) }), s ? (s.error ? CONN_TEXT[s.conn] + ' - ' + s.error : CONN_TEXT[s.conn]) : '');
        if (s && s.conn === 'online' && was !== 'online') cards[id].loadInfo();
      }
    },
    onShow() { loadCfg(); cards.siren.loadInfo(); cards.ww.loadInfo(); },
  };
}

// ── tabs + export helpers ───────────────────────────────────────────────────
function tabBar(names) {
  const bar = el('div', { class: 'tabs', role: 'tablist' });
  const holder = el('div');
  const api = { node: el('div', null, bar, holder), current: 0, onSelect: null };
  const btns = names.map((n, i) => el('button', { role: 'tab', text: n, onclick: () => api.select(i) }));
  bar.append(...btns);
  let panes = [];
  api.panes = (list) => { panes = list.map((p) => el('div', { class: 'tab' }, p)); holder.replaceChildren(...panes); api.select(0, true); };
  api.select = (i, silent) => {
    api.current = i;
    btns.forEach((b, j) => { b.classList.toggle('on', j === i); b.setAttribute('aria-selected', j === i); });
    panes.forEach((p, j) => p.classList.toggle('on', j === i));
    if (!silent && api.onSelect) api.onSelect(i);
  };
  return api;
}

async function exportCsv(id) {
  const r = await hc.csv(id);
  if (r.canceled) return;
  toast(r.ok ? 'Saved ' + r.path : 'Export failed: ' + r.error, r.ok ? 'ok' : 'er');
}
