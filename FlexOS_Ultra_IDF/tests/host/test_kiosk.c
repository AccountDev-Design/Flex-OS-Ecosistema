// Modo kiosco: carga, zona arrastrada y gesto de salida contra Arduino.
#include <stdint.h>
#include <stdlib.h>
#include "flex_kiosk_model.h"
#include "flex_test.h"

// Referencias: Prefs.h:280-287 (carga), kioskSetTick (Lock.h:592-621) y kioskTick (Lock.h:500-515)
static bool ref_load_on(bool on, int app, int app_n, int lock_type)
{
    bool k = on;
    if (k && (app < 0 || app >= app_n || lock_type == 0)) {
        k = false;
    }
    return k;
}

static bool ref_exit(bool down, bool in_exit, uint32_t down_ms, uint32_t now, int x, int y, int sx, int sy)
{
    return down && in_exit && (now - down_ms) > 1000 && abs(x - sx) < 12 && abs(y - sy) < 12;
}

void test_kiosk(void)
{
    uint32_t seed = 11;
    int same = 0, n = 0;
    for (int i = 0; i < 20000; i++, n++) {
        seed = seed * 1103515245u + 12345u;
        bool on = seed & 1;
        int app = (int)((seed >> 3) % 25) - 3;
        int lt = (int)((seed >> 9) % 3);
        flex_kiosk_t s = {on, app, 1, 2, 3, 4};
        flex_kiosk_t k = flex_kiosk_effective(&s, FLEX_APP_N_TEST, lt != 0);
        bool ref = ref_load_on(on, app, FLEX_APP_N_TEST, lt);
        same += k.on == ref && (!k.on || (k.app == app && k.x == 1 && k.h == 4)) && (k.on || k.app == -1);
    }
    CHECK_EQ_I(same, n);

    // zona: normalizada en cualquier direccion; < 20 px en un lado no es zona
    int x, y, w, h;
    CHECK(flex_kiosk_drag_rect(300, 560, 40, 420, &x, &y, &w, &h));
    CHECK(x == 40 && y == 420 && w == 260 && h == 140);
    CHECK(!flex_kiosk_drag_rect(100, 100, 119, 300, &x, &y, &w, &h));
    CHECK(flex_kiosk_drag_rect(100, 100, 120, 120, &x, &y, &w, &h));

    // salida: igual que kioskTick en 20000 contactos
    same = n = 0;
    for (int i = 0; i < 20000; i++, n++) {
        seed = seed * 1103515245u + 12345u;
        bool down = seed & 1, in = (seed >> 1) & 1;
        uint32_t d0 = 5000, now = d0 + ((seed >> 2) % 2000);
        int sx = 460, sy = 56, xx = sx + (int)((seed >> 13) % 31) - 15, yy = sy + (int)((seed >> 19) % 31) - 15;
        same += flex_kiosk_exit_due(down, in, d0, now, xx - sx, yy - sy) == ref_exit(down, in, d0, now, xx, yy, sx, sy);
    }
    CHECK_EQ_I(same, n);
    CHECK(!flex_kiosk_exit_due(true, true, 0, 1000, 0, 0));   // > 1000 ms, no >=
    CHECK(flex_kiosk_exit_due(true, true, 0, 1001, 11, -11));
    printf("kiosco: carga, zona y salida iguales a Arduino\n");
}
