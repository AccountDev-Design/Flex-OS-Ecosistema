// Referencia: fillCircleAA y strokeSegAA (FlexOS_Ultra_Gfx.h) de la version Arduino tal cual, sobre un buffer de pantalla de 480x800.
#define pxA prelude_pxA_abort   // el prelude trae un pxA que aborta; aqui se usa el de verdad
#include "ref_prelude.h"
#undef pxA
#include <math.h>
#include <stdio.h>
static int gClipLY0 = 0, gClipLY1 = SCR_W - 1;
static inline void putPhysA(int, int, uint16_t, uint8_t) { abort(); }
#include "ref_raster.inc"

extern "C" {
void ref_raster_circle(uint16_t *screen, int cx0, int cx1, int cy0, int cy1, float cx, float cy, float r, uint16_t col)
{
    gBuf = screen; gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
    fillCircleAA(cx, cy, r, col);
}
void ref_raster_seg(uint16_t *screen, int cx0, int cx1, int cy0, int cy1, float x0, float y0, float x1, float y1, float r, uint16_t col)
{
    gBuf = screen; gClipX0 = cx0; gClipX1 = cx1; gClipY0 = cy0; gClipY1 = cy1;
    strokeSegAA(x0, y0, x1, y1, r, col);
}
}
