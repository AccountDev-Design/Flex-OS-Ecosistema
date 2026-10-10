// Flex OS Ultra · pantalla de Modo seguro (docs/spec/01c §11.2-11.4; Arduino:
// FlexOS_Ultra_Recovery.h:545-690).
//
// Se entra al arrancar en Modo seguro (sin bloqueo) y desde la pildora "Modo
// seguro" del escritorio. RIESGO QUE ARDUINO DEJABA ABIERTO (§11.6): con 3
// reinicios anormales provocados se llegaba al escritorio, a Ajustes y al
// Explorador sin la clave. Aqui toda fila que da acceso pide la clave (si hay)
// una vez por arranque; "Reiniciar normalmente" no la pide (no da acceso).
#include <stdio.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define SCR_W 480
#define SCR_H 800
#define ROWS   7
#define ROW_Y0 292
#define ROW_H  66
#define CACHE_DIR "/System/Cache"

enum { R_REBOOT = 0, R_APPS3RD, R_CACHES, R_SETTINGS, R_FILES, R_HOME, R_RESET };

static const char *const ROW_T[ROWS] = {
    "Reiniciar normalmente",
    "Apps de terceros",
    "Limpiar cach\xC3\xA9s seguras",
    "Ajustes",
    "Explorador de archivos",
    "Ir al escritorio (limitado)",
    "Restablecer datos de f\xC3\xA1" "brica",
};

static struct {
    lv_obj_t *root;
    bool verified;      // la clave ya se acerto en este arranque
    int pending;        // fila que espera a la clave
    lv_obj_t *toast;
    lv_timer_t *toast_tmr;
} S = {.pending = -1};

static int apps3rd_count(void)
{
    uint32_t m = (uint16_t)flex_cfg_get_i32("apps3rd", 0);   // mascara heredada (siempre 0 hoy)
    int n = 0;
    for (; m; m >>= 1) {
        n += (int)(m & 1u);
    }
    return n;
}

// El restablecimiento lo aporta su asistente (flex_factory_ui.c); sin el, la fila no sale.
__attribute__((weak)) bool flex_factory_reset_available(void)
{
    return false;
}

__attribute__((weak)) void flex_factory_reset_open(bool from_safe)
{
    (void)from_safe;
}

// Fila con funcion real ahora mismo.
static bool row_shown(int i)
{
    return i != R_RESET || flex_factory_reset_available();
}

static bool row_enabled(int i)
{
    return i != R_APPS3RD || apps3rd_count() > 0;
}

static const char *row_sub(int i, char *b, size_t n)
{
    switch (i) {
    case R_REBOOT: return "Sale del Modo seguro y arranca normal";
    case R_APPS3RD:
        if (apps3rd_count() == 0) {
            return "Ninguna instalada: nada que desactivar";
        }
        snprintf(b, n, "Desactivar las %d instaladas", apps3rd_count());
        return b;
    case R_CACHES: return "Vistas previas y datos temporales";
    case R_SETTINGS:
    case R_FILES: return "Disponible en Modo seguro";
    case R_HOME: return "Toca \"Modo seguro\" en el escritorio para volver";
    default: return "Borra todo el contenido del dispositivo";
    }
}

static lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_clickable(o, false);
    return o;
}

static void text_at(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int w)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_one_line(l, w);
    lv_obj_set_x(l, x);
    flex_label_cap_y(l, y);
}

static void build(void);

static void run_row(int i)
{
    switch (i) {
    case R_REBOOT:
        flex_safe_exit_and_reboot();
        return;
    case R_APPS3RD:
        flex_cfg_set_i32("apps3rd", 0);
        build();
        return;
    case R_CACHES:
        // Solo lo que el sistema sabe rehacer: ni notas, ni dibujos, ni ajustes, ni credenciales
        flex_fs_wipe_dir_async(CACHE_DIR, NULL, NULL);
        flex_wallmgr_shed();
        build();
        return;
    case R_SETTINGS:
    case R_FILES:
        flex_safe_close_now();
        flex_app_launch(i == R_SETTINGS ? IC_AJUSTES : IC_ALMACEN, NULL);
        return;
    case R_HOME:
        flex_safe_close_now();
        flex_touch_drop_all();
        return;
    case R_RESET:
        flex_safe_close_now();
        flex_factory_reset_open(true);
        return;
    default:
        return;
    }
}

static void auth_ok(void *ctx)
{
    (void)ctx;
    S.verified = true;
    int i = S.pending;
    S.pending = -1;
    flex_safe_open();
    if (i >= 0) {
        run_row(i);
    }
}

static void auth_cancel(void *ctx)
{
    (void)ctx;
    S.pending = -1;
    flex_safe_open();   // la clave se cancela: de vuelta al Modo seguro
}

static void row_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!row_enabled(i)) {
        return;   // fila inerte: ni siquiera se repinta
    }
    // Lo que da acceso al aparato pide la clave (una vez por arranque).
    // Reiniciar no da acceso; Restablecer la pide en su asistente.
    bool gated = i != R_REBOOT && i != R_RESET && i != R_APPS3RD;
    if (gated && !S.verified && flex_auth_required()) {
        S.pending = i;
        flex_auth_req_t req = {.on_ok = auth_ok, .on_cancel = auth_cancel};
        if (flex_auth_verify(&req)) {
            lv_obj_set_hidden(S.root, true);   // la clave va encima, en la misma pantalla
            return;
        }
        S.pending = -1;
    }
    run_row(i);
}

static void build(void)
{
    if (!S.root) {
        return;
    }
    lv_obj_clean(S.root);
    const flex_palette_t *t = flex_th();
    lv_obj_set_style_bg_color(S.root, t->page, 0);
    lv_obj_t *l = flex_label(S.root, "Modo seguro", FLEX_FONT_S3, lv_color_hex(0xE2A046));
    flex_label_cap_center(l, SCR_W / 2, 44);
    l = flex_label(S.root, "FlexOS ha arrancado con lo m\xC3\xADnimo", FLEX_FONT_S1, t->txt2);
    flex_label_cap_center(l, SCR_W / 2, 82);

    lv_obj_t *c = card(S.root, 20, 112, 440, 148, 16, t->surf);
    text_at(c, "Motivo del \xC3\xBAltimo fallo", FLEX_FONT_S2, t->txt, 16, 14, 412);
    text_at(c, flex_safe_cause_text(flex_safe_cause()), FLEX_FONT_S2, lv_color_hex(0xD6684A), 16, 42, 412);
    char b[64];
    snprintf(b, sizeof(b), "Reinicios anormales seguidos: %d", flex_safe_fails());
    text_at(c, b, FLEX_FONT_S1, t->txt2, 16, 72, 412);
    text_at(c, "Apps no esenciales y personalizaci\xC3\xB3n", FLEX_FONT_S1, t->txt2, 16, 94, 412);
    text_at(c, "desactivadas mientras dure este modo.", FLEX_FONT_S1, t->txt2, 16, 114, 412);

    for (int i = 0; i < ROWS; i++) {
        if (!row_shown(i)) {
            continue;
        }
        int y = ROW_Y0 + i * ROW_H;
        bool on = row_enabled(i);
        lv_obj_t *r = card(S.root, 20, y, 440, ROW_H - 8, 14, t->surf);
        lv_obj_set_clickable(r, true);
        lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_color_t tc = on ? (i == R_RESET ? lv_color_hex(0xD6544A) : t->txt) : t->mute;
        text_at(r, ROW_T[i], FLEX_FONT_S2, tc, 16, 10, 400);
        char vb[56];
        text_at(r, row_sub(i, vb, sizeof(vb)), FLEX_FONT_S1, on ? t->txt2 : t->mute, 16, 34, 400);
    }
    // Arduino decia "Un arranque estable limpia el contador solo", pero en Modo
    // seguro no se limpia nunca solo (§11.6): se dice como se sale de verdad.
    l = flex_label(S.root, "Para salir: Reiniciar normalmente", FLEX_FONT_S1, t->mute);
    flex_label_cap_center(l, SCR_W / 2, SCR_H - 40);
}

void flex_safe_open(void)
{
    if (!flex_safe_mode()) {
        return;
    }
    if (!S.root) {
        S.root = flex_box(flex_shell_screen());
        lv_obj_set_size(S.root, SCR_W, SCR_H);
        lv_obj_set_style_bg_opa(S.root, LV_OPA_COVER, 0);
        lv_obj_set_clickable(S.root, true);   // nada de lo de debajo recibe toques
    }
    lv_obj_set_hidden(S.root, false);
    lv_obj_move_foreground(S.root);
    flex_shell_safe_begin();
    build();
}

void flex_safe_close_now(void)
{
    if (S.root) {
        lv_obj_delete(S.root);
        S.root = NULL;
        flex_shell_safe_end();
    }
}

bool flex_safe_screen_active(void)
{
    return S.root && !lv_obj_is_hidden(S.root);
}

// ---- app no permitida (safeDenyApp) ---------------------------------------------------
bool flex_safe_app_allowed(int id)
{
    return !flex_safe_mode() || id == IC_AJUSTES || id == IC_ALMACEN || id == IC_RELOJ || id == IC_CALC;
}

static void toast_off(lv_timer_t *t)
{
    (void)t;
    S.toast_tmr = NULL;
    if (S.toast) {
        lv_obj_delete(S.toast);
        S.toast = NULL;
    }
}

void flex_safe_deny_app(int id)
{
    if (S.toast_tmr) {
        lv_timer_delete(S.toast_tmr);   // un aviso nuevo reemplaza al anterior
    }
    toast_off(NULL);
    S.toast = card(lv_layer_top(), 28, 308, 424, 60, 16, lv_color_make(24, 26, 36));
    lv_obj_set_style_bg_opa(S.toast, 240, 0);
    lv_obj_t *l = flex_label(S.toast, "No disponible en Modo seguro", FLEX_FONT_S2, lv_color_make(240, 244, 252));
    flex_label_cap_center(l, 212, 10);
    l = flex_label(S.toast, flex_app_name(id), FLEX_FONT_S1, lv_color_make(170, 178, 196));
    flex_label_cap_center(l, 212, 34);
    S.toast_tmr = lv_timer_create(toast_off, 1800, NULL);
    lv_timer_set_repeat_count(S.toast_tmr, 1);
}

bool flex_safe_toast_visible(void)
{
    return S.toast != NULL;
}
