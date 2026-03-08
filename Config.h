#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==========================================
// 1. DISPLAY CONFIGURATION
// ==========================================
const int MODULE_WIDTH   = 30; 
const int MODULE_HEIGHT  = 7;  
const int NUM_MODULES    = 3;  

const int DISPLAY_WIDTH  = MODULE_WIDTH * NUM_MODULES; // 90
const int DISPLAY_HEIGHT = MODULE_HEIGHT;              // 7

// --- Colors & Logic ---
#define BLACK  LOW
#define YELLOW HIGH
const byte NULL_ADDR = 255; 

// ==========================================
// 2. FLIP-DOT PIN DEFINITIONS
// ==========================================
const int PIN_COL_DATA  = 13; // Header 20
const int PIN_ROW_DATA  = 12; // Header 19
const int PIN_COL_CLK   = 11; // Header 18
const int PIN_ROW_CLK   = 10; // Header 17
const int PIN_COL_LAT   = 9;  // Header 16
const int PIN_ROW_LAT   = 8;  // Header 15
const int PIN_FIRE      = 7;  // Header 14
const int PIN_POLARITY  = 6;  // Header 13

// ==========================================
// 3. LED PIN DEFINITIONS
// ==========================================
const int LED_DATA_PIN  = 2;   // Connects to Box Header Pin 9
const int LED_CLK_PIN   = 3;   // Connects to Box Header Pin 10
const int LED_LATCH_PIN = 4;   // Connects to Box Header Pin 7
const int LED_OE_PIN    = 5;   // Connects to Box Header Pin 4 (Active LOW)

const int LED_ROW_A     = A0;  // Connects to Box Header Pin 5 (Also Watchdog Trigger)
const int LED_ROW_B     = A1;  // Connects to Box Header Pin 6
const int LED_ROW_C     = A2;  // Connects to Box Header Pin 3

const int PIN_RELAY     = A3;  

// ==========================================
// 4. NETWORK & API CONFIGURATION
// ==========================================
// Replace these with your actual network details later
const char WIFI_SSID[] = "Harps 3F";
const char WIFI_PASS[] = "D@rt_inc";

// MBTA V3 API details
const char MBTA_API_KEY[] = "b9f6f484ed8c451991eff15b57352be6"; // Optional, but recommended for higher rate limits
const char STOP_ID[]      = "place-alwfl";       // Example: Alewife Station

// How often should the display check for new train data? (in milliseconds)
// 30000 ms = 30 seconds. Be gentle to the API!
const unsigned long API_UPDATE_INTERVAL = 30000; 

#endif // CONFIG_H