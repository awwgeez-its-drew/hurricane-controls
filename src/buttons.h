#pragma once
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "statemachine.h"

class ButtonHandler {
public:
    void begin() {
        pinMode(BTN_WAIL,   INPUT_PULLUP);
        pinMode(BTN_ATTACK, INPUT_PULLUP);
        pinMode(BTN_STOP, INPUT_PULLUP);
    }

    bool locked         = false;
    bool testModeActive = false;

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

        if (locked) return;
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
            sm.stop();
            stopTriggered = true;
        }
        if (!stopPressed) {
            stopTriggered = false;
        }
        prevStop = stopPressed;

        // ── ATTACK / FAST WAIL button (GPIO33) — dual-function ─────────────
        // Short tap (release before s.longPressMs) → Attack, fired on RELEASE
        // only, so a real long-press can be distinguished before anything
        // triggers. Hold >= s.longPressMs → Fast Wail, fired once while still
        // held (fire-and-forget — unlike Manual, it keeps running on release).
        // Press-confirm debounce: raw must read LOW continuously for
        // s.buttonDebounceMs before we trust it — a brief noise glitch (far
        // shorter than any real press) would otherwise register as an
        // instant press+release and fire a spurious short-tap Attack.
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

        if (attackPressed && !prevAttack) {
            attackPressTs   = now;
            attackLongArmed = false;
        }

        if (attackPressed && !attackLongArmed) {
            if ((now - attackPressTs) >= s.longPressMs) {
                attackLongArmed = true;
                if (sm.isIdle()) sm.trigger(RunMode::FAST_WAIL);
            }
        }

        if (!attackPressed && prevAttack) {
            if (!attackLongArmed) {
                if (sm.isIdle()) sm.trigger(RunMode::ATTACK);
            }
            attackLongArmed = false;
        }

        prevAttack = attackPressed;

        // ── WAIL / MANUAL button (GPIO32) — dual-function ─────────────────
        // Same press-confirm debounce as ATTACK above, ahead of the existing
        // release debounce (mechanical contact bounce): only accept HIGH
        // after it has been stable for RELEASE_DEBOUNCE_MS.
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

        if (wailPressed && !prevWail) {
            // Falling edge — button down
            wailPressTs            = now;
            longPressArmed         = false;
            physicalManualActive_  = false;
        }

        if (wailPressed && !longPressArmed) {
            if ((now - wailPressTs) >= s.longPressMs) {
                longPressArmed = true;
                if (sm.isIdle()) {
                    if (sm.trigger(RunMode::MANUAL))
                        physicalManualActive_ = true;
                }
            }
        }

        if (!wailPressed && prevWail) {
            // Rising edge — button released
            if (longPressArmed) {
                // Momentary: only stop what the physical button started
                if (physicalManualActive_) {
                    sm.stop();
                    physicalManualActive_ = false;
                }
                longPressArmed = false;
            } else {
                // Short tap → Wail
                if (sm.isIdle()) sm.trigger(RunMode::WAIL);
            }
        }

        prevWail = wailPressed;
    }

private:
    static constexpr uint32_t RELEASE_DEBOUNCE_MS   = 30;
    static constexpr uint32_t TEST_LOCK_TIMEOUT_MS  = 180000;   // 3 minutes

    bool     prevStop              = false;
    uint32_t stopPressTs           = 0;
    bool     stopTriggered         = false;

    bool     prevAttack                = false;
    bool     prevAttackRaw             = false;
    uint32_t attackPressCandidateTs_   = 0;
    uint32_t attackPressTs             = 0;
    uint32_t attackReleaseCandidateTs_ = 0;
    bool     attackLongArmed           = false;

    bool     prevWail               = false;
    bool     prevWailRaw            = false;
    uint32_t wailPressCandidateTs_  = 0;
    uint32_t wailPressTs            = 0;
    uint32_t wailReleaseCandidateTs_ = 0;
    bool     longPressArmed         = false;
    bool     physicalManualActive_  = false;

    uint32_t lockArmedAt_    = 0;
    bool     lockAutoExpire_ = false;
};

extern ButtonHandler buttons;
