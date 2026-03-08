#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include "Config.h"

namespace WiFiManager {

    // Connect to WiFi and sync NTP clock. Blocks until both succeed.
    // Safe to call before relay is on — no dot movement occurs here.
    void begin();

    // Reconnect if connection has been lost. Non-blocking check.
    void ensureConnected();

}

#endif // WIFI_MANAGER_H
