// Flex OS Ultra · pantalla: LDO del PHY + MIPI-DSI + ST7701 + panel DPI con
// dos framebuffers en PSRAM + brillo por LEDC.
//
// Es el UNICO componente que habla con el panel. La interfaz no dibuja aqui:
// dibuja LVGL (flex_display_lvgl.h) en los framebuffers de este driver.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enciende el panel con el brillo a cero. DEBE llamarse desde la tarea de UI
// (nucleo 1): las interrupciones del DSI y del DMA se reservan en el nucleo
// que crea el panel, y el cambio de framebuffer es seguro frente a la ISR solo
// si ambas corren en el mismo nucleo.
esp_err_t flex_display_init(void);

// Brillo elegido por el usuario, 5..100 % (suelo del 5 % como en la version
// Arduino, para que el deslizador nunca deje la pantalla invisible).
void    flex_display_set_brightness(uint8_t pct);
uint8_t flex_display_get_brightness(void);
// Enciende el retroiluminado al brillo guardado (lo hace solo el puerto de
// LVGL tras mostrar el primer cuadro) o lo apaga sin olvidar el brillo.
void    flex_display_backlight_enable(bool on);

// Fundidos de la suspension (blWritePct de Arduino): 0..100 lineal, sin suelo
// y sin cambiar el brillo elegido. flex_display_backlight_enable(true) vuelve
// al brillo del usuario con su curva.
void    flex_display_backlight_raw(uint8_t pct);
// DISPOFF / DISPON del ST7701 (la suspension apaga el panel tras el fundido).
// Solo tarea de UI.
void    flex_display_panel_on(bool on);

// Fines de cuadro del DPI contados por la ISR desde el arranque.
uint32_t flex_display_vsync_count(void);

// Diagnostico de la Fase 1: barras de color generadas por el propio host DSI,
// sin framebuffer ni DMA ni LVGL. Separa un fallo del enlace/panel de uno del
// pipeline grafico.
esp_err_t flex_display_test_pattern(bool on);

#ifdef __cplusplus
}
#endif
