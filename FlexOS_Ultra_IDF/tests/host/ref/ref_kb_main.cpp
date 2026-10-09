// Referencia: el teclado del sistema de la version Arduino, tal cual
// (FlexOS_Ultra_Keyboard.h, Prefs.h, KeyboardSettings.h, Lock.h y Power.h).
#include <stdio.h>
#include "ref_prelude.h"

static uint32_t g_ref_ms;
static uint32_t millis() { return g_ref_ms; }

#include "ref_kb_a.inc"
// mapaActivo no lo puede extraer extract_arduino.py (es un puntero a fila): la
// misma declaracion que Keyboard.h:61.
static const char* (*mapaActivo)[KB_COLS] = LAYOUT_ES;
#include "ref_kb_b.inc"

static const char* (*ref_map(int layout))[KB_COLS]
{
    return layout == 1 ? LAYOUT_EN : layout == 2 ? LAYOUT_NUM : layout == 3 ? LAYOUT_EMOJI : LAYOUT_ES;
}
static int ref_layout_code()
{
    return mapaActivo == LAYOUT_EN ? 1 : mapaActivo == LAYOUT_NUM ? 2 : mapaActivo == LAYOUT_EMOJI ? 3 : 0;
}

// Geometria: out = kw, kh, gap, x, rowsTop, panelTop, toolbarY, chipsY, funcY,
// topH, toolbarH, chipsH, sizeCheck, fkeyX[6], fkeyW[6]
extern "C" void ref_kb_geom(int size, int bot, int extras, int toolbar, int predict, int layout, int out[25])
{
    gKbSize = size;
    kbBotReserve = bot;
    kbExtrasOn = extras != 0;
    gKbToolbar = toolbar != 0;
    gKbPredict = predict != 0;
    mapaActivo = ref_map(layout);
    kbApplySize();
    int k = 0;
    out[k++] = KB_KW; out[k++] = KB_KH; out[k++] = KB_GAP; out[k++] = KB_X;
    out[k++] = kbRowsTop(); out[k++] = kbPanelTop(); out[k++] = kbToolbarY(); out[k++] = kbChipsY();
    out[k++] = kbFuncY(); out[k++] = kbTopH(); out[k++] = kbToolbarH(); out[k++] = kbChipsH();
    out[k++] = kbSizeCheck() ? 1 : 0;
    for (int i = 0; i < KB_FKEYS; i++) out[k++] = kbFKeyX(i);
    for (int i = 0; i < KB_FKEYS; i++) out[k++] = kbFKeyW(i);
}
extern "C" int ref_kb_cell_at(int px, int py) { return kbCellAt(px, py); }
extern "C" int ref_kb_frow_hit(int px, int py) { return kbFRowHit(px, py); }

extern "C" const char *ref_kb_base(int layout, int cell)
{
    return ref_map(layout)[cell / KB_COLS][cell % KB_COLS];
}

// kbResolveKey con un estado dado; devuelve el texto copiado y el shift final.
extern "C" void ref_kb_resolve(int layout, int shift, int cell, int consume, char res[8], int *shift_after)
{
    mapaActivo = ref_map(layout);
    kbShift = shift != 0;
    char u[6];
    const char *k = kbResolveKey(mapaActivo[cell / KB_COLS][cell % KB_COLS], u, consume != 0);
    snprintf(res, 8, "%s", k);
    *shift_after = kbShift ? 1 : 0;
}

// Fila de funciones de la pantalla de clave (Power.h:559-562), estado solamente.
extern "C" void ref_kb_fn(int layout, int shift, int lang_es, int fi, int out[3])
{
    mapaActivo = ref_map(layout);
    kbShift = shift != 0;
    kbLangEs = lang_es != 0;
    if(fi == 0) kbShift = !kbShift;
    else if(fi == 1) mapaActivo = (mapaActivo == LAYOUT_NUM) ? LAYOUT_EMOJI : (mapaActivo == LAYOUT_EMOJI) ? (kbLangEs ? LAYOUT_ES : LAYOUT_EN) : LAYOUT_NUM;
    else if(fi == 2){ kbLangEs = !kbLangEs; if(mapaActivo == LAYOUT_ES || mapaActivo == LAYOUT_EN) mapaActivo = kbLangEs ? LAYOUT_ES : LAYOUT_EN; }
    out[0] = ref_layout_code();
    out[1] = kbShift ? 1 : 0;
    out[2] = kbLangEs ? 1 : 0;
}

extern "C" const char *ref_kb_layer_label(int layout)
{
    mapaActivo = ref_map(layout);
    return kbLayerLabel();
}

// Preferencias: in/out = size, style, font, opacity, lp, fx, sym[4]
extern "C" void ref_kb_prefs_normalize(int v[10])
{
    gKbSize = v[0]; gKbStyle = v[1]; gKbFontSc = v[2]; gKbOpacity = v[3]; gKbLpMs = v[4]; gKbFxMs = v[5];
    for (int i = 0; i < KB_SYMS; i++) gKbSym[i] = v[6 + i];
    kbPrefsNormalize();
    v[0] = gKbSize; v[1] = gKbStyle; v[2] = gKbFontSc; v[3] = gKbOpacity; v[4] = gKbLpMs; v[5] = gKbFxMs;
    for (int i = 0; i < KB_SYMS; i++) v[6 + i] = gKbSym[i];
}
extern "C" void ref_kb_font(int font_sc, int style, int out[3])
{
    gKbFontSc = font_sc;
    gKbStyle = style;
    out[0] = kbFontSize(); out[1] = kbFontDy(); out[2] = kbRadius();
}
extern "C" const char *ref_kb_sym_at(const int sym[4], int i)
{
    for (int k = 0; k < KB_SYMS; k++) gKbSym[k] = sym[k];
    return kbSymAt(i);
}
extern "C" void ref_kb_shortcuts(char abr[KB_SC_MAX][KB_SC_ABR], char exp[KB_SC_MAX][KB_SC_EXP])
{
    kbShortcutsDefaults();
    memcpy(abr, gKbScAbr, sizeof(gKbScAbr));
    memcpy(exp, gKbScExp, sizeof(gKbScExp));
}

// Destello: tiempos en ms simulados.
extern "C" void ref_kb_fx_start(int cell, uint32_t now, int fx_ms)
{
    g_ref_ms = now; gKbFxMs = fx_ms;
    kbFxStart(cell);
}
extern "C" int ref_kb_fx_level(int cell, uint32_t now, int fx_ms)
{
    g_ref_ms = now; gKbFxMs = fx_ms;
    return kbFxLevel(cell);
}
extern "C" void ref_kb_fx_reset(void) { kbFxCell = -1; kbFxT0 = 0; }

// Autocompletado con los atajos de fabrica.
extern "C" int ref_kb_suggest(int lang_es, int predict, int emoji, const char *pref, const char *out[4], int maxn)
{
    kbShortcutsDefaults();
    kbLangEs = lang_es != 0; gKbPredict = predict != 0; gKbEmojiSug = emoji != 0;
    return kbSuggest(pref, out, maxn);
}
extern "C" int ref_kb_dict_has(int lang_es, const char *w)
{
    kbShortcutsDefaults();
    kbLangEs = lang_es != 0;
    return kbDictHas(w) ? 1 : 0;
}
extern "C" int ref_kb_dict_n(int lang_es) { return lang_es ? KB_DICT_ES_N : KB_DICT_EN_N; }
extern "C" const char *ref_kb_dict_word(int lang_es, int i) { return lang_es ? KB_DICT_ES[i] : KB_DICT_EN[i]; }
extern "C" int ref_kb_current_word(const char *buf, int cur, char *out, int outsz) { return kbCurrentWord(buf, cur, out, outsz); }

extern "C" int ref_kb_variants(char b, const char *var[4]) { return kbGetVariants(b, var); }
extern "C" int ref_kb_is_vowel(int layout, int cell)
{
    mapaActivo = ref_map(layout);
    return kbIsVowelCell(cell) ? 1 : 0;
}
extern "C" int ref_kb_utf8_prev(const char *s, int i) { return utf8Prev(s, i); }
extern "C" int ref_kb_utf8_count(const char *s) { return utf8Count(s); }

// Buffer de la clave (Lock.h:37, 64 bytes) con las teclas de Power.h:73 y :564.
extern "C" void ref_kb_pass_reset(void) { lsuPass[0] = 0; }
extern "C" void ref_kb_pass_append(const char *s) { lsuPassAppend(s); }
extern "C" void ref_kb_pass_back(void)
{
    int L = strlen(lsuPass); if(L > 0){ int q = L - 1; while(q > 0 && (lsuPass[q] & 0xC0) == 0x80) q--; lsuPass[q] = 0; }
}
extern "C" const char *ref_kb_pass(void) { return lsuPass; }
extern "C" int ref_kb_pass_cap(void) { return (int)sizeof(lsuPass); }

// Campo del editor de atajos (KeyboardSettings.h:126-139)
extern "C" void ref_kb_field_reset(int field) { kbsScField = field; kbsScA[0] = 0; kbsScE[0] = 0; }
extern "C" void ref_kb_field_append(const char *s) { kbsAppendField(s); }
extern "C" void ref_kb_field_back(void) { kbsBackField(); }
extern "C" const char *ref_kb_field(void) { return kbsScField == 0 ? kbsScA : kbsScE; }
