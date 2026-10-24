#pragma once
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "statemachine.h"
#include "motors.h"

// Reported (via mesh.h) whenever a physical button is pressed while
// buttons.locked is true — not a dual-function distinction, just "someone is
// touching this control that's currently disabled."
enum class LockedButton : uint8_t { NONE, STOP, WAIL, ATTACK };

class ButtonHandler {
public:
    void begin() {
        pinMode(BTN_WAIL,   INPUT_PULLUP);
        pinMode(BTN_ATTACK, INPUT_PULLUP);
        pinMode(BTN_STOP, INPUT_PULLUP);
    }

    bool locked         = false;
    bool testModeActive = false;
    LockedButton lastLockedPress = LockedButton::NONE;
    uint32_t     lockedPressSeq  = 0;   // bumped once per confirmed press while locked

    // Shared entry point for both lock toggles (Main page's plain icon and the
    // Test page's explicit TEST MODE switch) so auto-expiry bookkeeping only
    // lives in one place. Unlocking via EITHER entry point always fully exits
    // test mode — plain-locking (Main page) must never grant exclusive test
    // access, but exiting a lock must never leave test mode dangling either.
    void setLocked(bool on, bool autoExpire) {
        locked = on;
        if (on && autoExpire) {
            lockArmedAt_    = millis();
            lockAutoExpire_ = true;
        } else {
            lockAutoExpire_ = false;
        }
        if (!on) testModeActive = false;
    }

    // TEST MODE: locks physical buttons (like setLocked) AND makes the Test
    // page's component buttons the only thing that can control outputs —
    // see the exclusivity check in webserver.h's POST /cmd handler.
    void setTestMode(bool on) {
        testModeActive = on;
        setLocked(on, on);
        if (!on) allOff();   // guarantee a clean baseline on exit, manual or auto-expiry
    }

    bool lockAutoExpiring() const { return locked && lockAutoExpire_; }

    uint32_t lockRemainingSec() const {
        if (!locked || !lockAutoExpire_) return 0;
        uint32_t elapsed = millis() - lockArmedAt_;
        return (elapsed < TEST_LOCK_TIMEOUT_MS) ? (TEST_LOCK_TIMEOUT_MS - elapsed) / 1000 : 0;
    }

    void update() {
        uint32_t now = millis();

        // Auto-expire a TEST MODE lock even while otherwise skipping all
        // button processing below, so it can't be left on indefinitely.
        // lockAutoExpire_ is only ever armed by TEST MODE entry, so this is
        // always a "test mode timed out" event.
        if (locked && lockAutoExpire_ && (now - lockArmedAt_) >= TEST_LOCK_TIMEOUT_MS) {
            setTestMode(false);
        }

        if (locked) {
            // Awareness only — no debounce-chain actuation while locked, just
            // a single-stage confirm (reusing s.buttonDebounceMs for noise
            // immunity) so someone touching a disabled control is reported.
            const Settings& ls = settingsMgr.s;
            checkLockedPress(BTN_STOP,   LockedButton::STOP,   now, ls.buttonDebounceMs,
                              prevLockedStopRaw_, lockedStopCandTs_, lockedStopAnnounced_);
            checkLockedPress(BTN_WAIL,   LockedButton::WAIL,   now, ls.buttonDebounceMs,
                              prevLockedWailRaw_, lockedWailCandTs_, lockedWailAnnounced_);
            checkLockedPress(BTN_ATTACK, LockedButton::ATTACK, now, ls.buttonDebounceMs,
                              prevLockedAttackRaw_, lockedAttackCandTs_, lockedAttackAnnounced_);
            return;
        }
        const Settings& s = settingsMgr.s;

        // ── STOP / E-Stop (GPIO4) ─────────────────────────────────────────
        // Requires the pin to read LOW continuously for s.buttonDebounceMs
        // before triggering a stop, so a brief transient/EMI glitch on the
        // line can't fire an unwanted stop.
        bool stopPressed = (digitalRead(BTN_STOP) == LOW);
        if (stopPressed && !prevStop) {
            stopPressTs      = now;
            stopTriggered    = false;
        }
        if (stopPressed && !stopTriggered && (now - stopPressTs) >= s.buttonDebounceMs) {
            sm.stop(TriggerSource::LOCAL);
            stopTriggered = true;
        }
        if (!stopPressed) {
            stopTriggered = false;
        }
        prevStop = stopPressed;

        // ── ATTACK (GPIO33) / WAIL (GPIO32) press-state debounce ───────────
        // Same two-stage debounce for both: raw must read LOW continuously
        // for s.buttonDebounceMs before a press is trusted (filters a brief
        // noise glitch, far shorter than any real press, that would
        // otherwise register as an instant press+release), and released
        // state must then be stable for RELEASE_DEBOUNCE_MS (mechanical
        // contact bounce) before a release is trusted. Computed for both
        // buttons up front, ahead of either button's own dual-function
        // dispatch below, so the rotator-jog combo gesture (both held
        // together) can be detected and take priority before either
        // button's individual long-press/short-tap logic acts on it.
        bool attackRaw = (digitalRead(BTN_ATTACK) == LOW);
        if (attackRaw && !prevAttackRaw) attackPressCandidateTs_ = now;
        if (!attackRaw) attackPressCandidateTs_ = 0;
        bool attackConfirmed = attackRaw && attackPressCandidateTs_ != 0 &&
            (now - attackPressCandidateTs_) >= s.buttonDebounceMs;
        prevAttackRaw = attackRaw;

        if (attackConfirmed) {
            attackReleaseCandidateTs_ = 0;
        } else if (prevAttack && attackReleaseCandidateTs_ == 0) {
            attackReleaseCandidateTs_ = now;
        }
        bool attackPressed = attackConfirmed ||
            (attackReleaseCandidateTs_ != 0 &&
             (now - attackReleaseCandidateTs_) < RELEASE_DEBOUNCE_MS);

        bool wailRaw = (digitalRead(BTN_WAIL) == LOW);
        if (wailRaw && !prevWailRaw) wailPressCandidateTs_ = now;
        if (!wailRaw) wailPressCandidateTs_ = 0;
        bool wailConfirmed = wailRaw && wailPressCandidateTs_ != 0 &&
            (now - wailPressCandidateTs_) >= s.buttonDebounceMs;
        prevWailRaw = wailRaw;

        if (wailConfirmed) {
            wailReleaseCandidateTs_ = 0;          // button is held — clear release timer
        } else if (prevWail && wailReleaseCandidateTs_ == 0) {
            wailReleaseCandidateTs_ = now;         // first HIGH after a real press — start timer
        }
        bool wailPressed = wailConfirmed ||
            (wailReleaseCandidateTs_ != 0 &&
             (now - wailReleaseCandidateTs_) < RELEASE_DEBOUNCE_MS);

        // ── Rotator jog: WAIL + ATTACK held together ────────────────────────
        // A maintenance convenience, not a siren mode — lets the owner spin
        // the horn to the most aerodynamic position by hand. Takes priority
        // over either button's own long-press action, but only if neither
        // has already armed one (MANUAL/FAST_WAIL already committed to) —
        // pressing the second button after that point doesn't retroactively
        // downgrade an already-running siren actuation. Only available while
        // fully idle: driving the rotator directly here while the state
        // machine's own STARTING/STOPPING sequence is also managing it would
        // fight over the same relay, so sm.rotatorJogActive also blocks any
        // new trigger() for as long as this is held (see statemachine.h) —
        // the siren can't suddenly activate while someone's hands are on the
        // unit repositioning it, either.
        bool bothPressed = wailPressed && attackPressed;
        if (bothPressed && !sm.rotatorJogActive && !longPressArmed && !attackLongArmed &&
            sm.isIdle() && !sm.otaActive) {
            sm.rotatorJogActive    = true;
            wailComboSuppressed_   = true;
            attackComboSuppressed_ = true;
            rotatorOn();
        }
        if (sm.rotatorJogActive && !bothPressed) {
            rotatorOff();
            sm.rotatorJogActive = false;
            // Whichever button is still held gets a fresh long-press
            // countdown from this moment, rather than inheriting the
            // elapsed combo-hold time — otherwise releasing just one
            // button after a long joint hold would instantly arm the
            // other's long-press action.
            wailPressTs   = now;
            attackPressTs = now;
        }

        // ── ATTACK / FAST WAIL button — dual-function ───────────────────────
        // Short tap (release before s.longPressMs) → Attack, fired on RELEASE
        // only, so a real long-press can be distinguished before anything
        // triggers. Hold >= s.longPressMs → Fast Wail, fired once while still
        // held (fire-and-forget — unlike Manual, it keeps running on release).
        if (attackPressed && !prevAttack) {
            attackPressTs   = now;
            attackLongArmed = false;
            // Not if the combo block above just engaged the jog on this
            // very tick (attack's fresh-press edge coinciding with wail
            // already being held) — that already set this true, and this
            // reset running right after would otherwise immediately undo it.
            if (!sm.rotatorJogActive) attackComboSuppressed_ = false;
        }

        if (!sm.rotatorJogActive) {
            if (attackPressed && !attackLongArmed) {
                if ((now - attackPressTs) >= s.longPressMs) {
                    attackLongArmed = true;
                    if (sm.isIdle()) sm.trigger(RunMode::FAST_WAIL, TriggerSource::LOCAL);
                }
            }

            if (!attackPressed && prevAttack) {
                if (!attackLongArmed) {
                    // Short tap → Attack (unless this press was part of a
                    // rotator-jog combo, in which case it's a no-op release).
                    if (!attackComboSuppressed_ && sm.isIdle())
                        sm.trigger(RunMode::ATTACK, TriggerSource::LOCAL);
                }
                attackLongArmed = false;
            }
        }

        prevAttack = attackPressed;

        // ── WAIL / MANUAL button — dual-function ────────────────────────────
        if (wailPressed && !prevWail) {
            // Falling edge — button down
            wailPressTs            = now;
            longPressArmed         = false;
            physicalManualActive_  = false;
            // See the matching comment in the ATTACK block above.
            if (!sm.rotatorJogActive) wailComboSuppressed_ = false;
        }

        if (!sm.rotatorJogActive) {
            if (wailPressed && !longPressArmed) {
                if ((now - wailPressTs) >= s.longPressMs) {
                    longPressArmed = true;
                    if (sm.isIdle()) {
                        if (sm.trigger(RunMode::MANUAL, TriggerSource::LOCAL))
                            physicalManualActive_ = true;
                    }
                }
            }

            if (!wailPressed && prevWail) {
                // Rising edge — button released
                if (longPressArmed) {
                    // Momentary: only stop what the physical button started
                    if (physicalManualActive_) {
                        sm.stop(TriggerSource::LOCAL);
                        physicalManualActive_ = false;
                    }
                    longPressArmed = false;
                } else {
                    // Short tap → Wail (unless this press was part of a
                    // rotator-jog combo, in which case it's a no-op release).
                    if (!wailComboSuppressed_ && sm.isIdle())
                        sm.trigger(RunMode::WAIL, TriggerSource::LOCAL);
                }
            }
        }

        prevWail = wailPressed;
    }

private:
    static constexpr uint32_t RELEASE_DEBOUNCE_MS   = 30;
    static constexpr uint32_t TEST_LOCK_TIMEOUT_MS  = 180000;   // 3 minutes

    // Single-stage confirm (press debounced, re-arms on release) used only
    // for locked-out awareness reporting — no long-press/dual-function logic
    // needed here, just "was this button touched."
    void checkLockedPress(uint8_t pin, LockedButton which, uint32_t now, uint32_t debounceMs,
                           bool& prevRaw, uint32_t& candTs, bool& announced) {
        bool raw = (digitalRead(pin) == LOW);
        if (raw && !prevRaw) candTs = now;
        if (!raw) { candTs = 0; announced = false; }
        bool confirmed = raw && candTs != 0 && (now - candTs) >= debounceMs;
        if (confirmed && !announced) {
            announced = true;
            lastLockedPress = which;
            lockedPressSeq++;
        }
        prevRaw = raw;
    }

    bool     prevLockedStopRaw_      = false;
    uint32_t lockedStopCandTs_       = 0;
    bool     lockedStopAnnounced_    = false;
    bool     prevLockedWailRaw_      = false;
    uint32_t lockedWailCandTs_       = 0;
    bool     lockedWailAnnounced_    = false;
    bool     prevLockedAttackRaw_    = false;
    uint32_t lockedAttackCandTs_     = 0;
    bool     lockedAttackAnnounced_  = false;

    bool     prevStop              = false;
    uint32_t stopPressTs           = 0;
    bool     stopTriggered         = false;

    bool     prevAttack                = false;
    bool     prevAttackRaw             = false;
    uint32_t attackPressCandidateTs_   = 0;
    uint32_t attackPressTs             = 0;
    uint32_t attackReleaseCandidateTs_ = 0;
    bool     attackLongArmed           = false;
    bool     attackComboSuppressed_    = false;

    bool     prevWail               = false;
    bool     prevWailRaw            = false;
    uint32_t wailPressCandidateTs_  = 0;
    uint32_t wailPressTs            = 0;
    uint32_t wailReleaseCandidateTs_ = 0;
    bool     longPressArmed         = false;
    bool     physicalManualActive_  = false;
    bool     wailComboSuppressed_   = false;

    uint32_t lockArmedAt_    = 0;
    bool     lockAutoExpire_ = false;
};

extern ButtonHandler buttons;
