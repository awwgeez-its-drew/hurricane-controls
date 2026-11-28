'use strict';
// Schema-driven settings forms. A form only ever POSTs the fields the user
// actually changed, so saving one value can never overwrite another with a
// stale copy (the boards' /settings-data accepts partial bodies).
//
// Field types: sec (stored as ms, shown as seconds), int, float, text, secret,
// bool, select, time (HH:MM or empty), checks (comma-separated code list).

function buildForm(schema, { onChange } = {}) {
  const root = el('div', { class: 'stack' });
  const fields = [];   // {f, input, wrap, sub, orig}

  const humanise = (f, ms) => (f.t === 'sec' && ms >= 60000) ? ' = ' + fmtDur(Math.round(ms / 1000)) : '';

  for (const sec of schema) {
    const box = el('section', { class: 'fsec' }, el('h3', { text: sec.title }), sec.hint ? el('p', { class: 'hint', text: sec.hint }) : null);
    const grid = el('div', { class: 'fgrid' });
    for (const f of sec.fields) {
      const wrap = el('div', { class: 'field' + (f.t === 'bool' ? ' check' : '') });
      const sub = el('div', { class: 'sub' });
      const id = 'f_' + f.k + '_' + Math.random().toString(36).slice(2, 6);
      let input;
      if (f.t === 'bool') {
        input = el('input', { type: 'checkbox', id });
        wrap.append(input, el('div', null, el('label', { for: id, text: f.label }), f.help ? el('div', { class: 'sub', text: f.help }) : null));
      } else if (f.t === 'select') {
        input = el('select', { id }, f.opts.map(([v, l]) => el('option', { value: String(v), text: l })));
        wrap.append(el('label', { for: id, text: f.label }), input, sub);
      } else if (f.t === 'checks') {
        input = el('div', { class: 'checks', id });
        for (const [code, label] of f.opts) input.append(el('label', null, el('input', { type: 'checkbox', value: code }), label));
        wrap.append(el('label', { text: f.label }), input, sub);
        wrap.style.gridColumn = '1 / -1';
      } else {
        const type = f.t === 'sec' || f.t === 'int' || f.t === 'float' ? 'number' : f.t === 'time' ? 'time' : f.t === 'secret' ? 'password' : 'text';
        input = el('input', { type, id, autocomplete: 'off', placeholder: f.ph || '' });
        if (type === 'number') input.step = f.step || (f.t === 'sec' ? '0.1' : f.t === 'float' ? '0.0001' : '1');
        if (f.maxlen) input.maxLength = f.maxlen;
        wrap.append(el('label', { for: id, text: f.label }), input, sub);
      }
      const rec = { f, input, wrap, sub, orig: undefined };
      fields.push(rec);
      input.addEventListener('input', () => { refresh(rec); if (onChange) onChange(); });
      input.addEventListener('change', () => { refresh(rec); if (onChange) onChange(); });
      grid.append(wrap);
    }
    box.append(grid);
    root.append(box);
  }

  function read(rec) {
    const { f, input } = rec;
    if (f.t === 'bool') return input.checked;
    if (f.t === 'select') return /^-?\d+$/.test(input.value) && f.num ? parseInt(input.value, 10) : input.value;
    if (f.t === 'checks') return [...input.querySelectorAll('input:checked')].map((i) => i.value).join(',');
    if (f.t === 'sec') return input.value === '' ? NaN : Math.round(parseFloat(input.value) * 1000);
    if (f.t === 'int') return input.value === '' ? NaN : parseInt(input.value, 10);
    if (f.t === 'float') return input.value === '' ? NaN : parseFloat(input.value);
    return input.value.trim();
  }
  function write(rec, v) {
    const { f, input } = rec;
    if (f.t === 'bool') input.checked = !!v;
    else if (f.t === 'select') input.value = String(v);
    else if (f.t === 'checks') { const set = new Set(String(v || '').split(',').map((s) => s.trim()).filter(Boolean)); input.querySelectorAll('input').forEach((i) => { i.checked = set.has(i.value); }); }
    else if (f.t === 'sec') input.value = v == null ? '' : String(v / 1000);
    else input.value = v == null ? '' : String(v);
  }
  function refresh(rec) {
    const { f, wrap, sub } = rec;
    const v = read(rec);
    const dirty = rec.orig !== undefined && v !== rec.orig && !(Number.isNaN(v) && Number.isNaN(rec.orig));
    wrap.classList.toggle('dirty', dirty);
    let err = '';
    if (f.t === 'sec' || f.t === 'int' || f.t === 'float') {
      if (Number.isNaN(v)) err = 'Required';
      else if ((f.min != null && v < f.min) || (f.max != null && v > f.max)) {
        const sc = f.t === 'sec' ? 1000 : 1;
        err = `Allowed range: ${f.min / sc} to ${f.max / sc}${f.t === 'sec' ? ' s' : ''}`;
      }
    } else if (f.t === 'time' && v !== '' && !/^([01]\d|2[0-3]):[0-5]\d$/.test(v)) err = 'Use HH:MM';
    wrap.classList.toggle('err', !!err);
    rec.err = err;
    if (f.t !== 'bool') sub.textContent = err || ((f.help || '') + (f.t === 'sec' ? humanise(f, v) : ''));
  }

  return {
    node: root,
    load(values) {
      for (const rec of fields) {
        if (!(rec.f.k in values) && !rec.f.virtual) continue;
        const v = values[rec.f.k];
        write(rec, v);
        rec.orig = read(rec);
        refresh(rec);
      }
    },
    /** @returns {{changed:Object, errors:string[]}} */
    collect() {
      const changed = {}, errors = [];
      for (const rec of fields) {
        if (rec.orig === undefined) continue;
        refresh(rec);
        if (rec.err) { errors.push(`${rec.f.label}: ${rec.err}`); continue; }
        const v = read(rec);
        if (v !== rec.orig) changed[rec.f.k] = v;
      }
      return { changed, errors };
    },
    dirty() { return fields.some((r) => r.orig !== undefined && read(r) !== r.orig && !(Number.isNaN(read(r)) && Number.isNaN(r.orig))); },
  };
}

// Settings editor: loads <path>, renders the form, saves only changed keys.
function settingsEditor(boardId, schema, { path = '/settings-data', onLoaded, after } = {}) {
  const msg = el('span', { class: 'msg' });
  const saveBtn = el('button', { class: 'btn primary', text: 'Save changes', disabled: true });
  const revert = el('button', { class: 'btn', text: 'Revert', disabled: true });
  const form = buildForm(schema, { onChange: () => { const d = form.dirty(); saveBtn.disabled = !d; revert.disabled = !d; msg.textContent = ''; msg.className = 'msg'; } });
  const node = el('div', null, form.node, el('div', { class: 'savebar' }, saveBtn, revert, msg));
  let last = {};

  async function load() {
    msg.textContent = 'Loading…'; msg.className = 'msg';
    const r = await hc.req(boardId, 'GET', path);
    if (r.status !== 200 || !r.json) {
      msg.className = 'msg er';
      msg.textContent = r.error || (r.status === 401 ? 'Not signed in - check the password in Connections' : 'Could not load settings (HTTP ' + r.status + ')');
      return false;
    }
    last = r.json; form.load(r.json); msg.textContent = ''; saveBtn.disabled = revert.disabled = true;
    if (onLoaded) onLoaded(r.json);
    return true;
  }
  saveBtn.addEventListener('click', async () => {
    const { changed, errors } = form.collect();
    if (errors.length) { msg.className = 'msg er'; msg.textContent = errors[0]; return; }
    if (!Object.keys(changed).length) return;
    saveBtn.disabled = true; msg.className = 'msg'; msg.textContent = 'Saving…';
    // The boards silently drop POST bodies over 1024 bytes while still
    // answering ok, so a big batch of changes is split into safe chunks.
    let r = { status: 200, json: { ok: true } };
    for (const part of chunkChanges(changed, 800)) {
      r = await hc.req(boardId, 'POST', path, part);
      if (!(r.status === 200 && r.json && r.json.ok)) break;
    }
    if (r.status === 200 && r.json && r.json.ok) {
      toast('Settings saved', 'ok');
      await load();
      if (after) after(changed);
      msg.className = 'msg ok'; msg.textContent = 'Saved';
    } else {
      msg.className = 'msg er'; msg.textContent = r.error || 'Save failed (HTTP ' + r.status + ')'; saveBtn.disabled = false;
    }
  });
  revert.addEventListener('click', () => { form.load(last); saveBtn.disabled = revert.disabled = true; msg.textContent = ''; });
  return { node, load, form };
}

function chunkChanges(obj, maxBytes) {
  const out = []; let cur = {};
  for (const [k, v] of Object.entries(obj)) {
    const next = { ...cur, [k]: v };
    if (Object.keys(cur).length && new TextEncoder().encode(JSON.stringify(next)).length > maxBytes) { out.push(cur); cur = { [k]: v }; }
    else cur = next;
  }
  if (Object.keys(cur).length) out.push(cur);
  return out;
}
