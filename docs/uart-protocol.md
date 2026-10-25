# UART protocol v2 (Hurricane Controls ↔ Weather Watcher)

Authoritative reference for the dedicated UART1 link between the main
Hurricane Controls board (`src/weather_link.h`) and the Weather Watcher
sub-board (`weather-watcher/src/nws_client.h`). Introduced in firmware
1.10.0 / Weather Watcher 1.6.0 (WP1). See also `docs/weather-watcher.md`
(the Weather Watcher's own docs) and `docs/meshtastic-integration.md`
(a separate, unrelated link to a Meshtastic node).

## Transport

Plain text, newline-terminated lines, `Serial1` on both boards (pins in
each board's `config.h`). There is no password/whitelist here — the
physical wire itself is the trust boundary, same model as the physical
buttons. The link is otherwise WW-initiated; the two exceptions are
`SX ...` (main → WW, unsolicited) and `REBOOT` (main → WW, unsolicited,
see "Mesh `WXREBOOT`" in `docs/meshtastic-integration.md`).

## Versioning

There's no explicit version number field — `WX PING` / `OK: pong 2`
doubles as one. A Weather Watcher running this protocol that gets back
the old, bare `OK: pong` knows it's paired with pre-1.10.0 main-board
firmware (no ACKs, no `WX ALERTS`, no `SX` events) and sets
`sirenLegacyProtocol()`, surfaced on its own dashboard's Controller Link
row so a mismatched pair is visible instead of silently missing features.

## Messages

### `WX PING` → `OK: pong 2`

Pure link-health check, no gating. Used at Weather Watcher startup and
on-demand from its dashboard. `OK: pong` (no `2`) is the legacy reply
from main-board firmware older than 1.10.0.

### `WX STATUS <OK|ERROR|PENDING> <detail>` (WW → main, fire-and-forget)

The Weather Watcher's own WiFi/NWS-API health, pushed unprompted
whenever it changes meaningfully. `PENDING` = alive but hasn't finished
its first NWS poll yet. Folded into the Settings page's Weather Watcher
card and the Meshtastic `STATUS`/`PING` replies' `WW:` field.

### `WX TIME <epoch>` (WW → main, fire-and-forget)

The Weather Watcher's NTP-synced clock — the main board's primary time
source for its run log; its own NTP is a fallback for when this link is
down (see `clock.h`'s `updateNtpFallback()`).

### `WX ALERTS <n>` / `WX ALERT <vtecKey> <cat> <expEpoch|0> <event...>` / `WX ALERTS END`

A batch snapshot of the Weather Watcher's current alert list, sent after
every *successful* poll (even an empty list — see below), feeding the
main dashboard's active-warning banner (WP7). `ALERTS <n>` starts a
staging batch (`n` is informational only), one `ALERT` line per alert,
`ALERTS END` atomically swaps the staged batch into the live list so a
reader never sees a partially-received one.

- `vtecKey` — `office.phen.sig.etn` (or the alert's raw id as a
  fallback), the same stable id used for dedupe/the trigger queue/the
  persisted log — `-` if genuinely unavailable.
- `cat` — the `AlertCategory` enum value (0–7, see
  `weather-watcher/src/nws_client.h`).
- `expEpoch` — UTC epoch seconds from NWS's own `expires` field, `0` if
  unparseable or the Weather Watcher's clock wasn't synced yet.
- `event...` — the rest of the line, NWS's `event` string (e.g.
  "Tornado Warning"), not further escaped — this is always the last
  token on the line.

Re-sent every successful poll, list unchanged or not: re-sending an
empty list clears a stale banner, and the main board's own received-at
timestamp (`weather_link.h`'s `alertsReceivedMs()`) only advances on an
actual send, which is what it uses to grey out the banner if this
specific exchange stalls while the rest of the link stays up.

### `WX <MODE> <vtecKey> <descriptor>` (WW → main, trigger attempt)

`MODE` ∈ `WAIL` / `ATTACK` / `FASTWAIL`. `MANUAL` is excluded (it needs
momentary-hold semantics that don't fit an autonomous trigger) and so is
`GROWL` (a diagnostic test mode, not a warning tone). `descriptor` is
free text, not further tokenized (its own spaces/hyphens pass through),
e.g. `TORNADO EMERGENCY - CATASTROPHIC` (see `nws_client.h`'s
`categoryDescriptor()`) — carried through to the mesh activation
broadcast and the main board's run log for troubleshooting/
cross-reference.

Replies `ACK <vtecKey> <CODE> [detail]` (`vtecKey` is `-` if the
request's own id was empty/malformed). Checked in this order:

| Code | Meaning | Retryable? |
|---|---|---|
| `BADCMD` | Unrecognized mode word | No |
| `DISABLED` | `weatherAutoTriggerEnabled` is off | No |
| `TESTMODE` | TEST MODE has exclusive hardware access | Yes |
| `OTA` | A firmware update is in progress | Yes |
| `JOG` | Rotator jog (WAIL+ATTACK held together) is active | Yes |
| `STOPPING` | A run is in its stop sequence | Yes |
| `ACTIVE <mode>` | A run is already active (includes the race case: passed every check above but `trigger()` still refused because something else claimed `IDLE` first) | No |
| `TRIGGERED` | Accepted | — |

### `ACK` handling — the Weather Watcher's trigger retry queue

A serialized queue (max 8 entries, one in flight at a time — see
`QueuedTrigger` in `nws_client.h`). A higher-category escalation for an
already-queued event replaces rather than duplicates (restarts its
attempt counter, since it's effectively a fresh attempt).

- `TRIGGERED` / `DISABLED` / `BADCMD` → final, logged as `TRIGGERED` /
  `AUTO_OFF` / `BADCMD` (or `RETRIED_TRIGGERED` if it took more than one
  send to land).
- `ACTIVE` → final, logged as `ALREADY_ACTIVE`.
- `STOPPING` / `JOG` / `OTA` / `TESTMODE` → retried every 3s for up to
  30s total (from the first send); past that, final as `GAVE_UP_<CODE>`.
- No `ACK` within 3s of a send → resend, up to 3 times (4 sends total);
  past that, final as `NO_RESPONSE`.

Every final disposition is persisted to `weather-watcher/src/alertlog.h`
(WP3) and, depending on settings, pushed via ntfy (WP4) — see
`docs/weather-watcher.md`.

### `SX ACTIVATED <MODE> <SOURCE>` / `SX STOPPED <MODE>` / `SX STOP <SOURCE>` (main → WW, unsolicited)

Mirrors the same edge-detection `src/mesh.h` uses for its own
activation/stop broadcasts (`sourceWord()`/`runModeWord()` duplicated in
`weather_link.h` rather than shared, to avoid a circular include), so the
Weather Watcher hears about siren activity it didn't itself cause — a
WEB/MESH/button-initiated run while an NWS warning is still active is
exactly the kind of thing worth logging/notifying on (WP3/WP4).
`SOURCE` ∈ `LOCAL` / `WEB` / `MESH` / `NWS_ALERT`.

### `REBOOT` (main → WW, unsolicited)

Requests the Weather Watcher restart — the one other main → WW
direction, sent for the Meshtastic `SIREN WXREBOOT` command (see
`docs/meshtastic-integration.md`). Despite the name, this is the one
line `weather_link.h`'s `handleReply()`-equivalent on the Weather
Watcher side (`nws_client.h`'s `handleReply()`) doesn't treat as a reply
to anything it sent.

## Buffer sizes

`weather_link.h`'s `line_` buffer is 160 bytes (grown from 96 in v1 to
fit an `ALERT` line's event text plus its other fields). Weather
Watcher's own `rxLine_` buffer is unchanged.

## What isn't covered here

Bench-testing this protocol against a real siren currently relies on
real NWS alerts and manual serial monitor testing — there is no
compile-gated debug injector (`WX INJECT ...` or similar) in this
firmware; see `docs/bench-test.md` for what to actually check before/
after a flash.
