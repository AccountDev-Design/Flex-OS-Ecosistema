// Flex OS Ultra · metricas de depuracion de la pantalla y el tactil.
//
// Cadena de latencia del tactil (todas en microsegundos de esp_timer):
//   t_read  la tarea del tactil termina la lectura I2C de un cuadro del GT911
//   t_used  el indev de LVGL consume ese cuadro (tarea de UI)
//   t_flip  se entrega al panel el primer cuadro dibujado despues (cambio de FB)
//   t_shown el DPI empieza a mostrarlo (primer fin de cuadro tras el cambio)
// No incluye el retardo interno del GT911 (su periodo de escaneo), que no se
// puede medir desde el P4.
//
// Hilos: todo se llama desde la tarea de UI (los fines de cuadro los cuenta la
// ISR del panel y llegan aqui como total acumulado). C portable: el simulador
// del PC compila este mismo archivo. Nada se registra por fotograma: el
// resumen sale cada segundo por flex_metrics_get().
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float panel_hz;             // fines de cuadro del DPI por segundo (~60,5 esperado)
    float fps;                  // cuadros nuevos entregados al panel por segundo
    float render_ms_avg;        // inicio de refresco de LVGL -> cambio de FB
    float render_ms_max;
    float vsync_wait_ms_avg;    // espera al fin de cuadro antes de reutilizar el FB
    float ui_cpu_pct;           // tiempo de CPU de la tarea de UI (sin las esperas)
    float touch_used_ms_avg;    // t_used - t_read
    float touch_flip_ms_avg;    // t_flip - t_read
    float touch_flip_ms_max;
    float touch_shown_ms_avg;   // t_shown - t_read
    float touch_shown_ms_max;
    uint32_t touch_samples;     // muestras de latencia en la ultima ventana
    uint32_t frames_total;
    uint32_t vsync_timeouts;    // esperas de fin de cuadro que vencieron (no deberia pasar)
} flex_metrics_t;

void flex_metrics_render_start(int64_t now_us);
void flex_metrics_frame_flipped(int64_t now_us);
void flex_metrics_frame_shown(int64_t vsync_us);
void flex_metrics_vsync_wait(int64_t waited_us, bool timed_out);
void flex_metrics_input(int64_t t_read_us, int64_t t_used_us);
void flex_metrics_ui_busy(int64_t busy_us);

// Cierra la ventana de 1 s si toca. vsync_total = fines de cuadro contados por
// la ISR desde el arranque. Devuelve true si hay un resumen nuevo.
bool flex_metrics_tick(int64_t now_us, uint32_t vsync_total);
void flex_metrics_get(flex_metrics_t *out);

#ifdef __cplusplus
}
#endif
