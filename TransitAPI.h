#ifndef TRANSIT_API_H
#define TRANSIT_API_H

#include <Arduino.h>
#include "Config.h"

namespace TransitAPI {

    // Connects to WiFi (called once in setup)
    void begin();

    // Fetches data for a specific route based on the queryIndex (0, 1, or 2).
    // Updates the destination and timeString variables. Returns true if successful.
    bool fetchPrediction(int queryIndex, String &destination, String &timeString);

}

#endif // TRANSIT_API_H