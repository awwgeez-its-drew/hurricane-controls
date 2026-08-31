# Hurricane Controls

ESP32 firmware + web interface for controlling a motor-driven mechanical siren
(chopper, blower, rotator) — WAIL, ATTACK, FAST WAIL, and MANUAL run modes, a
Component Test page for bench-testing relays independently, and a
mobile-friendly web UI served directly from the device.

## ⚠️ Safety & Disclaimer

This firmware was written exclusively to control a specific mechanical siren
system equipped with an independent, "old-school" mechanical/analog emergency
stop that physically disconnects mains power from all components. **This is a
hard requirement, not a suggestion.**

Several safety-relevant behaviors in this firmware — including "TEST MODE,"
which intentionally disables the software STOP button while active — assume
this hardware failsafe exists on your installation. If your build does not
include an independent, physically-wired emergency stop capable of cutting
all mains power to the system, **do not use this firmware**, or at minimum do
not use TEST MODE, and audit every code path making the same assumption
before relying on it.

This is a hobbyist project, shared as-is for others building similar
mechanical siren controllers. It is not a certified life-safety or industrial
control product, and it has not been evaluated by an electrician, safety
engineer, or regulatory body. Building and operating a device that switches
mains-voltage electromechanical equipment carries real risk of injury, death,
fire, or property damage if wired or used incorrectly.

By using this code, you accept full responsibility for:
- Correct, code-compliant electrical wiring, performed or reviewed by a
  qualified person where required by local law,
- Providing your own independent hardware emergency stop / mains disconnect,
- Verifying this firmware's behavior matches your specific hardware before
  relying on it in any safety-relevant context,
- Any consequences of using, modifying, or distributing this code.

This project is licensed under the MIT License (see [LICENSE](LICENSE)),
which includes a standard "AS IS, WITHOUT WARRANTY OF ANY KIND" disclaimer.
Nothing in this README limits or modifies that license.

## Hardware requirements

- ESP32 dev board
- An independent mechanical/analog E-Stop that cuts mains power to the
  entire system (see disclaimer above — required, not optional)
- Chopper relay, blower SSR, and rotator relay, wired to the GPIOs in
  `src/config.h`
- Optional physical WAIL/ATTACK/STOP buttons (also configurable in
  `src/config.h`)

### Reference hardware

This firmware was written for and tested on the [ESP-32S WiFi Development
Board (NodeMCU-32S)](https://a.co/d/0iIUNPpi) — an ESP32-WROOM-32-based
board with a 30-pin GPIO breakout and USB-C. Other ESP32 dev boards should
work too, but pin numbering and the USB-serial chip may differ.

This project was built specifically to control the [1/2 Scale ACA Hurricane
Mk II](https://www.printables.com/model/929876-12-scale-aca-hurricane-mkii/),
a 3D-printed scale replica of the ACA Hurricane Mk II mechanical siren.
It should be adaptable to other similar chopper/blower/rotator siren
builds, but GPIO wiring and timing defaults may need adjustment.

## GPIO pinout

All pin assignments live in `src/config.h` and can be changed there if your
wiring differs.

| GPIO | Function | Mode | Active level |
|------|----------|------|--------------|
| 25 | Chopper relay | Output | LOW (energizes on LOW) |
| 26 | Blower SSR | Output | HIGH (energizes on HIGH) |
| 27 | Rotator relay | Output | LOW (energizes on LOW) |
| 2 | Status LED | Output | HIGH = on; see behavior below |
| 32 | WAIL / MANUAL button | Input, pull-up | LOW (pressed) — short tap = Wail, hold = Manual |
| 33 | ATTACK / FAST WAIL button | Input, pull-up | LOW (pressed) — short tap = Attack, hold = Fast Wail |
| 4 | STOP button | Input, pull-up | LOW (pressed) |

Note the chopper and rotator relays are active-LOW while the blower SSR is
active-HIGH — this is a real hardware difference (relay vs. solid-state
switching), not a typo, so wire and test each independently before trusting
the polarity.

### Status LED behavior

The status LED (GPIO 2) gives an at-a-glance read of the state machine
without needing the web UI open:

| State | LED |
|-------|-----|
| Idle | Off |
| Starting up / running (any active mode) | Blinking (toggles every 250ms, ~2 Hz) |
| Stopping (shutdown sequence in progress) | Steady on |

## Network security note

This device serves plain HTTP (no TLS) and, out of the box, starts an open
WiFi access point with no WiFi password. This is a deliberate, accepted
tradeoff for a hobbyist IoT device on a local/trusted network — not a bug.
If you need transport encryption or a closed AP, you'll need to add it
yourself; this project doesn't include it.

The web UI login has its own protections: a password-complexity requirement
and a rate limit (5 failed attempts triggers a 30-second lockout) on the
login endpoint, independent of your WiFi security.

## Default credentials

| | Default |
|---|---|
| WiFi AP SSID | `HurricaneControls` (open network, no password) |
| Login password | `Siren123!` |

The device boots into AP mode with the SSID above — connect to it directly
to reach the web UI before it's joined any home network. **Change the
default login password immediately** from the Settings page; see Setup
below.

Once configured, the device can join your home WiFi network instead of
staying in AP mode (Settings → Wi-Fi). Note: the ESP32 only supports
**2.4 GHz** WiFi, not 5 GHz — if your router broadcasts separate 2.4 GHz and
5 GHz networks (or names, e.g. `MyNetwork` vs. `MyNetwork-5G`), make sure you
join the 2.4 GHz one.

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Open this repo and build/flash the `esp32dev` environment:
   ```
   pio run -t upload
   ```
3. On first boot, connect to the `HurricaneControls` WiFi access point (see
   Default credentials above) and browse to `http://hurricane.local` or the
   device's AP IP.
4. Log in with the default password.
5. **Change the default password immediately** from the Settings page.
   Passwords must be at least 8 characters and include an uppercase letter,
   a lowercase letter, a number, and a special character.
6. From Settings, join the device to your home 2.4 GHz WiFi network instead
   of staying in AP mode.

## Meshtastic remote control (optional)

The firmware can be wired to a [Meshtastic](https://meshtastic.org) node
(tested with a Heltec V3) to trigger and stop the siren over LoRa mesh, with
a confirmation sent back over the mesh so you can see it worked from
anywhere in range — no WiFi needed.

**How it works**: the Meshtastic node's Serial Module (set to its plain-text
passthrough mode) bridges UART traffic to mesh text messages in both
directions. Text you send over UART gets broadcast on the mesh; mesh text
messages arrive back over the same UART. This firmware listens on that UART
for commands and replies with plain-text confirmations, the same way it does.

**Wiring**: cross-connect UART TX/RX between the two boards, plus a shared
ground — both are 3.3V-logic ESP32-family chips, so no level shifting is
needed.

| Hurricane Controls (GPIO, `src/config.h`) | Meshtastic node |
|---|---|
| `MESH_RX_PIN` (16) | node's serial TX |
| `MESH_TX_PIN` (17) | node's serial RX |
| GND | GND |

On the Meshtastic side, enable the Serial Module on your channel of choice,
set its mode to the plain-text passthrough mode, and set its baud rate to
match `MESH_BAUD` (default 38400) in `src/config.h`. Pin/baud field names
can vary slightly by Meshtastic app version — check your installed version's
Serial Module settings.

**Commands** are plain text lines, prefixed with `SIREN` (case-insensitive,
configurable via `MESH_COMMAND_PREFIX` in `src/config.h`) so multiple sirens
can share one channel without responding to each other's traffic:

| Command | Effect | Reply |
|---|---|---|
| `SIREN WAIL` / `SIREN ATTACK` / `SIREN FASTWAIL` | Starts that run mode | `WAIL START received` (or `ERR: busy` if already running) |
| `SIREN STOP` | Stops the current run | `STOP received`, then `SIREN STOPPED` once shutdown completes |
| `SIREN LOCK` / `SIREN UNLOCK` | Locks/unlocks the physical buttons (same as the Main page's lock icon) | `OK: locked` / `OK: unlocked` |
| `SIREN REBOOT` | Restarts the device | `OK: rebooting` |
| `SIREN PING` | Connectivity check | `PONG` |

A `SIREN STOPPED` message is also sent whenever a run ends on its own (e.g.
an Attack/Fast Wail duration expiring), not just after a mesh-issued STOP.
Any line without the `SIREN` prefix — another siren's traffic, general mesh
chat — is silently ignored. Mesh commands are gated by the same rules as the
web UI: they're rejected while the siren isn't idle, and `WAIL`/`ATTACK`/
`FASTWAIL`/`STOP` are blocked while TEST MODE is active.

**Security note**: this feature has no authentication beyond your Meshtastic
channel's own encryption. Anyone able to transmit on that channel can
command the siren, the same tradeoff already accepted for this project's
open WiFi AP and plain-HTTP web UI (see Network security note above) — use a
dedicated private channel, not a public/default one. This is an additional
command source into the same state machine as the web UI and physical
buttons; it does not bypass the independent hardware E-Stop required in the
Safety & Disclaimer section above.

## Credits

Created by awwgeez.its.drew · Coded by Claude
