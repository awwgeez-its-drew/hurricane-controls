'use strict';
// Field lists mirror the boards' /settings-data handlers (src/webserver.h and
// weather-watcher/src/webserver.h). min/max are the firmware's own clamps, in
// the stored unit (ms for "sec" fields).

const TZ_OPTS = [['EASTERN', 'Eastern'], ['CENTRAL', 'Central'], ['MOUNTAIN', 'Mountain'], ['ARIZONA', 'Arizona (no DST)'],
  ['PACIFIC', 'Pacific'], ['ALASKA', 'Alaska'], ['HAWAII', 'Hawaii (no DST)']];

const TIME_SCHEMA = (extra = []) => ({
  title: 'Date & time',
  hint: 'Used to timestamp the run log and alert log.',
  fields: [
    { k: 'ntpServer', label: 'NTP server', t: 'text', ph: 'pool.ntp.org', maxlen: 63 },
    { k: 'ntpUpdateHours', label: 'Sync every (hours)', t: 'int', min: 1, max: 168 },
    { k: 'timeZone', label: 'Time zone', t: 'select', opts: TZ_OPTS },
    { k: 'autoDst', label: 'Adjust for daylight saving time automatically', t: 'bool' },
    ...extra,
  ],
});

const SIREN_SCHEMA = [
  {
    title: 'Start-up sequence', hint: 'Delay between the siren starting and each component switching on.',
    fields: [
      { k: 'blowerDelay', label: 'Blower delay (s)', t: 'sec', min: 0, max: 60000 },
      { k: 'chopperDelay', label: 'Chopper delay (s)', t: 'sec', min: 0, max: 60000 },
      { k: 'rotatorDelay', label: 'Rotator delay (s)', t: 'sec', min: 0, max: 60000 },
    ],
  },
  {
    title: 'Wail',
    fields: [{ k: 'wailDuration', label: 'Duration (s)', t: 'sec', min: 1000, max: 3600000 }],
  },
  {
    title: 'Attack',
    fields: [
      { k: 'attackDuration', label: 'Duration (s)', t: 'sec', min: 1000, max: 3600000 },
      { k: 'attackOnTime', label: 'Sound on (s)', t: 'sec', min: 500, max: 600000 },
      { k: 'attackOffTime', label: 'Sound off (s)', t: 'sec', min: 500, max: 600000 },
    ],
  },
  {
    title: 'Fast wail',
    fields: [
      { k: 'fastWailDuration', label: 'Duration (s)', t: 'sec', min: 1000, max: 3600000 },
      { k: 'fastWailOnTime', label: 'Sound on (s)', t: 'sec', min: 500, max: 600000 },
      { k: 'fastWailOffTime', label: 'Sound off (s)', t: 'sec', min: 500, max: 600000 },
    ],
  },
  {
    title: 'Shutdown sequence', hint: 'Delay after STOP before each component switches off.',
    fields: [
      { k: 'stopBlowerDelay', label: 'Blower off after (s)', t: 'sec', min: 0, max: 60000 },
      { k: 'stopChopperDelay', label: 'Chopper off after (s)', t: 'sec', min: 0, max: 60000 },
      { k: 'stopRotDelay', label: 'Rotator off after (s)', t: 'sec', min: 0, max: 60000 },
    ],
  },
  {
    title: 'Growl test', hint: 'Each component runs alone, in this order: blower, rotator, chopper.',
    fields: [
      { k: 'growlBlowerTime', label: 'Blower (s)', t: 'sec', min: 500, max: 60000 },
      { k: 'growlRotatorTime', label: 'Rotator (s)', t: 'sec', min: 500, max: 60000 },
      { k: 'growlChopperTime', label: 'Chopper (s)', t: 'sec', min: 500, max: 60000 },
    ],
  },
  {
    title: 'Panel buttons',
    fields: [
      { k: 'longPressMs', label: 'Long-press time (ms)', t: 'int', min: 200, max: 5000 },
      { k: 'buttonDebounceMs', label: 'Debounce (ms)', t: 'int', min: 0, max: 1000 },
    ],
  },
  {
    title: 'Weather Watcher link',
    fields: [{ k: 'weatherAutoTriggerEnabled', label: 'Let the Weather Watcher start the siren automatically', t: 'bool',
      help: 'Off = the Weather Watcher can still report alerts, but never activates the siren.' }],
  },
  {
    title: 'Meshtastic',
    fields: [
      { k: 'meshWhitelistEnabled', label: 'Only accept commands from whitelisted senders', t: 'bool' },
      { k: 'meshWhitelist', label: 'Allowed sender short names', t: 'text', ph: 'comma-separated', maxlen: 127 },
      { k: 'meshPassword', label: 'Mesh command password', t: 'secret', maxlen: 63 },
      { k: 'meshNotifyActivation', label: 'Notify on siren activation', t: 'bool' },
      { k: 'meshNotifySystemEvents', label: 'Notify on system events', t: 'bool' },
      { k: 'meshNotifyWwHealth', label: 'Notify on Weather Watcher health changes', t: 'bool' },
      { k: 'meshNotifyStatus', label: 'Send scheduled Status Broadcast', t: 'bool' },
      { k: 'meshNotifyWatches', label: 'Notify on Tornado / Severe T-storm Watches', t: 'bool' },
    ],
  },
  {
    title: 'Status Broadcast', hint: 'Up to three daily times (24-hour, leave blank to disable) and what each broadcast includes.',
    fields: [
      { k: 'statusTime1', label: 'Time 1', t: 'time' },
      { k: 'statusTime2', label: 'Time 2', t: 'time' },
      { k: 'statusTime3', label: 'Time 3', t: 'time' },
      { k: 'statusIncludeMode', label: 'Include current mode', t: 'bool' },
      { k: 'statusIncludeLock', label: 'Include button lock state', t: 'bool' },
      { k: 'statusIncludeUptime', label: 'Include uptime', t: 'bool' },
      { k: 'statusIncludeCpuTemp', label: 'Include CPU temperature', t: 'bool' },
      { k: 'statusIncludeWwHealth', label: 'Include Weather Watcher health', t: 'bool' },
      { k: 'statusIncludeWifi', label: 'Include Wi-Fi state', t: 'bool' },
      { k: 'statusIncludeTime', label: 'Include clock time', t: 'bool' },
      { k: 'statusIncludeLastRun', label: 'Include last run', t: 'bool' },
      { k: 'statusIncludeAlerts', label: 'Include active alerts', t: 'bool' },
    ],
  },
  TIME_SCHEMA(),
];

const MODE_OPTS = [['OFF', 'Off - do not activate'], ['WAIL', 'Wail'], ['ATTACK', 'Attack'], ['FASTWAIL', 'Fast wail']];
const PRI_OPTS = [[0, 'Off (send nothing)'], [1, '1 - minimum'], [2, '2 - low'], [3, '3 - default'], [4, '4 - high'], [5, '5 - urgent']];
const pri = (k, label) => ({ k, label, t: 'select', opts: PRI_OPTS, num: true });

// code, label - mirrors OTHER_EXTREME_CANDIDATES in other_extreme_types.h
const OTHER_EXTREME = [
  ['FIRE', 'Fire Warning'], ['CEM', 'Civil Emergency Message'], ['CIVDNG', 'Civil Danger Warning'],
  ['HAZMAT', 'Hazardous Materials Warning'], ['RAD', 'Radiological Hazard Warning'], ['NUKE', 'Nuclear Power Plant Warning'],
  ['SHELTER', 'Shelter In Place Warning'], ['EVAC', 'Evacuation Immediate'], ['LAWENF', 'Law Enforcement Warning'],
  ['LOCAL', 'Local Area Emergency'], ['911', '911 Telephone Outage Emergency'], ['FFW', 'Flash Flood Warning'],
  ['FLW', 'Flood Warning'], ['XWIND', 'Extreme Wind Warning'], ['TSUNAMI', 'Tsunami Warning'], ['VOLCANO', 'Volcano Warning'],
  ['EQ', 'Earthquake Warning'], ['DUST', 'Dust Storm Warning'],
];

const WW_SCHEMA = [
  {
    title: 'Alert location', hint: 'Warnings are matched to this point. NWS requires a contact in the User-Agent.',
    fields: [
      { k: 'latitude', label: 'Latitude', t: 'float', min: -90, max: 90 },
      { k: 'longitude', label: 'Longitude', t: 'float', min: -180, max: 180 },
      { k: 'pollIntervalSec', label: 'Check NWS every (seconds)', t: 'int', min: 30, max: 3600, help: 'Lower than 30 would get this device rate-limited.' },
      { k: 'userAgentContact', label: 'Contact (email) for NWS', t: 'text', maxlen: 63 },
    ],
  },
  {
    title: 'Siren response by alert type',
    hint: 'What the Weather Watcher tells the siren to do. LIVE SETTING: these start a real siren with no one present.',
    fields: [
      { k: 'tornadoUnconfirmedMode', label: 'Tornado Warning - radar indicated', t: 'select', opts: MODE_OPTS },
      { k: 'tornadoConfirmedMode', label: 'Tornado Warning - observed / confirmed', t: 'select', opts: MODE_OPTS },
      { k: 'tornadoEmergencyMode', label: 'Tornado Emergency', t: 'select', opts: MODE_OPTS },
      { k: 'thunderstormBaseMode', label: 'Severe T-storm Warning - base', t: 'select', opts: MODE_OPTS },
      { k: 'thunderstormConsiderableMode', label: 'Severe T-storm - considerable', t: 'select', opts: MODE_OPTS },
      { k: 'thunderstormDestructiveMode', label: 'Severe T-storm - destructive', t: 'select', opts: MODE_OPTS },
      { k: 'otherExtremeMode', label: 'Other extreme emergencies', t: 'select', opts: MODE_OPTS },
      { k: 'repeatOnUpgrade', label: 'Re-activate if an alert escalates to a higher category', t: 'bool' },
      { k: 'otherExtremeIncluded', label: 'Other extreme emergencies: only these types may activate the siren', t: 'checks', opts: OTHER_EXTREME,
        help: 'Nothing is included by default.' },
    ],
  },
  TIME_SCHEMA(),
  {
    title: 'Push notifications (ntfy)',
    fields: [
      { k: 'ntfyEnabled', label: 'Send push notifications', t: 'bool' },
      { k: 'ntfyServer', label: 'ntfy server', t: 'text', ph: 'https://ntfy.sh', maxlen: 63 },
      { k: 'ntfyTopic', label: 'Topic', t: 'text', maxlen: 63 },
      { k: 'ntfyVerifyCert', label: 'Verify the server\'s TLS certificate', t: 'bool' },
    ],
  },
  {
    title: 'Notification priority - alerts', hint: 'ntfy priority per alert type (0 sends nothing for that type).',
    fields: [
      pri('ntfyPriTornadoWatch', 'Tornado Watch'), pri('ntfyPriSevereTstormWatch', 'Severe T-storm Watch'),
      pri('ntfyPriThunderstormBase', 'T-storm - base'), pri('ntfyPriThunderstormConsiderable', 'T-storm - considerable'),
      pri('ntfyPriThunderstormDestructive', 'T-storm - destructive'), pri('ntfyPriTornadoUnconfirmed', 'Tornado - radar indicated'),
      pri('ntfyPriTornadoConfirmed', 'Tornado - confirmed'), pri('ntfyPriTornadoEmergency', 'Tornado Emergency'),
      pri('ntfyPriOtherExtreme', 'Other extreme'),
    ],
  },
  {
    title: 'Notification priority - system',
    fields: [
      pri('ntfyPriActivations', 'Siren activations'), pri('ntfyPriStop', 'Siren stopped'), pri('ntfyPriFaults', 'Faults'),
      pri('ntfyPriLockedPress', 'Button pressed while locked'), pri('ntfyPriBoot', 'Board booted'), pri('ntfyPriScheduledStatus', 'Scheduled status'),
    ],
  },
];
