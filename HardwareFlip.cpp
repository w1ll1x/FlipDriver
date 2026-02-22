#include "HardwareFlip.h"

namespace HardwareFlip {

    // --- Private Helper Function ---
    // This is hidden inside the namespace. Only flipDot() can use it.
    void pulseLatch(int pin) {
        digitalWrite(pin, HIGH);
        delayMicroseconds(5);
        digitalWrite(pin, LOW);
    }

    // --- Initialization ---
    void begin() {
        pinMode(PIN_POLARITY, OUTPUT);
        pinMode(PIN_COL_DATA, OUTPUT); 
        pinMode(PIN_COL_CLK, OUTPUT); 
        pinMode(PIN_COL_LAT, OUTPUT);
        pinMode(PIN_ROW_DATA, OUTPUT); 
        pinMode(PIN_ROW_CLK, OUTPUT); 
        pinMode(PIN_ROW_LAT, OUTPUT);
        
        // CRITICAL SAFETY: Ensure fire pin starts HIGH (OFF)
        pinMode(PIN_FIRE, OUTPUT);
        digitalWrite(PIN_FIRE, HIGH); 
    }

    // --- Core Firing Logic ---
    void flipDot(int x, int y, bool color) {
        // Bounds checking
        if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;

        // ==========================================
        // 180-DEGREE FLIP FIX
        // Invert X and Y to rotate the entire display!
        // ==========================================
        x = (DISPLAY_WIDTH - 1) - x;
        y = (DISPLAY_HEIGHT - 1) - y;

        // 1. Calculate the addressing for 3 daisy-chained boards
        int target_board_id = (NUM_MODULES - 1) - (x / MODULE_WIDTH); 
        int local_col = x % MODULE_WIDTH;

        int hw_col = (MODULE_WIDTH - 1) - local_col;
        int hw_row = (MODULE_HEIGHT - 1) - y;

        // 2. Set the Polarity (Yellow vs Black)
        digitalWrite(PIN_POLARITY, color);

        // 3. Shift the Column Data
        for (int m = NUM_MODULES - 1; m >= 0; m--) {
            if (m == target_board_id) {
                shiftOut(PIN_COL_DATA, PIN_COL_CLK, MSBFIRST, hw_col);
            } else {
                shiftOut(PIN_COL_DATA, PIN_COL_CLK, MSBFIRST, NULL_ADDR);
            }
        }
        pulseLatch(PIN_COL_LAT);

        // 4. Shift the Row Data
        shiftOut(PIN_ROW_DATA, PIN_ROW_CLK, MSBFIRST, hw_row);
        pulseLatch(PIN_ROW_LAT);

        // Allow multiplexers to settle
        delayMicroseconds(100); 

        // ==========================================
        // STRICT HARDWARE SAFETY LOCK
        // Disable all OS/WiFi interrupts to guarantee 
        // the 1ms fire pulse is NEVER stretched.
        // ==========================================
        noInterrupts();
        digitalWrite(PIN_FIRE, LOW);
        delayMicroseconds(1000); 
        digitalWrite(PIN_FIRE, HIGH);
        interrupts();
        // ==========================================

        // Wait 1ms for mechanics to physically settle.
        // We use the custom safeDelay so the LEDs stay lit while we wait!
        safeDelay(1); 
    }

} // end namespace