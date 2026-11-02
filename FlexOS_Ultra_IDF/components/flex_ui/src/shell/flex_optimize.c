// Flex OS Ultra · "Optimizar Flex OS" (docs/spec/01c §17.8; Arduino: System.h:133-393).
//
// Panel modal de cinco etapas (una cada 420 ms; el trabajo de cada etapa corre al ENTRAR en ella):
// analizar, liberar la cache temporal y lo reconstruible de las apps suspendidas, soltar caches del
// sistema, verificar (y, si la presion sigue alta, modo visual eficiente temporal) y resultado.
// No borra notas, dibujos, ajustes, archivos ni apps; no reinicia; no cierra la app activa.
// Lo abren Almacenamiento y Device Care (flex_optimize_open con su aviso de cierre).
#include <stdio.h>
#include <string.h>
#include "FlexOS_Mem.h"
#include "flex_app.h"
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"

#define OPT_X 30
#define OPT_Y 214
#define OPT_W 420
#define OPT_H 372
#define OPT_STEP_MS 420
#define OPT_CACHE_DIR "/System/Cache"

enum { OPT_IDLE = 0, OPT_ANALYZE, OPT_CACHE, OPT_SUSPEND, OPT_VERIFY, OPT_DONE };

uint32_t flex_mem_shed_system_now(void);
uint32_t flex_mem_shed_app_now(int id);
void flex_mem_sample_now(void);
const FlexMemSnap *flex_mem_snap(void);

static const char *const STEP_TXT[6] = {"", "Analizando memoria", "Liberando cach\xC3\xA9 temporal", "Suspendiendo recursos no usados",
                                         "Verificando estabilidad", "Optimizaci\xC3\xB3n finalizada"};

static struct {
    lv_obj_t *root, *panel;
    int stage;
    uint32_t step_ms, free0, gained;
    int cache_n;
    bool eff_on;
    lv_timer_t *timer;
    void (*done_cb)(void);
} O;

static void render(void);

static void wipe_done(esp_err_t err, size_t count, void *user)
{
    (void)user;
    if (err == ESP_OK) {
        O.cache_n = (int)count;
        if (O.root && O.stage == OPT_DONE) {
            render();   // el resultado ya estaba a la vista: se completa la linea de la cache
        }
    }
}

static void finish(void)
{
    if (O.timer) {
        lv_timer_delete(O.timer);
        O.timer = NULL;
    }
    if (O.root) {
        lv_obj_delete(O.root);
    }
    O.root = O.panel = NULL;
    O.stage = OPT_IDLE;
    flex_touch_drop_all();
    void (*cb)(void) = O.done_cb;
    O.done_cb = NULL;
    if (cb) {
        cb();   // lo abrio otra pantalla: vuelve alli
    }
}

static void root_click_cb(lv_event_t *e)
{
    (void)e;
    if (O.stage == OPT_DONE) {
        finish();   // "Hecho" o un toque fuera del panel: las dos cierran
    }
}

static void mark(lv_obj_t *p, int x, int cy, bool done, bool cur)
{
    const flex_palette_t *t = flex_th();
    lv_obj_t *m = flex_box(p);
    lv_obj_set_pos(m, x - 8, cy - 8);
    lv_obj_set_size(m, 16, 16);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, 0);
    if (done) {
        lv_obj_set_style_bg_color(m, t->ok, 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        return;
    }
    lv_obj_set_style_border_width(m, 1, 0);
    lv_obj_set_style_border_color(m, cur ? t->primary : t->divider, 0);
    if (cur) {
        lv_obj_t *d = flex_box(m);
        lv_obj_set_size(d, 8, 8);
        lv_obj_center(d);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, t->primary, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
}

static lv_obj_t *line(lv_obj_t *p, const char *s, const lv_font_t *f, lv_color_t c, int x, int y, int clip_w)
{
    lv_obj_t *l = flex_label(p, s, f, c);
    lv_obj_set_x(l, x);
    if (clip_w > 0) {
        flex_label_one_line(l, clip_w);
    }
    flex_label_cap_y(l, y);
    return l;
}

// Todo el panel es modal: ningun hijo recoge el toque, lo recoge la raiz (cualquier toque cierra al final)
static void unclick(lv_obj_t *o)
{
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        lv_obj_set_clickable(c, false);
        unclick(c);
    }
}

static void render(void)
{
    if (!O.panel) {
        return;
    }
    lv_obj_clean(O.panel);
    const flex_palette_t *t = flex_th();
    lv_obj_t *ttl = flex_label(O.panel, "Optimizar Flex OS", FLEX_FONT_S3, t->txt);
    flex_label_cap_center(ttl, OPT_W / 2, 20);
    int y = 62;
    for (int st = OPT_ANALYZE; st <= OPT_DONE; st++) {
        bool done = O.stage > st || (O.stage == OPT_DONE && st == OPT_DONE);
        bool cur = O.stage == st;
        mark(O.panel, 30, y + 9, done, cur);
        line(O.panel, STEP_TXT[st], FLEX_FONT_S2, cur ? t->txt : (done ? t->txt2 : t->mute), 50, y, OPT_W - 66);
        y += 30;
    }
    y += 10;
    if (O.stage == OPT_DONE) {
        char l[96];
        if (O.gained > 0) {
            char g[24];
            flexMemFmt(O.gained, g, sizeof(g));
            snprintf(l, sizeof(l), "Se liberaron %s de recursos temporales.", g);
        } else {
            snprintf(l, sizeof(l), "No se encontraron recursos temporales seguros para liberar.");
        }
        line(O.panel, l, FLEX_FONT_S1, t->txt, 20, y, OPT_W - 40);
        y += 20;
        if (O.cache_n > 0) {
            snprintf(l, sizeof(l), "Cach\xC3\xA9 temporal en almacenamiento: %d archivo%s.", O.cache_n, O.cache_n == 1 ? "" : "s");
            line(O.panel, l, FLEX_FONT_S1, t->txt2, 20, y, OPT_W - 40);
            y += 20;
        }
        const FlexMemSnap *m = flex_mem_snap();
        if (m->psTotal) {
            char fr[24];
            flexMemFmt(m->psFree, fr, sizeof(fr));
            snprintf(l, sizeof(l), "PSRAM disponible: %s.", fr);
        } else {
            snprintf(l, sizeof(l), "PSRAM: No disponible en esta placa.");
        }
        line(O.panel, l, FLEX_FONT_S1, t->txt, 20, y, OPT_W - 40);
        y += 20;
        if (O.eff_on) {
            line(O.panel, "Modo visual eficiente activado temporalmente.", FLEX_FONT_S1, t->warn, 20, y, OPT_W - 40);
        }
        lv_obj_t *b = flex_box(O.panel);
        lv_obj_set_pos(b, 120, OPT_H - 46 - 18);
        lv_obj_set_size(b, 180, 46);
        lv_obj_set_style_radius(b, 23, 0);
        lv_obj_set_style_bg_color(b, t->primary, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_t *bl = flex_label(b, "Hecho", FLEX_FONT_S2, t->on_acc);
        flex_label_cap_center(bl, 90, 14);
    } else {
        line(O.panel, "No se borran notas, dibujos ni archivos.", FLEX_FONT_S1, t->mute, 20, y, OPT_W - 40);
    }
    unclick(O.panel);
}

// UNA etapa por vuelta, separada por tiempo; el trabajo corre una sola vez, al ENTRAR en ella
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (O.stage == OPT_IDLE || O.stage == OPT_DONE || lv_tick_elaps(O.step_ms) < OPT_STEP_MS) {
        return;
    }
    O.step_ms = lv_tick_get();
    switch (O.stage) {
    case OPT_ANALYZE:
        flex_mem_sample_now();
        O.stage = OPT_CACHE;
        break;
    case OPT_CACHE:
        // la carpeta que el propio sistema declara desechable; no se toca ninguna otra. El borrado
        // va por la tarea de almacenamiento (nunca la flash desde la UI) y su cuenta llega despues
        flex_fs_wipe_dir_async(OPT_CACHE_DIR, wipe_done, NULL);
        for (int i = 0; i < FLEX_APP_N; i++) {
            flex_mem_shed_app_now(i);   // solo las SUSPENDIDAS y sin trabajo real
        }
        O.stage = OPT_SUSPEND;
        break;
    case OPT_SUSPEND:
        flex_mem_shed_system_now();
        O.stage = OPT_VERIFY;
        break;
    case OPT_VERIFY: {
        flex_mem_sample_now();
        // modo visual eficiente: solo si la presion SIGUE alta despues de soltar (temporal, no se guarda)
        if (flexMemLevel(flex_mem_snap()) >= FLEXMEM_LV_WARN && !flex_look()->eff_mode) {
            flex_theme_set_eff_mode(true);
            O.eff_on = true;
        }
        uint32_t now = flex_mem_snap()->psFree;
        O.gained = now > O.free0 ? now - O.free0 : 0u;
        O.stage = OPT_DONE;
        break;
    }
    default:
        break;
    }
    render();
}

void flex_optimize_open(void (*on_done)(void))
{
    if (O.stage != OPT_IDLE) {
        return;
    }
    O.done_cb = on_done;
    O.stage = OPT_ANALYZE;
    O.step_ms = lv_tick_get();
    O.gained = 0;
    O.cache_n = 0;
    O.eff_on = false;
    flex_mem_sample_now();
    O.free0 = flex_mem_snap()->psFree;
    flex_touch_drop_all();   // el toque que abrio el panel no se filtra
    O.root = flex_box(flex_shell_screen());
    lv_obj_set_size(O.root, 480, 800);
    lv_obj_set_clickable(O.root, true);   // modal: durante el trabajo cualquier toque se traga
    lv_obj_add_event_cb(O.root, root_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(O.root);
    O.panel = flex_box(O.root);
    lv_obj_set_pos(O.panel, OPT_X, OPT_Y);
    lv_obj_set_size(O.panel, OPT_W, OPT_H);
    lv_obj_set_style_radius(O.panel, 24, 0);
    flex_surface(O.panel, FLEX_SURF_ELEVATED, FLEX_BD_FLAT);
    lv_obj_set_style_border_width(O.panel, 1, 0);
    lv_obj_set_style_border_color(O.panel, flex_th()->border, 0);
    lv_obj_set_clickable(O.panel, false);
    O.timer = lv_timer_create(tick_cb, 40, NULL);
    render();
}

bool flex_optimize_active(void)
{
    return O.stage != OPT_IDLE;
}

int flex_optimize_stage(void)
{
    return O.stage;
}

void flex_optimize_close_now(void)   // bloquear o suspender no deben dejar el panel colgado
{
    if (O.stage != OPT_IDLE) {
        finish();
    }
}
