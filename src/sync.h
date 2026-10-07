#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// AsyncWebServer handlers run on the async_tcp FreeRTOS task, while loop()
// runs on loopTask — without a lock, a web request could call sm.trigger()/
// sm.stop() or rewrite settings in the middle of sm.update() or a mesh/
// weather-link command on the other task. Everything that reads or mutates
// control state (state machine, buttons, settings, weather-link status, run
// log) takes this one lock. Recursive, so a guarded helper can safely call
// another guarded helper. Hold times are microseconds, except an NVS save.
inline SemaphoreHandle_t ctrlMutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
}

struct CtrlLock {
    CtrlLock()  { xSemaphoreTakeRecursive(ctrlMutex(), portMAX_DELAY); }
    ~CtrlLock() { xSemaphoreGiveRecursive(ctrlMutex()); }
    CtrlLock(const CtrlLock&) = delete;
    CtrlLock& operator=(const CtrlLock&) = delete;
};

// Wraparound-safe "has this millis() deadline passed?" check — a plain
// `millis() >= deadline` breaks when millis() rolls over every ~49.7 days.
inline bool deadlinePassed(uint32_t deadline) {
    return (int32_t)(millis() - deadline) >= 0;
}
