#pragma once
#include "config.h"
#include <Arduino.h>

// Every relay command in the firmware funnels through these six functions —
// logging here gives a complete, unambiguous record of what the firmware
// actually commanded, regardless of caller (Test page, state machine, or
// physical buttons). Compare against what the hardware actually does to
// tell a firmware-issued command apart from a relay-board-level effect.
inline void chopperOn()  { Serial.printf("[%lu] chopperOn\n",  millis()); digitalWrite(RELAY_CHOPPER,  LOW);  }
inline void chopperOff() { Serial.printf("[%lu] chopperOff\n", millis()); digitalWrite(RELAY_CHOPPER,  HIGH); }
inline void blowerOn()   { Serial.printf("[%lu] blowerOn\n",   millis()); digitalWrite(SSR_BLOWER,     HIGH); }
inline void blowerOff()  { Serial.printf("[%lu] blowerOff\n",  millis()); digitalWrite(SSR_BLOWER,     LOW);  }
inline void rotatorOn()  { Serial.printf("[%lu] rotatorOn\n",  millis()); digitalWrite(RELAY_ROTATOR,  LOW);  }
inline void rotatorOff() { Serial.printf("[%lu] rotatorOff\n", millis()); digitalWrite(RELAY_ROTATOR,  HIGH); }

inline void allOff() {
    blowerOff();
    chopperOff();
    rotatorOff();
}

// Returns packed relay state for status: bit0=chopper, bit1=blower, bit2=rotator
// 1 = ON regardless of active polarity
inline uint8_t relayState() {
    uint8_t state = 0;
    if (digitalRead(RELAY_CHOPPER)  == LOW)  state |= 0x01;
    if (digitalRead(SSR_BLOWER)     == HIGH) state |= 0x02;
    if (digitalRead(RELAY_ROTATOR)  == LOW)  state |= 0x04;
    return state;
}
