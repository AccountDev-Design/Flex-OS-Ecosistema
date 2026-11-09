// Lo que el sketch de Arduino ofrece a Jumper y que, fuera del sketch, hay que dar: constantes de pantalla, el lienzo, el tactil, el reloj,
// los records (NVS) y la salida. NADA del juego ni del dibujo se reescribe aqui.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flex_storage.h"

#define SCR_W 480       // FlexOS_Ultra_Types.h:289
#define SCR_H 800
#define LW    SCR_H     // lienzo logico horizontal (el motor gira 90 grados)
#define LH    SCR_W

typedef struct { uint8_t w, h; int8_t bx; int8_t topoff; uint8_t adv; uint16_t off; } FGlyph;   // FlexOS_Ultra_Types.h:45

// ---- lienzo ----
static uint16_t* gBuf = nullptr;            // donde escriben las primitivas
static uint16_t* fb = nullptr;              // "pantalla" y "borrador": aqui es UN solo lienzo (el juego y LVGL corren en la misma tarea, asi que
static uint16_t* bbuf = nullptr;            //  no hay desgarro: el intercambio de punteros de jmpPresent es inocuo)
static uint16_t* gRtTarget = nullptr;       // hospedaje en una ventana de Modo PC: no aplica
static inline void setBuf(uint16_t* b){ gBuf = b; }
static inline void bbufSys(void) {}
static bool g_frame_ready = false;
static inline void flxFlush(int, int){ g_frame_ready = true; }
static inline void present(int, int){ g_frame_ready = true; }

// ---- tactil (el "Touch T" del sketch: solo lo que lee Jumper) ----
struct JTouch { bool down, pressed; int x, y; };
static JTouch T = {false, false, 0, 0};

// ---- reloj ----
static uint32_t g_now_us = 0;
static inline uint32_t micros(void){ return g_now_us; }
static inline uint32_t millis(void){ return g_now_us / 1000u; }

// ---- records: las mismas claves del espacio "flexos" que Arduino (jmp_bn, jmp_bp, jmp_coin, jmp_cmp, jmp_att) ----
struct JPrefs {
  void begin(const char*, bool) {}
  void end(void) {}
  uint32_t getUInt(const char* k, uint32_t d) { return flex_cfg_get_u32(k, d); }
  bool getBool(const char* k, bool d) { return flex_cfg_get_bool(k, d); }
  void putUInt(const char* k, uint32_t v) { flex_cfg_set_u32(k, v); }
  void putBool(const char* k, bool v) { flex_cfg_set_bool(k, v); }
};
static JPrefs prefs;

// ---- salida ----
static bool g_close_req = false;
static inline void appClose(void){ g_close_req = true; }
