// Flex OS Ultra · servicio del Cronometro: estado global, capsula de la barra de estado y tarjeta expandida
// (docs/spec/03 §2; Arduino: FlexOS_Ultra_AppChrono.h secciones 1, 3, 5 y 6).
//
// Una sola fuente de verdad (flex_crono_t del modelo) con el tiempo inyectado (lv_tick_get): la app Reloj, la capsula, la
// tarjeta, el mosaico del panel rapido y el widget del escritorio preguntan aqui. El cronometro sigue contando aunque
// nadie lo dibuje.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flex_chrono_model.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Arranque (flex_shell_start): gancho de tacto, temporizador de la capsula y la tarjeta
void flex_crono_svc_init(void);

const flex_crono_t *flex_crono_svc(void);
uint32_t flex_crono_svc_now(void);                 // lv_tick_get: ms de 32 bits que dan la vuelta
// Acciones (las mismas que los dos botones y que el mosaico del panel rapido)
void flex_crono_svc_left(void);                    // Parcial / Reiniciar
void flex_crono_svc_right(void);                   // Iniciar / Detener / Continuar
void flex_crono_svc_toggle(void);                  // mosaico del panel rapido: marcha <-> pausa

// Banderas de "repintar" que consume la app (FLEX_CRONO_D_*): devuelve y baja la bandera
enum { FLEX_CRONO_D_LAPS = 0, FLEX_CRONO_D_BTN, FLEX_CRONO_D_DIAL };
bool flex_crono_svc_take(int what);

// Datos que ya leen otros modulos (el widget del escritorio los tenia como weak)
uint32_t flex_crono_elapsed_ms(void);
bool flex_crono_running(void);
void flex_crono_state_text(char *out, size_t n);   // "En marcha" / "En pausa" / "Detenido" (mosaico del panel rapido)

// Barra de estado: donde van la hora y la capsula, con la regla de Arduino (manda la capsula si no cabe todo).
// clock_w = ancho de la hora normal. text = "MM:SS" o "H:MM:SS".
void flex_crono_svc_bar_view(int clock_w, flex_crono_bar_t *bar, int *cap_w, char *text, size_t n);
// Una app que cubre la barra de estado (visor de medios, pantalla completa) lo avisa: sin capsula ni toque mientras tanto
void flex_crono_svc_hide_bar(bool hidden);

// Abre (o trae al frente) la pestana Cronometro de Reloj; cierra la tarjeta
void flex_crono_svc_open_app(void);

// Tarjeta expandida
bool flex_crono_card_active(void);                 // a la vista (abriendose, abierta o cerrandose)
int flex_crono_card_phase(void);                   // FLEX_CC_*
void flex_crono_card_open(void);
void flex_crono_card_close(void);                  // con animacion
void flex_crono_card_close_now(void);              // sin animacion (otra pantalla se adueno)
// Pruebas: lo que la tarjeta muestra de verdad (etiquetas LVGL) y su rectangulo actual
const char *flex_crono_card_text(int which);       // 0 tiempo · 1 vuelta · 2 boton izquierdo · 3 boton derecho
bool flex_crono_card_rect(lv_area_t *out);
// Pruebas: una pasada del temporizador del servicio (la tarjeta y la capsula)
void flex_crono_svc_tick(void);
// Pruebas: estado de la capsula de la barra (true si esta pintada) y su rectangulo
bool flex_crono_cap_shown(lv_area_t *out, char *text, size_t n);
// Pruebas: devuelve el servicio a su estado de arranque (parado, sin tarjeta)
void flex_crono_svc_reset(void);

// ---- Reloj (app_reloj.c) ----
void flex_app_reloj_goto_tab(int tab);             // 0 Hora · 1 Cronometro · 2 Temporizador
int flex_app_reloj_tab(void);
void flex_app_reloj_theme_changed(void);          // cambio de tema: la app, si esta creada, rehace sus colores
const char *flex_app_reloj_text(int which);        // pruebas: textos mostrados (ver app_reloj.c)
uint32_t flex_app_reloj_color(int which);          // pruebas: color (RGB) con que se ve ese texto
int flex_app_reloj_frames(void);                   // pruebas: cuadros de la esfera repintados hasta ahora
lv_obj_t *flex_app_reloj_seg_obj(void);            // pruebas: la barra de pestanas
int flex_app_reloj_seg_active(void);               // pruebas: pestana marcada en la barra (-1: sin barra)

#ifdef __cplusplus
}
#endif
