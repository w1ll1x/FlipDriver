#include "QuoteAPI.h"
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include "HardwareLED.h"

namespace QuoteAPI {

    WiFiSSLClient client;
    const char* host = "flipper-production.up.railway.app";
    const int httpsPort = 443;

    bool fetch(String &message) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("QuoteAPI: Not connected to WiFi");
            return false;
        }

        if (!client.connect(host, httpsPort)) {
            Serial.println("QuoteAPI: Connection failed");
            return false;
        }

        // Use HTTP/1.0 to avoid chunked transfer encoding from the server
        client.print(String("GET /sign/small/next/ HTTP/1.0\r\n") +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n\r\n");

        // Wait for response with timeout, keeping LEDs alive
        unsigned long timeout = millis();
        while (client.available() == 0) {
            HardwareLED::scan();
            if (millis() - timeout > 10000) {
                Serial.println("QuoteAPI: Timeout waiting for response");
                client.stop();
                return false;
            }
        }

        // Print and consume HTTP headers
        Serial.println("QuoteAPI: --- RAW HEADERS ---");
        while (client.connected() || client.available()) {
            String line = client.readStringUntil('\n');
            Serial.println(line);
            if (line == "\r" || line == "\r\n" || line.length() == 0 || line == "\n") break;
        }
        Serial.println("QuoteAPI: --- END HEADERS ---");

        // Read the full body into a String before parsing
        // (avoids issues with chunked encoding or stream timing)
        String body = "";
        unsigned long bodyTimeout = millis();
        while (client.connected() || client.available()) {
            if (client.available()) {
                char c = client.read();
                body += c;
                bodyTimeout = millis();
            } else if (millis() - bodyTimeout > 2000) {
                break;
            }
        }
        client.stop();

        Serial.print("QuoteAPI: --- RAW BODY --- (");
        Serial.print(body.length());
        Serial.println(" bytes)");
        Serial.println(body);
        Serial.println("QuoteAPI: --- END BODY ---");

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.print("QuoteAPI: JSON parse error: ");
            Serial.println(error.c_str());
            return false;
        }

        const char* msg = doc["message"];
        if (!msg || strlen(msg) == 0) {
            Serial.println("QuoteAPI: Missing or empty message field");
            return false;
        }

        message = String(msg);
        message.toUpperCase();
        Serial.print("QuoteAPI: Got quote: ");
        Serial.println(message);
        return true;
    }

} // end namespace
