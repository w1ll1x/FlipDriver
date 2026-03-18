// ==========================================
// FLIP-DOT QUOTE + TRANSIT DISPLAY
// Target: Porter Square 90x7 Sign
// ==========================================

#include "Config.h"
#include "HardwareFlip.h"
#include "HardwareLED.h"
#include "GraphicsEngine.h"   // transitively includes Ferranti7.h
#include "WiFiManager.h"
#include "QuoteAPI.h"
#include "TransitAPI.h"

// We need access to the memory buffers to track the physical dots
extern bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];
extern bool next_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];


// --- State ---
String currentQuote = "";
String nextQuote    = "";  // fetched and staged; becomes currentQuote after transit

// --- Phase Machine ---
//
// Display cycle:
//   PH_DISPLAY_QUOTE  (5 min)
//     → PH_FETCHING_BOTH   (transit once best-effort, quote with retry — display frozen)
//     → PH_LETTER_FALL     (quote letters fall/explode off)
//     → PH_FADE_IN         (settle into transit or new quote)
//   PH_DISPLAY_TRANSIT  (20 s)
//     → PH_FADE_OUT        (noise wipe to black — next_buffer is all black)
//     → PH_FADE_IN         (settle noise to black — screen goes clear)
//     → PH_TYPEWRITER      (type new quote letter by letter)
//   PH_DISPLAY_QUOTE  ...repeat
//
// Rule: ALL network/WiFi calls live exclusively in PH_FETCHING_BOTH.
//       Dot addressing lives exclusively in PH_LETTER_FALL, PH_FADE_OUT,
//       PH_FADE_IN, and PH_TYPEWRITER.
enum Phase {
    PH_DISPLAY_QUOTE,
    PH_FETCHING_BOTH,
    PH_LETTER_FALL,     // quote exits: letters fall / explode off
    PH_DISPLAY_TRANSIT,
    PH_FADE_OUT,        // noise wipe (kept for future use; currently: transit → black)
    PH_FADE_IN,         // settle dots into target content (shared)
    PH_TYPEWRITER       // type new quote onto blank screen
};
Phase currentPhase = PH_DISPLAY_QUOTE;
Phase pendingPhase = PH_DISPLAY_QUOTE;  // destination when PH_FADE_IN finishes

unsigned long phaseStartTime = 0;
unsigned long lastFrameTime  = 0;

const unsigned long QUOTE_DISPLAY_MS   = 300000UL; // 5 minutes
const unsigned long TRANSIT_DISPLAY_MS =  20000UL; // 20 seconds
const unsigned long FADE_OUT_MS        =   4000;   // 4 seconds of escalating noise
const unsigned long FRAME_DT           =     25;   // ms per animation frame

// --- Settle-In Tracker ---
struct Point { uint8_t x, y; };
Point    todo[DISPLAY_WIDTH * DISPLAY_HEIGHT];
uint16_t todoCnt = 0;

// --- Typewriter State ---
const unsigned int TW_MIN_MS        =  55;  // fastest keystroke
const unsigned int TW_MAX_MS        = 210;  // slowest keystroke
const unsigned int TW_THINK_CHANCE  =  30;  // % chance of a "thinking" pause before each word
const unsigned int TW_THINK_MIN_MS  = 700;  // shortest thinking pause
const unsigned int TW_THINK_MAX_MS  = 1800; // longest thinking pause
int           tw_charIdx     = 0;    // next character to type
int           tw_cursorX     = 0;    // x position of next character
unsigned long tw_nextCharTime = 0;   // millis() when to type the next character


// ==========================================
// LETTER FALL ANIMATION
// ==========================================
// Each flipDot call takes ~2ms. All dot movement is via the same
// HardwareFlip::flipDot interface used everywhere else — no new hardware risk.
//
// Timing targets ~30 seconds total for a 15-letter quote:
//   Fall letters : one row cleared every LF_FALL_STEP_FRAMES (~150 ms/row × 7 rows ≈ 1 s)
//   Explode letters: burst erase + 7 debris pieces with gravity (~1.5 s of flight)
//   Inter-letter gap: random LF_GAP_MIN..LF_GAP_MAX frames (~0.5–2.5 s)

#define MAX_LF_LETTERS 24
#define MAX_LF_DEBRIS   7   // more pieces = more intense explosion

const uint8_t LF_FALL_STEP_FRAMES  =  1;   // frames between fall row clears (~25 ms/row — full speed)
const uint8_t LF_DEBRIS_STEP_FRAMES = 2;   // frames between debris steps (~50 ms/step)
const uint8_t LF_GAP_MIN_FRAMES    = 12;   // min idle gap between letters (~300 ms)
const uint8_t LF_GAP_MAX_FRAMES    = 65;   // max idle gap between letters (~1600 ms)

struct LFLetter {
    int16_t startX;
    int16_t width;
    bool    explode;
};

struct LFDebris {
    int16_t x;
    int8_t  y;
    int8_t  vx;   // horizontal velocity, applied every step
    int8_t  vy;   // vertical velocity; gravity increments this each step
    bool    active;
};

LFLetter lf_letters[MAX_LF_LETTERS];
int      lf_letterCount = 0;
int      lf_order[MAX_LF_LETTERS];
int      lf_idx         = 0;

LFDebris lf_debris[MAX_LF_DEBRIS];
int      lf_debrisCount = 0;

bool     lf_doingFall   = false;  // currently clearing a fall letter row by row
int      lf_fallRow     = 0;      // which row is being cleared next (counts down)
bool     lf_doingDebris = false;  // currently stepping debris for an explode letter
uint8_t  lf_stepTimer   = 0;      // shared countdown for fall-row and debris-step pacing
uint8_t  lf_letterTimer = 0;      // idle frames remaining before next letter starts

// Shift all pixels in a fall letter down by one row.
// Only flips dots that actually change state: for each column, the new value at y
// is simply the old value at y-1 (BLACK for y=0). Processing bottom-to-top ensures
// we read the unmodified old value of y-1 before we write it.
// This means a solid N-pixel column generates exactly 2 flips (top→BLACK, bottom+1→YELLOW)
// instead of 2N flips, which keeps the hardware happy and the animation smooth.
// lf_fallRow is a steps-remaining counter — call DISPLAY_HEIGHT times to clear.
void stepFallShift(LFLetter &la) {
    for (int x = (int)la.startX; x < (int)(la.startX + la.width); x++) {
        if (x < 0 || x >= DISPLAY_WIDTH) continue;
        for (int y = DISPLAY_HEIGHT - 1; y >= 0; y--) {
            bool newVal = (y > 0) ? current_buffer[x][y - 1] : BLACK;
            if (current_buffer[x][y] != newVal) {
                HardwareFlip::flipDot(x, y, newVal);
                current_buffer[x][y] = newVal;
            }
        }
    }
}

// Erase a letter's pixels in a random burst, then seed MAX_LF_DEBRIS debris pieces
// with random horizontal scatter and upward initial velocity (gravity pulls them down).
void eraseLetter_Explode(LFLetter &la) {
    // Collect lit pixels. Max possible: ~8px wide × 7 rows = 56 — buffer 60 for safety.
    uint8_t pixX[60], pixY[60];
    int count = 0;
    for (int x = (int)la.startX; x < (int)(la.startX + la.width); x++) {
        for (int y = 0; y < DISPLAY_HEIGHT; y++) {
            if (x < 0 || x >= DISPLAY_WIDTH) continue;
            if (current_buffer[x][y] && count < 60) {
                pixX[count] = (uint8_t)x;
                pixY[count] = (uint8_t)y;
                count++;
            }
        }
    }
    if (count == 0) return;

    // Shuffle pixel order for the random burst erase
    for (int i = count - 1; i > 0; i--) {
        int j = random(i + 1);
        uint8_t tx = pixX[i]; pixX[i] = pixX[j]; pixX[j] = tx;
        uint8_t ty = pixY[i]; pixY[i] = pixY[j]; pixY[j] = ty;
    }

    // Assign debris pieces from the shuffled set.
    // Wide scatter (vx ±1–3) and upward initial vy (−2 to 0) for dramatic arcs.
    lf_debrisCount = min(count, MAX_LF_DEBRIS);
    for (int d = 0; d < lf_debrisCount; d++) {
        int8_t scatter = (int8_t)(random(3) + 1);               // magnitude 1–3
        lf_debris[d].x      = pixX[d];
        lf_debris[d].y      = (int8_t)pixY[d];
        lf_debris[d].vx     = (d % 2 == 0) ? scatter : -scatter; // alternate left/right
        lf_debris[d].vy     = -(int8_t)random(3);                // 0, -1, or -2 (upward burst)
        lf_debris[d].active = true;
    }

    // Erase every pixel in shuffled order (the burst)
    for (int i = 0; i < count; i++) {
        if (current_buffer[pixX[i]][pixY[i]]) {
            HardwareFlip::flipDot(pixX[i], pixY[i], BLACK);
            current_buffer[pixX[i]][pixY[i]] = BLACK;
        }
    }

    // Redraw debris at their origin — they fly from here
    for (int d = 0; d < lf_debrisCount; d++) {
        int dx = lf_debris[d].x;
        int dy = lf_debris[d].y;
        if (dx >= 0 && dx < DISPLAY_WIDTH && dy >= 0 && dy < DISPLAY_HEIGHT) {
            if (!current_buffer[dx][dy]) {
                HardwareFlip::flipDot(dx, dy, YELLOW);
                current_buffer[dx][dy] = YELLOW;
            }
        }
    }
}

// Advance all active debris pieces one step. Gravity increments vy each step.
// Pieces that leave the display (any edge) or find their target cell occupied
// are immediately deactivated — no invisible ghosts that could cause stray flips.
// Returns true if any piece is still on screen.
bool stepDebris() {
    bool anyActive = false;
    for (int d = 0; d < lf_debrisCount; d++) {
        if (!lf_debris[d].active) continue;
        int dx = lf_debris[d].x;
        int dy = lf_debris[d].y;

        // Erase current position (only if the cell is actually lit — never flip blindly)
        if (dx >= 0 && dx < DISPLAY_WIDTH && dy >= 0 && dy < DISPLAY_HEIGHT) {
            if (current_buffer[dx][dy]) {
                HardwareFlip::flipDot(dx, dy, BLACK);
                current_buffer[dx][dy] = BLACK;
            }
        }

        // Apply gravity then move
        lf_debris[d].vy++;          // gravity: +1 per step (vy grows positive → falls)
        dx += lf_debris[d].vx;
        dy += lf_debris[d].vy;
        lf_debris[d].x = (int16_t)dx;
        lf_debris[d].y = (int8_t)dy;

        // Redraw at new position if in bounds and cell is free.
        // If the cell is occupied (another piece landed here), retire rather than
        // leaving a ghost that could erase that cell on the next pass.
        if (dx >= 0 && dx < DISPLAY_WIDTH && dy >= 0 && dy < DISPLAY_HEIGHT) {
            if (!current_buffer[dx][dy]) {
                HardwareFlip::flipDot(dx, dy, YELLOW);
                current_buffer[dx][dy] = YELLOW;
                anyActive = true;
            } else {
                lf_debris[d].active = false;
            }
        } else {
            lf_debris[d].active = false;
        }
    }
    return anyActive;
}

// Kick off the letter-fall animation for the given text.
// next_buffer must already hold the incoming content before calling.
void beginLetterFall(const String &text, Phase afterFadeIn) {
    lf_letterCount  = 0;
    lf_idx          = 0;
    lf_debrisCount  = 0;
    lf_doingFall    = false;
    lf_fallRow      = 0;
    lf_doingDebris  = false;
    lf_stepTimer    = 0;
    lf_letterTimer  = 0;
    pendingPhase    = afterFadeIn;

    // Recompute rendering start x (same centering as renderToTarget)
    int textW  = GraphicsEngine::getStringWidth(text);
    int startX = (DISPLAY_WIDTH - textW) / 2;
    if (startX < 0) startX = 0;

    // Build letter list from font advance widths
    int cursor = startX;
    int nc     = min((int)text.length(), MAX_LF_LETTERS);
    for (int i = 0; i < nc; i++) {
        char c = text[i];
        lf_letters[i].startX = cursor;
        lf_letters[i].width  = (c >= 0x20 && c <= 0x7E)
            ? (int16_t)pgm_read_byte(&Ferranti7Glyphs[c - 0x20].xAdvance)
            : 0;
        lf_letters[i].explode = (random(10) < 4);  // ~40% explode, ~60% fall
        lf_order[i] = i;
        cursor += lf_letters[i].width;
    }
    lf_letterCount = nc;

    // Fisher-Yates shuffle so letters leave in a random order
    for (int i = nc - 1; i > 0; i--) {
        int j = random(i + 1);
        int tmp = lf_order[i]; lf_order[i] = lf_order[j]; lf_order[j] = tmp;
    }

    currentPhase   = PH_LETTER_FALL;
    phaseStartTime = millis();
    lastFrameTime  = millis();
    Serial.print("[LETTER_FALL] Starting: ");
    Serial.print(nc);
    Serial.println(" letters");
}


// ==========================================
// SHARED HELPERS
// ==========================================

// Render text centered into the hidden next_buffer — no dot movement
void renderToTarget(const String &text) {
    GraphicsEngine::clearBuffer();
    int textW  = GraphicsEngine::getStringWidth(text);
    int startX = (DISPLAY_WIDTH - textW) / 2;
    if (startX < 0) startX = 0;
    GraphicsEngine::drawString(startX, 0, text);
}

// Validate destination and render transit layout into next_buffer — no dot movement.
// Destination is left-aligned; time is right-aligned.
// Returns false if destination is not ASHMONT or BRAINTREE (skip transit phase).
bool renderTransitToTarget(const String &destination, const String &timeStr) {
    if (destination != "ASHMONT" && destination != "BRAINTREE" && destination != "INBOUND") {
        Serial.print("[TRANSIT] Unexpected destination '");
        Serial.print(destination);
        Serial.println("' — skipping");
        return false;
    }
    GraphicsEngine::clearBuffer();
    GraphicsEngine::drawString(0, 0, destination);
    int timeW = GraphicsEngine::getStringWidth(timeStr);
    GraphicsEngine::drawString(DISPLAY_WIDTH - timeW, 0, timeStr);
    return true;
}

// Catalog every dot that disagrees with next_buffer
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

// Kick off a noise fade-out → fade-in → afterFadeIn sequence
void beginFadeOut(Phase afterFadeIn) {
    pendingPhase   = afterFadeIn;
    currentPhase   = PH_FADE_OUT;
    phaseStartTime = millis();
    lastFrameTime  = millis();
}


// ==========================================
// SETUP
// ==========================================

void setup() {
    Serial.begin(9600);
    randomSeed(analogRead(A5));

    Serial.println("=== FLIP-DOT BOOT ===");

    // 1. Keep 24V off — dots physically cannot move while relay is LOW
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    Serial.println("[SETUP] Relay LOW — 24V off");

    // 2. Init hardware (relay still LOW — safe)
    HardwareFlip::begin();
    GraphicsEngine::begin();
    HardwareLED::setEnabled(false);
    Serial.println("[SETUP] Hardware initialized");

    // 3. Connect to WiFi — relay is LOW, no dot movement possible
    Serial.println("[SETUP] Connecting to WiFi...");
    WiFiManager::begin();

    // 4. Fetch the first quote — relay still LOW, no dot movement possible.
    //    Retry indefinitely; we must have content before the display powers on.
    Serial.println("[SETUP] Fetching initial quote...");
    while (!QuoteAPI::fetch(currentQuote)) {
        Serial.println("[SETUP] Quote fetch failed — retrying in 5s");
        delay(5000);
    }
    Serial.print("[SETUP] Initial quote: ");
    Serial.println(currentQuote);

    // 5. Power on the display now that we have content ready
    delay(1000);
    digitalWrite(PIN_RELAY, HIGH);
    Serial.println("[SETUP] Relay HIGH — 24V live");
    delay(500);

    // 6. Wipe and show the fetched quote
    Serial.println("[SETUP] Sweep wipe + rendering initial quote");
    GraphicsEngine::sweepWipe();
    renderToTarget(currentQuote);
    GraphicsEngine::render();
    currentPhase   = PH_DISPLAY_QUOTE;
    phaseStartTime = millis();
    Serial.println("[SETUP] Done — entering display loop");
    Serial.println("=====================");
}


// ==========================================
// LOOP
// ==========================================

void loop() {
    unsigned long now = millis();

    // ==========================================
    // PHASE: DISPLAY QUOTE (5 minutes)
    // ==========================================
    if (currentPhase == PH_DISPLAY_QUOTE) {
        if (now - phaseStartTime >= QUOTE_DISPLAY_MS) {
            Serial.println("[QUOTE] Display time elapsed — fetching next content");
            currentPhase = PH_FETCHING_BOTH;
        }
    }

    // ==========================================
    // PHASE: FETCH BOTH APIs
    // All network activity happens here — display is frozen, no dot movement.
    //
    // 1. Transit: one attempt, best-effort (failure skips the transit phase).
    // 2. Quote:   retry until success.
    //
    // After both are resolved, stage the first incoming frame in next_buffer
    // and kick off the letter-fall animation. No network calls after this.
    // ==========================================
    else if (currentPhase == PH_FETCHING_BOTH) {
        Serial.println("[FETCH] Starting fetch — display frozen");
        WiFiManager::ensureConnected();

        // --- Transit (best-effort, one try — up to 2 trains as fallback) ---
        // Only ASHMONT, BRAINTREE, or INBOUND are valid; anything else skips the phase.
        // If the first train has an unrecognised destination, the second is tried.
        Serial.println("[FETCH] Requesting MBTA inbound predictions...");
        String transitDests[2], transitTimes[2];
        bool transitReady = false;
        int trainCount = TransitAPI::fetchPredictions(1, transitDests, transitTimes, 2);
        Serial.print("[FETCH] Transit predictions received: ");
        Serial.println(trainCount);
        for (int t = 0; t < trainCount && !transitReady; t++) {
            Serial.print("[FETCH] Train ");
            Serial.print(t + 1);
            Serial.print(": ");
            Serial.print(transitDests[t]);
            Serial.print("  ");
            Serial.println(transitTimes[t]);
            transitReady = renderTransitToTarget(transitDests[t], transitTimes[t]);
            if (transitReady) Serial.println("[FETCH] Transit layout staged in next_buffer");
        }
        if (!transitReady) Serial.println("[FETCH] Transit fetch failed or no valid destination — skipping transit phase");

        // --- Quote (retry until success) ---
        Serial.println("[FETCH] Requesting next quote...");
        nextQuote = "";
        int quoteAttempt = 0;
        while (!QuoteAPI::fetch(nextQuote)) {
            quoteAttempt++;
            Serial.print("[FETCH] Quote fetch failed (attempt ");
            Serial.print(quoteAttempt);
            Serial.println(") — retrying in 5s");
            delay(5000);
            WiFiManager::ensureConnected();
        }
        Serial.print("[FETCH] Quote ready: ");
        Serial.println(nextQuote);

        // --- Route: transit first (if available), otherwise go straight to new quote ---
        if (transitReady) {
            // next_buffer already holds the transit layout; animate the old quote out
            Serial.println("[FETCH] Route: QUOTE → TRANSIT → QUOTE");
            beginLetterFall(currentQuote, PH_DISPLAY_TRANSIT);
        } else {
            // Stage blank screen in next_buffer — typewriter will type the quote after fall
            Serial.println("[FETCH] Route: QUOTE → TYPEWRITER (transit skipped)");
            String oldQuote = currentQuote;
            currentQuote = nextQuote;
            GraphicsEngine::clearBuffer();  // next_buffer = blank; typewriter types onto it
            beginLetterFall(oldQuote, PH_TYPEWRITER);
        }
    }

    // ==========================================
    // PHASE: LETTER FALL
    // Letters of the current quote leave the display one at a time in random order.
    //   Fall  (~60%): pixels erase bottom-row-first (looks like the letter falls away)
    //   Explode (~40%): pixels pop off randomly, then a few debris dots fall downward
    // No network calls. Dot movement only.
    // ==========================================
    else if (currentPhase == PH_LETTER_FALL) {
        if (now - lastFrameTime >= FRAME_DT) {
            lastFrameTime = now;

            if (lf_letterTimer > 0) {
                // Random idle gap between letters — count down
                lf_letterTimer--;

            } else if (lf_doingFall) {
                // Clear one row of the current fall letter every LF_FALL_STEP_FRAMES
                lf_stepTimer++;
                if (lf_stepTimer >= LF_FALL_STEP_FRAMES) {
                    lf_stepTimer = 0;
                    LFLetter &la = lf_letters[lf_order[lf_idx]];
                    stepFallShift(la);
                    if (--lf_fallRow <= 0) {
                        // All rows shifted off — gap then next letter
                        lf_doingFall   = false;
                        lf_letterTimer = (uint8_t)random(LF_GAP_MIN_FRAMES, LF_GAP_MAX_FRAMES + 1);
                        lf_idx++;
                    }
                }

            } else if (lf_doingDebris) {
                // Step debris pieces every LF_DEBRIS_STEP_FRAMES (~75 ms/step)
                lf_stepTimer++;
                if (lf_stepTimer >= LF_DEBRIS_STEP_FRAMES) {
                    lf_stepTimer = 0;
                    bool anyLeft = stepDebris();
                    if (!anyLeft) {
                        lf_doingDebris = false;
                        lf_letterTimer = (uint8_t)random(LF_GAP_MIN_FRAMES, LF_GAP_MAX_FRAMES + 1);
                        lf_idx++;
                    }
                }

            } else if (lf_idx < lf_letterCount) {
                // Start the next letter in the shuffled order
                LFLetter &la = lf_letters[lf_order[lf_idx]];
                if (la.width > 0) {
                    if (la.explode) {
                        lf_stepTimer = 0;
                        eraseLetter_Explode(la);
                        lf_doingDebris = (lf_debrisCount > 0);
                        if (!lf_doingDebris) {
                            // No lit pixels / no debris — skip straight to gap
                            lf_letterTimer = (uint8_t)random(LF_GAP_MIN_FRAMES, LF_GAP_MAX_FRAMES + 1);
                            lf_idx++;
                        }
                    } else {
                        // Start fall — lf_fallRow counts down from DISPLAY_HEIGHT (7 steps)
                        lf_fallRow   = DISPLAY_HEIGHT;
                        lf_stepTimer = 0;
                        lf_doingFall = true;
                        // lf_idx advances when the last row is cleared
                    }
                } else {
                    lf_idx++;  // skip zero-width characters (spaces)
                }

            } else {
                // All letters gone — settle the new content in
                Serial.println("[LETTER_FALL] Complete → FADE_IN");
                buildTodo();
                currentPhase  = PH_FADE_IN;
                lastFrameTime = now;
            }
        }
    }

    // ==========================================
    // PHASE: DISPLAY TRANSIT (20 seconds)
    // nextQuote is already staged — no network calls needed.
    // ==========================================
    else if (currentPhase == PH_DISPLAY_TRANSIT) {
        if (now - phaseStartTime >= TRANSIT_DISPLAY_MS) {
            Serial.println("[TRANSIT] Display time elapsed — fading to black, then typewriter");
            currentQuote = nextQuote;
            GraphicsEngine::clearBuffer();  // next_buffer = all black → fade to blank screen
            beginFadeOut(PH_TYPEWRITER);
        }
    }

    // ==========================================
    // PHASE: FADE OUT (Escalating Random Noise)
    // Used for transit → quote transition only.
    // No network calls. Dot movement only.
    // ==========================================
    else if (currentPhase == PH_FADE_OUT) {
        if (now - lastFrameTime >= FRAME_DT) {
            lastFrameTime = now;
            float t = float(now - phaseStartTime) / FADE_OUT_MS;
            if (t > 1.0) t = 1.0;

            // Exponential curve: starts as a trickle, ends as a storm
            int maxFlips       = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 10;
            int flipsThisFrame = 1 + int(pow(t, 4) * maxFlips);

            for (int i = 0; i < flipsThisFrame; i++) {
                int  rx = random(DISPLAY_WIDTH);
                int  ry = random(DISPLAY_HEIGHT);
                bool rc = random(2);

                if (current_buffer[rx][ry] != rc) {
                    HardwareFlip::flipDot(rx, ry, rc);
                    current_buffer[rx][ry] = rc;
                }
            }
        }

        if (now - phaseStartTime >= FADE_OUT_MS) {
            buildTodo();
            currentPhase  = PH_FADE_IN;
            lastFrameTime = now;
        }
    }

    // ==========================================
    // PHASE: FADE IN (Settle dots into target content)
    // No network calls. Dot movement only.
    // ==========================================
    else if (currentPhase == PH_FADE_IN) {
        if (todoCnt > 0 && now - lastFrameTime >= FRAME_DT) {
            lastFrameTime = now;

            int flipsThisFrame = 1 + (todoCnt / 15);

            for (int i = 0; i < flipsThisFrame && todoCnt > 0; i++) {
                int   idx         = random(todoCnt);
                Point p           = todo[idx];
                bool  targetColor = next_buffer[p.x][p.y];

                if (current_buffer[p.x][p.y] != targetColor) {
                    HardwareFlip::flipDot(p.x, p.y, targetColor);
                    current_buffer[p.x][p.y] = targetColor;
                }

                // Remove from to-do list by swapping with last element
                todo[idx] = todo[--todoCnt];
            }
        }

        if (todoCnt == 0) {
            if (pendingPhase == PH_TYPEWRITER) {
                // Compute centered start x for the quote, reset typewriter cursor
                int textW  = GraphicsEngine::getStringWidth(currentQuote);
                int startX = (DISPLAY_WIDTH - textW) / 2;
                if (startX < 0) startX = 0;
                tw_charIdx      = 0;
                tw_cursorX      = startX;
                tw_nextCharTime = millis() + (unsigned long)random(TW_MIN_MS, TW_MAX_MS + 1);
                GraphicsEngine::clearBuffer();  // keep next_buffer clean for drawChar
                Serial.println("[FADE_IN] Complete → TYPEWRITER");
            }
            currentPhase   = pendingPhase;
            phaseStartTime = millis();
        }
    }

    // ==========================================
    // PHASE: TYPEWRITER (type quote letter by letter onto blank screen)
    // No network calls. Dot movement only.
    // ==========================================
    else if (currentPhase == PH_TYPEWRITER) {
        if (tw_charIdx < (int)currentQuote.length()) {
            if (millis() >= tw_nextCharTime) {
                char c       = currentQuote[tw_charIdx];
                int  advance = GraphicsEngine::drawChar(tw_cursorX, 6, c);

                // Flip any newly lit pixels in the character's column range
                for (int x = tw_cursorX; x < tw_cursorX + advance && x < DISPLAY_WIDTH; x++) {
                    if (x < 0) continue;
                    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                        if (next_buffer[x][y] && !current_buffer[x][y]) {
                            HardwareFlip::flipDot(x, y, YELLOW);
                            current_buffer[x][y] = YELLOW;
                        }
                    }
                }

                tw_cursorX += advance;
                tw_charIdx++;

                // Check if the NEXT character starts a new word (current char was a space,
                // or this is the very first character). If so, maybe take a thinking pause.
                bool nextIsWordStart = (tw_charIdx < (int)currentQuote.length()) &&
                                       (tw_charIdx == 0 || c == ' ');
                unsigned long delay_ms;
                if (nextIsWordStart && (int)random(100) < TW_THINK_CHANCE) {
                    delay_ms = (unsigned long)random(TW_THINK_MIN_MS, TW_THINK_MAX_MS + 1);
                    Serial.print("[TYPEWRITER] thinking pause ");
                    Serial.print(delay_ms);
                    Serial.println("ms");
                } else {
                    delay_ms = (unsigned long)random(TW_MIN_MS, TW_MAX_MS + 1);
                }
                tw_nextCharTime = millis() + delay_ms;
                Serial.print("[TYPEWRITER] '");
                Serial.print(c);
                Serial.print("'  x=");
                Serial.println(tw_cursorX);
            }
        } else {
            // All characters typed — quote fully on screen
            Serial.println("[TYPEWRITER] Complete → DISPLAY_QUOTE");
            currentPhase   = PH_DISPLAY_QUOTE;
            phaseStartTime = millis();
        }
    }
}
