#ifndef HARDWARE_LED_H
#define HARDWARE_LED_H

#include <Arduino.h>
#include "Config.h"

// We tell the LED hardware to look for this memory buffer, 
// which we will define later in the Graphics Engine.
extern bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];

namespace HardwareLED {

    // Setup the LED pins
    void begin();

    // The core multiplexing loop. Must be called constantly!
    void scan();

    // Turn the LED layer ON or OFF completely
    void setEnabled(bool state);

}

#endif // HARDWARE_LED_H