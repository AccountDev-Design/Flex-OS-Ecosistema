// Arbitraje del tactil: port literal de flexPollTouch (Touch.h:341-432) y
// suspGestureUpdate (Touch.h:233-304). Ver flex_touch_arb.h.
#include "flex_touch_arb.h"

#include <stdlib.h>
#include <string.h>

void flex_arb_init(flex_arb_t *a)
{
    memset(a, 0, sizeof(*a));
}

void flex_arb_drop_all(flex_arb_t *a, uint32_t now_ms)
{
    // touchDropAll (Touch.h:45-57): anula los eventos y arma el candado; si el
    // dedo ya estaba arriba al armarlo, se suelta en el siguiente poll sin dato.
    // (AppFramework.h:1179-1191)
    flex_arb_touch_t *t = &a->t;
    bool finger_on = t->down || t->pressed;   // el dedo SIGUE apoyado: hay contacto heredado
    t->pressed = t->released = t->tap = false;
    t->swipe_up = t->swipe_down = t->swipe_left = t->swipe_right = false;
    t->down = false;
    t->moved = false;
    t->start_x = t->x;
    t->start_y = t->y;
    t->down_ms = t->last_ms = now_ms;
    a->swallow = true;
    a->swallow_seen_ms = finger_on ? now_ms : 0;
}

bool flex_arb_kiosk_in_exit(int x, int y)
{
    return x >= FLEX_ARB_KIOSK_BADGE_X - FLEX_ARB_KIOSK_PAD &&
           x <= FLEX_ARB_KIOSK_BADGE_X + FLEX_ARB_KIOSK_BADGE_S + FLEX_ARB_KIOSK_PAD &&
           y >= FLEX_ARB_KIOSK_BADGE_Y - FLEX_ARB_KIOSK_PAD &&
           y <= FLEX_ARB_KIOSK_BADGE_Y + FLEX_ARB_KIOSK_BADGE_S + FLEX_ARB_KIOSK_PAD;
}

static bool kiosk_blocked(const flex_arb_t *a, int x, int y)
{
    if (!a->kiosk_filter || a->kw <= 0 || a->kh <= 0) {
        return false;
    }
    bool in = x >= a->kx && x < a->kx + a->kw && y >= a->ky && y < a->ky + a->kh;
    return in && !flex_arb_kiosk_in_exit(x, y);
}

static void clear_events(flex_arb_touch_t *t)
{
    t->pressed = t->released = t->tap = false;
    t->swipe_up = t->swipe_down = t->swipe_left = t->swipe_right = false;
}

static void do_release(flex_arb_touch_t *t, uint32_t now)
{
    t->down = false;
    t->released = true;
    t->dx = (int16_t)(t->x - t->start_x);
    t->dy = (int16_t)(t->y - t->start_y);
    uint32_t dur = now - t->down_ms;
    int adx = abs(t->dx), ady = abs(t->dy);
    if (adx < 16 && ady < 16 && dur < 550) {
        t->tap = true;   // el destino se decide al apoyar
        t->x = t->start_x;
        t->y = t->start_y;
    } else if (ady > 55 && ady >= adx) {
        if (t->dy < 0) {
            t->swipe_up = true;
        } else {
            t->swipe_down = true;
        }
    } else if (adx > 55 && adx > ady) {
        if (t->dx < 0) {
            t->swipe_left = true;
        } else {
            t->swipe_right = true;
        }
    }
}

static int susp_update(flex_arb_t *a, uint32_t now)
{
    int evt = FLEX_ARB_EV_NONE;
    int n = (now - a->fingers_ms > FLEX_ARB_SILENCE_MS) ? 0 : (int)a->fingers;
    if (a->tap2_ms && now - a->tap2_ms > FLEX_ARB_TAP_WINDOW_MS) {
        a->tap2_ms = 0;
    }
    if (a->tap1_ms && now - a->tap1_ms > FLEX_ARB_TAP_WINDOW_MS) {
        a->tap1_ms = 0;
    }
    if (n > 0) {
        if (!a->ep_act) {
            a->ep_act = true;
            a->ep_t0 = now;
            a->ep_run2 = 0;
            a->ep_had2 = false;
            a->ep_had3 = false;
            a->pinch_used = false;
        }
        if (n >= 3) {
            a->ep_had3 = true;
        }
        if (n >= 2) {
            if (a->ep_run2 < 255) {
                a->ep_run2++;
            }
            if (a->ep_run2 >= FLEX_ARB_TAP_FRAMES) {
                a->ep_had2 = true;
            }
        } else {
            a->ep_run2 = 0;
        }
    } else if (a->ep_act) {
        a->ep_act = false;
        uint32_t dur = now - a->ep_t0;
        bool too_long = dur > FLEX_ARB_TAP_MAX_MS;
        if (too_long || a->ep_had3) {
            a->tap1_ms = 0;
            a->tap2_ms = 0;
        } else if (a->ep_had2) {
            bool veto = a->kiosk_on || a->typing || a->pinch_used;
            if (a->tap2_ms && (now - a->tap2_ms) >= FLEX_ARB_TAP_GAP_MS) {
                a->tap2_ms = 0;
                if (!a->suspended && !veto) {
                    evt = FLEX_ARB_EV_SUSPEND;
                }
            } else {
                a->tap2_ms = now;
            }
            a->tap1_ms = 0;
        } else {
            if (a->suspended) {
                if (a->tap1_ms && (now - a->tap1_ms) >= FLEX_ARB_TAP_GAP_MS) {
                    a->tap1_ms = 0;
                    evt = FLEX_ARB_EV_WAKE;
                } else {
                    a->tap1_ms = now;
                }
            } else {
                a->tap1_ms = 0;
            }
            a->tap2_ms = 0;
        }
    }
    a->susp_swallow = a->suspended || (a->ep_had2 && !a->typing && !a->owns_two);
    return evt;
}

int flex_arb_poll(flex_arb_t *a, int ev, int x, int y, int fingers, uint32_t now)
{
    flex_arb_touch_t *t = &a->t;
    clear_events(t);
    if (ev != -1) {
        a->fingers = (uint8_t)(fingers < 0 ? 0 : fingers);
        a->fingers_ms = now;
    }
    if (ev == 1 && kiosk_blocked(a, x, y)) {
        ev = -1;   // descarte silencioso: "sin dato", no "soltado"
    }
    bool was_down = t->down;
    if (ev == 1) {
        t->x = (int16_t)x;
        t->y = (int16_t)y;
        t->last_ms = now;
        if (!was_down) {
            t->down = true;
            t->pressed = true;
            t->start_x = (int16_t)x;
            t->start_y = (int16_t)y;
            t->down_ms = now;
            t->moved = false;
        } else if (abs(x - t->start_x) > 12 || abs(y - t->start_y) > 12) {
            t->moved = true;
        }
    } else if (ev == 0) {
        if (was_down) {
            do_release(t, now);
        }
    } else if (was_down && now - t->last_ms > FLEX_ARB_SILENCE_MS) {
        do_release(t, now);
    }
    if (a->swallow) {
        if (ev == 1) {
            a->swallow_seen_ms = now;
        }
        bool up = ev == 0 || (ev == -1 && now - a->swallow_seen_ms > FLEX_ARB_SILENCE_MS);
        if (up) {
            a->swallow = false;
        } else {
            clear_events(t);
            t->down = false;
            t->moved = false;
        }
    }
    int evt = susp_update(a, now);
    if (a->susp_swallow) {
        clear_events(t);
        t->down = false;
        t->moved = false;
    }
    return evt;
}
