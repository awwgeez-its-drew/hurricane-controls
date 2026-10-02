# Weather Watcher — NWS Alert Auto-Activation

This document describes the optional **Weather Watcher** add-on: a second,
separately-flashed ESP32 (firmware in `weather-watcher/` in this repo) that
polls National Weather Service alerts for a specific point, shows a live
dashboard of what's currently in effect, and automatically triggers the
siren on the main Hurricane Controls board over a dedicated wired link when
a warning crosses a configured severity threshold.

This is **not required** for the main siren controller to function — it's an
entirely optional, independent piece of hardware you can add later.

## Why a second board, and why point-based alerts

[api.weather.gov](https://api.weather.gov) serves **storm-based warnings**
(Tornado Warning, Severe Thunderstorm Warning, and a handful of others) with
a real polygon geometry — the actual shape of the warned storm cell, not the
whole county. Querying `/alerts/active?point=<lat>,<lon>` matches against
that real polygon, so you only get triggered for alerts that genuinely cover
your exact coordinates, not every alert anywhere in your county.

Doing the WiFi/HTTPS/JSON polling directly on the main siren board was
considered and rejected: it would add real RAM/flash pressure to a board
that's otherwise fully self-contained, and would make the siren's own
reliability depend on internet/WiFi/the NWS API all being up. Instead, a
second ESP32 — cheap, and architecturally the same "optional UART-connected
helper board" pattern already used for the Meshtastic bridge — does all the
internet-facing work, and only ever sends a short, simple command over a
dedicated wire to the main board.

## Severity tiers

Every active alert covering your point is classified into one of four
tiers, used both for the dashboard's color-coding and for deciding whether
to trigger the siren. Any alert type other than Tornado Warning or Severe
Thunderstorm Warning is ignored entirely — not displayed, never triggers.

| Tier | Color | Condition | Triggers the siren? |
|---|---|---|---|
| Tornado Emergency | Purple | Tornado Warning with `tornadoDamageThreat` = `CATASTROPHIC` | Yes |
| Tornado Confirmed | Purple | Tornado Warning with `tornadoDetection` = `OBSERVED`, or any `tornadoDamageThreat` tag present | Yes |
| Warning (red) | Red | Tornado Warning not meeting the above (unconfirmed/radar-indicated), **or** Severe Thunderstorm Warning with a `thunderstormDamageThreat` tag (`CONSIDERABLE`/`DESTRUCTIVE`) | Only the Severe T-storm case |
| Warning (orange) | Orange | Severe Thunderstorm Warning with no damage-threat tag | No |

In other words: for Tornado Warnings, only the purple tier (confirmed or
higher) triggers — a plain, unconfirmed Tornado Warning shows up red on the
dashboard for awareness but never sounds the siren. For Severe Thunderstorm
Warnings, only a damage-tagged one (red) triggers — the base-level orange
tier is display-only. This matches the original design intent: only
automatically activate for the subset of warnings serious enough to
warrant it, while still giving full situational awareness of everything
active nearby.

**Important caveat on "PDS" and "Tornado Emergency"**: there is no dedicated,
structured field for "Particularly Dangerous Situation" wording in the NWS
API — it's free text embedded in the warning's body, not a queryable tag.
`tornadoDamageThreat = CATASTROPHIC` is specifically reserved for Tornado
Emergency–tier events and is the closest reliable machine-readable proxy;
"any damage-threat tag present" (not just `CATASTROPHIC`) is what's used for
the broader "Tornado Confirmed" tier, so it reliably captures confirmed,
PDS-tier, and Tornado Emergency–tier warnings without needing to parse the
free-text product body.

A Watch (Tornado Watch, Severe Thunderstorm Watch) never qualifies — watches
are zone/county-based, not polygon-based, and don't carry these tags.

## Re-triggering on escalation

Each tracked warning is keyed by its **VTEC event identifier** (issuing
office + phenomena + significance + event-tracking-number, parsed from
`properties.parameters.VTEC`) — not by `properties.id`. This matters because
**NWS issues a brand-new `id` for every single update to an ongoing
warning** (a continuation, an upgrade, a cancellation — anything), even
though it's fundamentally the same warning event throughout its life. The
VTEC identifier is what actually stays stable across that whole lifecycle.

By default, a given warning event triggers the siren **once** — the first
time it crosses its activation threshold. If the Settings page's **"Re-
trigger if an already-triggered alert escalates further"** toggle is on, a
warning that already triggered can trigger again later if it escalates to a
*higher* tier than it had already acted on — e.g. a Tornado Warning that
triggered once when it was confirmed, then later gets upgraded to a Tornado
Emergency. It will not re-trigger for a re-issuance that doesn't represent a
real escalation (e.g. a routine continuation with unchanged tags), since the
tier it's already acted on hasn't increased.

## Dashboard

The main page after logging in. Polls `/status-data` once per second:

- **Status lights** — green/red dots for **Wi-Fi**, **NWS API**, and
  **Controller Link**. Tap any of them to expand a plain-English detail
  line: "Status OK" when healthy, or the specific problem otherwise (e.g.
  "Location not configured", an HTTP error code, "Wi-Fi not connected", a
  wiring-check message). NWS API is only green once the location is
  configured *and* the most recent poll succeeded. Controller Link reflects
  the dedicated UART wire to the main board — it's tested automatically
  once at startup, and **tapping it fires a fresh on-demand test** (unlike
  the other two lights, which just show their already-known status). It
  stays at its neutral default color until the first test resolves (up to
  ~3 seconds).
- **Clock** — current time in 24-hour `HH:MM:SS`, from NTP (see below).
  Shows "Not synced" until the clock has successfully synced. Directly below
  it: the board's own CPU temperature (internal die sensor, same caveat as
  the main board's — reads warmer than ambient) and uptime.
- **Last polling attempt** — timestamp of the most recent poll, success or
  failure.
- **Restart icon** (top right, next to Settings) — same confirm-then-restart
  flow as the main board.
- **Current Alerts** — every alert in a visible tier (see table above),
  sorted most-severe-first, color-coded, each linking (opens in a new tab)
  to its raw `api.weather.gov` record — NWS retired their human-readable
  per-alert webpage, so this is the closest thing to an official source
  link that still reliably resolves. An alert that has triggered the siren
  carries a **"SIREN ACTIVATED"** tag. The list is rebuilt from scratch
  every poll, so an alert that's no longer active simply disappears on the
  next cycle.
- **Recent Alerts** — the last 5 alerts that triggered a siren activation,
  with a timestamp, most recent first. This is a running log, independent
  of what's currently active.

## Settings

All cards are collapsed by default (tap the title to expand) — same pattern
as the main Hurricane Controls board's Settings page.

- **Wi-Fi** — join a network or fall back to AP-only mode.
- **Security** — change the login password (same complexity policy as the
  main board: 8+ characters, upper/lower/digit/special).
- **Alert Location** — latitude/longitude (not a zip/city), poll interval,
  and the User-Agent contact string.
- **Trigger Modes** — which run mode (`WAIL`/`ATTACK`/`FASTWAIL`) to request
  for a qualifying Tornado Warning vs. a qualifying Severe Thunderstorm
  Warning, plus the re-trigger-on-escalation toggle described above.
- **Time (NTP)** — NTP server (default `pool.ntp.org`), UTC offset in hours,
  and a daylight-saving-time checkbox (+1h on top of the offset). Applied
  immediately on save, no reboot needed — though it only has anything to
  sync against while connected to WiFi with internet access.

## Login

Same mechanism as the main Hurricane Controls board: a single password,
session cookie (`HttpOnly`, `SameSite=Strict`), and a lockout after 5 failed
attempts (30 seconds). Default password is `Weather123!` — **change it** from
the Security card before relying on this device, same as you would on the
main board.

## Wiring

A dedicated, directly-wired UART link — not shared with the Meshtastic bridge
(which uses its own separate UART2 link; see `docs/meshtastic-integration.md`).
Both boards are 3.3V-logic ESP32s, so no level shifting is needed.

| Weather Watcher (`weather-watcher/src/config.h`) | Hurricane Controls (`src/config.h`) |
|---|---|
| `LINK_TX_PIN` (17) | `WEATHER_RX_PIN` (18) |
| `LINK_RX_PIN` (16) | `WEATHER_TX_PIN` (19) |
| GND | GND |

Baud rate must match on both sides: `LINK_BAUD` / `WEATHER_BAUD`, both
default to 38400.

## Wire protocol

One command, sent by the Weather Watcher whenever a tracked warning first
crosses its activation threshold (or re-crosses it on escalation, if that
setting is on):

```
WX <MODE>
```

`MODE` is one of `WAIL`, `ATTACK`, or `FASTWAIL` — configured independently
for Tornado vs. Severe Thunderstorm Warnings (Trigger Modes card). `MANUAL`
and `GROWL` are deliberately not reachable this way: `MANUAL` needs
momentary-hold semantics that don't fit an autonomous trigger, and `GROWL`
is a diagnostic test mode, not a warning tone.

A second command, `WX PING`, is a pure link-health check — it never touches
the state machine and isn't gated by anything (not the auto-trigger toggle,
not TEST MODE). It's what powers the dashboard's **Controller Link** status
light: sent once automatically at Weather Watcher startup, and again
whenever that light is tapped.

The main board replies on the same link:

| Reply | Meaning |
|---|---|
| `OK: triggered` | The mode started successfully |
| `OK: pong` | Reply to `WX PING` — the link is alive |
| `ERR: busy` | The siren was already running something |
| `ERR: disabled` | The Settings page "Automatic weather-triggered activation" toggle is off |
| `ERR: test mode active` | TEST MODE is active — see "Interaction with TEST MODE and lockout" below |
| `ERR: unknown command` | Malformed or unrecognized mode word |

There's no whitelist or password on this link, unlike the Meshtastic bridge
— the dedicated physical wire between the two boards is the trust boundary,
the same model used for the main board's own physical buttons.

## Interaction with TEST MODE and lockout

- **The plain lock icon (`buttons.locked`) does not block a weather
  trigger.** It never did for any remote source — it only ever restricts the
  physical button scan in `buttons.h`; the web UI and Meshtastic commands
  already bypass it too, and the weather link is consistent with that.
- **TEST MODE (`buttons.testModeActive`) does block it, hard.** TEST MODE's
  entire purpose is exclusive hardware access for the Test page's own
  component buttons during a bench test, and that must not be interrupted by
  an autonomous trigger — this is the one activation source every other path
  (web, mesh, weather) is equally blocked by.
- A dedicated Settings page toggle on the **main board**, **"Automatic
  weather-triggered activation"** (on by default), is a separate kill-switch
  independent of both of the above — turn it off any time you want the wire
  connected but inert.

## Setting up the Weather Watcher board

1. Flash `weather-watcher/` (its own PlatformIO project — `pio run -t
   upload` from inside that directory) onto a second ESP32.
2. Wire it to the main board per the table above.
3. On first boot it starts its own open WiFi AP, `WeatherWatcher` — connect
   to it and browse to `http://weather-watcher.local` or its AP IP.
4. Log in with the default password (`Weather123!`) and **change it
   immediately** from the Security card.
5. Join it to your home WiFi network (Wi-Fi card).
6. Set your **exact coordinates** (not your city/zip) in the Alert Location
   card. A latitude/longitude lookup from any map service works fine.
7. Set a **User-Agent contact** (your email, or a website) — required by the
   [NWS API's usage policy](https://www.weather.gov/documentation/services-web-api),
   which asks every client to identify itself.
8. Set your NTP server/UTC offset/DST in the Time card so the dashboard's
   clock and alert timestamps are correct.
9. Choose which run mode each alert category should trigger, and whether
   escalation should re-trigger (Trigger Modes card).
10. On the main Hurricane Controls board's Settings page, confirm "Automatic
    weather-triggered activation" is on (it's on by default).

## Known limitations

- **TLS certificate verification is disabled** (`WiFiClientSecure::
  setInsecure()`) for the HTTPS request to `api.weather.gov` — pinning
  NOAA's certificate chain on an ESP32 is possible but adds real
  maintenance burden (certs rotate) for a non-sensitive, read-only public
  API call. Traffic is still encrypted in transit; this only means the
  watcher doesn't verify it's talking to the genuine `api.weather.gov` and
  not a man-in-the-middle on your own network.
- **The clock depends on NTP/internet access.** In AP-only mode, or if your
  network has no internet route, the dashboard shows "Not synced" and alert
  timestamps fall back to "Unknown time"/an elapsed-seconds count instead of
  a wall-clock time.
- **This is a best-effort, supplementary automation, not a certified
  warning system.** It depends on your home internet, WiFi, and the NWS API
  all being available at the moment a warning is issued. If any of those is
  down, it fails silently (no activation) rather than falsely triggering —
  but that also means no activation at all during an outage. This does not
  replace other warning methods (Wireless Emergency Alerts, NOAA Weather
  Radio, outdoor warning sirens from your local emergency management
  agency, etc.) — treat it as an extra layer, not your only one.
- Not independently verified against a live, in-progress tornado/severe
  thunderstorm warning from this development environment (no internet
  access to `api.weather.gov` available while building this) — bench-test
  by temporarily pointing the watcher at coordinates with a real warning in
  progress somewhere, or by bench-testing the wiring/dispatch path with a
  deliberately mismatched location first to confirm nothing ever triggers
  when it shouldn't.
