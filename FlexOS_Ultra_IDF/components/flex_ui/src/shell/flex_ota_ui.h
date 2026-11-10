// Flex OS Ultra · interfaz de las actualizaciones OTA (N5b; Arduino: FlexOS_OTA.cpp, capas 1-4). La implementacion esta en flex_ota_ui.c; el motor, en flex_ota.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void flex_ota_ui_start(void);          // crea el temporizador de la capa (flex_shell_start)
void flex_ota_ui_install_started(void); // el motor pide una instalacion: abre la pantalla de instalacion (hook que flex_ota_request_install llama)
void flex_ota_open_settings(void);     // Ajustes > Actualizaciones (flexOtaOpenSettings): lo llama la fila de Ajustes y el mosaico del panel rapido
bool flex_ota_ui_owns_screen(void);    // flexOtaOwnsScreen: changelog, instalacion o ajustes (la tarjeta flotante NO cuenta)
bool flex_ota_ui_overlay_active(void); // flexOtaOverlayActive: cualquier capa, tarjeta incluida
bool flex_ota_ui_busy(void);           // la instalacion trabajando: Atras, Inicio y Recientes no hacen nada
void flex_ota_ui_back(void);           // Atras: sale de la capa (changelog, ajustes o aviso de error); la tarjeta no cuenta
void flex_ota_ui_close_now(void);      // Inicio / Recientes / bloquear: cierra la capa salvo con una instalacion en marcha

void flex_app_settings_refresh(void);   // apps/app_settings.c: vuelve a leer las filas de la categoria abierta

// Botones de las capas (los toca el simulador por su centro)
enum { FLEX_OTA_BT_CHECK = 1, FLEX_OTA_BT_INSTALL, FLEX_OTA_BT_CANCEL, FLEX_OTA_BT_CLOSE, FLEX_OTA_BT_BACK, FLEX_OTA_BT_TOAST };

// Pruebas (simulador)
int flex_ota_ui_overlay(void);   // 0 oculto, 1 aviso, 2 changelog, 3 instalacion, 4 ajustes
int flex_ota_ui_builds(void);
bool flex_ota_ui_btn_xy(int id, int *x, int *y);
bool flex_ota_ui_shown(const char *text);        // algun texto de la capa es exactamente este
bool flex_ota_ui_shown_part(const char *text);   // ...o lo contiene
int flex_ota_ui_card_y(void);
int flex_ota_ui_log_scroll(void);   // desplazamiento del changelog (-1: sin changelog en pantalla)
int flex_ota_ui_log_max(void);      // lo que se puede desplazar en total
#ifdef FLEX_SIM
void flex_ota_ui_test_open_changelog(void);
int flex_ota_ui_bar_width(void);
uint32_t flex_ota_ui_tree_crc(void);   // huella de geometria, colores y textos de la capa visible
#endif
