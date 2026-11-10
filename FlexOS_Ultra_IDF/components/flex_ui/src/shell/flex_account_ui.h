// Flex OS Ultra · pantalla de Flex Account (N3c; Arduino: FlexOS_Account_Bridge.h). La implementacion esta en flex_account_ui.c; el servicio, en flex_account; lo que se
// decide (colores, etiquetas, zonas del toque) esta en flex_account_ui_model.c y lo prueba tests/host contra el puente de Arduino.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "flex_account_ui_model.h"
#include "flex_account.h"

void flex_account_ui_start(void);   // crea el temporizador de la pantalla (flex_shell_start)
// Abre la pantalla de la cuenta. `where` dice a donde se vuelve; `done` (solo para FLEX_ACC_RET_OOBE) se llama al salir
void flex_account_ui_open(flex_acc_return_t where, void (*done)(void));
bool flex_account_ui_owns_screen(void);   // la pantalla es de la cuenta: Atras, Inicio, la cortina y la caja de apps no actuan como siempre
void flex_account_ui_back(void);          // Atras = "Volver sin cambios" (no en la primera configuracion: ahi se omite con la barra)
void flex_account_ui_close_now(void);     // Inicio / Recientes / bloquear: cierra sin mas (cancela el enlace en curso)

void flex_app_settings_refresh(void);   // apps/app_settings.c: vuelve a leer las filas de la categoria abierta

// Simbolos de Ajustes (apps/app_settings.c los declara weak)
void flex_account_settings_text(char *out, size_t n);
void flex_account_settings_enter(void);

// Pruebas (simulador)
int flex_account_ui_builds(void);            // cuantas veces se ha dibujado la pantalla
int flex_account_ui_wifi_requests(void);     // cuantas veces se ha pedido abrir el configurador de Wi-Fi
bool flex_account_ui_ask_open(void);
bool flex_account_ui_shown(const char *text);        // algun texto de la pantalla es exactamente este
bool flex_account_ui_shown_part(const char *text);   // ...o lo contiene
#ifdef FLEX_SIM
uint32_t flex_account_ui_tree_crc(void);   // huella de geometria, colores y textos de la pantalla
int flex_account_ui_bar_width(void);       // ancho del relleno de la barra de progreso (-1 si no hay)
void flex_account_ui_test_tick(void);
struct _lv_obj_t *flex_account_ui_test_root(void);   // la raiz de la pantalla (la escena recorre el arbol)
void flex_account_ui_test_render(const flex_account_snapshot_t *s, bool online, bool ask, flex_acc_return_t where);      // un paso del temporizador, sin esperar los 80 ms
#endif
