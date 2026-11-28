# Hurricane Controls Dashboard

A Windows desktop app that connects to **both** boards at once and gives you full
control of each from one window:

| Board | What you get |
|---|---|
| **Siren Controller** (`src/`) | Live state and timers over the board's WebSocket, hold-to-confirm WAIL / ATTACK / FAST WAIL / GROWL, press-and-hold MANUAL, STOP, panel-button lock, TEST MODE with per-component hold buttons, run log + CSV export, every setting |
| **Weather Watcher** (`weather-watcher/`) | Live NWS alert status, link/API health, alert-decision history, the siren response for every alert type, location & polling, ntfy push settings/token/test, alert-log CSV export |
| **Both** | Connection manager (address + password), device info, firmware (OTA) upload, restart, password changes, rename, Wi-Fi |

It talks to the boards' existing web API, so **no firmware changes are needed** and
the boards' own web pages keep working alongside it.

## Run it

Needs [Node.js 20+](https://nodejs.org/) (one time).

```bash
cd dashboard
npm install
npm start
```

On first launch it opens **Connections & System**: enter each board's address
(IP or `hostname.local`) and web password, then **Save & connect**.
Defaults are in the main README (`Siren123!` / `Weather123!`), change them.

### Try it without hardware

```bash
npm run mock      # fake siren on 127.0.0.1:18080 (pw Siren123!), fake Weather Watcher on :18081 (pw Weather123!)
npm start
```

### Build a Windows installer / portable .exe

On a Windows PC: `npm install` then `npm run dist`; the files land in `dashboard/dist/`.
Or let GitHub build it: the **Dashboard** workflow uploads both `.exe` files as an
artifact on every push, and attaches them to a release when you push a tag such as
`dashboard-v1.0.0`.

## Safety behaviour

The dashboard keeps the firmware's own safety model and adds a few of its own.
It is **not** a safety device: your independent hardware E-stop is still required
(see the main README).

* **Start modes need a 1 s hold; STOP needs 0.4 s.** A stray click can't start the
  siren. A global STOP is always visible in the top bar.
* **MANUAL and component test only run while the button is held.** The app sends a
  keep-alive every 500 ms; the board switches the output off itself ~2 s after they
  stop (app crash, network loss). The app also releases on window blur/minimise.
* **STOP is sent immediately** on its own connection, never queued behind other requests.
* TEST MODE shows the firmware's warning: while it is on, the siren disables run modes
  **and software STOP**. Turning it on asks for confirmation.
* Start buttons are disabled unless the siren is idle and out of TEST MODE.

## Windows sign-in

By default the dashboard asks for your **Windows credentials** (the standard Windows
Security dialog, limited to the signed-in user) before it will:

* start any run mode, enter TEST MODE, run a component, or change the panel-button lock
* change settings, passwords, Wi-Fi or hostname, restart a board, or upload firmware
* edit a saved board connection (first-time setup is exempt)

One sign-in unlocks everything for **5 minutes**; click the lock chip in the top bar to
lock sooner. **STOP, the keep-alives that keep MANUAL running, and all read-only views
never need it**, so a prompt can't get between you and stopping the siren. If the prompt
fails for any reason (cancelled, wrong password, PowerShell blocked) the action is
refused: it fails closed. Turn the protection off under **Connections & System > Security**
(that itself needs a sign-in). The rules live in the app's main process
(`lib/auth-gate.js`), not the UI, so the UI can't skip them.

Limits, honestly:

* **Password only.** The check verifies your Windows *password*. If you normally sign in
  with a PIN, fingerprint or face, choose **Sign-in options > Password** in the dialog.
  A PIN can't be verified by this method and is reported as such. (Windows Hello support
  could be added later.)
* **It is a guard against casual use, not a vault.** Anyone already signed in as you,
  with file access, could edit the app's config. It does not protect the boards
  themselves, which still have their own web passwords.
* **Windows only, and not yet run on real Windows.** The dialog code
  (`lib/winauth.js`) uses built-in PowerShell + Windows APIs, and CI on Linux cannot
  exercise it; the gating logic around it is tested. Bench check on your PC: start a mode
  (expect the dialog, wrong password refused, right password allowed), confirm STOP works
  while locked, and confirm cancelling leaves the siren untouched. On a locked-down PC
  (AppLocker / constrained PowerShell) the prompt reports "unavailable" and protected
  actions stay refused.

## Behaviour worth knowing

* **Login lockout.** The boards lock *everyone* out for 30 s after 5 wrong passwords.
  The dashboard therefore never retries a wrong password: it shows "wrong password"
  and waits for you to fix it. If someone else trips the lockout, it waits and
  reconnects by itself.
* **Sessions.** Each board keeps only 4 logins. The dashboard uses one, reuses it, and
  signs out on exit.
* **Settings saves send only what you changed**, with the firmware's own limits
  checked first. Large changes are split because the boards silently ignore POST
  bodies over 1 KB.
* Weather Watcher status is read from its public `/status-data`, so monitoring works
  even before a password is set.
* Passwords are stored with Windows DPAPI (via Electron `safeStorage`), per Windows user.
* Plain HTTP, same as the boards' own UIs. Use it on a trusted network.

## Tests

```bash
npm test          # client vs. behavioural mock of both boards (sessions, lockout, failsafes, OTA...)
npm run test:ui   # real Electron app + Playwright against the mocks (Linux: needs xvfb); includes the sign-in gate
```

The mocks follow `src/webserver.h` and `weather-watcher/src/webserver.h`; they are
not the firmware. **Not yet exercised against real hardware or on Windows**, so do a
bench check before relying on it: start/stop each mode with the siren safely
unpowered or disconnected, and confirm keep-alive release (pull the PC's network
cable while holding MANUAL).

## Layout

```
main.js          Electron main process: config, IPC, OTA/CSV dialogs
lib/board-client.js   HTTP/WebSocket client for one board (sessions, lockout, retry)
preload.js       Narrow, whitelisted bridge to the renderer
renderer/        UI (plain JS, no framework, strict CSP)
test/            mock boards + protocol tests + Electron UI test
```
