# Changelog

Both boards ship together — a version pair is always meant to be flashed
as a set. Main-board and Weather Watcher versions are independent
(different `FW_VERSION` in each board's `config.h`), listed together here
by the release they shipped in.

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
