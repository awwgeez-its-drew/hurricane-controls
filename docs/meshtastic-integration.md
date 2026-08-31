# Meshtastic Integration — Controller-Side Reference

This document describes how the Hurricane Controls firmware (this repo)
implements its half of the Meshtastic bridge, for anyone configuring the
Meshtastic node (e.g. a Heltec V3) that talks to it. The controller side is
implemented and code-reviewed but **not yet bench-tested against real
Meshtastic hardware** — treat exact behavior as "as designed," and verify
against the real Serial Module once both sides are wired up.

Source of truth: `src/mesh.h` (`MeshBridge` class) and the `MESH_*` constants
in `src/config.h`. If anything here and the code disagree, the code wins —
this file may drift as the firmware evolves.

## What the controller expects from the Meshtastic node

A **plain-text, line-based UART bridge**, not Meshtastic's protobuf/StreamAPI
protocol. Concretely: the Meshtastic node should be running its **Serial
Module in a plain-text passthrough mode** — i.e., whatever it receives on its
serial RX line, it broadcasts as a text message on the mesh; whatever text
message it receives from the mesh, it writes back out its serial TX line.
(Meshtastic calls this mode something like "Simple" in current docs, but
verify the exact mode name/label against the installed Meshtastic app/CLI
version — it wasn't possible to confirm live docs when this was built.)

The controller does **not** speak Meshtastic's binary API — no protobufs, no
framing beyond newlines. Just raw ASCII text, one command or reply per line.

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

The controller's Serial Module RX/TX GPIO choice on the Meshtastic node side
is not fixed by this firmware — pick whatever free pins the Meshtastic
board's Serial Module config allows, and wire accordingly.

## Command protocol

One command per line, terminated by `\n` or `\r` (either is accepted; both
CRLF and bare LF work). Max line length is 63 characters — longer lines will
be truncated at the buffer boundary (see `line_[64]` in `mesh.h`).

**Every command must start with a prefix**, `SIREN` by default
(case-insensitive), followed by whitespace and the command word:

```
SIREN WAIL
siren stop
Siren Ping
```

The prefix is a `constexpr char MESH_COMMAND_PREFIX[]` in `src/config.h` —
change it per physical unit if multiple sirens will share one Meshtastic
channel, so each only reacts to its own traffic.

**Any line that does not start with the prefix is silently ignored — no
reply is sent.** This is deliberate: on a shared mesh channel, other
devices' commands/replies and general chat will pass over this same serial
link (since the Meshtastic Serial Module echoes all mesh text traffic to
serial), and the controller must not spam back "unknown command" for
traffic that isn't addressed to it. A line that *does* carry the prefix but
has an unrecognized word after it is treated as addressed-but-malformed and
does get an error reply (see table below).

Matching, trimming, and case-folding are all handled by the controller —
whoever/whatever sends commands doesn't need to worry about exact casing or
trailing whitespace.

### Commands and replies

| Command | Preconditions | Effect | Reply |
|---|---|---|---|
| `SIREN WAIL` | Siren idle, TEST MODE off | Starts WAIL mode | `WAIL START received` |
| `SIREN ATTACK` | Siren idle, TEST MODE off | Starts ATTACK mode | `ATTACK START received` |
| `SIREN FASTWAIL` | Siren idle, TEST MODE off | Starts FAST WAIL mode | `FASTWAIL START received` |
| `SIREN WAIL`/`ATTACK`/`FASTWAIL` | Siren **not** idle | (no-op) | `ERR: busy` |
| `SIREN WAIL`/`ATTACK`/`FASTWAIL`/`STOP` | TEST MODE active | (no-op, blocked) | `ERR: test mode active` |
| `SIREN STOP` | TEST MODE off | Stops the current run | `STOP received` immediately, then `.SIREN STOPPED` once shutdown completes (see below) |
| `SIREN LOCK` | — | Locks physical buttons (same as the Main page's lock icon) | `OK: locked` |
| `SIREN UNLOCK` | — | Unlocks physical buttons (also clears TEST MODE if it was active) | `OK: unlocked` |
| `SIREN REBOOT` | — | Restarts the controller | `OK: rebooting` (sent before reset) |
| `SIREN PING` | — | Connectivity check | `PONG` |
| `SIREN <anything else>` | — | No effect | `ERR: unknown command` |
| *(no `SIREN` prefix)* | — | No effect | **no reply at all** |

### Asynchronous "run ended" notification

Independent of any command, the controller sends **`.SIREN STOPPED`**
exactly once, whenever the state machine transitions from an active run to
idle, **for any reason**: a mesh-issued `STOP`, a web-UI or physical-button
stop, or a timed mode (Attack/Fast Wail) simply running out its duration.
So a `SIREN WAIL` that isn't followed by a manual stop will still
eventually produce a `.SIREN STOPPED` line on its own once the run
completes.

Note the **leading period** — this is a report, not a command, but it still
happens to start with the word `SIREN`. Without the period it would
literally match `MESH_COMMAND_PREFIX` on every *other* siren sharing the
channel, each of which would then try to parse `STOPPED` as a command word,
fail, and reply `ERR: unknown command` — the exact flood the prefix scheme
exists to prevent, just self-inflicted by the controller's own status
message. The period breaks that match while keeping the line
human-readable.

There are no other unsolicited status messages — no periodic heartbeat, no
per-state-transition chatter. Just the immediate ack on a start/stop command
and the single end-of-run notification.

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
routing/noise-filtering on a shared channel, not security). Anyone who can
transmit on the Meshtastic channel this node is bridging can control the
siren. The only real access control is the Meshtastic channel's own
pre-shared key — **use a dedicated private channel**, not a public or
default one, when setting this up.

## Open item for the Meshtastic-side agent

Confirm against the actual installed Meshtastic firmware/app version:
1. The exact Serial Module mode to select for plain ASCII passthrough
   (referred to above as "Simple"/plain-text mode — naming may have
   changed).
2. Whether that mode's default line-batching/timeout behavior needs tuning
   so short single-line commands/replies aren't delayed or coalesced
   oddly before being sent to/from the mesh.
3. Which GPIOs are free for the Serial Module on the specific Meshtastic
   board in use (Heltec V3), since its OLED/LoRa radio already claim some
   pins.
