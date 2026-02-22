#include "HardwareLED.h"

namespace HardwareLED {

    // Internal state tracking
    bool isEnabled = true;

    void begin() {
        pinMode(LED_DATA_PIN, OUTPUT);
        pinMode(LED_CLK_PIN, OUTPUT);
        pinMode(LED_LATCH_PIN, OUTPUT);
        pinMode(LED_OE_PIN, OUTPUT);
        pinMode(LED_ROW_A, OUTPUT);
        pinMode(LED_ROW_B, OUTPUT);
        pinMode(LED_ROW_C, OUTPUT);
        
        digitalWrite(LED_LATCH_PIN, HIGH);
        
        // Start with LEDs OFF until explicitly enabled
        digitalWrite(LED_OE_PIN, HIGH); 
    }

    void setEnabled(bool state) {
        isEnabled = state;
        if (!isEnabled) {
            // Instantly blank the screen if turned off.
            // Stopping the scan will also intentionally starve the watchdog timer.
            digitalWrite(LED_OE_PIN, HIGH);
        }
    }

    void scan() {
        if (!isEnabled) return;

        for (int software_row = 0; software_row < 7; software_row++) {
            
            int hw_row = 6 - software_row;
            int mapped_y = (DISPLAY_HEIGHT - 1) - software_row;
            
            // ==========================================
            // SHIELD THE SHIFT REGISTERS FROM WIFI INTERRUPTS
            // ==========================================
            noInterrupts(); 
            
            digitalWrite(LED_OE_PIN, HIGH); 
            digitalWrite(LED_LATCH_PIN, LOW);
            
            digitalWrite(LED_DATA_PIN, LOW);
            digitalWrite(LED_CLK_PIN, HIGH); digitalWrite(LED_CLK_PIN, LOW);
            digitalWrite(LED_CLK_PIN, HIGH); digitalWrite(LED_CLK_PIN, LOW);
            
            for (int x = 0; x < DISPLAY_WIDTH; x++) {
                int mapped_x = (DISPLAY_WIDTH - 1) - x;
                bool pixelState = current_buffer[mapped_x][mapped_y]; 
                
                digitalWrite(LED_DATA_PIN, pixelState ? HIGH : LOW);
                digitalWrite(LED_CLK_PIN, HIGH);
                digitalWrite(LED_CLK_PIN, LOW);
            }

            digitalWrite(LED_LATCH_PIN, HIGH);
            
            digitalWrite(LED_ROW_A, hw_row & 1);
            digitalWrite(LED_ROW_B, (hw_row & 2) >> 1);
            digitalWrite(LED_ROW_C, (hw_row & 4) >> 2);
            
            digitalWrite(LED_OE_PIN, LOW); 
            
            // Re-enable interrupts so the WiFi chip can breathe!
            interrupts(); 
            // ==========================================
            
            delayMicroseconds(200); 
        }
    }

} // end namespace