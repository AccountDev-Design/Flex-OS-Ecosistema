// Flex OS Ultra · modelo del apagado (C puro). Ver flex_poweroff.h.
#include "flex_poweroff.h"

// ---- deslizador ---------------------------------------------------------------------
bool flex_poff_grab(int knob, int x, int y)
{
    int kx = FLEX_POFF_TRACK_X + FLEX_POFF_KNOB_PAD + knob;
    return y >= FLEX_POFF_TRACK_Y && y <= FLEX_POFF_TRACK_Y + FLEX_POFF_TRACK_H && x >= kx - 24 &&
           x <= kx + FLEX_POFF_KNOB_D + 24;
}

int flex_poff_knob_at(int x, int grab)
{
    int v = x - grab - (FLEX_POFF_TRACK_X + FLEX_POFF_KNOB_PAD);
    return v < 0 ? 0 : v > FLEX_POFF_RUN ? FLEX_POFF_RUN : v;
}

bool flex_poff_done(int knob)
{
    return knob * 100 / FLEX_POFF_RUN >= FLEX_POFF_DONE_PCT;
}

int flex_poff_spring_step(int knob, int target)
{
    if (knob == target) {
        return knob;
    }
    int d = target - knob;
    knob += d > 0 ? (d + 3) / 4 : (d - 3) / 4;
    int r = target - knob;
    return (r < 0 ? -r : r) < 3 ? target : knob;
}

// ---- filtro de encendido -------------------------------------------------------------
void flex_wakegate_init(flex_wakegate_t *g, uint32_t now_ms)
{
    g->t0 = now_ms;
    g->last_finger = now_ms;
    g->held_from = now_ms;
    g->held = false;
}

flex_wg_result_t flex_wakegate_step(flex_wakegate_t *g, uint32_t now_ms, int fingers)
{
    if (fingers >= 1) {
        g->last_finger = now_ms;
        if (!g->held) {
            g->held = true;
            g->held_from = now_ms;
        }
        if (now_ms - g->held_from >= FLEX_WG_HOLD_MS) {
            return FLEX_WG_BOOT;
        }
    } else {
        g->held = false;   // la cuenta de los 3 s vuelve a empezar
        if (now_ms - g->last_finger >= FLEX_WG_NOFINGER_MS) {
            return FLEX_WG_SLEEP;
        }
    }
    return now_ms - g->t0 >= FLEX_WG_WINDOW_MS ? FLEX_WG_SLEEP : FLEX_WG_WAIT;
}
