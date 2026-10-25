# Bench test checklist — firmware 1.10.0 / Weather Watcher 1.6.0

This release was built and reviewed without the ability to run a real
`pio run`/`pio test` build in the development sandbox (its network policy
blocks the PlatformIO package registry), and without a compile-gated
debug injector to simulate NWS alerts on the bench — both were flagged
up front and accepted as the tradeoff for implementing all nine work
packages in one pass. Everything here was verified by careful code
review, plus headless-browser checks of the extracted dashboard HTML/JS
(screenshots taken during development) for the UI-facing pieces (hold-to-
confirm, the health strip, the warning banner, the polygon map). **A real
`pio run` for both boards, and the checklist below, still need to happen
on real hardware before this is trusted as the new baseline.**

## Before flashing

- [ ] `pio run -e esp32dev` succeeds for both `platformio.ini` and
      `weather-watcher/platformio.ini`. Watch for anything the sandbox
      couldn't check: unused-variable/implicit-conversion warnings, and
      actual flash/RAM usage against the `min_spiffs.csv` partition
      budget (ground rule 7: stop and report back if either board is
      over ~90%).
- [ ] `pio test -e native` if a native test environment gets added later
      (none exists yet — see "What's not tested" below).

## After flashing — both boards, basic sanity

- [ ] Main board boots, every relay reads OFF, Settings page loads,
      `/status` WebSocket connects (badge updates live).
- [ ] Weather Watcher boots, dashboard loads, `WX PING` link test from
      its dashboard succeeds (Controller Link row goes green) — confirms
      the physical UART1 wiring and baud rate before testing anything
      protocol-related.
- [ ] Main board's Settings → Weather Watcher card shows "last heard Ns
      ago" updating, not stuck or LOST, once both boards are up.

## UART protocol v2 (WP1/WP2)

- [ ] With `weatherAutoTriggerEnabled` on and the location set to
      somewhere with no active alerts, confirm `WX ALERTS 0` / `ALERTS
      END` arrive every poll (watch `Serial` on the main board) and the
      dashboard's warning banner stays empty — not a false "all clear"
      box, nothing at all.
- [ ] Manually send a `WX WAIL TEST-001 Bench test` line from the Weather
      Watcher's serial monitor (bypassing a real NWS alert) and confirm:
      the siren starts, the main board replies `ACK TEST-001 TRIGGERED`,
      and the Weather Watcher's Recent Alerts card logs a `TRIGGERED`
      disposition.
- [ ] Repeat while the siren is already active from a button press —
      expect `ACK ... ACTIVE <mode>`, logged as `ALREADY_ACTIVE`, no
      second activation.
- [ ] Repeat with `weatherAutoTriggerEnabled` off — expect `ACK ...
      DISABLED`, logged as `AUTO_OFF`.
- [ ] Repeat while in TEST MODE — expect `ACK ... TESTMODE`, retried
      every 3s, and confirm it eventually gives up (`GAVE_UP_TESTMODE`)
      if TEST MODE is never exited, or succeeds once it is.
- [ ] Power-cycle the main board mid-run (pull power during an active
      WAIL/ATTACK) and confirm the run log shows an `INTERRUPTED` entry
      on reboot, not a silently-dropped one.
- [ ] Unplug the UART wire between the boards and confirm the main
      board's Settings page and Meshtastic `STATUS` line both show
      `LOST` within 90 seconds, and `RECOVERED` within a poll or two of
      reconnecting it.

## Persistent logs (WP3)

- [ ] Trigger a few runs, restart the main board, confirm Settings →
      Usage still shows them (not just the lifetime totals).
- [ ] `GET /log.csv` (logged in) downloads a CSV with real UTC
      timestamps.
- [ ] Same for the Weather Watcher's `/alertlog.csv`.

## ntfy (WP4)

- [ ] Set up a real ntfy.sh topic (or a self-hosted instance), enter it
      in Settings, hit "Send Test Notification," confirm it actually
      arrives on a phone/desktop subscribed to that topic.
- [ ] Toggle "Verify server certificate" off against a self-signed local
      ntfy instance if you run one, confirm it still sends (the on-by-
      default path against ntfy.sh/a Let's-Encrypt-issued instance should
      already be covered by the test above).

## Mirrored mesh events on ntfy

All of this is code-reviewed and traced sender-to-receiver (not run on
real hardware from this sandbox) — see `docs/uart-protocol.md`'s `SX ...`
table. With `ntfyNotifySystemEvents`/`ntfyNotifyStop`/`ntfyNotifyStatus`
all on:

- [ ] Toggle the physical lockout icon on the main dashboard — one
      "Local button lockout" push arrives, priority 3, no special tag.
- [ ] Press a physical button while locked out — one "Locked button
      pressed" push arrives, priority 4, with the ⚠️ tag.
- [ ] Toggle the weather-trigger kill-switch (Settings checkbox or a
      mesh `WEATHERON`/`WEATHEROFF` command) — one "Weather auto-trigger"
      push arrives, priority 3.
- [ ] Power-cycle the main board — a "Main board started up" push
      arrives shortly after boot, priority 3; wait or adjust
      `SX_STATUS_INTERVAL_MS` down temporarily to also confirm the
      periodic "Main board status" push.
- [ ] Start a run from any source and let it finish — a "Siren stopped"
      push arrives once it's actually idle again (not immediately when
      stop is pressed), priority 3, with the ✅ tag.
- [ ] Confirm a real siren activation (any source) and a real NWS
      warning crossing threshold (`NOTIFY_ONLY` or `TRIGGERED`) both
      arrive at priority 5 with the 🚨 tag — visually distinct at a
      glance from the priority-3 pushes above.
- [ ] With `ntfyNotifySystemEvents` off, confirm none of the
      lockout/locked-press/weather-trigger pushes arrive, while the
      priority-5 activation/warning pushes still do (those aren't gated
      by that toggle).

## UI pieces (WP5–WP8)

These were checked via headless-browser screenshots against the
extracted page during development, not on the real device's screen —
worth a quick look on an actual phone:

- [ ] Health strip dot colors look right in a real OK/warn/error state
      (e.g. pull Wi-Fi to see it go red).
- [ ] Hold-to-confirm actually requires the full hold on a real touch
      screen, not just a mouse — check that a quick tap does nothing.
- [ ] Active-warning banner shows up during a real NWS alert for your
      point, with the right color and the "ACTIVATED THIS SIREN" tag
      when it actually triggered this siren.
- [ ] Polygon mini-map's "Show map" button actually loads a map — this
      is the one piece flagged as a real unknown: `api.weather.gov`'s
      CORS behavior couldn't be checked from the sandbox. If it fails
      with "Could not load the map (network or CORS error)," that's
      expected to investigate, not a sign everything else is broken.

## What's not tested

- No native (host-side) unit tests exist yet for the trigger-queue state
  machine, the poll-backoff schedule, or `parseIso8601()`/`parseVtecKey()`
  — they're pure C++ but the surrounding classes pull in Arduino/WiFi
  headers that would need a mock layer to compile natively. Reviewed by
  hand instead; a `test/` + `weather-watcher/test/` PlatformIO Unity
  setup would be a reasonable next addition.
- No simulated-alert debug injector, per Decision D — everything above
  that needs a "real" alert either uses a hand-typed serial command or
  waits for an actual NWS warning.
