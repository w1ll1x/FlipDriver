// ==========================================
// MBTA TRANSIT SIGN - MASTER STATE MACHINE
// Hardware: Arduino UNO R4 WiFi
// Mode: DUMMY DATA HARDWARE TEST
// ==========================================

#include "Config.h"
#include "HardwareFlip.h"
#include "HardwareLED.h"
#include "GraphicsEngine.h"

// --- State Machine Variables ---
unsigned long lastFetchTime = 0;
const unsigned long DEMO_UPDATE_INTERVAL = 10000; // 10 seconds for demo purposes

// Dummy data arrays to test our layout and inversion fixes
int dummyIndex = 0;
String dummyDestinations[] = {"ALEWIFE", "ASHMONT", "BRAINTREE", "N STATION"};
String dummyTimes[] = {"5 MIN", "ARR", "12 MIN", "BRD"};

void setup() {
  Serial.begin(9600);
  Serial.println("--- MBTA TRANSIT SIGN INITIALIZING ---");

  // 1. Initialize Hardware Layers
  HardwareFlip::begin();
  HardwareLED::begin();
  GraphicsEngine::begin();

  // 2. Physical Reset
  // Forces all physical dots to BLACK, syncing them with the software buffer.
  Serial.println("Performing initial mechanical wipe...");
  GraphicsEngine::sweepWipe(); 
  
  // 3. Enable LEDs
  HardwareLED::setEnabled(false); 

  Serial.println("System Ready. Starting State Machine.");
  
  // Force an immediate first draw
  GraphicsEngine::showStaticMessage("INITIALIZING", "...");
  lastFetchTime = millis();
}

void loop() {
  // ========================================================
  // STATE 1: HIGH-SPEED MULTIPLEXING (The heartbeat)
  // ========================================================
  HardwareLED::scan();

  // ========================================================
  // STATE 2: THE UPDATE TIMER
  // ========================================================
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastFetchTime >= DEMO_UPDATE_INTERVAL) {
    lastFetchTime = currentMillis;

    // Grab the next dummy message
    String newDest = dummyDestinations[dummyIndex];
    String newTime = dummyTimes[dummyIndex];

    // ========================================================
    // STATE 3: THE MECHANICAL UPDATE
    // ========================================================
    Serial.print("Updating Sign: ");
    Serial.print(newDest);
    Serial.print("  ");
    Serial.println(newTime);
    
    // This function automatically calculates differences, physically flips 
    // the dots, and uses safeDelay() to keep the LEDs brightly lit!
    GraphicsEngine::showStaticMessage(newDest, newTime);

    // Cycle the dummy data
    dummyIndex++;
    if (dummyIndex > 3) dummyIndex = 0;
  }
}