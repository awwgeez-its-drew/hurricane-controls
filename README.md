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

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Open this repo and build/flash the `esp32dev` environment:
   ```
   pio run -t upload
   ```
3. On first boot, the device starts a WiFi access point named
   `HurricaneControls` (open, no password). Connect to it and browse to
   `http://hurricane.local` or the device's AP IP.
4. Log in with the default password: **`Siren123!`**
5. **Change the default password immediately** from the Settings page.
   Passwords must be at least 8 characters and include an uppercase letter,
   a lowercase letter, a number, and a special character.
6. From Settings, you can also join the device to your home WiFi network
   instead of staying in AP mode.

## Credits

Created by awwgeez.its.drew · Coded by Claude
