// Referencia: el tactil de alto nivel de la version Arduino (flexPollTouch,
// tDoRelease, suspGestureUpdate, touchDropAll), tal cual, con el GT911, el reloj
// y los modulos vecinos sustituidos por un guion.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_now;
static inline uint32_t millis() { return s_now; }
static int s_ev, s_x, s_y, s_n;
static int s_events;   // 1 = suspEnter, 2 = suspWake (ultimo de esta vuelta)
static bool s_typing, s_kiosk_filter;

static int8_t gtPoll(uint16_t &gx, uint16_t &gy);
static bool kioskTouchBlocked(int px, int py);
static void hpzUpdate() {}
static bool hpzSwallowing() { return false; }
static bool kbTypingNow() { return s_typing; }
static void suspEnter();
static void suspWake();
static int gNavPress = -1, gNavGlow = -1;

#include "ref_touch.inc"

static int8_t gtPoll(uint16_t &gx, uint16_t &gy)
{
    if (s_ev != -1) {
        gtFingers = (uint8_t)s_n;
        gtFingersMs = millis();
    }
    gx = (uint16_t)s_x;
    gy = (uint16_t)s_y;
    return (int8_t)s_ev;
}
static bool kioskTouchBlocked(int px, int py)
{
    if (!KIOSK_ON || !s_kiosk_filter) {
        return false;
    }
    if (!kioskInExcluded(px, py)) {
        return false;
    }
    return !kioskInExit(px, py);
}
static void suspEnter() { s_events = 1; }
static void suspWake() { s_events = 2; }

typedef struct {
    bool down, pressed, released, tap, moved, su, sd, sl, sr;
    int x, y, sx, sy, dx, dy;
} ref_t_out;

extern "C" void ref_touch_reset(void)
{
    T = Touch();
    gTouchSwallow = false; gTouchSwallowSeenMs = 0;
    gSuspOn = false; gTouchPinchUsed = false; gTouchOwnsTwoFinger = false;
    gEpAct = false; gEpT0 = 0; gEpRun2 = 0; gEpHad2 = false; gEpHad3 = false;
    gTap2Ms = 0; gTap1Ms = 0; gSuspSwallow = false;
    gTouchHeld = false; gtFingers = 0; gtFingersMs = 0;
    kioskOn = false; kioskExX = kioskExY = kioskExW = kioskExH = 0;
}
extern "C" void ref_touch_inputs(bool kiosk_filter, int kx, int ky, int kw, int kh, bool kiosk_on, bool typing,
                                 bool owns_two, bool pinch_used, bool suspended)
{
    s_kiosk_filter = kiosk_filter;
    kioskExX = kx; kioskExY = ky; kioskExW = kw; kioskExH = kh;
    kioskOn = kiosk_on;
    s_typing = typing;
    gTouchOwnsTwoFinger = owns_two;
    if (pinch_used) gTouchPinchUsed = true;
    gSuspOn = suspended;
}
extern "C" void ref_touch_drop_all(uint32_t now) { s_now = now; touchDropAll(); }
extern "C" int ref_touch_poll(int ev, int x, int y, int n, uint32_t now, ref_t_out *o)
{
    s_now = now; s_ev = ev; s_x = x; s_y = y; s_n = n; s_events = 0;
    flexPollTouch();
    o->down = T.down; o->pressed = T.pressed; o->released = T.released; o->tap = T.tap; o->moved = T.moved;
    o->su = T.swipeUp; o->sd = T.swipeDown; o->sl = T.swipeLeft; o->sr = T.swipeRight;
    o->x = T.x; o->y = T.y; o->sx = T.startX; o->sy = T.startY; o->dx = T.dx; o->dy = T.dy;
    return s_events;
}
extern "C" bool ref_touch_pinch_used(void) { return gTouchPinchUsed; }
