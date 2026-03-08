#include "WiFiManager.h"
#include <WiFiS3.h>
#include "HardwareLED.h"

namespace WiFiManager {

    void begin() {
        Serial.print("Connecting to WiFi: ");
        Serial.println(WIFI_SSID);

        WiFi.begin(WIFI_SSID, WIFI_PASS);
        while (WiFi.status() != WL_CONNECTED) {
            HardwareLED::scan();
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nWiFi Connected!");

        Serial.print("Syncing internal clock...");
        while (WiFi.getTime() == 0) {
            HardwareLED::scan();
            delay(250);
            Serial.print(".");
        }
        Serial.println(" Synced!");
    }

    void ensureConnected() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost. Reconnecting...");
            WiFi.begin(WIFI_SSID, WIFI_PASS);
        }
    }

} // end namespace
