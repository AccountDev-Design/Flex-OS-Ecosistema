// Flex OS Ultra · Modo kiosco: logica pura (docs/spec/01a §5.14, 01c §12).
// Sin LVGL ni NVS: se prueba en el PC (tests/host/test_kiosk.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_KIOSK_EXIT_MS   1000   // mantener el candado (kioskTick)
#define FLEX_KIOSK_EXIT_MOVE 12     // sin moverse mas de esto
#define FLEX_KIOSK_MIN_AREA  20     // menos de 20x20 es un toque, no una zona

typedef struct {
    bool on;
    int app;
    int x, y, w, h;   // zona excluida (w o h <= 0: ninguna)
} flex_kiosk_t;

// cfgLoad (Prefs.h:280-287): app fuera de rango o sin clave -> desactivado en RAM
// (la NVS no se reescribe). Devuelve el estado efectivo.
flex_kiosk_t flex_kiosk_effective(const flex_kiosk_t *saved, int app_n, bool have_key);

// Rectangulo arrastrado (kioskSetTick): normalizado; true si es una zona (>= 20x20).
bool flex_kiosk_drag_rect(int x0, int y0, int x1, int y1, int *x, int *y, int *w, int *h);

// Gesto de salida (kioskTick): dedo apoyado que empezo en el candado ampliado,
// mas de 1 s y sin moverse 12 px. in_exit = el apoyo cayo en la zona del candado.
bool flex_kiosk_exit_due(bool down, bool in_exit, uint32_t down_ms, uint32_t now, int dx, int dy);

#ifdef __cplusplus
}
#endif
