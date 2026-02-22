#ifndef HARDWARE_FLIP_H
#define HARDWARE_FLIP_H

#include <Arduino.h>
#include "Config.h"

// This tells the compiler that 'safeDelay' exists elsewhere in the project.
// We will define it in the main .ino file so it can scan the LEDs while waiting!
extern void safeDelay(unsigned long ms);

// We use a namespace to keep these functions neatly organized.
namespace HardwareFlip {
    
    // Call this once in setup() to configure the pins
    void begin();

    // The core function to move a single mechanical dot
    void flipDot(int x, int y, bool color);

}

#endif // HARDWARE_FLIP_H