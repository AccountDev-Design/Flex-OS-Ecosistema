// Referencia: la matematica de Liquid Glass de la version Arduino, tal cual.
#include "ref_prelude.h"
#include "ref_glass.inc"

extern "C" void ref_glass_level(uint8_t lvl, uint8_t out[9])
{
    gGlassLvl = lvl;
    glassLevelApply();
    uint8_t v[9] = {gGlassLvl, gGlR, gGlTintMin, gGlTintMax, gGlTintBase, gGlSpec, gGlShade, gGlCornS, gGlCornW};
    memcpy(out, v, 9);
}
extern "C" int ref_glass_luma(uint16_t c) { return glassLuma(c); }
extern "C" uint8_t ref_glass_tint_mix(uint32_t sum, int n, uint16_t tint, uint8_t min_mix)
{
    gGlMinMix = min_mix;
    uint8_t m = glassTintMix(sum, n, tint);
    gGlMinMix = 0;
    return m;
}
extern "C" void ref_glass_shade(int j, int h, uint16_t *c, uint8_t *a)
{
    uint16_t cc; uint8_t aa;
    glassShadeRow(j, h, cc, aa);
    *c = cc; *a = aa;
}
extern "C" int ref_glass_inset(int j, int h, int rad) { return glInset(j, h, rad); }
extern "C" void ref_glass_blur(uint16_t *buf, int w, int h, int R)
{
    glassBuf = buf;
    glassBlur(w, h, R);
    glassBuf = NULL;
}
