#include "GraphicsEngine.h"
#include "HardwareFlip.h"
#include "HardwareLED.h"

// Define the physical memory buffers here in global space
bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];
bool next_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];

// ==========================================
// THE SYNCHRONIZATION ENGINE
// ==========================================
// This function replaces standard delay(). It allows the Arduino to wait 
// for mechanical dots to settle while continuously keeping the LEDs alive!
void safeDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        HardwareLED::scan(); 
    }
}

namespace GraphicsEngine {

    void begin() {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                current_buffer[x][y] = BLACK;
                next_buffer[x][y] = BLACK;
            }
        }
    }

    // ==========================================
    // DRAWING LOGIC (Writes to hidden next_buffer)
    // ==========================================
    void clearBuffer() {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                next_buffer[x][y] = BLACK;
            }
        }
    }

    void drawPixel(int x, int y, bool color) {
        if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < DISPLAY_HEIGHT) {
            next_buffer[x][y] = color;
        }
    }

    int drawChar(int x, int y, char c) {
        if (c < 0x20 || c > 0x7E) return 0;

        int index = c - 0x20;
        uint16_t bitmapOffset = pgm_read_word(&Ferranti7Glyphs[index].bitmapOffset);
        uint8_t  w            = pgm_read_byte(&Ferranti7Glyphs[index].width);
        uint8_t  h            = pgm_read_byte(&Ferranti7Glyphs[index].height);
        uint8_t  xAdv         = pgm_read_byte(&Ferranti7Glyphs[index].xAdvance);
        int8_t   xOff         = pgm_read_byte(&Ferranti7Glyphs[index].xOffset);
        int8_t   yOff         = pgm_read_byte(&Ferranti7Glyphs[index].yOffset);

        uint8_t bits = 0, bit = 0;

        for (int yy = 0; yy < h; yy++) {
            for (int xx = 0; xx < w; xx++) {
                if (!(bit++ & 7)) {
                    bits = pgm_read_byte(&Ferranti7Bitmaps[bitmapOffset++]);
                }
                if (bits & 0x80) {
                    drawPixel(x + xx + xOff, y + yy + yOff, YELLOW);
                }
                bits <<= 1; 
            }
        }
        return xAdv;
    }

    int getStringWidth(String text) {
        int width = 0;
        for (int i = 0; i < text.length(); i++) {
            char c = text[i];
            if (c >= 0x20 && c <= 0x7E) {
                width += pgm_read_byte(&Ferranti7Glyphs[c - 0x20].xAdvance);
            }
        }
        return width;
    }

    void drawString(int x, int y, String text) {
        int cursor_x = x;
        int baseline_y = y + 6; 
        for (int i = 0; i < text.length(); i++) {
            int advance = drawChar(cursor_x, baseline_y, text[i]);
            cursor_x += advance;
        }
    }

    void showStaticMessage(String leftText, String rightText) {
        clearBuffer();
        drawString(0, 0, leftText);
        
        if (rightText.length() > 0) {
            int textW = getStringWidth(rightText);
            int startX = DISPLAY_WIDTH - textW; 
            drawString(startX, 0, rightText); 
        }
        render(); // Push to physical hardware
    }

    // ==========================================
    // HARDWARE EXECUTION
    // ==========================================
    
    // Smart flip: Only touches dots that need to change
    void render() {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                if (next_buffer[x][y] != current_buffer[x][y]) {
                    HardwareFlip::flipDot(x, y, next_buffer[x][y]);
                    current_buffer[x][y] = next_buffer[x][y];
                }
            }
        }
    }

    void clearSign() {
        clearBuffer(); 
        render();                 
    }

    // The iconic column-by-column mechanical wipe
    void sweepWipe() {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
            for (int y = 0; y < DISPLAY_HEIGHT; y++) {
                HardwareFlip::flipDot(x, y, BLACK);
                current_buffer[x][y] = BLACK; // LEDs turn off instantly as the dot flips
            }
            // 15ms power recovery delay between columns to ensure strong flips
            safeDelay(15); 
        }
    }

} // end namespace