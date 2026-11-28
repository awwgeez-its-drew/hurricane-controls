'use strict';
// Decides which requests need a Windows sign-in, and remembers an unlock
// window. Pure logic: the actual Windows prompt is injected (lib/winauth.js),
// so this is fully testable off Windows.
//
// What is protected (everything that starts motion or changes configuration):
//   siren  POST /cmd  wail | attack | fastwail | manual | growl | btn-lock
//                     test-lock{on:true} | test{on:true}
//   both   POST /settings-data /password /ap-password /wifi-data /ntfy-token /restart
//   and (in main.js) firmware upload, and editing the saved board connections.
//
// Deliberately NOT protected - these only ever make things safer or are
// read-only:
//   STOP, manual-ka / test-ka keep-alives, test{on:false}, test-lock{on:false},
//   every GET, CSV export, ntfy-test, test-link.
// A credential prompt must never stand between you and stopping the siren.

const UNLOCK_MS = 5 * 60 * 1000;

const START_MODES = new Set(['wail', 'attack', 'fastwail', 'manual', 'growl', 'btn-lock']);
const CONFIG_POSTS = /^\/(settings-data|password|ap-password|wifi-data|ntfy-token|restart)$/;

/** @returns {string|null} a short human reason if the request is protected, else null */
function protectedReason(id, method, path, body) {
  if (method !== 'POST') return null;
  if (CONFIG_POSTS.test(path)) return path === '/restart' ? 'restart the board' : 'change settings';
  if (id === 'siren' && path === '/cmd' && body && typeof body === 'object') {
    const m = String(body.mode);
    if (START_MODES.has(m)) return m === 'btn-lock' ? 'change the panel-button lock' : 'start the siren';
    if (m === 'test-lock' && body.on === true) return 'enter TEST MODE';
    if (m === 'test' && body.on === true) return 'run a siren component';
  }
  return null;
}

class AuthGate {
  /**
   * @param {{required:()=>boolean, verify:(reason:string)=>Promise<{ok:boolean,reason?:string,message?:string}>, now?:()=>number, unlockMs?:number}} o
   */
  constructor(o) {
    this.isRequired = o.required;
    this.verify = o.verify;
    this.now = o.now || Date.now;
    this.unlockMs = o.unlockMs || UNLOCK_MS;
    this.until = 0;
    this._pending = null;
    this.onChange = null;
  }

  get unlocked() { return !this.isRequired() || this.now() < this.until; }

  snapshot() { return { required: !!this.isRequired(), until: this.until }; }

  lock() { this.until = 0; if (this.onChange) this.onChange(); }

  /** Resolves {ok:true} when allowed; {ok:false, message} when cancelled/denied/unavailable. */
  async ensure(reason) {
    if (this.unlocked) return { ok: true };
    // Several protected requests arriving together share ONE prompt.
    if (!this._pending) {
      this._pending = (async () => {
        try {
          const r = await this.verify(reason);
          if (r && r.ok) { this.until = this.now() + this.unlockMs; if (this.onChange) this.onChange(); return { ok: true }; }
          return { ok: false, reason: (r && r.reason) || 'denied', message: (r && r.message) || 'Windows sign-in was cancelled - nothing was sent' };
        } catch (e) {
          // A broken prompt must fail closed, never open.
          return { ok: false, reason: 'unavailable', message: 'Windows sign-in is unavailable: ' + e.message };
        } finally { this._pending = null; }
      })();
    }
    return this._pending;
  }
}

module.exports = { AuthGate, protectedReason, UNLOCK_MS };
