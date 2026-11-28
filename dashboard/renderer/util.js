'use strict';
// Small DOM helpers. Everything that comes from a board (alert text from NWS,
// hostnames, etc.) is inserted with textContent via el(), never innerHTML.

function el(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      if (v == null || v === false) continue;
      if (k === 'class') e.className = v;
      else if (k === 'text') e.textContent = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (k === 'dataset') Object.assign(e.dataset, v);
      else e.setAttribute(k, v === true ? '' : v);
    }
  }
  for (const k of kids.flat()) if (k != null && k !== false) e.append(k.nodeType ? k : document.createTextNode(String(k)));
  return e;
}
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function fmtClock(sec) {
  if (sec == null || sec < 0) return '--:--';
  const m = Math.floor(sec / 60), s = sec % 60;
  return String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
}
function fmtUptime(sec) {
  if (sec == null) return '-';
  const d = Math.floor(sec / 86400), h = Math.floor(sec % 86400 / 3600), m = Math.floor(sec % 3600 / 60);
  return (d ? d + 'd ' : '') + (h || d ? h + 'h ' : '') + m + 'm';
}
function fmtDur(s) { return s >= 60 ? Math.floor(s / 60) + 'm ' + (s % 60) + 's' : s + 's'; }
function fmtAgo(s) {
  if (s == null || s < 0) return 'never';
  if (s < 60) return s + 's ago';
  if (s < 3600) return Math.floor(s / 60) + 'm ago';
  if (s < 86400) return Math.floor(s / 3600) + 'h ago';
  return Math.floor(s / 86400) + 'd ago';
}
function fmtUntil(exp) {
  if (!exp) return '';
  const d = new Date(exp * 1000);
  let h = d.getHours(); const m = d.getMinutes();
  const ap = h >= 12 ? 'pm' : 'am';
  h = h % 12 || 12;
  return 'until ' + h + (m ? ':' + String(m).padStart(2, '0') : '') + ap;
}

// ── toast / modal ───────────────────────────────────────────────────────────
function toast(text, kind = '') {
  const t = el('div', { class: 'toast ' + kind, text });
  $('#toast').append(t);
  setTimeout(() => t.remove(), kind === 'er' ? 6000 : 3000);
}

function modal({ title, body, buttons }) {
  return new Promise((resolve) => {
    const root = $('#modal');
    const close = (v) => { root.hidden = true; root.replaceChildren(); document.removeEventListener('keydown', onKey); resolve(v); };
    const onKey = (e) => { if (e.key === 'Escape') close(null); };
    document.addEventListener('keydown', onKey);
    const row = el('div', { class: 'row' });
    for (const b of buttons) row.append(el('button', { class: 'btn ' + (b.cls || ''), text: b.label, onclick: () => close(b.value) }));
    root.replaceChildren(el('div', { class: 'dlg', role: 'dialog', 'aria-modal': 'true', 'aria-label': title }, el('h2', { text: title }), body, row));
    root.hidden = false;
    const first = root.querySelector('button.primary, button.danger') || root.querySelector('button');
    if (first) first.focus();
  });
}
const confirmBox = (title, text, okLabel = 'Confirm', danger = false) =>
  modal({
    title, body: el('p', { text }),
    buttons: [{ label: 'Cancel', value: false }, { label: okLabel, value: true, cls: danger ? 'danger' : 'primary' }],
  });

// Ask for a new password (twice) - resolves with the string or null.
function askPassword(title, hint) {
  const a = el('input', { type: 'password', autocomplete: 'new-password' });
  const b = el('input', { type: 'password', autocomplete: 'new-password' });
  const err = el('p', { class: 'msg er' });
  const body = el('div', { class: 'stack' },
    hint ? el('p', { class: 'hint', text: hint }) : null,
    el('div', { class: 'field' }, el('label', { text: 'New password' }), a),
    el('div', { class: 'field' }, el('label', { text: 'Repeat' }), b), err);
  return new Promise(async (resolve) => {
    for (;;) {
      const r = await modal({ title, body, buttons: [{ label: 'Cancel', value: false }, { label: 'Change password', value: true, cls: 'primary' }] });
      if (!r) return resolve(null);
      if (a.value !== b.value) { err.textContent = 'The two entries do not match.'; continue; }
      return resolve(a.value);
    }
  });
}

// ── hold-to-confirm ─────────────────────────────────────────────────────────
// Same idea as the board's own web UI: a start/stop only fires after the
// button has been held for the full duration.
const REDUCE_MOTION = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
function armHold(btn, ms, onDone) {
  const fill = btn.querySelector('.hfill');
  let raf = null, t0 = 0;
  const cancel = () => { if (raf) cancelAnimationFrame(raf); raf = null; if (fill) fill.style.height = '0%'; };
  const frame = (now) => {
    const p = Math.min(1, (now - t0) / ms);
    if (fill && !REDUCE_MOTION) fill.style.height = (p * 100) + '%';
    if (p >= 1) { cancel(); onDone(); return; }
    raf = requestAnimationFrame(frame);
  };
  btn.addEventListener('pointerdown', (e) => {
    if (btn.disabled || e.button !== 0) return;
    e.preventDefault();
    try { btn.setPointerCapture(e.pointerId); } catch (_) { /* ignore */ }
    t0 = performance.now(); raf = requestAnimationFrame(frame);
  });
  for (const ev of ['pointerup', 'pointercancel', 'lostpointercapture', 'pointerleave']) btn.addEventListener(ev, cancel);
  window.addEventListener('blur', cancel);
  // Keyboard: Space/Enter must be *held* as well - never a single press.
  btn.addEventListener('keydown', (e) => { if ((e.key === ' ' || e.key === 'Enter') && !e.repeat && !btn.disabled) { e.preventDefault(); t0 = performance.now(); raf = requestAnimationFrame(frame); } });
  btn.addEventListener('keyup', cancel);
  btn.addEventListener('blur', cancel);
}

// Momentary "press and hold" with keep-alives (Manual mode, component test).
// down() resolves true if the board accepted it; ka() runs every 500 ms;
// up() always runs on release/blur/hide so nothing is left running.
function armMomentary(btn, { down, ka, up }) {
  let held = false, accepted = false, timer = null;
  const release = async () => {
    if (!held) return;
    held = false; clearInterval(timer); timer = null;
    btn.classList.remove('on');
    // If the board hasn't answered yet, the pointerdown handler below sees
    // held=false when it does, and sends the "off" itself.
    if (accepted) { accepted = false; await up(); }
  };
  btn.addEventListener('pointerdown', async (e) => {
    if (btn.disabled || held || e.button !== 0) return;
    e.preventDefault();
    try { btn.setPointerCapture(e.pointerId); } catch (_) { /* ignore */ }
    held = true;
    const ok = await down();
    if (!ok) { held = false; return; }
    if (!held) { await up(); return; }          // released before the reply arrived
    accepted = true;
    btn.classList.add('on');
    timer = setInterval(ka, 500);
  });
  for (const ev of ['pointerup', 'pointercancel', 'lostpointercapture']) btn.addEventListener(ev, release);
  window.addEventListener('blur', release);
  document.addEventListener('visibilitychange', () => { if (document.hidden) release(); });
  window.addEventListener('beforeunload', release);
}

// ── alert sound (matches the boards' own UI) ────────────────────────────────
function claxon() {
  try {
    const ctx = new (window.AudioContext || window.webkitAudioContext)();
    const beep = (f, t, d) => {
      const o = ctx.createOscillator(), g = ctx.createGain();
      o.connect(g); g.connect(ctx.destination); o.type = 'sawtooth'; o.frequency.value = f;
      g.gain.setValueAtTime(0.18, t); g.gain.exponentialRampToValueAtTime(0.001, t + d);
      o.start(t); o.stop(t + d);
    };
    const t = ctx.currentTime;
    beep(880, t, .13); beep(660, t + .13, .13); beep(880, t + .26, .13);
    setTimeout(() => ctx.close(), 800);
  } catch (_) { /* no audio */ }
}

// Strong-password rule, mirrors isStrongPassword() in the firmware.
function strongPw(pw) {
  return pw.length >= 8 && /[A-Z]/.test(pw) && /[a-z]/.test(pw) && /[0-9]/.test(pw) && /[^A-Za-z0-9]/.test(pw);
}

// ── Windows sign-in state (pushed from the main process, which enforces it) ──
const AUTH = { required: false, until: 0, available: false, enabled: true };
const authLocked = () => AUTH.required && Date.now() >= AUTH.until;
async function unlockInteractive() {
  const r = await hc.unlock();
  toast(r.ok ? 'Unlocked for 5 minutes' : r.error, r.ok ? 'ok' : 'er');
  return !!r.ok;
}
