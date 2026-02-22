#include "TransitAPI.h"
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include "HardwareLED.h" 

namespace TransitAPI {

    WiFiSSLClient client;
    const char* host = "api-v3.mbta.com";
    const int httpsPort = 443;

    struct RouteQuery {
        String fallbackHeadsign;
        bool dynamicHeadsign;
        String url;
    };

    // ONLY Red Line endpoints for Porter Square
    // 0: Outbound to Alewife (Direction 1)
    // 1: Inbound to Ashmont/Braintree (Direction 0)
    RouteQuery queries[2] = {
        {"ALEWIFE", true, "/predictions?filter[stop]=place-portr&filter[route]=Red&filter[direction_id]=1&sort=departure_time&page[limit]=2&include=trip"},
        {"INBOUND", true, "/predictions?filter[stop]=place-portr&filter[route]=Red&filter[direction_id]=0&sort=departure_time&page[limit]=2&include=trip"}
    };

    // --- TIME MATH ENGINE ---
    unsigned long isoToEpoch(const char* isoTime) {
        if (!isoTime || strlen(isoTime) < 19) return 0;
        int y, M, d, h, m, s, tz_h, tz_m;
        char tz_sign;
        
        if (sscanf(isoTime, "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &M, &d, &h, &m, &s, &tz_sign, &tz_h, &tz_m) < 6) return 0;

        static const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        int days = d - 1;
        for(int i = 1; i < M; i++) days += daysInMonth[i-1];
        if (M > 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) days++; 
        for(int i = 1970; i < y; i++) {
            days += 365;
            if ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) days++;
        }

        unsigned long epoch = days * 86400UL + h * 3600UL + m * 60UL + s;
        long tz_offset = tz_h * 3600 + tz_m * 60;
        if (tz_sign == '-') epoch += tz_offset; 
        else epoch -= tz_offset;

        return epoch;
    }

    String formatSignTime(unsigned long trainEpoch, const char* status) {
        if (status != nullptr) {
            String s = String(status);
            s.toUpperCase();
            if (s == "BOARDING" || s == "STOPPED AT STATION") return "BRD";
            if (s == "ARRIVING" || s == "STOPPED 1 STOP AWAY") return "ARR";
        }

        if (trainEpoch > 0) {
            unsigned long currentEpoch = WiFi.getTime(); 
            if (currentEpoch == 0) return "SCHED"; 

            long diffSeconds = (long)trainEpoch - (long)currentEpoch;

            if (diffSeconds <= 45) return "BRD";     
            if (diffSeconds <= 90) return "ARR";     

            int minutes = diffSeconds / 60;
            if (minutes < 0) return "BRD"; 
            if (minutes > 99) return "99+ MIN";      
            return String(minutes) + " MIN";         
        }

        return "SCHED";
    }

    const char* findTripHeadsign(JsonDocument& doc, const char* targetTripId) {
        if (!targetTripId) return nullptr;
        JsonArray included = doc["included"].as<JsonArray>();
        for (JsonObject inc : included) {
            const char* type = inc["type"];
            const char* id = inc["id"];
            if (type && id && strcmp(type, "trip") == 0 && strcmp(id, targetTripId) == 0) {
                return inc["attributes"]["headsign"];
            }
        }
        return nullptr;
    }

    // --- API CONNECTION ---
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

    bool fetchPrediction(int queryIndex, String &destination, String &timeString) {
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(WIFI_SSID, WIFI_PASS);
            return false;
        }

        RouteQuery q = queries[queryIndex];
        destination = q.fallbackHeadsign;

        if (!client.connect(host, httpsPort)) return false;

        client.print(String("GET ") + q.url + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Accept: application/vnd.api+json\r\n" +
                     (String(MBTA_API_KEY).length() > 0 ? String("x-api-key: ") + MBTA_API_KEY + "\r\n" : "") +
                     "Connection: close\r\n\r\n");

        unsigned long timeout = millis();
        while (client.available() == 0) {
            HardwareLED::scan(); 
            if (millis() - timeout > 5000) {
                client.stop();
                return false;
            }
        }

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") break; 
        }

        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, client);
        if (error) {
            client.stop();
            return false;
        }

        bool foundValidTrain = false;
        JsonArray data = doc["data"].as<JsonArray>();
        
        for (JsonObject item : data) {
            const char* status = item["attributes"]["status"];
            const char* depTime = item["attributes"]["departure_time"];
            const char* arrTime = item["attributes"]["arrival_time"];
            
            const char* tIso = (depTime != nullptr) ? depTime : arrTime;
            unsigned long epoch = isoToEpoch(tIso);

            if (epoch > 0 || status != nullptr) {
                timeString = formatSignTime(epoch, status);

                if (q.dynamicHeadsign) {
                    const char* tripId = item["relationships"]["trip"]["data"]["id"];
                    const char* headsign = findTripHeadsign(doc, tripId);
                    if (headsign != nullptr) {
                        destination = String(headsign);
                        destination.toUpperCase();
                    }
                }
                foundValidTrain = true;
                break; 
            }
        }

        if (!foundValidTrain) timeString = "---";
        
        client.stop();
        return true;
    }

} // end namespace