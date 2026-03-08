// ==========================================
// FLIP-DOT QUOTE DISPLAY
// Target: Porter Square 90x7 Sign
// ==========================================

#include "Config.h"
#include "HardwareFlip.h"
#include "HardwareLED.h"
#include "GraphicsEngine.h"
#include "WiFiManager.h"
#include "QuoteAPI.h"

// We need access to the memory buffers to track the physical dots
extern bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];
extern bool next_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];


// --- State ---
String currentQuote = "";

// --- Phase Machine ---
// PH_DISPLAY:  Holding the current quote on screen
// PH_FETCHING: Blocking HTTP fetch; display stays frozen, no dot movement
// PH_FADE_OUT: Escalating random noise wipe
// PH_FADE_IN:  Settle dots into the new quote
enum Phase { PH_DISPLAY, PH_FETCHING, PH_FADE_OUT, PH_FADE_IN };
Phase currentPhase = PH_DISPLAY;
unsigned long phaseStartTime = 0;
unsigned long lastFrameTime  = 0;

const unsigned long DISPLAY_MS      = 300000UL; // 5 minutes
const unsigned long FETCH_RETRY_MS  = 30000UL;  // retry delay after a failed fetch
const unsigned long FADE_OUT_MS     = 4000;     // 4 seconds of escalating noise
const unsigned long FRAME_DT        = 25;       // ms per animation frame

// --- Settle-In Tracker ---
struct Point { uint8_t x, y; };
Point todo[DISPLAY_WIDTH * DISPLAY_HEIGHT];
uint16_t todoCnt = 0;

// Helper: Center text and render it to the hidden next_buffer
void renderQuoteToTarget(const String &text) {
    GraphicsEngine::clearBuffer();
    int textW = GraphicsEngine::getStringWidth(text);
    int startX = (DISPLAY_WIDTH - textW) / 2;
    if (startX < 0) startX = 0;
    GraphicsEngine::drawString(startX, 0, text);
}

// Helper: Find every physical dot that doesn't match the target text
void buildTodo() {
    todoCnt = 0;
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            if (current_buffer[x][y] != next_buffer[x][y]) {
                todo[todoCnt].x = x;
                todo[todoCnt].y = y;
                todoCnt++;
            }
        }
    }
}

void setup() {
    Serial.begin(9600);
    randomSeed(analogRead(A5));

    // 1. Keep 24V off — dots physically cannot move while relay is LOW
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);

    // 2. Init hardware (relay still LOW — safe)
    HardwareFlip::begin();
    GraphicsEngine::begin();
    HardwareLED::setEnabled(false);

    // 3. Connect to WiFi — relay is LOW, no dot movement possible
    WiFiManager::begin();

    // 4. Fetch the first quote — relay still LOW, no dot movement possible.
    //    Retry indefinitely; we must have a quote before the display powers on.
    Serial.println("Fetching initial quote...");
    while (!QuoteAPI::fetch(currentQuote)) {
        Serial.println("Retrying in 5 seconds...");
        delay(5000);
    }

    // 5. Power on the display now that we have content ready
    delay(1000);
    digitalWrite(PIN_RELAY, HIGH); // 24V live
    delay(500);

    // 6. Wipe and show the fetched quote
    GraphicsEngine::sweepWipe();
    renderQuoteToTarget(currentQuote);
    GraphicsEngine::render();
    phaseStartTime = millis();
}

void loop() {
    unsigned long now = millis();

    // ==========================================
    // PHASE 1: DISPLAY TEXT (5 minutes)
    // ==========================================
    if (currentPhase == PH_DISPLAY) {
        if (now - phaseStartTime >= DISPLAY_MS) {
            currentPhase = PH_FETCHING;
        }
    }

    // ==========================================
    // PHASE 2: FETCH NEXT QUOTE
    // Blocking HTTP call — display stays frozen, no dot manipulation.
    // On success: render new quote to target buffer, then begin transition.
    // On failure: stay displayed and retry after FETCH_RETRY_MS.
    // ==========================================
    else if (currentPhase == PH_FETCHING) {
        WiFiManager::ensureConnected();
        String newQuote;
        if (QuoteAPI::fetch(newQuote)) {
            currentQuote = newQuote;
            renderQuoteToTarget(currentQuote); // write new content to next_buffer
            currentPhase  = PH_FADE_OUT;
            phaseStartTime = millis();
            lastFrameTime  = millis();
        } else {
            Serial.println("Quote fetch failed — retrying in 30s");
            // Re-enter display phase; it will flip to PH_FETCHING again after FETCH_RETRY_MS
            phaseStartTime = millis() - DISPLAY_MS + FETCH_RETRY_MS;
            currentPhase   = PH_DISPLAY;
        }
    }

    // ==========================================
    // PHASE 3: FADE OUT (Escalating Random Noise)
    // ==========================================
    else if (currentPhase == PH_FADE_OUT) {
        if (now - lastFrameTime >= FRAME_DT) {
            lastFrameTime = now;
            float t = float(now - phaseStartTime) / FADE_OUT_MS;
            if (t > 1.0) t = 1.0;

            // Exponential curve: starts as a trickle, ends as a storm
            int maxFlips = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 10;
            int flipsThisFrame = 1 + int(pow(t, 4) * maxFlips);

            for (int i = 0; i < flipsThisFrame; i++) {
                int rx = random(DISPLAY_WIDTH);
                int ry = random(DISPLAY_HEIGHT);
                bool rc = random(2);

                if (current_buffer[rx][ry] != rc) {
                    HardwareFlip::flipDot(rx, ry, rc);
                    current_buffer[rx][ry] = rc;
                }
            }
        }

        if (now - phaseStartTime >= FADE_OUT_MS) {
            buildTodo(); // catalog all the noise we just made
            currentPhase  = PH_FADE_IN;
            lastFrameTime = now;
        }
    }

    // ==========================================
    // PHASE 4: FADE IN (Settle dots into target text)
    // ==========================================
    else if (currentPhase == PH_FADE_IN) {
        if (todoCnt > 0 && now - lastFrameTime >= FRAME_DT) {
            lastFrameTime = now;

            int flipsThisFrame = 1 + (todoCnt / 15);

            for (int i = 0; i < flipsThisFrame && todoCnt > 0; i++) {
                int idx = random(todoCnt);
                Point p = todo[idx];
                bool targetColor = next_buffer[p.x][p.y];

                if (current_buffer[p.x][p.y] != targetColor) {
                    HardwareFlip::flipDot(p.x, p.y, targetColor);
                    current_buffer[p.x][p.y] = targetColor;
                }

                // Remove from to-do list by swapping with last element
                todo[idx] = todo[--todoCnt];
            }
        }

        if (todoCnt == 0) {
            currentPhase   = PH_DISPLAY;
            phaseStartTime = millis();
        }
    }
}
