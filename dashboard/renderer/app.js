'use strict';
(async function main() {
  const pagesRoot = $('#pages');
  const pages = {};
  let current = 'overview';
  let state = { siren: null, ww: null };

  function go(name) {
    if (current && pages[current] && pages[current].onHide) pages[current].onHide();
    current = name;
    $$('#nav button').forEach((b) => b.classList.toggle('on', b.dataset.page === name));
    for (const [k, p] of Object.entries(pages)) p.node.classList.toggle('on', k === name);
    if (pages[name].onShow) pages[name].onShow();
    renderTop();
    pages[name].render(state);
  }

  pages.overview = pageOverview(go);
  pages.siren = pageSiren();
  pages.ww = pageWW();
  pages.system = pageSystem();
  for (const p of Object.values(pages)) pagesRoot.append(p.node);
  $$('#nav button').forEach((b) => b.addEventListener('click', () => go(b.dataset.page)));

  // Global STOP (always visible).
  armHold($('#gStop'), 400, doStop);

  function renderTop() {
    const s = state.siren, w = state.ww;
    $('#topSiren').replaceChildren(el('i', { class: 'dot ' + connDot(s) }), 'Siren: ' + (s ? CONN_TEXT[s.conn] : '…'));
    $('#topWw').replaceChildren(el('i', { class: 'dot ' + connDot(w) }), 'Weather Watcher: ' + (w ? CONN_TEXT[w.conn] : '…'));
    $('#navDotSiren').className = 'dot ' + connDot(s);
    $('#navDotWw').className = 'dot ' + connDot(w);
    const d = sirenOnline(s) ? s.data.status : null;
    $('#gStop').disabled = !d || (d.mode === 'idle' && !d.testMode);
    // Alerts live in the Overview banner; on other pages show a compact, clickable count.
    const alerts = (w && w.conn === 'online' && w.data && w.data.status && w.data.status.currentAlerts) || [];
    const box = $('#topAlerts');
    if (!alerts.length || current === 'overview') { box.replaceChildren(); return; }
    const sev = topSeverity(alerts);
    box.replaceChildren(el('button', { class: 'chip alertchip ' + sev, title: 'Open the Overview', onclick: () => go('overview'), text: '\u26A0 ' + alerts.length + ' active alert' + (alerts.length === 1 ? '' : 's') + ' - ' + alerts.map((a) => a.event).join(', ') }));
  }

  function render() {
    renderTop();
    pages[current].render(state);
  }

  hc.onState((st) => { state = st; render(); });
  state = await hc.state();
  const cfg = await hc.cfgGet();
  $('#appVer').textContent = 'Dashboard v' + (await hc.info()).version;
  go(cfg.siren.host || cfg.ww.host ? 'overview' : 'system');
  render();
})();
