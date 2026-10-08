// Entorno minimo para compilar en el PC las funciones extraidas de la version
// Arduino (solo la ruta vertical, gLand = false). Lo que no debe llamarse
// aborta la prueba.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 480
#define SCR_H 800
static uint16_t *gBuf;
static bool gLand = false;
static int gClipX0 = 0, gClipX1 = SCR_W - 1, gClipY0 = 0, gClipY1 = SCR_H - 1;
static inline void setBuf(uint16_t *b) { gBuf = b; }
static inline void putPhys(int, int, uint16_t) { abort(); }
static inline void pxA(int, int, uint16_t, uint8_t) { abort(); }
