// Flex OS Ultra · Modo kiosco: logica pura. Ver flex_kiosk_model.h.
#include "flex_kiosk_model.h"

#include <stdlib.h>

flex_kiosk_t flex_kiosk_effective(const flex_kiosk_t *saved, int app_n, bool have_key)
{
    flex_kiosk_t k = *saved;
    if (!k.on || k.app < 0 || k.app >= app_n || !have_key) {
        // sin clave no habria salida: el kiosco no se aplica
        k = (flex_kiosk_t){false, -1, 0, 0, 0, 0};
    }
    return k;
}

bool flex_kiosk_drag_rect(int x0, int y0, int x1, int y1, int *x, int *y, int *w, int *h)
{
    *x = x0 < x1 ? x0 : x1;
    *y = y0 < y1 ? y0 : y1;
    *w = abs(x1 - x0);
    *h = abs(y1 - y0);
    return *w >= FLEX_KIOSK_MIN_AREA && *h >= FLEX_KIOSK_MIN_AREA;
}

bool flex_kiosk_exit_due(bool down, bool in_exit, uint32_t down_ms, uint32_t now, int dx, int dy)
{
    return down && in_exit && now - down_ms > FLEX_KIOSK_EXIT_MS && abs(dx) < FLEX_KIOSK_EXIT_MOVE &&
           abs(dy) < FLEX_KIOSK_EXIT_MOVE;
}
