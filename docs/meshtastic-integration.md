# Meshtastic Integration — Controller-Side Reference

This document describes how the Hurricane Controls firmware (this repo)
implements its half of the Meshtastic bridge, for anyone configuring the
Meshtastic node (e.g. a Heltec V3) that talks to it. This has been bench-
tested end-to-end on real hardware (Heltec V3 + a NodeMCU-32S controller) —
see "Lessons from bring-up" below for the two real issues that turned up
and how they were fixed.

Source of truth: `src/mesh.h` (`MeshBridge` class) and the `MESH_*` constants
in `src/config.h`. If anything here and the code disagree, the code wins —
this file may drift as the firmware evolves.

## What the controller expects from the Meshtastic node

A **line-based UART bridge** using the Meshtastic Serial Module's **Text
Message mode** — not "Simple" mode (tried and confirmed non-functional on
real hardware, see below), and not Meshtastic's protobuf/StreamAPI protocol
("Default" mode, which is a different thing entirely and also confirmed
non-functional for this purpose).

Text Message mode has one behavior that matters a lot for parsing: **it
prefixes every incoming mesh message with the sender's short node ID**
before writing it to serial, e.g. a message typed as `SIREN PING` on another
node arrives at the controller as:

```
3a3c: SIREN PING
```

`mesh.h` strips this `<sender-id>: ` prefix before matching the command, and
also uses the extracted ID for the whitelist check (see below). The
controller does **not** speak Meshtastic's binary API — no protobufs, just
this one line-oriented text format.

## Physical link

UART2 on the ESP32, cross-connected to the Meshtastic node's serial pins,
plus a shared ground. Both sides are 3.3V logic (ESP32-WROOM-32 controller,
ESP32-S3-based Heltec V3) — no level shifting needed.

| Controller (`src/config.h`) | Connects to |
|---|---|
| `MESH_RX_PIN` = GPIO 16 | Meshtastic node's serial **TX** |
| `MESH_TX_PIN` = GPIO 17 | Meshtastic node's serial **RX** |
| GND | GND |

**Baud rate**: `MESH_BAUD` = 38400 (8N1). The Meshtastic node's Serial
Module baud setting must match this exactly, or match whatever you change
`MESH_BAUD` to — it's a `constexpr` in `src/config.h`, easy to change on
either the controller or Meshtastic config, just keep them in sync.

The Meshtastic node's own Serial Module RX/TX GPIO choice is not fixed by
this firmware — pick free pins on that board's own pinout. **On a Heltec
V3 specifically, GPIO2 and GPIO3 are confirmed free/working; GPIO1 is
confirmed NOT free** — see "Lessons from bring-up" below.

## Command protocol

One command per line, terminated by `\n` or `\r` (either is accepted; both
CRLF and bare LF work). Max line length is 63 characters — longer lines will
be truncated at the buffer boundary (see `line_[64]` in `mesh.h`).

**Every command must start with a prefix**, `SIREN` by default
(case-insensitive), followed by whitespace and the command word, optionally
followed by a password (see "Command password" below). On the wire (after
Text Message mode's sender-ID prefix), a real command looks like
`3a3c: SIREN WAIL PASS123`; what you actually type/send is just:

```
SIREN WAIL
SIREN WAIL PASS123
siren stop
Siren Ping
```

The prefix is a `constexpr char MESH_COMMAND_PREFIX[]` in `src/config.h` —
change it per physical unit if multiple sirens will share one Meshtastic
channel, so each only reacts to its own traffic.

**Any line that does not start with the prefix (after the sender-ID is
stripped) is silently ignored — no reply is sent.** This is deliberate: on
a shared mesh channel, other devices' commands/replies and general chat
will pass over this same serial link (since the Meshtastic Serial Module
echoes all mesh text traffic to serial), and the controller must not spam
back "unknown command" for traffic that isn't addressed to it. A line that
*does* carry the prefix but has an unrecognized word after it is treated as
addressed-but-malformed and does get an error reply (see table below).

Matching, trimming, and case-folding are all handled by the controller —
whoever/whatever sends commands doesn't need to worry about exact casing or
trailing whitespace.

### Sender whitelist

Beyond the `SIREN` addressing prefix, the controller also checks the
sender's node ID (the part Text Message mode prepends, e.g. `3a3c`) against
an allow-list before doing anything — even for a correctly-prefixed,
recognized command.

- Configured via the **Settings page → Mesh Settings** card, or the
  `meshWhitelist` field in `src/settings.h` / the `/settings-data` API.
- Comma-separated list of short hex node IDs, case-insensitive, whitespace
  around entries is trimmed (e.g. `3A3C, 1B93`).
- **An empty whitelist blocks every command** — this is a fail-safe
  default, not "allow everyone."
- A sender not on the list is treated exactly like a non-addressed line:
  **silently ignored, no reply.** No `ERR: unauthorized` or similar, both to
  avoid leaking that the whitelist exists/behaves a certain way, and to
  avoid giving an unwanted sender a reason to keep retrying.
- Default whitelist ships with one confirmed-good test node: `3A3C`.
- **This only works under Text Message mode.** The whitelist check is keyed
  off the sender-ID prefix that mode adds; under Simple mode (no ID prefix)
  every sender ID would be empty and every command would be rejected. Since
  Text Message mode is the one confirmed to actually work (see below), this
  isn't expected to matter in practice — but it's why the whitelist and the
  mode choice are coupled, not independent settings.

### Command password

Beyond the whitelist, a command's *last word* can be checked against an
optional plaintext password before anything happens — a lightweight second
factor on top of sender-ID filtering.

- Configured via the same **Settings page → Mesh Settings** card, or the
  `meshPassword` field in `src/settings.h` / the `/settings-data` API.
- Plaintext, no hashing — this is a convenience layer, not the primary
  defense (the Meshtastic channel's own encryption and the sender whitelist
  are). Default is empty (no password required).
- Format: `SIREN <command> <password>`, e.g. `SIREN WAIL PASS123`. The
  password is whatever token immediately follows the command word.
- Comparison is case-insensitive (mirroring the whitelist's case handling).
- **An empty configured password means none is required at all** — a
  command with or without a trailing token both work.
- **`SIREN PING` is exempt** — it never requires a password, so connectivity
  can always be checked regardless of what's configured.
- A missing or wrong password is treated exactly like a non-whitelisted
  sender: **silently ignored, no reply.** Same rationale as the whitelist —
  don't confirm to an unauthorized sender that they were close.
- Checked *after* the sender whitelist — both gates must pass (whitelist,
  then password) for a non-PING command to do anything.

### Commands and replies

| Command | Preconditions | Effect | Reply |
|---|---|---|---|
| `SIREN WAIL [pw]` | Siren idle, TEST MODE off, sender whitelisted, password correct (if set) | Starts WAIL mode | `WAIL ACTIVATED (activation point: MESH)` (see "Outgoing broadcasts" below) |
| `SIREN ATTACK [pw]` | Siren idle, TEST MODE off, sender whitelisted, password correct (if set) | Starts ATTACK mode | `ATTACK ACTIVATED (activation point: MESH)` |
| `SIREN FASTWAIL [pw]` | Siren idle, TEST MODE off, sender whitelisted, password correct (if set) | Starts FAST WAIL mode | `FASTWAIL ACTIVATED (activation point: MESH)` |
| `SIREN WAIL`/`ATTACK`/`FASTWAIL [pw]` | Siren **not** idle | (no-op) | `ERR: busy` |
| `SIREN WAIL`/`ATTACK`/`FASTWAIL`/`STOP [pw]` | TEST MODE active | (no-op, blocked) | `ERR: test mode active` |
| `SIREN STOP [pw]` | TEST MODE off, sender whitelisted, password correct (if set) | Stops the current run | `STOP ACTIVATED (activation point: MESH)`, then `<MODE> CYCLE COMPLETED - SIREN STOPPED` once shutdown completes |
| `SIREN LOCK [pw]` | Sender whitelisted, password correct (if set) | Locks physical buttons (same as the Main page's lock icon) | `LOCAL BUTTON LOCKOUT ACTIVE` |
| `SIREN UNLOCK [pw]` | Sender whitelisted, password correct (if set) | Unlocks physical buttons (also clears TEST MODE if it was active) | `LOCAL BUTTON LOCKOUT INACTIVE` |
| `SIREN REBOOT [pw]` | Sender whitelisted, password correct (if set) | Restarts the controller | `OK: rebooting` (sent before reset) |
| `SIREN PING` | Sender whitelisted (no password ever required) | Connectivity/status check | `MODE: <STANDBY\|mode> // LOCAL CONTROLS <LOCKED\|UNLOCKED> // UPTIME: ... // CPU TEMP: ...` |
| `SIREN <anything else> [pw]` | Sender whitelisted, password correct (if set) | No effect | `ERR: unknown command` |
| *(no `SIREN` prefix, sender not whitelisted, or wrong/missing password)* | — | No effect | **no reply at all** |

Note that `WAIL`/`ATTACK`/`FASTWAIL`/`STOP`/`LOCK`/`UNLOCK` no longer get a
command-specific "received" ack — the source-agnostic broadcasts described
next now cover those cases too (a mesh-issued command produces its
broadcast with no perceptible delay, since it fires later in the same
`update()` tick). `ERR: busy`/`ERR: test mode active`/`ERR: unknown
command`/`OK: rebooting` remain direct, immediate replies since they're
specific to a *rejected or reboot* mesh command, not a state change any
other source could also produce.

### Outgoing broadcasts (not replies — proactive)

Beyond responding to commands, the controller proactively broadcasts
whenever something happens, **regardless of source** (mesh, web UI, or
physical button) — this is how a mesh-only observer sees local/web
activity, not just its own commands. None of these start with the literal
word `SIREN`, so — unlike the old `.SIREN STOPPED` design — no leading-period
trick is needed to keep them from being misread as a command by another
unit; only *incoming* lines need the `SIREN` prefix to be recognized.

| Event | Message | Fires when |
|---|---|---|
| Run mode activated | `\x07<MODE> ACTIVATED (activation point: LOCAL\|WEB\|MESH)` | `sm.trigger()` succeeds, from any source. `MODE` ∈ WAIL/ATTACK/FASTWAIL/MANUAL. **Carries a leading BEL (`0x07`)** — Meshtastic treats this as an alert, notifying differently than a normal message on compatible apps (unverified against a live app from this sandbox). |
| Stop invoked | `\x07STOP ACTIVATED (activation point: LOCAL\|WEB\|MESH)` | `sm.stop()` is called by an external source — even a no-op call while already idle. Internal/automatic stops (duration expiry) do **not** trigger this. Also carries the alert-bell prefix, same rationale as activation. |
| Run cycle completed | `<MODE> CYCLE COMPLETED - SIREN STOPPED` | The state machine reaches `IDLE` from an active run, for any reason (explicit stop or a timed mode's duration expiring). No alert-bell prefix — informational, not urgent. |
| Lockout changed | `LOCAL BUTTON LOCKOUT ACTIVE` / `LOCAL BUTTON LOCKOUT INACTIVE` | `buttons.locked` changes state, from any cause (Main page icon, TEST MODE entry/exit, or a mesh `LOCK`/`UNLOCK`). |
| Startup | `STARTUP COMPLETE`, immediately followed by one STATUS line | Once, at the very end of `setup()` — after WiFi and the web UI are also up. |
| Periodic status | `STATUS: <STANDBY\|MODE> - LOCAL CONTROL <LOCKED\|UNLOCKED> // UPTIME: <Xd Xh Xm> // CPU TEMP: <NN>F` | Every 12 hours (`MeshBridge::STATUS_INTERVAL_MS`), and once at startup. |

Implementation notes (`src/mesh.h`, `src/statemachine.h`):
- **Source attribution** (`LOCAL`/`WEB`/`MESH`) is threaded through a new
  `TriggerSource` parameter on `StateMachine::trigger()`/`stop()`, set by
  whichever call site invokes it (`buttons.h` passes `LOCAL`, `webserver.h`'s
  `POST /cmd` passes `WEB`, `mesh.h` passes `MESH`). This avoids a circular
  include (`mesh.h` already includes `buttons.h`) by keeping the state
  machine — already the one shared choke point — as the source of truth,
  rather than having `mesh.h` call into `buttons.h`/`webserver.h` directly.
  Internal/automatic `stop()` calls (duration expiry, a defensive fallback)
  use the default `TriggerSource::AUTO` and are never reported as a
  user-invoked "STOP ACTIVATED".
- **"STOP ACTIVATED" needs a call counter, not just state-change
  edge-detection** — pressing STOP while already idle is a legitimate no-op
  that produces no observable state transition to detect. `stopCallSeq`
  increments on every external `stop()` call (not `AUTO` ones); `mesh.h`
  compares it each tick against the last value it saw.
- **"CYCLE COMPLETED" needs the mode cached before it goes stale** —
  `runMode` resets to `NONE` the instant `stop()` runs, well before `state`
  actually reaches `IDLE` (it sits in `STOPPING` for the configured shutdown
  delays first). `mesh.h` caches the last non-`NONE` `runMode` every tick so
  it still has the right value once `IDLE` is finally reached.
- **CPU temperature** comes from the ESP32's own internal `temperatureRead()`
  — the chip's die temperature, not ambient/room temperature. It will read
  noticeably warmer than the room; this is expected and needs no extra
  hardware or calibration.
- **Alert-bell prefix**: the activation and stop-invoked broadcasts are sent
  with a leading BEL character (`\x07`), which Meshtastic is understood to
  treat as an alert-style message rather than a normal silent one on
  compatible clients. This is a deliberate choice, limited to those two
  broadcasts (not `CYCLE COMPLETED`, lockout changes, or `STATUS`), since
  those two represent something happening right now rather than routine
  status. Not independently verified against a live Meshtastic app from
  this sandbox — confirm the actual notification behavior on real hardware.

## Safety semantics (why, not just what)

Every mesh command funnels through the *exact same* functions the web UI and
physical buttons use (`sm.trigger()`, `sm.stop()`, `buttons.setLocked()` —
see `src/statemachine.h` / `src/buttons.h`). There is no separate mesh-only
code path for actually driving relays. Practically, this means:

- Mesh `WAIL`/`ATTACK`/`FASTWAIL` will fail with `ERR: busy` under exactly
  the same idle-check every other trigger path already has.
- Mesh commands are blocked during TEST MODE exactly like web commands are
  (see the exclusivity logic in `src/webserver.h`'s `POST /cmd` handler,
  mirrored in `mesh.h`) — the Test page's own component buttons remain the
  only thing that can drive outputs while TEST MODE is active.
- This bridge is **not** a substitute for the project's required independent
  hardware E-Stop (see the main `README.md`'s Safety & Disclaimer section).
  It's just another software command source, at the same trust level as the
  web UI — it does not and cannot bypass hardware safety measures.

## Security model (relevant to Meshtastic-side config)

There is **no authentication in the command protocol itself** — no token, no
passphrase, nothing beyond the `SIREN` addressing prefix (which is for
routing/noise-filtering on a shared channel, not security) and the sender
whitelist above (which only recognizes senders you've explicitly added).
Anyone who can transmit on the Meshtastic channel this node is bridging, and
whose node ID you've whitelisted, can control the siren. The whitelist
narrows this from "anyone on the channel" to "anyone on the channel whose ID
you've added," but the channel's own pre-shared key is still the first line
of defense — **use a dedicated private channel**, not a public or default
one, when setting this up.

## Lessons from bring-up

Two real issues turned up getting this working end-to-end on a Heltec V3 +
NodeMCU-32S pair, both resolved and reflected in the design above:

1. **Heltec V3's GPIO1 is not a free pin — it's `VBAT_Read`, the board's
   battery-voltage ADC sense line.** The Serial Module was initially
   configured with `txd` on GPIO1, which meant the UART TX line was always
   fighting the onboard battery-sense circuitry — total silence in both
   directions, with the module otherwise correctly enabled/configured
   (confirmed via `meshtastic --info`: `enabled: true`, correct mode, correct
   baud — only the pin was wrong). Moving to GPIO2 (`rxd`) / GPIO3 (`txd`),
   both genuinely general-purpose on this board, fixed it immediately.
   **Takeaway**: check the target Meshtastic board's actual pinout diagram
   for pins already committed to onboard functions (battery sense, PMU,
   OLED, LoRa radio, etc.) before assigning Serial Module `rxd`/`txd` —
   don't assume a low GPIO number is generic-purpose just because it's
   broken out to a header.
2. **Only Text Message mode actually delivered messages.** Both "Simple"
   mode and "Default" mode were tried (after the pin fix, so pins/baud were
   already correct) and confirmed — via a direct USB-serial tap on the
   Serial Module's GPIO pins, bypassing Meshtastic's own debug console
   entirely — to produce no output at all. Text Message mode worked
   immediately once selected. This project's design and this doc originally
   assumed Simple mode would be the right choice (a closer match to a raw
   passthrough); that assumption was wrong for this firmware version, and
   Text Message mode's sender-ID-prefixed format is what `mesh.h` now
   parses for.

One diagnostic note worth preserving: **Meshtastic's own debug console
(over the node's native USB) is a different interface from the GPIO Serial
Module link** and does not reliably reflect what's happening on those GPIO
pins — don't trust its absence of `Module 'serial'`-style log lines as
proof the module isn't working. A direct serial tap on the actual configured
`rxd`/`txd` pins is the only fully conclusive test.

Also observed: the Serial Module has its own internal `timeout` setting
(seen as `2` seconds in a live config dump) that buffers and auto-sends
serial input after that delay, rather than requiring an explicit line
terminator from whatever's typing into it. This doesn't require any
firmware change here — the controller's replies are sent via
`Serial2.println()` regardless, and the module handles its own send timing
independently of how the controller terminates its lines.
