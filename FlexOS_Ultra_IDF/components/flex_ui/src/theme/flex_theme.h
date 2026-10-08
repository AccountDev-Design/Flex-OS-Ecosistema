// Flex OS Ultra · sistema de diseno en LVGL: paleta semantica, preferencias de
// aspecto, tipografia y propagacion de un cambio de tema.
//
// Tres preferencias ortogonales, como la version Arduino (docs/spec/02 §2):
//   apariencia (oscuro/claro) · material (Liquid Glass/Plano) · estilo de icono.
// Ningun componente decide colores con "dark ? A : B": todos leen la paleta
// activa (flex_th()), y al cambiar el tema se avisa a quien tenga cache.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "flex_glass_math.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Paleta semantica (FlexTheme de Arduino, Theme.h:75-174) --------------
typedef struct {
    lv_color_t page, win, surf, surf2, glass, glass2;
    lv_color_t txt, txt2, mute, nav, border, divider, disabled, track, sel, scrim, shadow;
    lv_color_t on_acc, primary, danger, ok, warn, err, acc_soft;
    lv_color_t key_panel, key_face, key_alt;
    // Fuera de la paleta en Arduino (AppFramework.h:1015-1017), aqui tokens
    lv_color_t navbar_bg, navbar_fg, navbar_line;
} flex_palette_t;

// Sobre el fondo de pantalla: fijos en las dos apariencias (Theme.h:211-222)
#define FLEX_ONWALL       lv_color_hex(0xFFFFFF)
#define FLEX_ONWALL2      lv_color_hex(0xD7DEEE)
#define FLEX_WALLSURF     lv_color_hex(0x2C365C)
#define FLEX_WALLSURF2    lv_color_hex(0x303C6E)
#define FLEX_WALLPANEL    lv_color_hex(0x24283A)

// ---- Preferencias de aspecto (NVS "flexos", mismas claves y tipos que Arduino)
typedef struct {
    bool    dark;          // "dark"      bool  (def. true)
    bool    glass;         // "glass"     bool  (def. false = Plano)
    uint8_t icon_style;    // "iconstyle" int   0 Plano · 1 Vidrio
    uint8_t glass_level;   // "glasslv"   int   0..100 pasos de 5 (def. 50)
    bool    wall_pal;      // "wallpal"   bool  aplicar paleta del fondo
    uint8_t look;          // "hlook"     int   tema integrado 0..7
    uint8_t wall_home;     // "wallh"     int   0..7 o 200 (imagen)
    uint8_t wall_lock;     // "walll"     int
    uint8_t wall_fit;      // "wallfit"   int   0 rellenar · 1 ajustar · 2 centrar
    uint8_t nav_mode;      // "navmode"   int   0 Botones · 1 Gestos iOS
    uint8_t anim_style;    // "animstyle" int   0 Zoom · 1 Fundido · 2 Deslizar
    bool    h24;           // "h24"       bool  reloj de 24 h (def. false)
    bool    eff_mode;      // modo visual eficiente (temporal, no se guarda)
} flex_look_prefs_t;

// ---- Tipografia: Outfit, los "tamanos" de la version Arduino ----------------
// size 1..6 de Arduino -> fuente LVGL (docs/spec/02 §5.1)
extern const lv_font_t flex_font_outfit_11, flex_font_outfit_13, flex_font_outfit_16, flex_font_outfit_19,
    flex_font_outfit_25, flex_font_outfit_31, flex_font_outfit_38, flex_font_clock_200;
#define FLEX_FONT_S1   (&flex_font_outfit_11)   // etiquetas pequenas, pies, nombres bajo iconos
#define FLEX_FONT_S2   (&flex_font_outfit_13)   // cuerpo, filas, botones, hora de la barra
#define FLEX_FONT_BODY (&flex_font_outfit_16)   // cuerpo comodo (listas largas)
#define FLEX_FONT_S3   (&flex_font_outfit_19)   // titulo de cabecera de app
#define FLEX_FONT_S4   (&flex_font_outfit_25)   // titulos de pantalla
#define FLEX_FONT_S5   (&flex_font_outfit_31)   // numeros/codigos grandes
#define FLEX_FONT_S6   (&flex_font_outfit_38)   // hora del panel rapido
#define FLEX_FONT_CLOCK (&flex_font_clock_200)  // reloj del bloqueo/escritorio

// ---- Estado ---------------------------------------------------------------
const flex_palette_t      *flex_th(void);
const flex_look_prefs_t   *flex_look(void);
const flex_glass_params_t *flex_glass_params(void);
lv_color_t flex_accent(void);     // acento activo (paleta del fondo o del tema)
lv_color_t flex_accent2(void);    // acento claro
lv_color_t flex_on_color_lv(lv_color_t bg);   // blanco o casi negro legible encima

// Carga las preferencias de la NVS (cache de flex_storage) y aplica el tema.
// Llamar una vez desde la tarea de UI despues de lv_init().
void flex_theme_init(void);

// Cambios (desde Ajustes, panel rapido, temas). Guardan en la NVS como Arduino
// (solo si cambia el valor) y propagan el cambio.
void flex_theme_set_dark(bool dark);
void flex_theme_set_glass(bool glass);
void flex_theme_set_icon_style(uint8_t style);
void flex_theme_set_glass_level(uint8_t level);
void flex_theme_set_nav_mode(uint8_t mode);
void flex_theme_set_anim_style(uint8_t style);
void flex_theme_set_h24(bool h24);
void flex_theme_set_wallpapers(uint8_t home, uint8_t lock);
void flex_theme_set_wall_palette(bool on);
// Aplica un tema integrado entero (apariencia, material, iconos, fondos, acento).
bool flex_theme_apply_look(uint8_t look);
// Modo visual eficiente (lo enciende la optimizacion de memoria; no se guarda).
void flex_theme_set_eff_mode(bool on);
// La paleta extraida del fondo del escritorio (la calcula flex_wallmgr).
void flex_theme_set_wall_accent(uint16_t acc565, uint16_t acc2_565);

// Quien guarda pixeles ya tematizados (miniaturas, backdrops) se suscribe aqui.
typedef void (*flex_theme_listener_t)(void *ctx);
void flex_theme_listen(flex_theme_listener_t cb, void *ctx);

// ---- Ayudas de construccion -------------------------------------------------
// Etiqueta con fuente y color; text se copia.
lv_obj_t *flex_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color);
// Contenedor transparente sin scroll ni borde (para maquetar).
lv_obj_t *flex_box(lv_obj_t *parent);

static inline lv_color_t flex_c565(uint16_t c)
{
    return lv_color_make((uint8_t)(((c >> 11) & 0x1F) * 255 / 31), (uint8_t)(((c >> 5) & 0x3F) * 255 / 63),
                         (uint8_t)((c & 0x1F) * 255 / 31));
}
static inline uint16_t flex_lv_to_565(lv_color_t c)
{
    return (uint16_t)(((c.red & 0xF8) << 8) | ((c.green & 0xFC) << 3) | (c.blue >> 3));
}

#ifdef __cplusplus
}
#endif
