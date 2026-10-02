# Weather Watcher — NWS Alert Auto-Activation

This document describes the optional **Weather Watcher** add-on: a second,
separately-flashed ESP32 (firmware in `weather-watcher/` in this repo) that
polls National Weather Service alerts for a specific point and, on a
qualifying Tornado or Severe Thunderstorm Warning, automatically triggers
the siren on the main Hurricane Controls board over a dedicated wired link.

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

## What counts as a "qualifying" alert

Configured in `weather-watcher/src/nws_client.h`'s `qualifyingMode()` (not
currently exposed as a setting — see "Known limitations" below):

| Alert type | Qualifies when | NWS field(s) used |
|---|---|---|
| Tornado Warning | `tornadoDetection` is `OBSERVED` (radar/spotter-confirmed), OR a `tornadoDamageThreat` tag is present at all | `properties.parameters.tornadoDetection`, `properties.parameters.tornadoDamageThreat` |
| Severe Thunderstorm Warning | `thunderstormDamageThreat` is `CONSIDERABLE` or `DESTRUCTIVE` | `properties.parameters.thunderstormDamageThreat` |

**Important caveat on "PDS" and "Tornado Emergency"**: there is no dedicated,
structured field for either in the NWS API — both are free-text wording
embedded in the warning's body text, not a queryable tag. `tornadoDamageThreat
= CATASTROPHIC` is specifically reserved for Tornado Emergency–tier events
and is the closest reliable machine-readable proxy, which is why "any
damage-threat tag present" (not just `CATASTROPHIC`) is used as the
qualifying condition for Tornado Warnings — it reliably captures confirmed,
PDS-tier, and Tornado Emergency–tier warnings without needing to parse the
free-text product body.

A Watch (Tornado Watch, Severe Thunderstorm Watch) never qualifies — watches
are zone/county-based, not polygon-based, and don't carry these tags.

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

One command, sent by the Weather Watcher whenever a new qualifying alert is
found:

```
WX <MODE>
```

`MODE` is one of `WAIL`, `ATTACK`, or `FASTWAIL` — configured independently
for Tornado vs. Severe Thunderstorm Warnings in the Weather Watcher's own web
UI ("Trigger Modes" card). `MANUAL` and `GROWL` are deliberately not
reachable this way: `MANUAL` needs momentary-hold semantics that don't fit
an autonomous trigger, and `GROWL` is a diagnostic test mode, not a warning
tone.

The main board replies on the same link:

| Reply | Meaning |
|---|---|
| `OK: triggered` | The mode started successfully |
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
- A dedicated Settings page toggle, **"Automatic weather-triggered
  activation"** (on by default), is a separate kill-switch independent of
  both of the above — turn it off any time you want the wire connected but
  inert, e.g. during setup/testing of the Weather Watcher itself.

## Setting up the Weather Watcher board

1. Flash `weather-watcher/` (its own PlatformIO project — `pio run -t
   upload` from inside that directory) onto a second ESP32.
2. Wire it to the main board per the table above.
3. On first boot it starts its own open WiFi AP, `WeatherWatcher` — connect
   to it and browse to `http://weather-watcher.local` or its AP IP.
4. Join it to your home WiFi network (Wi-Fi card).
5. Set your **exact coordinates** (not your city/zip) in the Alert Location
   card. A latitude/longitude lookup from any map service works fine.
6. Set a **User-Agent contact** (your email, or a website) — required by the
   [NWS API's usage policy](https://www.weather.gov/documentation/services-web-api),
   which asks every client to identify itself.
7. Choose which run mode each alert category should trigger (Trigger Modes
   card).
8. On the main Hurricane Controls board's Settings page, confirm "Automatic
   weather-triggered activation" is on (it's on by default).

There is no login/password on this board's web UI — see the comment at the
top of `weather-watcher/src/webserver.h` for why (no relays, no safety-
critical function; it only ever sends a short text command over a dedicated
wire). Keep it on a trusted home network, same as the main board's own open
AP (see the main `README.md`'s "Network security note").

## Known limitations

- **Qualifying criteria are not yet exposed as a setting** — changing which
  tags/event types trigger a command currently requires editing
  `weather-watcher/src/nws_client.h`'s `qualifyingMode()` and reflashing.
- **TLS certificate verification is disabled** (`WiFiClientSecure::
  setInsecure()`) for the HTTPS request to `api.weather.gov` — pinning
  NOAA's certificate chain on an ESP32 is possible but adds real
  maintenance burden (certs rotate) for a non-sensitive, read-only public
  API call. Traffic is still encrypted in transit; this only means the
  watcher doesn't verify it's talking to the genuine `api.weather.gov` and
  not a man-in-the-middle on your own network.
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
  access to `api.weather.gov` available) — bench-test by temporarily
  pointing the watcher at a real warning in progress somewhere, or by
  relaxing `qualifyingMode()` to match any Tornado Watch for a dry run of
  the wiring/dispatch path.
