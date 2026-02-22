#ifndef GRAPHICS_ENGINE_H
#define GRAPHICS_ENGINE_H

#include <Arduino.h>
#include "Config.h"
#include "Ferranti7.h" 

// This declares the actual memory buffer that HardwareLED.h is looking for via 'extern'
extern bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];

// Expose the safe delay so HardwareFlip can use it
extern void safeDelay(unsigned long ms);

namespace GraphicsEngine {

    // Initialize the buffers
    void begin();

    // High-level drawing commands (these draw to the hidden next_buffer)
    void clearBuffer();
    void drawPixel(int x, int y, bool color);
    int drawChar(int x, int y, char c);
    int getStringWidth(String text);
    void drawString(int x, int y, String text);
    void showStaticMessage(String leftText, String rightText);

    // Hardware Execution Commands
    void render();       // Compares next_buffer to current_buffer and flips the differences
    void clearSign();    // Smart clear (only flips yellow dots to black)
    void sweepWipe();    // Iconic physical wipe from left to right

}

#endif // GRAPHICS_ENGINE_H