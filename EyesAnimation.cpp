// ==========================================
// EYES ANIMATION
// Target: Porter Square 90x7 Flip-Dot Sign
//
// Each eye: 12 wide x 7 tall, almond/lemon shape
//   y0: oooxxxxxxooo   (x+3..x+8, 6px top cap)
//   y1: ooxoooooooxo   (x+2, x+9)
//   y2: oxooooooooox   (x+1, x+10)
//   y3: xooooooooooox  (x+0, x+11 — single pixel at each end)
//   y4: oxooooooooox   (x+1, x+10)
//   y5: ooxoooooooxo   (x+2, x+9)
//   y6: oooxxxxxxooo   (x+3..x+8, 6px bottom cap)
//
// Eyes centered on 90px display:
//   Left eye:  x=28..39  (12px)
//   Right eye: x=50..61  (12px)
//   Gap: 10px,  margins: 28px each side
//
// Pupil: 3x3, valid top-left (px, py) relative to eyeX:
//   py=1 (rows 1-3): px in [3..6]   (4 x-positions)
//   py=2 (rows 2-4): px in [2..7]   (6 x-positions)
//   py=3 (rows 3-5): px in [3..6]   (4 x-positions)
// ==========================================

#include "EyesAnimation.h"
#include "Config.h"
#include "HardwareFlip.h"
#include "HardwareLED.h"

extern bool current_buffer[DISPLAY_WIDTH][DISPLAY_HEIGHT];

// ==========================================
// CONSTANTS
// ==========================================

static const int LEFT_EYE_X  = 28;
static const int RIGHT_EYE_X = 50;
static const int PUPIL_SIZE  = 3;

static const unsigned long MOVE_STEP_MS   = 70;
static const unsigned long SQUINT_MOVE_MS = 140;
static const unsigned long WINK_MOVE_MS   = 100;
static const unsigned long IDLE_MIN_MS    = 800;
static const unsigned long IDLE_MAX_MS    = 4000;
static const int           WINK_CENTER_PX = 4;
static const int           WINK_CENTER_PY = 2;

// ==========================================
// STATE
// ==========================================

static int pupilPX  = 4;
static int pupilPY  = 2;
static int targetPX = 4;
static int targetPY = 2;

enum EyeState {
    EYE_IDLE,
    EYE_MOVING,
    EYE_BLINK_CLOSE,
    EYE_BLINK_OPEN,
    EYE_SQUINT_CLOSE,
    EYE_SQUINT_LOOK,
    EYE_SQUINT_OPEN,
    EYE_WINK_CENTER,
    EYE_WINK_CLOSE,
    EYE_WINK_OPEN
};
static EyeState eyeState = EYE_IDLE;

static unsigned long stateTimer  = 0;
static int           animStep    = 0;
static unsigned long animStepMs  = 45;
static int           squintDepth = 1;
static int           squintSides = 0;
static int           winkEyeX   = LEFT_EYE_X;

// ==========================================
// LOW-LEVEL DRAWING
// ==========================================

static void setDot(int x, int y, bool color) {
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) return;
    if (current_buffer[x][y] != color) {
        HardwareFlip::flipDot(x, y, color);
        current_buffer[x][y] = color;
    }
}

// ==========================================
// EYE OUTLINE
// ==========================================

static void drawEyeOutline(int ex) {
    for (int x = ex+3; x <= ex+8; x++) {
        setDot(x, 0, YELLOW);
        setDot(x, 6, YELLOW);
    }
    setDot(ex+2, 1, YELLOW);  setDot(ex+9,  1, YELLOW);
    setDot(ex+2, 5, YELLOW);  setDot(ex+9,  5, YELLOW);
    setDot(ex+1, 2, YELLOW);  setDot(ex+10, 2, YELLOW);
    setDot(ex+1, 4, YELLOW);  setDot(ex+10, 4, YELLOW);
    setDot(ex+0, 3, YELLOW);  setDot(ex+11, 3, YELLOW);
}

// ==========================================
// INTERIOR HELPERS
// ==========================================

static bool interiorBounds(int y, int &lo, int &hi) {
    if      (y == 1 || y == 5) { lo = 3; hi = 8;  return true; }
    else if (y == 2 || y == 4) { lo = 2; hi = 9;  return true; }
    else if (y == 3)           { lo = 1; hi = 10; return true; }
    return false;
}

static void fillInteriorRow(int ex, int y, bool color) {
    int lo, hi;
    if (!interiorBounds(y, lo, hi)) return;
    for (int x = ex + lo; x <= ex + hi; x++) setDot(x, y, color);
}

static void restoreInteriorRow(int ex, int y) {
    int lo, hi;
    if (!interiorBounds(y, lo, hi)) return;
    for (int ix = lo; ix <= hi; ix++) {
        bool inPupil  = (ix >= pupilPX && ix < pupilPX + PUPIL_SIZE &&
                         y  >= pupilPY && y  < pupilPY + PUPIL_SIZE);
        bool isCenter = (ix == pupilPX + 1 && y == pupilPY + 1);
        setDot(ex + ix, y, (inPupil && !isCenter) ? YELLOW : BLACK);
    }
}

// ==========================================
// PUPIL
// ==========================================

static void pupilXBounds(int py, int &lo, int &hi) {
    if (py == 2) { lo = 2; hi = 7; }
    else         { lo = 3; hi = 6; }
}

static int clampPX(int px, int py) {
    int lo, hi;
    pupilXBounds(py, lo, hi);
    return constrain(px, lo, hi);
}

static void drawPupil(int ex, int px, int py, bool color) {
    for (int dx = 0; dx < PUPIL_SIZE; dx++)
        for (int dy = 0; dy < PUPIL_SIZE; dy++)
            setDot(ex + px + dx, py + dy, color);
    if (color == YELLOW)
        setDot(ex + px + 1, py + 1, BLACK);
}

static void updatePupil(int ex, int oldPX, int oldPY, int newPX, int newPY) {
    int oax = ex + oldPX, nax = ex + newPX;
    for (int dx = 0; dx < PUPIL_SIZE; dx++) {
        for (int dy = 0; dy < PUPIL_SIZE; dy++) {
            int x = oax + dx, y = oldPY + dy;
            bool covered = (x >= nax && x < nax + PUPIL_SIZE &&
                            y >= newPY && y < newPY + PUPIL_SIZE);
            if (!covered) setDot(x, y, BLACK);
        }
    }
    drawPupil(ex, newPX, newPY, YELLOW);
}

// ==========================================
// SQUINT HELPERS
// ==========================================

static bool squintRowCovered(int y) {
    return (y <= squintDepth) || (y >= 6 - squintDepth);
}

static void updatePupilSquint(int ex, int oldPX, int oldPY, int newPX, int newPY) {
    int oax = ex + oldPX, nax = ex + newPX;
    for (int dx = 0; dx < PUPIL_SIZE; dx++) {
        for (int dy = 0; dy < PUPIL_SIZE; dy++) {
            int x = oax + dx, y = oldPY + dy;
            if (squintRowCovered(y)) continue;
            bool covered = (x >= nax && x < nax + PUPIL_SIZE && y >= newPY && y < newPY + PUPIL_SIZE);
            if (!covered) setDot(x, y, BLACK);
        }
    }
    for (int dx = 0; dx < PUPIL_SIZE; dx++) {
        for (int dy = 0; dy < PUPIL_SIZE; dy++) {
            int y = newPY + dy;
            if (squintRowCovered(y)) continue;
            bool isCenter = (dx == 1 && dy == 1);
            setDot(ex + newPX + dx, y, isCenter ? BLACK : YELLOW);
        }
    }
}

static void drawSquintStep(int ex, int step) {
    fillInteriorRow(ex, step + 1, YELLOW);
    fillInteriorRow(ex, 5 - step, YELLOW);
}

static void restoreSquintStep(int ex, int step) {
    restoreInteriorRow(ex, step + 1);
    restoreInteriorRow(ex, 5 - step);
}

// ==========================================
// BLINK HELPERS
// ==========================================

static void drawBlinkStep(int ex, int step) {
    if (step == 0) {
        fillInteriorRow(ex, 1, YELLOW);
        fillInteriorRow(ex, 5, YELLOW);
    } else if (step == 1) {
        fillInteriorRow(ex, 2, YELLOW);
        fillInteriorRow(ex, 4, YELLOW);
    } else {
        fillInteriorRow(ex, 3, YELLOW);
    }
}

static void restoreBlinkStep(int ex, int step) {
    if (step == 0) {
        restoreInteriorRow(ex, 1);
        restoreInteriorRow(ex, 5);
    } else if (step == 1) {
        restoreInteriorRow(ex, 2);
        restoreInteriorRow(ex, 4);
    } else {
        restoreInteriorRow(ex, 3);
    }
}

static void redrawEye(int ex, int px, int py) {
    for (int y = 1; y <= 5; y++) {
        int lo, hi;
        if (!interiorBounds(y, lo, hi)) continue;
        for (int ix = lo; ix <= hi; ix++) {
            bool inPupil  = (ix >= px && ix < px + PUPIL_SIZE &&
                             y  >= py && y  < py + PUPIL_SIZE);
            bool isCenter = (ix == px + 1 && y == py + 1);
            setDot(ex + ix, y, (inPupil && !isCenter) ? YELLOW : BLACK);
        }
    }
}

// ==========================================
// STATE TRANSITIONS
// ==========================================

static void startIdle() {
    eyeState   = EYE_IDLE;
    stateTimer = millis() + random(IDLE_MIN_MS, IDLE_MAX_MS + 1);
}

static void startMove() {
    int newPX, newPY, lo, hi;
    do {
        newPY = random(3) + 1;
        pupilXBounds(newPY, lo, hi);
        newPX = random(lo, hi + 1);
    } while (newPX == pupilPX && newPY == pupilPY);
    targetPX   = newPX;
    targetPY   = newPY;
    eyeState   = EYE_MOVING;
    stateTimer = millis() + MOVE_STEP_MS;
}

static void startBlink() {
    animStepMs = random(35, 65);
    eyeState   = EYE_BLINK_CLOSE;
    animStep   = 0;
    stateTimer = millis() + animStepMs;
}

static void startSquint() {
    animStepMs  = random(50, 80);
    squintDepth = 1;
    squintSides = 2;
    eyeState    = EYE_SQUINT_CLOSE;
    animStep    = 0;
    stateTimer  = millis() + animStepMs;
}

static void startWink() {
    animStepMs = random(90, 130);
    winkEyeX   = (random(2) == 0) ? LEFT_EYE_X : RIGHT_EYE_X;
    targetPX   = WINK_CENTER_PX;
    targetPY   = WINK_CENTER_PY;
    if (pupilPX == WINK_CENTER_PX && pupilPY == WINK_CENTER_PY) {
        eyeState   = EYE_WINK_CLOSE;
        animStep   = 0;
        stateTimer = millis() + animStepMs;
    } else {
        eyeState   = EYE_WINK_CENTER;
        stateTimer = millis() + WINK_MOVE_MS;
    }
}

// ==========================================
// PUBLIC API
// ==========================================

void Eyes::begin() {
    drawEyeOutline(LEFT_EYE_X);
    drawEyeOutline(RIGHT_EYE_X);
    drawPupil(LEFT_EYE_X,  pupilPX, pupilPY, YELLOW);
    drawPupil(RIGHT_EYE_X, pupilPX, pupilPY, YELLOW);
    startIdle();
}

void Eyes::tick() {
    unsigned long now = millis();

    switch (eyeState) {

        case EYE_IDLE:
            if (now >= stateTimer) {
                int r = random(8);
                if      (r == 0) startBlink();
                else if (r == 1) startSquint();
                else if (r == 2) startWink();
                else             startMove();
            }
            break;

        case EYE_MOVING:
            if (now >= stateTimer) {
                int oldPX = pupilPX, oldPY = pupilPY;
                if      (pupilPX < targetPX) pupilPX++;
                else if (pupilPX > targetPX) pupilPX--;
                if      (pupilPY < targetPY) pupilPY++;
                else if (pupilPY > targetPY) pupilPY--;
                pupilPX = clampPX(pupilPX, pupilPY);
                updatePupil(LEFT_EYE_X,  oldPX, oldPY, pupilPX, pupilPY);
                updatePupil(RIGHT_EYE_X, oldPX, oldPY, pupilPX, pupilPY);
                if (pupilPX == targetPX && pupilPY == targetPY) startIdle();
                else stateTimer = now + MOVE_STEP_MS;
            }
            break;

        case EYE_BLINK_CLOSE:
            if (now >= stateTimer) {
                drawBlinkStep(LEFT_EYE_X,  animStep);
                drawBlinkStep(RIGHT_EYE_X, animStep);
                animStep++;
                if (animStep > 2) {
                    eyeState   = EYE_BLINK_OPEN;
                    animStep   = 2;
                    stateTimer = now + animStepMs * 3;
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;

        case EYE_BLINK_OPEN:
            if (now >= stateTimer) {
                restoreBlinkStep(LEFT_EYE_X,  animStep);
                restoreBlinkStep(RIGHT_EYE_X, animStep);
                animStep--;
                if (animStep < 0) {
                    redrawEye(LEFT_EYE_X,  pupilPX, pupilPY);
                    redrawEye(RIGHT_EYE_X, pupilPX, pupilPY);
                    startIdle();
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;

        case EYE_SQUINT_CLOSE:
            if (now >= stateTimer) {
                drawSquintStep(LEFT_EYE_X,  animStep);
                drawSquintStep(RIGHT_EYE_X, animStep);
                animStep++;
                if (animStep >= squintDepth) {
                    int lo, hi;
                    pupilXBounds(2, lo, hi);
                    targetPY   = 2;
                    targetPX   = (pupilPX <= (lo + hi) / 2) ? hi : lo;
                    eyeState   = EYE_SQUINT_LOOK;
                    stateTimer = now + MOVE_STEP_MS;
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;

        case EYE_SQUINT_LOOK:
            if (now >= stateTimer) {
                int oldPX = pupilPX, oldPY = pupilPY;
                if      (pupilPX < targetPX) pupilPX++;
                else if (pupilPX > targetPX) pupilPX--;
                if      (pupilPY < targetPY) pupilPY++;
                else if (pupilPY > targetPY) pupilPY--;
                pupilPX = clampPX(pupilPX, pupilPY);
                updatePupilSquint(LEFT_EYE_X,  oldPX, oldPY, pupilPX, pupilPY);
                updatePupilSquint(RIGHT_EYE_X, oldPX, oldPY, pupilPX, pupilPY);
                if (pupilPX == targetPX && pupilPY == targetPY) {
                    squintSides--;
                    if (squintSides > 0) {
                        int lo, hi;
                        pupilXBounds(2, lo, hi);
                        targetPX   = (pupilPX >= (lo + hi) / 2) ? lo : hi;
                        targetPY   = 2;
                        stateTimer = now + random(350, 650);
                    } else {
                        eyeState   = EYE_SQUINT_OPEN;
                        animStep   = squintDepth - 1;
                        stateTimer = now + random(250, 500);
                    }
                } else {
                    stateTimer = now + SQUINT_MOVE_MS;
                }
            }
            break;

        case EYE_SQUINT_OPEN:
            if (now >= stateTimer) {
                restoreSquintStep(LEFT_EYE_X,  animStep);
                restoreSquintStep(RIGHT_EYE_X, animStep);
                animStep--;
                if (animStep < 0) {
                    redrawEye(LEFT_EYE_X,  pupilPX, pupilPY);
                    redrawEye(RIGHT_EYE_X, pupilPX, pupilPY);
                    startIdle();
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;

        case EYE_WINK_CENTER:
            if (now >= stateTimer) {
                int oldPX = pupilPX, oldPY = pupilPY;
                if      (pupilPX < targetPX) pupilPX++;
                else if (pupilPX > targetPX) pupilPX--;
                if      (pupilPY < targetPY) pupilPY++;
                else if (pupilPY > targetPY) pupilPY--;
                pupilPX = clampPX(pupilPX, pupilPY);
                updatePupil(LEFT_EYE_X,  oldPX, oldPY, pupilPX, pupilPY);
                updatePupil(RIGHT_EYE_X, oldPX, oldPY, pupilPX, pupilPY);
                if (pupilPX == targetPX && pupilPY == targetPY) {
                    eyeState   = EYE_WINK_CLOSE;
                    animStep   = 0;
                    stateTimer = now + random(150, 350);
                } else {
                    stateTimer = now + WINK_MOVE_MS;
                }
            }
            break;

        case EYE_WINK_CLOSE:
            if (now >= stateTimer) {
                drawBlinkStep(winkEyeX, animStep);
                animStep++;
                if (animStep > 2) {
                    eyeState   = EYE_WINK_OPEN;
                    animStep   = 2;
                    stateTimer = now + animStepMs * 3;
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;

        case EYE_WINK_OPEN:
            if (now >= stateTimer) {
                restoreBlinkStep(winkEyeX, animStep);
                animStep--;
                if (animStep < 0) {
                    redrawEye(winkEyeX, pupilPX, pupilPY);
                    startIdle();
                } else {
                    stateTimer = now + animStepMs;
                }
            }
            break;
    }
}
