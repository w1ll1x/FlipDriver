#ifndef EYES_ANIMATION_H
#define EYES_ANIMATION_H

namespace Eyes {
    void begin();  // Draw initial eyes — call once after hardware init
    void tick();   // Drive the state machine — call every loop()
}

#endif // EYES_ANIMATION_H
