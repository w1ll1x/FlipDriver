#ifndef MBTA_API_H
#define MBTA_API_H

#include <Arduino.h>
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include <time.h>
#include <limits.h>

// --- Data Structures ---
struct Prediction {
  bool valid;
  String headsign; // e.g. "ALEWIFE"
  String timeStr;  // e.g. "5 MIN" or "ARR"
};

class MbtaApi {
  private:
    const char* _ssid;
    const char* _pass;
    const char* _apiKey;
    const char* _host = "api-v3.mbta.com";
    
    // Optimized URL for Porter Square (Red Line) -> Small payload (~2KB)
    const char* _urlPath = "/predictions?filter%5Bstop%5D=place-portr&filter%5Broute%5D=Red&sort=time&page%5Blimit%5D=10&include=trip&fields%5Bprediction%5D=direction_id,departure_time,arrival_time,trip&fields%5Btrip%5D=headsign";

    Prediction _preds[2]; // Index 0 = Southbound (Ashmont/Braintree), 1 = Northbound (Alewife)

    // Helper: Convert ISO string to Epoch Time
    bool parseIsoToEpoch(const char* iso, time_t &outEpoch) {
      if (!iso || strlen(iso) < 19) return false;
      int y, M, d, h, m, s, tzH, tzM;
      char tzSign = '+';
      sscanf(iso, "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &M, &d, &h, &m, &s, &tzSign, &tzH, &tzM);
      
      struct tm t = {0};
      t.tm_year = y - 1900; t.tm_mon = M - 1; t.tm_mday = d;
      t.tm_hour = h; t.tm_min = m; t.tm_sec = s;
      
      // Manual Epoch Calc for R4
      uint32_t days = 0;
      for (int i = 1970; i < y; i++) days += (i % 4 == 0 && (i % 100 != 0 || i % 400 == 0)) ? 366 : 365;
      const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for (int i = 0; i < M - 1; i++) {
        days += daysInMonth[i];
        if (i == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) days++;
      }
      days += (d - 1);
      time_t faceValue = (days * 86400) + (h * 3600) + (m * 60) + s;
      
      long offsetSec = (tzH * 3600) + (tzM * 60);
      if (tzSign == '-') offsetSec = -offsetSec;
      outEpoch = faceValue - offsetSec;
      return true;
    }

    const char* findHeadsign(JsonArray included, const char* tripId) {
      if (!tripId) return "";
      for (JsonObject inc : included) {
        if (strcmp(inc["type"], "trip") == 0 && strcmp(inc["id"], tripId) == 0) {
          return inc["attributes"]["headsign"] | "";
        }
      }
      return "";
    }

  public:
    MbtaApi(const char* ssid, const char* pass, const char* apiKey) {
      _ssid = ssid;
      _pass = pass;
      _apiKey = apiKey;
    }

    void connect() {
      if (WiFi.status() == WL_CONNECTED) return;
      Serial.print("[MBTA] Connecting WiFi...");
      while (WiFi.begin(_ssid, _pass) != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println(" Connected!");
    }

    // Call this in loop(). Returns true if data was successfully updated.
    bool update() {
      if (WiFi.status() != WL_CONNECTED) {
        connect(); // Auto-reconnect
      }

      WiFiSSLClient client;
      client.setTimeout(5000);

      Serial.println("[MBTA] Fetching data...");
      if (!client.connect(_host, 443)) {
        Serial.println("[MBTA] Connection failed.");
        return false;
      }

      // HTTP/1.0 Request (Non-chunked)
      client.print("GET "); client.print(_urlPath); client.println(" HTTP/1.0");
      client.print("Host: "); client.println(_host);
      client.println("Accept: application/vnd.api+json");
      client.print("x-api-key: "); client.println(_apiKey);
      client.println();

      // Skip Headers
      while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
      }

      // Parse JSON (Optimized for 8KB)
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, client);
      client.stop();

      if (error) {
        Serial.print("[MBTA] JSON Failed: "); Serial.println(error.c_str());
        return false;
      }

      // Logic Extraction
      JsonArray data = doc["data"].as<JsonArray>();
      JsonArray included = doc["included"].as<JsonArray>();
      time_t now = WiFi.getTime();
      
      time_t bestTime[2] = { (time_t)LONG_MAX, (time_t)LONG_MAX };
      String bestHeadsign[2] = { "", "" };

      for (JsonObject item : data) {
        int dir = item["attributes"]["direction_id"] | -1;
        if (dir < 0 || dir > 1) continue;

        const char* tIso = item["attributes"]["departure_time"];
        if (!tIso) tIso = item["attributes"]["arrival_time"];
        if (!tIso) continue;

        time_t when;
        if (!parseIsoToEpoch(tIso, when)) continue;
        if (when < now - 30) continue; // Ignore old trains

        if (when < bestTime[dir]) {
          bestTime[dir] = when;
          const char* tid = item["relationships"]["trip"]["data"]["id"];
          bestHeadsign[dir] = findHeadsign(included, tid);
        }
      }

      // Format for Display
      for (int i = 0; i < 2; i++) {
        if (bestTime[i] == (time_t)LONG_MAX) {
          _preds[i].valid = false;
          _preds[i].headsign = "NO TRAINS";
          _preds[i].timeStr = "";
        } else {
          _preds[i].valid = true;
          int mins = (int)((bestTime[i] - now + 59) / 60);
          if (mins < 0) mins = 0;
          
          _preds[i].timeStr = (mins <= 0) ? "ARR" : String(mins) + " MIN";
          
          if (bestHeadsign[i].length() > 0) _preds[i].headsign = bestHeadsign[i];
          else _preds[i].headsign = (i == 1) ? "ALEWIFE" : "OUTBOUND";
          
          _preds[i].headsign.toUpperCase();
        }
      }
      return true;
    }

    // Get the latest data for direction (0 = South, 1 = North)
    Prediction getPrediction(int direction) {
      if (direction < 0 || direction > 1) return _preds[0];
      return _preds[direction];
    }
};

#endif