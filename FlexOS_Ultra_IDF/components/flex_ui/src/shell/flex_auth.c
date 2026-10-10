// Flex OS Ultra · pantallas de la clave del sistema (docs/spec/01a §5;
// Arduino: FlexOS_Ultra_Power.h:30-660 y FlexOS_Ultra_Lock.h:40-340).
//
//   verificar:  fondo = escritorio + velo rgb(8,10,18) a70, colores "sobre
//               wallpaper" (iguales en claro y oscuro); fundido 190 + 230 ms.
//   crear:      fondo de pagina y colores de la paleta (desde Ajustes).
//
// La derivacion del hash va en otra tarea (flex_lock_verify_async): la UI sigue
// animando mientras tanto y el ultimo punto se pinta ANTES de arrancarla.
#include "flex_auth.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "flex_frame.h"
#include "flex_glass.h"
#include "flex_kb.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define FADE_OUT_MS   190
#define FADE_IN_MS    230
#define REVEAL_MS     400
#define SHAKE_MS      180
#define SHAKE_AMP     12.0f
#define SHAKE_CYC     3.0f
#define WRONG_MS      500
#define PRESS_MS      200
#define KB_SLIDE_MS   300
#define PIN_MAX       8
#define PIN_MIN       4
#define PASS_MIN      4
#define RETRY_MS      20
#define RETRY_MAX     50

// Banda del contador de espera (Lock.h:224-231)
#define LW_BAND_Y0 180
#define LW_MSG_Y   186
#define LW_MSG2_Y  206
#define LW_NUM_Y   232

typedef enum { M_NONE = 0, M_SEL, M_PIN, M_PASS } auth_mode_t;

static const char *const PIN_KEYS[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK"};

static struct {
    lv_obj_t *root, *bg, *content, *band;
    lv_obj_t *dots[PIN_MAX];
    lv_obj_t *flash[12];
    lv_obj_t *wait_box, *wait_m2, *wait_num;
    lv_obj_t *pass_dots;
    lv_obj_t *kb;                        // teclado del sistema (contrasena)
    lv_timer_t *tick, *retry;
    auth_mode_t mode;
    bool verify;
    flex_auth_req_t req;
    void (*setup_done)(void *ctx);
    void *setup_ctx;
    char secret[FLEX_LOCK_SECRET_MAX];   // lo tecleado (se borra al arrancar la verificacion)
    int n;                               // digitos/caracteres que se ven como puntos
    int saved_len;                       // "locklen": autoconfirmar el PIN
    bool checking;
    int retries;
    uint32_t gen;
    uint32_t wrong_ms;
    bool wrong;
    bool shaking;
    bool revealing;
    int last_wait_sec;
} A;

// Espera progresiva: vive mas que la pantalla (salir no perdona la espera) y
// arranca "no cobrada" en cada arranque (reiniciar no es via de escape).
static struct {
    bool on;
    uint32_t until;
    bool served;
} W;

// ---- colores segun lo que hay debajo (Lock.h:64-72) -------------------------------
static lv_color_t txt_hi(void) { return A.verify ? FLEX_ONWALL : flex_th()->txt; }
static lv_color_t txt_lo(void) { return A.verify ? FLEX_ONWALL2 : flex_th()->txt2; }

static uint32_t now_ms(void) { return lv_tick_get(); }

bool flex_auth_required(void)
{
    return flex_cfg_get_i32("locktype", 0) != 0;
}

bool flex_auth_active(void)
{
    return A.root != NULL;
}

bool flex_auth_from_lock(void)
{
    return A.root != NULL && A.verify && A.req.from_lock;
}

// ---- espera progresiva (Lock.h:238-336) -------------------------------------------
static bool wait_active(void)
{
    if (!W.on) {
        return false;
    }
    if ((int32_t)(now_ms() - W.until) >= 0) {
        W.on = false;
        W.served = true;   // cumplida
        return false;
    }
    return true;
}

uint32_t flex_auth_wait_left_ms(void)
{
    return wait_active() ? W.until - now_ms() : 0;
}

static void wait_arm(uint32_t ms)
{
    W.on = ms > 0;
    W.until = now_ms() + ms;
}

static void arm_pending_penalty(void)
{
    if (W.on || W.served) {
        return;
    }
    wait_arm(flex_lock_penalty_ms(flex_lock_fails()));
}

// ---- construccion comun ------------------------------------------------------------
static void back_cb(lv_event_t *e);

static void back_arrow(lv_obj_t *parent)
{
    // Dos trazos de 2.4 px (30,26)->(18,18)->(30,10); zona x < 48, y < 48
    static const lv_point_precise_t pts[3] = {{30, 26}, {18, 18}, {30, 10}};
    lv_obj_t *z = flex_box(parent);
    lv_obj_set_size(z, 48, 48);
    lv_obj_set_clickable(z, true);
    lv_obj_add_event_cb(z, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_line_create(z);
    lv_line_set_points(l, pts, 3);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, txt_hi(), 0);
    lv_obj_set_clickable(l, false);
}

static lv_obj_t *glass_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r)
{
    lv_obj_t *o = flex_box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    if (A.verify) {
        // TH_WALLSURF (Plano) / TH_WALLSURF2 (vidrio) sobre el wallpaper velado
        flex_surface(o, FLEX_SURF_WALL, FLEX_BD_HOME);
        flex_surface_set_veil(o, lv_color_make(8, 10, 18), 70);
    } else {
        flex_surface(o, FLEX_SURF_CARD, FLEX_BD_FLAT);   // SET_CARD_BG / SET_CARD_GLASS
    }
    return o;
}

static void build_root(void)
{
    lv_obj_t *scr = flex_shell_screen();
    A.root = flex_box(scr);
    lv_obj_set_size(A.root, 480, 800);
    lv_obj_set_clickable(A.root, true);   // nada de lo de debajo recibe toques
    lv_obj_move_foreground(A.root);

    A.bg = flex_box(A.root);
    lv_obj_set_size(A.bg, 480, 800);
    lv_obj_set_clickable(A.bg, false);
    lv_obj_set_style_bg_opa(A.bg, LV_OPA_COVER, 0);
    const lv_image_dsc_t *wall = A.verify ? flex_wallmgr_image(FLEX_WALL_HOME) : NULL;
    if (wall) {
        lv_obj_t *img = lv_image_create(A.bg);
        lv_image_set_src(img, wall);
        lv_obj_t *veil = flex_box(A.bg);   // blurBgVeil: igual en las dos apariencias
        lv_obj_set_size(veil, 480, 800);
        lv_obj_set_style_bg_opa(veil, 70, 0);
        lv_obj_set_style_bg_color(veil, lv_color_make(8, 10, 18), 0);
    } else {
        lv_obj_set_style_bg_color(A.bg, A.verify ? flex_th()->scrim : flex_th()->page, 0);
    }
    A.content = flex_box(A.root);
    lv_obj_set_size(A.content, 480, 800);
    lv_obj_set_clickable(A.content, false);   // los contenedores no se comen los toques
}

static void destroy(void)
{
    A.gen++;   // un resultado que llegue tarde ya no es de nadie
    if (A.tick) {
        lv_timer_delete(A.tick);
        A.tick = NULL;
    }
    if (A.retry) {
        lv_timer_delete(A.retry);
        A.retry = NULL;
    }
    flex_lock_wipe(A.secret, sizeof(A.secret));
    if (A.root) {
        lv_anim_delete(A.root, NULL);
        if (A.band) {
            lv_anim_delete(A.band, NULL);
        }
        if (A.kb) {
            lv_anim_delete(A.kb, NULL);
        }
        if (A.content) {
            lv_anim_delete(A.content, NULL);
        }
        lv_obj_delete(A.root);
    }
    uint32_t gen = A.gen;
    memset(&A, 0, sizeof(A));
    A.gen = gen;
}

// ---- fundidos (authFade*, Lock.h:99-187) -------------------------------------------
static int32_t smoothstep_path(const lv_anim_t *a)
{
    float p = a->duration ? (float)(a->act_time < 0 ? 0 : a->act_time) / (float)a->duration : 1.0f;
    p = p > 1.0f ? 1.0f : p;
    p = p * p * (3.0f - 2.0f * p);
    return a->start_value + (int32_t)((float)(a->end_value - a->start_value) * p);
}

static void opa_exec(void *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, (lv_opa_t)v, 0);
}

static void kb_slide_start(void);

static void fade_in_done(lv_anim_t *a)
{
    (void)a;
    if (A.mode == M_PASS) {
        kb_slide_start();   // con contrasena, el teclado entra despues del fundido
    }
}

static void fade_out_done(lv_anim_t *a)
{
    (void)a;
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, A.content);
    lv_anim_set_exec_cb(&b, opa_exec);
    lv_anim_set_values(&b, 0, 255);
    lv_anim_set_duration(&b, FADE_IN_MS);
    lv_anim_set_path_cb(&b, smoothstep_path);
    lv_anim_set_completed_cb(&b, fade_in_done);
    lv_anim_start(&b);
}

static void fade_start(void)
{
    // 1) lo que habia se va hacia el fondo de la clave; 2) entra el metodo
    lv_obj_set_style_opa(A.root, 0, 0);
    lv_obj_set_style_opa(A.content, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, A.root);
    lv_anim_set_exec_cb(&a, opa_exec);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_duration(&a, FADE_OUT_MS);
    lv_anim_set_path_cb(&a, smoothstep_path);
    lv_anim_set_completed_cb(&a, fade_out_done);
    lv_anim_start(&a);
}

// ---- sacudida (lsuShakeOff, Lock.h:208-216) ----------------------------------------
static void shake_exec(void *obj, int32_t e)
{
    float p = (float)e / (float)SHAKE_MS;
    int32_t off = e >= SHAKE_MS ? 0 : (int32_t)(SHAKE_AMP * (1.0f - p) * sinf(p * SHAKE_CYC * 6.2831853f));
    if (obj == A.kb) {
        flex_kb_set_shift_x(obj, off);   // contrasena: solo las teclas, el panel quieto
    } else {
        lv_obj_set_style_translate_x(obj, off, 0);   // PIN: puntos y teclado juntos
    }
}

static void shake_done(lv_anim_t *a)
{
    (void)a;
    A.shaking = false;
}

static void shake_start(void)
{
    lv_obj_t *target = A.mode == M_PASS ? A.kb : A.band;
    if (!target) {
        return;
    }
    A.shaking = true;   // mientras dura no se aceptan pulsaciones
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, target);
    lv_anim_set_exec_cb(&a, shake_exec);
    lv_anim_set_values(&a, 0, SHAKE_MS);
    lv_anim_set_duration(&a, SHAKE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, shake_done);
    lv_anim_start(&a);
}

// ---- puntos y espera -----------------------------------------------------------------
static void dots_refresh(void)
{
    if (A.mode == M_PIN) {
        bool red = A.wrong && now_ms() - A.wrong_ms < WRONG_MS;
        lv_color_t fill = red ? flex_th()->err : flex_accent();
        for (int i = 0; i < PIN_MAX; i++) {
            lv_obj_t *d = A.dots[i];
            if (i < A.n) {
                lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(d, fill, 0);
                lv_obj_set_style_border_width(d, 0, 0);
            } else {
                lv_obj_set_style_bg_opa(d, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(d, 1, 0);
                lv_obj_set_style_border_color(d, txt_lo(), 0);
            }
        }
    } else if (A.mode == M_PASS && A.pass_dots) {
        // Un punto por caracter UTF-8, r7 en x = 30 + 24 i, y = 120, max. 18 visibles
        uint32_t have = lv_obj_get_child_count(A.pass_dots);
        int want = A.n > 18 ? 18 : A.n;
        while ((int)have < want) {
            lv_obj_t *d = flex_box(A.pass_dots);
            lv_obj_set_size(d, 15, 15);
            lv_obj_set_pos(d, 30 + 24 * (int32_t)have - 7, 0);
            lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(d, flex_accent(), 0);
            have++;
        }
        while ((int)have > want) {
            lv_obj_delete(lv_obj_get_child(A.pass_dots, (int32_t)have - 1));
            have--;
        }
    }
}

static void wait_refresh(void)
{
    if (!A.wait_box) {
        return;
    }
    bool on = wait_active();
    lv_obj_set_hidden(A.wait_box, !on);
    if (!on) {
        A.last_wait_sec = -1;
        return;
    }
    uint32_t rem = W.until - now_ms();
    int secs = (int)((rem + 999) / 1000);   // redondea hacia arriba
    if (secs == A.last_wait_sec) {
        return;
    }
    A.last_wait_sec = secs;
    lv_obj_set_hidden(A.wait_m2, flex_lock_fails() < 6);
    char cd[16];
    if (secs >= 60) {
        snprintf(cd, sizeof(cd), "%d:%02d", secs / 60, secs % 60);
    } else {
        snprintf(cd, sizeof(cd), "%d s", secs);
    }
    lv_label_set_text(A.wait_num, cd);
    flex_label_cap_center(A.wait_num, 240, LW_NUM_Y - LW_BAND_Y0);
}

static void build_wait(void)
{
    A.wait_box = flex_box(A.content);
    lv_obj_set_pos(A.wait_box, 0, LW_BAND_Y0);
    lv_obj_set_size(A.wait_box, 480, 97);
    lv_obj_set_clickable(A.wait_box, false);
    lv_obj_t *m1 = flex_label(A.wait_box, "Demasiados intentos fallidos", FLEX_FONT_S2, flex_th()->err);
    flex_label_cap_center(m1, 240, LW_MSG_Y - LW_BAND_Y0);
    A.wait_m2 = flex_label(A.wait_box, "Bloqueo temporal de 5 minutos", FLEX_FONT_S2, txt_lo());
    flex_label_cap_center(A.wait_m2, 240, LW_MSG2_Y - LW_BAND_Y0);
    A.wait_num = flex_label(A.wait_box, "", FLEX_FONT_S4, txt_hi());
    lv_obj_set_hidden(A.wait_box, true);
    A.last_wait_sec = -1;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (A.wrong && now_ms() - A.wrong_ms >= WRONG_MS) {
        A.wrong = false;
        dots_refresh();
    }
    wait_refresh();
}

// ---- verificacion ----------------------------------------------------------------------
static void finish_ok(void)
{
    flex_auth_req_t req = A.req;
    destroy();
    if (req.on_ok) {
        req.on_ok(req.ctx);
    }
}

static void reveal_exec(void *obj, int32_t e)
{
    (void)obj;
    // Mezcla lineal fondo velado -> escritorio con temblor horizontal que decae
    float p = (float)e / (float)REVEAL_MS;
    int32_t sh = e >= REVEAL_MS ? 0 : (int32_t)((1.0f - p) * 6.0f * sinf((float)e * 0.05f));
    lv_obj_set_style_opa(A.root, (lv_opa_t)(255 - (e >= REVEAL_MS ? 255 : (int32_t)(p * 255.0f))), 0);
    flex_shell_home_shift(sh);
}

static void reveal_done(lv_anim_t *a)
{
    (void)a;
    flex_shell_home_shift(0);
    flex_touch_drop_all();   // el dedo del ultimo digito no abre un icono
    finish_ok();
}

static void reveal_start(void)
{
    A.revealing = true;
    lv_anim_delete(A.root, opa_exec);
    lv_anim_delete(A.content, opa_exec);
    lv_obj_set_hidden(A.content, true);
    flex_shell_reveal_prepare();   // escritorio debajo, el bloqueo fuera
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, A.root);
    lv_anim_set_exec_cb(&a, reveal_exec);
    lv_anim_set_values(&a, 0, REVEAL_MS);
    lv_anim_set_duration(&a, REVEAL_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, reveal_done);
    lv_anim_start(&a);
}

static void check_result(bool ok, void *user)
{
    if (!A.root || (uint32_t)(uintptr_t)user != A.gen || !A.checking) {
        return;   // la pantalla ya no es la que lo pidio
    }
    A.checking = false;
    if (ok) {
        // lockOnSuccess: espera fuera, contador a cero (y a la flash)
        W.on = false;
        W.served = true;
        flex_lock_set_fails(0);
        flex_cfg_flush_async();
        if (A.req.reveal) {
            reveal_start();
        } else {
            finish_ok();
        }
        return;
    }
    // lockOnFail: el contador ya se subio al arrancar la verificacion
    W.served = false;
    wait_arm(flex_lock_penalty_ms(flex_lock_fails()));
    A.wrong = true;
    A.wrong_ms = now_ms();
    A.n = 0;
    dots_refresh();
    shake_start();
    wait_refresh();
}

static void retry_cb(lv_timer_t *t);

static void check_launch(void)
{
    if (flex_lock_verify_async(A.secret, check_result, (void *)(uintptr_t)A.gen)) {
        flex_lock_wipe(A.secret, sizeof(A.secret));
        if (A.retry) {
            lv_timer_delete(A.retry);
            A.retry = NULL;
        }
        return;
    }
    // Ocupada (una verificacion anterior aun termina) o sin memoria: se reintenta
    if (++A.retries > RETRY_MAX) {
        flex_lock_wipe(A.secret, sizeof(A.secret));
        if (A.retry) {
            lv_timer_delete(A.retry);
            A.retry = NULL;
        }
        check_result(false, (void *)(uintptr_t)A.gen);
        return;
    }
    if (!A.retry) {
        A.retry = lv_timer_create(retry_cb, RETRY_MS, NULL);
    }
}

static void retry_cb(lv_timer_t *t)
{
    (void)t;
    check_launch();
}

static void check_start(void)
{
    A.checking = true;   // el teclado no acepta toques hasta el veredicto
    A.retries = 0;
    // El fallo se cuenta (y se manda a la flash) ANTES de saber el resultado: un
    // reinicio justo despues de ver "incorrecto" no puede regalar intentos.
    // Si acierta, el contador vuelve a cero.
    flex_lock_set_fails(flex_lock_fails() + 1);
    flex_cfg_flush_async();
    check_launch();
}

// ---- guardar (crear clave) -------------------------------------------------------------
static void setup_exit(void)
{
    void (*done)(void *) = A.setup_done;
    void *ctx = A.setup_ctx;
    destroy();
    if (done) {
        done(ctx);
    }
}

static void save_result(bool ok, void *user)
{
    (void)ok;   // como Arduino: si no se pudo grabar, Ajustes sigue mostrando el bloqueo anterior
    if (!A.root || (uint32_t)(uintptr_t)user != A.gen) {
        return;
    }
    setup_exit();
}

static void save_fail_async(void *gen)
{
    save_result(false, gen);
}

static void save_secret(int type)
{
    A.checking = true;
    if (!flex_lock_set_async(A.secret, type, save_result, (void *)(uintptr_t)A.gen)) {
        lv_async_call(save_fail_async, (void *)(uintptr_t)A.gen);
    }
    flex_lock_wipe(A.secret, sizeof(A.secret));
}

// ---- teclado numerico --------------------------------------------------------------------
static void press_flash(int i)
{
    lv_obj_t *f = A.flash[i];
    if (!f) {
        return;
    }
    lv_anim_delete(f, opa_exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f);
    lv_anim_set_exec_cb(&a, opa_exec);
    lv_anim_set_values(&a, 90, 0);   // lsuTxtHi a (1 - p) 90
    lv_anim_set_duration(&a, PRESS_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void pin_key_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (A.checking || A.shaking || A.revealing || wait_active()) {
        return;   // durante la verificacion, la sacudida o la espera el teclado es inerte
    }
    press_flash(i);
    if (i == 9) {   // borrar
        if (A.n > 0) {
            A.secret[--A.n] = 0;
        }
    } else if (i == 11) {   // OK
        if (A.verify) {
            if (A.n > 0) {   // un OK sin digitos no gasta un intento (en Arduino si)
                check_start();
            }
            return;
        }
        if (A.n >= PIN_MIN) {
            save_secret(FLEX_LOCK_PIN);
        }
        return;
    } else if (A.n < PIN_MAX) {
        A.secret[A.n++] = PIN_KEYS[i][0];
        A.secret[A.n] = 0;
        if (A.verify && A.saved_len > 0 && A.n == A.saved_len) {
            dots_refresh();   // el ultimo punto se pinta ANTES de arrancar la verificacion
            check_start();
            return;
        }
    }
    dots_refresh();
}

static void build_pin(void)
{
    A.mode = M_PIN;
    lv_obj_t *title = flex_label(A.content, A.verify ? "Introduce el PIN" : "Crear PIN",
                                 A.verify ? FLEX_FONT_S3 : FLEX_FONT_S4, txt_hi());
    flex_label_cap_center(title, 240, 60);
    back_arrow(A.content);

    // Banda que se sacude junta: puntos (y = 150) y teclado (y = 300..664)
    A.band = flex_box(A.content);
    lv_obj_set_size(A.band, 480, 800);
    lv_obj_set_clickable(A.band, false);
    for (int i = 0; i < PIN_MAX; i++) {
        lv_obj_t *d = flex_box(A.band);
        lv_obj_set_clickable(d, false);
        lv_obj_set_size(d, 17, 17);
        lv_obj_set_pos(d, 142 + 28 * i - 8, 150 - 8);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        A.dots[i] = d;
    }
    for (int i = 0; i < 12; i++) {
        int c = i % 3, r = i / 3;
        int32_t x = 30 + c * (132 + 12), y = 300 + r * (82 + 12);
        lv_obj_t *k = glass_box(A.band, x, y, 132, 82, 16);
        lv_obj_set_clickable(k, true);
        lv_obj_add_event_cb(k, pin_key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_color_t col = i == 9 ? flex_th()->warn : i == 11 ? flex_th()->ok : txt_hi();
        lv_obj_t *l = flex_label(k, PIN_KEYS[i], FLEX_FONT_S3, col);
        flex_label_cap_center(l, 66, 41 - 12);
        lv_obj_t *f = flex_box(k);
        lv_obj_set_size(f, 132, 82);
        lv_obj_set_style_radius(f, 16, 0);
        lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(f, txt_hi(), 0);
        lv_obj_set_style_opa(f, 0, 0);
        lv_obj_set_clickable(f, false);
        A.flash[i] = f;
    }
    build_wait();
    dots_refresh();
}

// ---- contrasena ----------------------------------------------------------------------------
// El teclado del sistema (flex_kb) entra deslizandose desde abajo en 300 ms.
static void pass_kb_build(lv_obj_t *band);
static void pass_kb_slide(void);

static void kb_slide_start(void)
{
    pass_kb_slide();
}

static void build_pass(void)
{
    A.mode = M_PASS;
    lv_obj_t *title = flex_label(A.content, A.verify ? "Introduce contrase\xC3\xB1" "a" : "Crear contrase\xC3\xB1" "a",
                                 FLEX_FONT_S3, txt_hi());
    flex_label_cap_center(title, 240, 50);
    back_arrow(A.content);
    A.pass_dots = flex_box(A.content);
    lv_obj_set_pos(A.pass_dots, 0, 120 - 7);
    lv_obj_set_size(A.pass_dots, 480, 15);
    lv_obj_set_clickable(A.pass_dots, false);
    build_wait();
    pass_kb_build(A.content);   // en la contrasena se sacuden solo las teclas
}

// Lo llama el teclado de la contrasena (flex_auth_pass.c)
static bool flex_auth_pass_input_ok(void)
{
    return A.root && A.mode == M_PASS && !A.checking && !A.shaking && !A.revealing && !wait_active();
}

static void flex_auth_pass_append(const char *utf8)
{
    if (!flex_auth_pass_input_ok() || !utf8) {
        return;
    }
    size_t l = strlen(A.secret), sl = strlen(utf8);
    if (l + sl < sizeof(A.secret) - 1) {
        memcpy(A.secret + l, utf8, sl + 1);
        if ((utf8[0] & 0xC0) != 0x80) {
            A.n = 0;
            for (const char *p = A.secret; *p; p++) {
                A.n += (*p & 0xC0) != 0x80;
            }
        }
        dots_refresh();
    }
}

static void flex_auth_pass_backspace(void)
{
    if (!flex_auth_pass_input_ok()) {
        return;
    }
    size_t l = strlen(A.secret);
    if (l > 0) {
        size_t q = l - 1;
        while (q > 0 && (A.secret[q] & 0xC0) == 0x80) {
            q--;
        }
        A.secret[q] = 0;
        A.n = A.n > 0 ? A.n - 1 : 0;
        dots_refresh();
    }
}

static void flex_auth_pass_enter(void)
{
    if (!flex_auth_pass_input_ok()) {
        return;
    }
    if (A.verify) {
        if (A.secret[0]) {
            check_start();
        }
    } else if (strlen(A.secret) >= PASS_MIN) {   // como Arduino: 4 BYTES
        save_secret(FLEX_LOCK_PASS);
    }
}

// Teclado del sistema sin barra ni sugerencias, espanol por defecto (lsuEnter)
static void kb_text_cb(lv_obj_t *kb, const char *utf8, void *user)
{
    (void)kb;
    (void)user;
    flex_auth_pass_append(utf8);
}

static void kb_back_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    flex_auth_pass_backspace();
}

static void kb_enter_cb(lv_obj_t *kb, void *user)
{
    (void)kb;
    (void)user;
    flex_auth_pass_enter();
}

static void pass_kb_build(lv_obj_t *parent)
{
    flex_kb_cfg_t c = {
        .flags = A.verify ? FLEX_KB_F_WALLPAPER : FLEX_KB_F_PAGE,
        .backdrop = A.verify ? FLEX_BD_HOME : FLEX_BD_FLAT,
        .on_text = kb_text_cb,
        .on_backspace = kb_back_cb,
        .on_enter = kb_enter_cb,
    };
    A.kb = flex_kb_create(parent, &c);
    if (!A.kb) {
        return;
    }
    if (A.verify) {
        flex_surface_set_veil(A.kb, lv_color_make(8, 10, 18), 70);   // el panel esta sobre el fondo velado
    }
    lv_obj_set_y(A.kb, 800);   // fuera hasta que entre deslizandose
}

static void pass_kb_slide(void)
{
    if (A.kb) {
        flex_kb_slide_in(A.kb);
    }
}

// ---- selector PIN / Contrasena (crear) ------------------------------------------------------
static void sel_async(void *arg)
{
    int which = (int)(intptr_t)arg;
    if (!A.root || A.mode != M_SEL) {
        return;
    }
    lv_obj_clean(A.content);
    A.kb = NULL;
    A.band = NULL;
    A.pass_dots = NULL;
    memset(A.dots, 0, sizeof(A.dots));
    memset(A.flash, 0, sizeof(A.flash));
    A.wait_box = NULL;
    if (which == 0) {
        build_pin();
    } else {
        build_pass();
        kb_slide_start();
    }
}

static void sel_cb(lv_event_t *e)
{
    lv_async_call(sel_async, lv_event_get_user_data(e));
}

static void build_sel(void)
{
    A.mode = M_SEL;
    back_arrow(A.content);
    lv_obj_t *t1 = flex_label(A.content, "Bloqueo de pantalla", FLEX_FONT_S3, txt_hi());
    flex_label_cap_center(t1, 240, 74);
    lv_obj_t *t2 = flex_label(A.content, "Elige un metodo", FLEX_FONT_S2, txt_lo());
    flex_label_cap_center(t2, 240, 118);
    static const char *const lbl[2] = {"PIN", "Contrase\xC3\xB1" "a"};
    for (int k = 0; k < 2; k++) {
        int32_t y = k == 0 ? 220 : 370;
        lv_obj_t *b = flex_box(A.content);
        lv_obj_set_pos(b, 40, y);
        lv_obj_set_size(b, 400, 120);
        lv_obj_set_style_radius(b, 22, 0);
        // Plano: TH_PRIM; Vidrio: tinte mix(TH_PRIM, TH_SURF, 60)
        flex_surface(b, FLEX_SURF_TINT, FLEX_BD_FLAT);
        flex_surface_set_tint(b, lv_color_mix(flex_th()->surf, flex_accent(), 60));
        flex_surface_set_flat(b, flex_accent(), LV_OPA_COVER);
        lv_obj_set_clickable(b, true);
        lv_obj_add_event_cb(b, sel_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_t *l = flex_label(b, lbl[k], FLEX_FONT_S4, flex_th()->on_acc);
        flex_label_cap_center(l, 200, 42);
    }
}

// ---- salir (lsuExit, Power.h:74-124) ---------------------------------------------------------
static void back_async(void *gen)
{
    if (!A.root || (uint32_t)(uintptr_t)gen != A.gen || A.revealing) {
        return;
    }
    if (!A.verify) {
        setup_exit();   // crear: vuelve a Ajustes
        return;
    }
    // La espera no se perdona al salir (W sigue armada); la verificacion a medias
    // se corta y su resultado se ignora.
    flex_auth_req_t req = A.req;
    destroy();
    if (req.on_cancel) {
        req.on_cancel(req.ctx);
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    // Se borra la pantalla entera: fuera del evento de su propio boton
    lv_async_call(back_async, (void *)(uintptr_t)A.gen);
}

void flex_auth_abort(void)
{
    if (A.root) {
        if (A.revealing) {
            flex_shell_home_shift(0);
        }
        destroy();
    }
}

// ---- entradas ------------------------------------------------------------------------------
bool flex_auth_verify(const flex_auth_req_t *req)
{
    if (!req || !flex_auth_required()) {
        return false;
    }
    flex_auth_abort();
    A.verify = true;
    A.req = *req;
    A.saved_len = flex_lock_len();
    arm_pending_penalty();   // contador ya alto -> se cobra antes del primer intento
    flex_shell_auth_begin();
    build_root();
    // gLockType 1 = PIN; cualquier otro valor distinto de 0 = contrasena (Power.h:654)
    if (flex_cfg_get_i32("locktype", 0) == FLEX_LOCK_PIN) {
        build_pin();
    } else {
        build_pass();
    }
    A.tick = lv_timer_create(tick_cb, 30, NULL);
    wait_refresh();
    fade_start();
    return true;
}

void flex_auth_setup(void (*on_done)(void *ctx), void *ctx)
{
    if (flex_kiosk_active()) {
        return;   // kiosco: crear o cambiar la clave no se abre (Power.h:420)
    }
    flex_auth_abort();
    A.verify = false;
    A.setup_done = on_done;
    A.setup_ctx = ctx;
    flex_shell_auth_begin();
    build_root();
    build_sel();
    A.tick = lv_timer_create(tick_cb, 30, NULL);
}
