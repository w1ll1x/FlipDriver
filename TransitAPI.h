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

    // Fetches up to maxResults predictions for a route in one HTTP call.
    // Fills destinations[] and timeStrings[] arrays. Returns number of valid results found.
    int fetchPredictions(int queryIndex, String destinations[], String timeStrings[], int maxResults);

}

#endif // TRANSIT_API_H