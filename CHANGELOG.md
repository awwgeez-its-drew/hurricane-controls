# Changelog

Both boards ship together — a version pair is always meant to be flashed
as a set. Main-board and Weather Watcher versions are independent
(different `FW_VERSION` in each board's `config.h`), listed together here
by the release they shipped in.

## 2.0.2 / Weather Watcher 2.0.2

Granular notification priorities, a configurable Status Broadcast, and
two new display-only alert categories.

- **Granular ntfy priorities** (Weather Watcher): the "Also notify for"
  list and every weather category in the Alert response table are now
  a priority select (Off / 1-5, ntfy's own scale) instead of a plain
  on/off toggle — Faults, Activations (the three old per-source toggles
  collapse into one, since the message itself names the source), Siren
  stopped/cycle completed, Locked button pressed, Boot announcement,
  Scheduled status updates, plus each of the 7 weather categories.
  Faults always sends a non-responding siren at the max priority
  regardless of the configured value. Lock/unlock and weather-trigger
  toggle notifications are dropped. Existing installs migrate their old
  on/off choices into a starting priority the first boot after
  updating — nothing changes silently.
- **Status Broadcast** (main board, new Settings card, just under
  Time): replaces the fixed 12-hour interval with up to 3 configurable
  daily local times, and makes every data point in the periodic status
  line individually toggleable — mode, lock state, uptime, CPU temp and
  Weather Watcher health (on by default, matching the old fixed
  message), plus new optional fields: Wi-Fi status, current time, last
  activation, and current alerts (off by default). `SIREN PING`'s reply
  now mirrors these same fields on demand, instead of its own
  hardcoded set. Both the Meshtastic line and the push to the Weather
  Watcher report on this one shared schedule.
- **Tornado Watch / Severe Thunderstorm Watch**: new display-only alert
  categories (never trigger the siren) on both boards' Current Alerts,
  sorted below real Warnings, red/yellow colored, with their own ntfy
  priority and a one-time Meshtastic "WATCH ISSUED" ping (deduped per
  alert, on by default). The main board's warning banner is renamed
  "Active alerts" now that it can show Watches too.
- **Alert expiry**: the Weather Watcher's Current Alerts cards show a
  live countdown next to each event name ("Tornado Watch (5h 13min
  remaining)"); the main board shows a static "Tornado Watch until
  11pm" in the viewer's own local time instead.
- **Fix**: SPC-issued Watch alerts' "View on NWS" link and polygon map
  both failed — their `properties.id` comes back from the NWS API as a
  bare `urn:oid:...` instead of the full URL the rest of the API uses.
  Normalized once where every alert is first read. Also fixes a related
  dedup-key collision risk for any VTEC-less alert (which watches
  always are).
- **Mesh whitelist fixes** (main board): sender matching was gated on
  every character before the `:` being hex, which only worked by
  coincidence for the one test node whose default short name happened
  to look hex — any node with a custom short name (letters, not just
  hex digits) was silently rejected. The whitelist now matches
  Meshtastic's actual short name, whatever it's set to. Also added a
  master "Enforce sender whitelist" toggle for sites that need to
  accept commands from any node on the channel, with a visible warning
  while it's off.

## 2.0.1 / Weather Watcher 2.0.1

Phone-layout follow-up to the 2.0.0 redesign; nothing changes on wide
screens.

- **System status** on both dashboards is now a collapsible card with a
  single status light in its title. It starts collapsed on screens
  narrower than the two-column layout (phones, small tablets) and
  expanded on wider ones, and follows if the screen crosses that width
  later (rotating a tablet, resizing a window).
- **Phone card order**: Weather Watcher shows Current alerts, System
  status, Time, Recent alerts; Hurricane Controls shows Siren status,
  System status, Run modes — so the mode buttons sit near the top of the
  screen.

## 2.0.0 / Weather Watcher 2.0.0

Web UI redesign for both boards. Both version numbers jump to 2.0.0 to
mark it; every endpoint, element ID and control behaves as before.

- **New look** on every page of both boards: a dark control-room theme
  with a wider two-column layout on desktop that stacks on phones, the
  boards' own siren/Weather Watcher artwork, and Meshtastic and ntfy
  logos on their settings cards.
- **Control panel**: time remaining is the main readout (elapsed for
  Manual), a strobing status light while running, relay lamps, health
  chips shown without tapping, and a push-button icon with a green open /
  red closed padlock for the physical-button lock. The hold-to-confirm
  fill now spans the whole button.
- **Warnings** on both dashboards show a damage-threat pill
  (CATASTROPHIC / CONFIRMED / DESTRUCTIVE / CONSIDERABLE) and, when an
  alert started the siren, "SIREN ACTIVATED — MODE" (plus the start time
  on the main board). `/alerts` gains `tag`, `actMode` and `actTime`;
  the Weather Watcher's `/status-data` alerts gain `tag` and `mode`.
- **Meshtastic link status** (main board): since Text Message mode can't
  query the node, the board records what it passively sees — the last
  line received from the node (with its sender id), the last
  `SIREN PING` answered, and the last line sent. Shown as a chip in the
  control panel's health row and a status block on the Settings
  Meshtastic card, and sent as `health.mesh` in `/status` and the
  WebSocket push (seconds ago, -1 = not since restart). A quiet channel
  stays grey rather than red and doesn't affect the overall health
  summary.
- **Component Test** shows a large auto-off countdown and draining bar
  while TEST MODE is on, and the Growl Test durations from Settings.
- **Settings** pages get a section menu; the main board's timing cards
  are merged into a start/stop sequence table and a mode timing table,
  and the Weather Watcher's trigger modes and ntfy categories are merged
  into one Alert response table with a siren-mode selector and push
  switch per warning type.

## 1.10.0 / Weather Watcher 1.6.0

Reliability- and visibility-focused release across both boards: a
versioned, acknowledged UART protocol, persistent exportable logs, push
notifications, and several dashboard additions.

- **UART protocol v2** (`docs/uart-protocol.md`, new): trigger commands
  now get an `ACK <vtecKey> <CODE>` reply instead of firing-and-forgetting;
  the Weather Watcher runs a serialized retry queue (wait for ACK, retry
  retryable blocks, give up after 30s/4 unanswered sends) instead of a
  single blind send. `WX PING` replies `OK: pong 2`, so a mismatched pair
  (old main-board firmware, new Weather Watcher, or vice versa) is visible
  on the Weather Watcher's dashboard instead of silently missing features.
  NWS poll interval now backs off on failure (15/15/30/60s, honoring
  `Retry-After` on 429/503) instead of a flat retry, and exposes a
  `stale` flag once data is meaningfully out of date.
- **Weather Watcher lost/recovered detection**: the main board tracks how
  long it's been since the dedicated link last said anything and
  broadcasts `WEATHER WATCHER LOST`/`RECOVERED` over Meshtastic, plus a
  10-minute re-announce while the Weather Watcher is reachable but
  reporting an error. Settings page shows "last heard Ns ago."
- **Persistent, exportable event logs** on both boards (LittleFS-backed,
  survive a restart/OTA update) — the main board's run log and a new
  Weather Watcher alert-disposition log, each with a login-gated CSV
  export. A run interrupted by power loss is now recorded as
  `INTERRUPTED` rather than silently vanishing.
- **ntfy push notifications** (Weather Watcher only, off by default):
  per-category toggles, fault alerts, and non-NWS activation-source
  alerts, with certificate-verified HTTPS by default.
- **System health strip** on both dashboards — a quick-glance
  Wi-Fi/link/memory summary, tap to expand.
- **Hold-to-confirm** on the main dashboard's WAIL/ATTACK/FAST WAIL/STOP
  — a sustained hold (1s to start, 0.4s to stop) instead of a single tap,
  to guard against an accidental activation.
- **Active-warning banner** on the main dashboard, fed by the Weather
  Watcher's alert list over the UART link.
- **Polygon mini-map** on the Weather Watcher's Current Alerts card — an
  NWS alert's warning polygon, fetched client-side and rendered inline;
  the device's own location dot is login-gated.

See `docs/bench-test.md` for what to actually check on real hardware
before/after flashing this release — there's no compile-gated debug
injector, so several code paths here are proven by native-logic review
and real NWS alerts/manual serial testing rather than a simulated bench
test.

## 1.9.3 / Weather Watcher 1.5.2 and earlier

Not tracked in this file — see `git log` for the full history up to this
point (run-log alert type/ID, the WAIL+ATTACK rotator-jog gesture, the
OTA stall-race fix, Meshtastic remote-restart, and the releases before
that).
