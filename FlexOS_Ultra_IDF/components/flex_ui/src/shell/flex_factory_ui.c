// Flex OS Ultra · asistente de restablecimiento de fabrica (docs/spec/01c §13.2-13.5;
// Arduino: FlexOS_Ultra_Recovery.h:48-440).
//
// Aviso -> (clave, o escribir RESTABLECER sin clave) -> deslizar -> progreso
// por etapas -> "Listo" y reinicio; o pantalla de fallo con Reintentar /
// Reiniciar. El borrado lo hace flex_reset (flex_system) en el escritor de
// almacenamiento; aqui solo se confirma y se ensena el progreso. Nada se borra
// hasta soltar el deslizador al 92 % (ficha de un solo uso). Cancelar en
// cualquier paso previo vuelve a donde se estaba sin haber tocado ningun dato.
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_auth.h"
#include "flex_frame.h"
#include "flex_kb.h"
#include "flex_reset.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define SCR_W 480
#define SCR_H 800
#define WORD  "RESTABLECER"
#define WORD_MAX 19
#define STEP_MIN_MS 260   // FR_STEP_MIN_MS: cada etapa se ve al menos esto
#define DONE_MS     1200  // "Listo / Reiniciando..." antes de reiniciar

// Deslizador (FRS_*): pista 400x72 en (40,600), pomo de 60, recorrido 328
#define SL_X 40
#define SL_Y 600
#define SL_W 400
#define SL_H 72
#define SL_PAD 6
#define SL_D 60
#define SL_RUN (SL_W - 2 * SL_PAD - SL_D)
#define SL_DONE_PCT 92

enum { V_NONE = 0, V_INTRO, V_TYPE, V_SLIDE, V_RUN, V_FAIL };

static struct {
    lv_obj_t *root, *content, *kb, *field, *btn, *btn_l, *knob, *step_l, *name_l, *seg[FLEX_FR_STEPS];
    lv_timer_t *tick;
    int view;
    bool from_safe;
    flex_shell_state_t prev;
    char typed[WORD_MAX + 1];
    int knob_x, grab;
    bool drag;
    uint32_t token;
    int latest, shown, err;   // progreso: lo ultimo que dijo el motor y lo que se ensena
    uint32_t shown_ms;
    bool done_shown;
    uint32_t done_ms;
} F;

// ---- colores propios del asistente (Recovery.h:73-76) ------------------------------------
static bool dark(void)
{
    return flex_look()->dark;
}
static lv_color_t c_bg(void) { return dark() ? lv_color_hex(0x10121A) : lv_color_hex(0xF6F8FC); }
static lv_color_t c_hi(void) { return dark() ? lv_color_hex(0xF0F2F8) : lv_color_hex(0x14161E); }
static lv_color_t c_lo(void) { return dark() ? lv_color_hex(0xA0A6B6) : lv_color_hex(0x6E7484); }
static lv_color_t c_card(void) { return dark() ? lv_color_hex(0x1E222E) : lv_color_hex(0xFFFFFF); }
#define C_RED    lv_color_hex(0xC83C3C)
#define C_BULLET lv_color_hex(0xDC5050)
#define C_OK     lv_color_hex(0x5ABE82)
#define C_KNOB   lv_color_hex(0xD2423C)
#define C_WARN   lv_color_hex(0xDC6E5A)
#define C_FAILST lv_color_hex(0xE6785A)

static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, lv_color_t c, bool fill)
{
    lv_obj_t *o = flex_box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    if (fill) {
        lv_obj_set_style_bg_color(o, c, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *text_c(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    flex_label_cap_center(l, SCR_W / 2, y);
    return l;
}

static lv_obj_t *text_at(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    lv_obj_set_x(l, x);
    flex_label_cap_y(l, y);
    return l;
}

static lv_obj_t *button(int x, int y, int w, lv_color_t bg, const char *s, lv_color_t tc, lv_event_cb_t cb)
{
    lv_obj_t *b = box(F.content, x, y, w, 58, 29, bg, true);
    lv_obj_set_clickable(b, true);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = flex_label(b, s, FLEX_FONT_S2, tc);
    flex_label_cap_center(l, w / 2, 19);
    return b;
}

static void show(int view);

// ---- salir sin haber tocado nada (frCancelToSettings) --------------------------------------
static void destroy(void)
{
    if (F.tick) {
        lv_timer_delete(F.tick);
        F.tick = NULL;
    }
    if (F.root) {
        lv_obj_delete(F.root);
        F.root = F.content = F.kb = NULL;
    }
    F.view = V_NONE;
    F.drag = false;
    F.token = 0;
    memset(F.typed, 0, sizeof(F.typed));
}

static void cancel_to_origin(void)
{
    bool safe = F.from_safe;
    flex_shell_state_t prev = F.prev;
    destroy();
    flex_shell_factory_end(prev);
    if (safe) {
        flex_safe_open();
    }
}

static void cancel_async(void *arg)
{
    (void)arg;
    if (F.view == V_INTRO || F.view == V_TYPE || F.view == V_SLIDE) {
        cancel_to_origin();
    }
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(cancel_async, NULL);   // la vista se borra: fuera del evento de su boton
}

// ---- aviso (frDrawIntro) --------------------------------------------------------------------
static void pin_ok(void *ctx)
{
    (void)ctx;
    flex_factory_reset_open(F.from_safe);   // la capa vuelve (la clave la tapaba)
    show(V_SLIDE);
}

static void pin_cancel(void *ctx)
{
    (void)ctx;
    flex_factory_reset_open(F.from_safe);
    cancel_to_origin();   // cancelar la clave sale del asistente (Ajustes / Modo seguro)
}

static void continue_cb(lv_event_t *e)
{
    (void)e;
    if (flex_auth_required()) {
        flex_auth_req_t req = {.on_ok = pin_ok, .on_cancel = pin_cancel};
        if (flex_auth_verify(&req)) {
            lv_obj_set_hidden(F.root, true);   // la clave va en la pantalla, debajo de esta capa
            return;
        }
    }
    show(V_TYPE);   // sin clave: escribir RESTABLECER
}

static void build_intro(lv_obj_t *p)
{
    static const char *const ITEMS[] = {
        "Cuentas locales y sus tokens",
        "Redes Wi-Fi guardadas y sus claves",
        "PIN o contrase\xC3\xB1" "a de bloqueo",
        "Ajustes, fondo y personalizaci\xC3\xB3n",
        "Notas, dibujos y archivos del usuario",
        "Apps instaladas y sus datos",
        "Sesiones abiertas e historial",
        "V\xC3\xADnculo con el tel\xC3\xA9" "fono y Flex Storage",
    };
    text_c(p, "Restablecer datos", FLEX_FONT_S3, c_hi(), 44);
    text_c(p, "de f\xC3\xA1" "brica", FLEX_FONT_S3, c_hi(), 76);
    lv_obj_t *c1 = box(p, 20, 124, 440, 300, 16, c_card(), true);
    text_at(c1, "Se eliminar\xC3\xA1n de este dispositivo:", FLEX_FONT_S2, c_hi(), 16, 16);
    for (int i = 0; i < 8; i++) {
        int y = 46 + i * 22;
        box(c1, 22 - 3, y + 8 - 3, 6, 6, 3, C_BULLET, true);
        text_at(c1, ITEMS[i], FLEX_FONT_S1, c_lo(), 34, y);
    }
    lv_obj_t *c2 = box(p, 20, 440, 440, 92, 16, c_card(), true);
    text_at(c2, "NO se elimina:", FLEX_FONT_S2, c_hi(), 16, 12);
    text_at(c2, "El firmware instalado sigue siendo el mismo.", FLEX_FONT_S1, c_lo(), 16, 40);
    text_at(c2, "No se vuelve a una versi\xC3\xB3n anterior por OTA.", FLEX_FONT_S1, c_lo(), 16, 60);
    button(28, 704, 202, c_card(), "Cancelar", c_hi(), cancel_cb);
    button(250, 704, 202, C_RED, "Continuar", lv_color_white(), continue_cb);
}

// ---- escribir RESTABLECER (frDrawType, solo sin clave) -----------------------------------
static bool word_ok(void)
{
    return strcmp(F.typed, WORD) == 0;
}

static void type_refresh(void)
{
    if (!F.field) {
        return;
    }
    bool ok = word_ok();
    lv_label_set_text(F.field, F.typed[0] ? F.typed : "...");
    lv_obj_set_style_text_color(F.field, ok ? C_OK : c_hi(), 0);
    lv_obj_set_style_bg_color(F.btn, ok ? C_RED : c_card(), 0);
    lv_obj_set_style_text_color(F.btn_l, ok ? lv_color_white() : c_lo(), 0);
}

static void kb_text(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    (void)user;
    for (const char *s = utf8; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        size_t n = strlen(F.typed);
        if (ch < 0x80 && isalpha(ch) && n < WORD_MAX) {   // solo A-Z, en mayuscula
            F.typed[n] = (char)toupper(ch);
            F.typed[n + 1] = 0;
        }
    }
    type_refresh();
}

static void kb_back(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    size_t n = strlen(F.typed);
    if (n) {
        F.typed[n - 1] = 0;
    }
    type_refresh();
}

static void type_go(void *arg)
{
    (void)arg;
    if (F.view == V_TYPE && word_ok()) {
        show(V_SLIDE);
    }
}

static void kb_enter(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    lv_async_call(type_go, NULL);   // el teclado se borra al cambiar de vista
}

static void type_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(type_go, NULL);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    lv_async_call(cancel_async, NULL);
}

static void build_type(lv_obj_t *p)
{
    lv_obj_t *bk = box(p, 0, 0, 48, 48, 0, c_bg(), false);
    lv_obj_set_clickable(bk, true);
    lv_obj_add_event_cb(bk, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ar = flex_label(bk, LV_SYMBOL_LEFT, FLEX_FONT_S3, c_hi());
    lv_obj_set_pos(ar, 14, 8);
    text_c(p, "Confirma el borrado", FLEX_FONT_S3, c_hi(), 46);
    text_c(p, "Este equipo no tiene bloqueo configurado.", FLEX_FONT_S1, c_lo(), 84);
    text_c(p, "Escribe RESTABLECER para continuar.", FLEX_FONT_S1, c_lo(), 104);
    lv_obj_t *fb = box(p, 32, 150, 416, 56, 12, c_card(), true);
    F.field = flex_label(fb, "...", FLEX_FONT_S3, c_hi());
    flex_label_cap_center(F.field, 208, 18);
    F.btn = box(p, 32, 230, 416, 58, 29, c_card(), true);
    lv_obj_set_clickable(F.btn, true);
    lv_obj_add_event_cb(F.btn, type_btn_cb, LV_EVENT_CLICKED, NULL);
    F.btn_l = flex_label(F.btn, "Continuar", FLEX_FONT_S2, c_lo());
    flex_label_cap_center(F.btn_l, 208, 19);
    flex_kb_cfg_t kc = {.flags = FLEX_KB_F_PAGE | FLEX_KB_F_NO_FAST, .backdrop = FLEX_BD_FLAT,
                        .on_text = kb_text, .on_backspace = kb_back, .on_enter = kb_enter};
    F.kb = flex_kb_create(p, &kc);
    flex_kb_set_lang_es(F.kb, true);
    flex_kb_set_shift(F.kb, true);   // mayusculas
    type_refresh();
}

// ---- ultimo paso (frDrawSlide) --------------------------------------------------------------
static void knob_place(void)
{
    if (F.knob) {
        lv_obj_set_x(F.knob, SL_X + SL_PAD + F.knob_x);
    }
}

static void wipe_progress(int stage, int err, void *user);

static void begin_wipe(void *arg)
{
    (void)arg;
    // ARMED en la interfaz: ninguna app sigue abierta ni en Recientes
    for (int id = 0; id < FLEX_APP_N; id++) {
        if (flex_app_is_open(id)) {
            flex_app_terminate(id);
        }
    }
    F.latest = F.shown = FLEX_FR_ARMED;
    F.shown_ms = lv_tick_get();
    F.err = 0;
    F.done_shown = false;
    uint32_t t = F.token;
    F.token = 0;
    show(V_RUN);
    if (!flex_reset_start(t, wipe_progress, NULL)) {
        F.latest = FLEX_FR_FAIL;   // la ficha no valia o no se pudo arrancar: nada borrado
        F.err = 0;
    }
}

static void slide_cb(lv_event_t *e)
{
    if (F.view != V_SLIDE) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int kx = SL_X + SL_PAD + F.knob_x;
    if (code == LV_EVENT_PRESSED) {
        if (pt.y >= SL_Y && pt.y <= SL_Y + SL_H && pt.x >= kx - 24 && pt.x <= kx + 84) {
            F.drag = true;
            F.grab = pt.x - kx;
        }
    } else if (code == LV_EVENT_PRESSING && F.drag) {
        int v = pt.x - F.grab - (SL_X + SL_PAD);
        F.knob_x = v < 0 ? 0 : v > SL_RUN ? SL_RUN : v;
        knob_place();
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && F.drag) {
        F.drag = false;
        if (code == LV_EVENT_RELEASED && F.knob_x * 100 / SL_RUN >= SL_DONE_PCT) {
            lv_async_call(begin_wipe, NULL);
            return;
        }
        F.knob_x = 0;   // vuelve de golpe (sin animacion, como Arduino)
        knob_place();
    }
}

static void build_slide(lv_obj_t *p)
{
    F.token = flex_reset_token();
    F.knob_x = 0;
    text_c(p, "\xC3\x9Altimo paso", FLEX_FONT_S3, c_hi(), 120);
    lv_obj_t *c = box(p, 24, 180, 432, 150, 16, c_card(), true);
    (void)c;
    text_c(p, "Se borrar\xC3\xA1 todo el contenido", FLEX_FONT_S2, c_hi(), 202);
    text_c(p, "y la configuraci\xC3\xB3n de este", FLEX_FONT_S2, c_hi(), 230);
    text_c(p, "dispositivo.", FLEX_FONT_S2, c_hi(), 258);
    text_c(p, "Esta acci\xC3\xB3n no se puede deshacer.", FLEX_FONT_S1, C_WARN, 292);
    box(p, SL_X, SL_Y, SL_W, SL_H, SL_H / 2, c_card(), true);
    text_c(p, "Desliza para restablecer", FLEX_FONT_S2, c_lo(), 626);
    F.knob = box(p, 0, SL_Y + SL_PAD, SL_D, SL_D, SL_D / 2, C_KNOB, true);
    lv_obj_t *ch = flex_label(F.knob, LV_SYMBOL_RIGHT, FLEX_FONT_S2, lv_color_white());
    lv_obj_center(ch);
    knob_place();
    button(28, 704, 424, c_card(), "Cancelar", c_hi(), cancel_cb);
}

// ---- progreso (frDrawRun) y fallo (frDrawFail) ----------------------------------------------
static void run_refresh(void)
{
    if (F.view != V_RUN || !F.step_l) {
        return;
    }
    if (F.shown == FLEX_FR_DONE) {
        lv_obj_clean(F.content);
        F.step_l = F.name_l = NULL;
        text_c(F.content, "Listo", FLEX_FONT_S3, c_hi(), 380);
        text_c(F.content, "Reiniciando...", FLEX_FONT_S2, c_lo(), 420);
        return;
    }
    char b[32];
    snprintf(b, sizeof(b), "Paso %d de %d", flex_fr_step_index(F.shown), FLEX_FR_STEPS);
    lv_label_set_text(F.step_l, b);
    lv_label_set_text(F.name_l, flex_fr_stage_name(F.shown));
    for (int i = 0; i < FLEX_FR_STEPS; i++) {
        lv_obj_set_style_bg_color(F.seg[i], i < flex_fr_step_index(F.shown) ? C_RED : c_card(), 0);
    }
}

static void build_run(lv_obj_t *p)
{
    text_c(p, "Restableciendo", FLEX_FONT_S3, c_hi(), 240);
    F.step_l = text_c(p, "", FLEX_FONT_S2, c_lo(), 300);
    F.name_l = text_c(p, "", FLEX_FONT_S2, c_hi(), 336);
    for (int i = 0; i < FLEX_FR_STEPS; i++) {
        F.seg[i] = box(p, 48 + i * 55, 396, 49, 10, 5, c_card(), true);
    }
    text_c(p, "No apagues el dispositivo", FLEX_FONT_S1, c_lo(), 440);
    run_refresh();
}

static void retry_cb(lv_event_t *e)
{
    (void)e;
    if (F.view != V_FAIL) {
        return;
    }
    F.latest = F.shown = FLEX_FR_ARMED;
    F.shown_ms = lv_tick_get();
    F.err = 0;
    show(V_RUN);
    if (!flex_reset_retry(wipe_progress, NULL)) {
        F.latest = FLEX_FR_FAIL;
    }
}

static void reboot_cb(lv_event_t *e)
{
    (void)e;
    flex_reset_reboot();   // con el marcador en FAIL el arranque vuelve aqui
}

static void build_fail(lv_obj_t *p)
{
    text_c(p, "Restablecimiento", FLEX_FONT_S3, c_hi(), 200);
    text_c(p, "incompleto", FLEX_FONT_S3, c_hi(), 236);
    text_c(p, "Fallo en:", FLEX_FONT_S2, c_lo(), 300);
    text_c(p, flex_fr_stage_name(F.err ? F.err : FLEX_FR_ARMED), FLEX_FONT_S2, C_FAILST, 330);
    text_c(p, "El dispositivo se queda en recuperaci\xC3\xB3n:", FLEX_FONT_S1, c_lo(), 380);
    text_c(p, "no arranca con datos a medias.", FLEX_FONT_S1, c_lo(), 400);
    button(28, 628, 424, C_RED, "Reintentar", lv_color_white(), retry_cb);
    button(28, 704, 424, c_card(), "Reiniciar", c_hi(), reboot_cb);
}

// El motor avisa (por el buzon): etapa en curso, DONE o FAIL.
static void wipe_progress(int stage, int err, void *user)
{
    (void)user;
    if (stage > F.latest || stage == FLEX_FR_FAIL || stage == FLEX_FR_DONE) {
        F.latest = stage;
        F.err = err;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (F.view != V_RUN) {
        return;
    }
    uint32_t now = lv_tick_get();
    if (F.done_shown) {
        if (now - F.done_ms >= DONE_MS) {
            F.done_shown = false;
            flex_reset_reboot();   // en el firmware no vuelve
        }
        return;
    }
    if (F.latest == F.shown || now - F.shown_ms < STEP_MIN_MS) {
        return;
    }
    if (F.latest == FLEX_FR_FAIL) {
        show(V_FAIL);
        return;
    }
    // una etapa cada STEP_MIN_MS como poco, sin saltarse ninguna en pantalla
    F.shown = F.latest == FLEX_FR_DONE && F.shown >= FLEX_FR_DEFAULTS ? FLEX_FR_DONE
                                                                      : (F.shown < F.latest ? F.shown + 1 : F.latest);
    F.shown_ms = now;
    run_refresh();
    if (F.shown == FLEX_FR_DONE) {
        F.done_shown = true;
        F.done_ms = now;
    }
}

// ---- vistas ---------------------------------------------------------------------------------
static void show(int view)
{
    if (!F.root) {
        return;
    }
    lv_obj_set_hidden(F.root, false);
    lv_obj_clean(F.root);
    F.kb = F.field = F.btn = F.btn_l = F.knob = F.step_l = F.name_l = NULL;
    F.drag = false;
    F.view = view;
    lv_obj_set_style_bg_color(F.root, c_bg(), 0);
    F.content = flex_box(F.root);
    lv_obj_set_size(F.content, SCR_W, SCR_H);
    lv_obj_set_clickable(F.content, false);
    switch (view) {
    case V_INTRO: build_intro(F.content); break;
    case V_TYPE: build_type(F.content); break;
    case V_SLIDE: build_slide(F.content); break;
    case V_RUN: build_run(F.content); break;
    case V_FAIL: build_fail(F.content); break;
    default: break;
    }
    flex_touch_drop_all();   // el dedo que cambio de vista no toca la nueva
}

static void create_root(void)
{
    if (F.root) {
        return;
    }
    F.root = flex_box(flex_shell_screen());   // en la pantalla: la clave se pone encima
    lv_obj_set_size(F.root, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(F.root, LV_OPA_COVER, 0);
    lv_obj_set_clickable(F.root, true);   // nada de lo de debajo recibe toques
    lv_obj_add_event_cb(F.root, slide_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(F.root, slide_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(F.root, slide_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(F.root, slide_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_move_foreground(F.root);
    F.tick = lv_timer_create(tick_cb, 30, NULL);
}

bool flex_factory_reset_available(void)
{
    return true;
}

void flex_factory_reset_open(bool from_safe)
{
    if (F.root) {
        lv_obj_set_hidden(F.root, false);   // vuelta de la clave: la vista sigue
        lv_obj_move_foreground(F.root);
        flex_shell_factory_begin();
        return;
    }
    // Sin OTA todavia (Fase 14): el aviso "Actualizacion en curso" no puede darse.
    F.from_safe = from_safe;
    F.prev = flex_shell_state();
    flex_shell_factory_begin();
    create_root();
    show(V_INTRO);
}

// Arranque con un borrado a medias (o fallido): directo al progreso o al fallo.
void flex_factory_resume_boot(void)
{
    const flex_fr_marker_t *m = flex_reset_marker();
    F.from_safe = false;
    F.prev = FLEX_SH_HOME;
    flex_shell_factory_begin();
    create_root();
    if (m->stage == FLEX_FR_FAIL) {
        F.err = m->err;
        F.latest = F.shown = FLEX_FR_FAIL;
        show(V_FAIL);
        return;
    }
    F.latest = F.shown = m->stage;
    F.shown_ms = lv_tick_get();
    F.done_shown = false;
    show(V_RUN);
    if (!flex_reset_resume(wipe_progress, NULL)) {
        F.latest = FLEX_FR_FAIL;
    }
}

bool flex_factory_active(void)
{
    return F.root != NULL;
}

int flex_factory_view(void)
{
    return F.view;
}

int flex_factory_shown_stage(void)
{
    return F.shown;
}

int flex_factory_knob(void)
{
    return F.knob_x;
}

#ifdef FLEX_SIM
void flex_factory_sim_reset(void)
{
    destroy();
    F.latest = F.shown = F.err = 0;
    F.done_shown = false;
    flex_shell_factory_end(FLEX_SH_HOME);
}
#endif
