#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// AsyncWebServer handlers run on the async_tcp FreeRTOS task, while loop()
// runs on loopTask — the public dashboard's /status-data is built on the
// former while poll() rewrites the alert lists (String fields) on the
// latter, which without a lock can free a buffer mid-read. Everything that
// reads or mutates the alert lists, poll/link status, or settings takes this
// one lock — never held across the HTTPS request itself. Recursive, so a
// guarded helper can safely call another guarded helper.
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
