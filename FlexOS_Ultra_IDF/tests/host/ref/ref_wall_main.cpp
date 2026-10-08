// Referencia: los fondos de la version Arduino, compilados tal cual.
#include "ref_prelude.h"
#include "ref_wall.inc"

extern "C" void ref_wall_render(uint16_t *buf, int id, bool blobs, int y0, int y1)
{
    wallpaperEnsureLut();
    setBuf(buf);
    gClipX0 = 0; gClipX1 = SCR_W - 1; gClipY0 = y0; gClipY1 = y1;
    switch (id) {
    case 1: wallAurora(); break;
    case 2: wallNocturno(); break;
    case 3: wallHalo(); break;
    case 4: wallOnyx(); break;
    case 5: wallOceano(); break;
    case 6: wallVioleta(); break;
    case 7: wallNaturaleza(); break;
    default: wallFlexOriginal(blobs); break;
    }
}

extern "C" void ref_wall_palette(const uint16_t *buf, uint16_t *acc, uint16_t *acc2)
{
    wallPaletteBuild(buf);
    *acc = gWallAcc;
    *acc2 = gWallAcc2;
}

extern "C" uint16_t ref_on_color(uint16_t c) { return onColor(c); }
