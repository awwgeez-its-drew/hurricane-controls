#pragma once
#include <Arduino.h>

// Link-health timestamps for the Meshtastic node, written by mesh.h and read
// by webserver.h — kept in their own small header so the web UI doesn't have
// to pull in mesh.h just to show them. The node runs its Serial Module in
// Text Message mode, which has no way to query the node itself, so these are
// passive observations only: a quiet channel and a dead node look the same
// until someone sends something. Guarded by CtrlLock like the rest of the
// control state. All times are millis(); 0 = not since boot.
struct MeshHealth {
    uint32_t lastRxMs      = 0;   // any line from the node (proves node -> board wiring)
    uint32_t lastPingMs    = 0;   // last whitelisted SIREN PING answered — a full mesh round trip
    uint32_t lastTxMs      = 0;   // last line this board handed the node to broadcast
    char     lastSender[9] = "";  // node id on the last received line, if it carried one
};

extern MeshHealth meshHealth;
