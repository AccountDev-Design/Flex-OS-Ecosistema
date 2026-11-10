// Flex OS Ultra · pantalla de Flex Account (N3c; Arduino: FlexOS_Account_Bridge.h). Una pantalla completa sobre el fondo de pantalla con la marca, una tarjeta oscura y, segun
// el estado de la cuenta, uno de estos contenidos: sin vincular (o con el motivo del fallo), "Creando enlace seguro", el codigo para escribir en el celular, o la cuenta
// vinculada con su pildora de estado y "Desvincular cuenta" (con confirmacion). Las medidas, los colores y los textos son los de Arduino; lo que se decide (etiquetas,
// colores del vinculo, que hace cada toque) sale de flex_account_ui_model.c, que prueba tests/host contra el puente de Arduino sobre una rejilla de toques.
//
// Reglas de Arduino que se conservan: la pantalla se repinta cuando CAMBIA algo (estado, progreso, Wi-Fi, vinculo, etapa, error), no en cada cuadro; el toque se resuelve con el
// estado con el que se DIBUJO; la confirmacion de desvincular manda sobre cualquier toque; entrar con una cuenta guardada y Wi-Fi pide validarla (sin bloquear: la red va en la
// tarea de fondo); y salir cancela el enlace solo si hay uno en curso. Con sin-Wi-Fi nunca se dice "sin cuenta": sigue vinculada (pildora ambar suave).
#include <stdio.h>
#include <string.h>
#include "flex_account_svc.h"
#include "flex_account_ui.h"
#include "flex_frame.h"
#include "flex_kit.h"
#include "flex_ntp.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define W FLEX_ACC_W
#define H FLEX_ACC_H
#define POLL_MS 80
#define TAP_SLOP 12

// Hooks de otros modulos (todavia sin migrar o sin enlazar): weak
extern void flex_wifi_oobe_enter(void) __attribute__((weak));        // el configurador de Wi-Fi (X3): vuelve aqui con flex_account_ui_open(mismo destino)
extern void flex_cloud_account_unlinked(void) __attribute__((weak)); // N4: la nube suelta lo de la cuenta

static struct {
    lv_obj_t *root, *content, *bar_fill;
    bool open, ask, first_boot, have_last, press_moved;
    flex_acc_return_t where;
    void (*done)(void);
    flex_account_state_t built;
    uint8_t last_progress;
    bool last_online;
    flex_account_link_t last_link;
    char last_stage[72], last_error[144];
    uint32_t poll_ms;
    int press_x, press_y, builds, wifi_requests;
    lv_timer_t *tmr;
    lv_point_precise_t seg[4][2];
    int nseg;
} A;

static lv_color_t rgb(uint32_t c) { return lv_color_make((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c); }

// ---- piezas (las primitivas de Arduino sobre objetos LVGL) ----
static lv_obj_t *rrect(int x, int y, int w, int h, int r, uint32_t color, int alpha)
{
    lv_obj_t *o = flex_box(A.content);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, rgb(color), 0);
    lv_obj_set_style_bg_opa(o, (lv_opa_t)alpha, 0);
    return o;
}
static lv_obj_t *circle(float cx, float cy, float r, uint32_t color)
{
    int d = (int)(2.0f * r + 0.5f);
    return rrect((int)(cx - r + 0.5f), (int)(cy - r + 0.5f), d, d, LV_RADIUS_CIRCLE, color, 255);
}
static lv_obj_t *text(const char *s, int fs, uint32_t color, int cx, int y)
{
    lv_obj_t *l = flex_kit_text(A.content, s, fs, rgb(color), cx, y, true);
    lv_obj_set_clickable(l, false);
    return l;
}
// strokeSeg: trazo de extremos redondos (rad = radio, grosor 2*rad)
static void stroke(int x0, int y0, int x1, int y1, int rad, uint32_t color)
{
    if (A.nseg >= 4) {
        return;
    }
    lv_point_precise_t *p = A.seg[A.nseg++];
    p[0].x = x0;
    p[0].y = y0;
    p[1].x = x1;
    p[1].y = y1;
    lv_obj_t *l = lv_line_create(A.content);
    lv_line_set_points(l, p, 2);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 2 * rad, 0);
    lv_obj_set_style_line_color(l, rgb(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_clickable(l, false);
}
static void button(int x, int y, int w, int h, const char *label, bool primary)
{
    rrect(x, y, w, h, h / 2, primary ? 0x6F52EE : 0xFFFFFF, primary ? 245 : 62);
    text(label, 2, 0xFFFFFF, x + w / 2, y + h / 2 - 8);
}

// ---- la confirmacion de "Desvincular cuenta" ----
static void ask_draw(void)
{
    int x, y, w, h;
    flex_account_ask_geom(&x, &y, &w, &h);
    rrect(0, 0, W, H, 0, 0x000000, 150);
    rrect(x, y, w, h, 26, 0x181C34, 252);
    text("\xC2\xBF" "Desvincular esta cuenta?", 2, 0xFFFFFF, W / 2, y + 24);
    const uint32_t t1 = 0xCDD2E4, t2 = 0xAEB5CD;
    text("Se borra la credencial guardada en este P4.", 1, t1, W / 2, y + 70);
    text("Flex Cloud dejara de funcionar hasta que", 1, t1, W / 2, y + 92);
    text("vuelvas a vincular una cuenta.", 1, t1, W / 2, y + 112);
    text("Tus archivos en la nube NO se borran.", 1, 0x73E7E2, W / 2, y + 140);
    text("El aparato sigue en tu lista de la web", 1, t2, W / 2, y + 170);
    text("hasta que lo quites alli.", 1, t2, W / 2, y + 190);
    int cx, dx, by, bw, bh;
    flex_account_ask_btns(&cx, &dx, &by, &bw, &bh);
    rrect(cx, by, bw, bh, bh / 2, 0xFFFFFF, 62);
    text("Cancelar", 2, 0xFFFFFF, cx + bw / 2, by + bh / 2 - 8);
    rrect(dx, by, bw, bh, bh / 2, 0xDC3C54, 250);
    text("Desvincular", 2, 0xFFFFFF, dx + bw / 2, by + bh / 2 - 8);
}

// ---- la pantalla ----
static void render_with(const flex_account_snapshot_t *sp, bool online)
{
    const flex_account_snapshot_t s = *sp;
    if (A.content) {
        lv_obj_delete(A.content);
    }
    A.content = flex_box(A.root);
    lv_obj_set_size(A.content, W, H);
    lv_obj_set_clickable(A.content, false);
    A.nseg = 0;
    A.bar_fill = NULL;
    A.builds++;

    // Halo y marca propios de Flex Account (nada incrustado: todo con formas)
    circle(W / 2, 112, 54.0f, 0x6648E6);
    circle(W / 2 - 19, 102, 12.0f, 0x53E2DC);
    circle(W / 2 + 17, 119, 19.0f, 0xFFFFFF);
    text("Flex Account", 4, 0xFFFFFF, W / 2, 187);
    if (!A.first_boot) {
        stroke(38, 31, 24, 43, 3, 0xFFFFFF);
        stroke(24, 43, 38, 55, 3, 0xFFFFFF);
    }
    const int card_x = 28, card_y = 238, card_w = W - 56, card_h = 386;
    rrect(card_x, card_y, card_w, card_h, 30, 0x121628, 218);

    if (s.state == FLEX_ACCOUNT_LINKED) {
        // VINCULADA. El estado del vinculo va en una pildora: sin red la cuenta SIGUE vinculada y la pantalla lo dice asi
        bool relink = flex_account_needs_relink(&s);
        uint32_t lc = flex_account_link_rgb(s.link);
        circle(W / 2, 300, 34.0f, relink ? lc : 0x2CBE85);
        if (relink) {
            rrect(W / 2 - 3, 280, 6, 26, 3, 0xFFFFFF, 255);
            circle(W / 2, 316, 3.5f, 0xFFFFFF);
        } else {
            stroke(W / 2 - 14, 300, W / 2 - 3, 313, 4, 0xFFFFFF);
            stroke(W / 2 - 3, 313, W / 2 + 18, 285, 4, 0xFFFFFF);
        }
        text(flex_account_linked_title(&s), 3, 0xFFFFFF, W / 2, 350);
        text(s.flex_address, 2, 0x73E7E2, W / 2, 392);
        if (s.display_name[0]) {
            text(s.display_name, 1, 0xCDD2E4, W / 2, 422);
        }
        const char *pill = flex_account_link_label(s.link);
        int pw = flex_ui_text_w(pill, 1, NULL) + 40;
        if (pw > card_w - 40) {
            pw = card_w - 40;
        }
        int px = W / 2 - pw / 2;
        rrect(px, 446, pw, 28, 14, lc, 60);
        circle((float)(px + 16), 460.0f, 4.0f, lc);
        text(pill, 1, 0xFFFFFF, W / 2 + 6, 453);
        if (s.error[0]) {
            text(s.error, 1, 0xFF9AA6, W / 2, 486);
        } else if (s.link_detail[0]) {
            text(s.link_detail, 1, 0xAEB5CD, W / 2, 486);
        }
        text("Tu correo de recuperacion nunca se muestra.", 1, 0x969EB8, W / 2, 512);
        if (relink) {
            button(58, 548, W - 116, 58, online ? "Volver a vincular" : "Conectar Wi-Fi", true);
        } else {
            button(58, 548, W - 116, 58, flex_account_back_label(A.where), true);
        }
        // Oscuro como la tarjeta: legible sobre cualquier fondo de pantalla
        rrect(W / 2 - 120, FLEX_ACC_UNLINK_Y, 240, FLEX_ACC_UNLINK_H, FLEX_ACC_UNLINK_H / 2, 0x121628, 215);
        text("Desvincular cuenta", 1, 0xFF9AA6, W / 2, FLEX_ACC_UNLINK_Y + 9);
    } else if (s.state == FLEX_ACCOUNT_REQUESTING) {
        text("Creando enlace seguro", 3, 0xFFFFFF, W / 2, 300);
        const int px = 60, py = 368, pw = W - 120;
        rrect(px, py, pw, 16, 8, 0x343A52, 255);
        int filled = pw * s.progress / 100;
        if (filled < 8) {
            filled = 8;
        }
        A.bar_fill = rrect(px, py, filled, 16, 8, 0x5CDCD7, 255);
        text(s.stage, 1, 0xCDD2E4, W / 2, 412);
        text("FlexOS no guarda tu contrasena.", 1, 0xAEB5CD, W / 2, 462);
        button(110, 540, W - 220, 54, "Cancelar", false);
    } else if (s.state == FLEX_ACCOUNT_CODE_READY) {
        text("Abre en tu celular", 2, 0xCDD2E4, W / 2, 274);
        text("flex-developer-studio", 1, 0x73E7E2, W / 2, 308);
        text(".ralvarezsantos980.chatgpt.site/activate", 1, 0x73E7E2, W / 2, 330);
        text("y escribe este codigo", 1, 0xAEB5CD, W / 2, 375);
        rrect(90, 405, W - 180, 86, 24, 0xFFFFFF, 245);
        if (s.code[0]) {   // un ultimo fotograma sin codigo puede llegar si se desvincula con el enlace vivo: no se dibuja una etiqueta vacia
            text(s.code, 5, 0x2F2268, W / 2, 430);
        }
        text(flex_account_code_note(&s), 1, s.error[0] ? 0xFFB53D : 0xAEB5CD, W / 2, 514);
        button(110, 552, W - 220, 54, "Cancelar", false);
    } else {
        text(flex_account_unlinked_title(&s), 2, 0xFFFFFF, W / 2, 278);
        if (s.state == FLEX_ACCOUNT_ERROR && s.error[0]) {
            text(s.error, 1, 0xFF9AA6, W / 2, 318);
        } else if (s.link == FLEX_LINK_ERROR && s.link_detail[0]) {
            text(s.link_detail, 1, 0xFF9AA6, W / 2, 318);
        } else {
            text("Publica apps, comenta, recibe soporte", 1, 0xCDD2E4, W / 2, 318);
            text("y usa tu identidad usuario@flex.", 1, 0xCDD2E4, W / 2, 340);
        }
        if (!online) {
            rrect(52, 378, W - 104, 48, 18, 0xFFB53D, 52);
            text("Necesitas conectar Wi-Fi primero", 1, 0xFFDFA1, W / 2, 394);
        }
        button(52, 452, W - 104, 58, flex_account_primary_label(s.state, online), true);
        button(52, 526, W - 104, 54, online ? "Crear una cuenta" : "Configurar red", false);
    }

    text(A.first_boot ? "Omitir por ahora" : "Volver sin cambios", 2, 0xFFFFFF, W / 2, 704);
    text("@flex es una identidad publica, no un buzon de correo.", 1, 0xD2D6E5, W / 2, 750);
    if (A.ask && s.state == FLEX_ACCOUNT_LINKED) {
        ask_draw();
    } else {
        A.ask = false;   // la cuenta ya no esta: nada que confirmar
    }

    A.built = s.state;
    A.last_progress = s.progress;
    A.last_online = online;
    A.last_link = s.link;
    A.have_last = true;
    snprintf(A.last_stage, sizeof(A.last_stage), "%s", s.stage);
    snprintf(A.last_error, sizeof(A.last_error), "%s", s.error);
}

static void render(void)
{
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    render_with(&s, flex_net_online());
}

// ---- cerrar ----
static void close_ui(void)
{
    if (!A.open) {
        return;
    }
    A.open = false;
    A.ask = false;
    if (A.root) {
        lv_obj_delete(A.root);
    }
    A.root = A.content = A.bar_fill = NULL;
    flex_touch_drop_all();
    flex_shell_nav_refresh();
    flex_app_settings_refresh();   // la fila de Ajustes > General > Flex Account ya dice otra cosa
}

// accountFinish: cancelar SOLO si hay un enlace en curso (antes se llamaba siempre y quedaba la peticion de cancelacion levantada despues de vincular con exito)
static void finish(void)
{
    if (flex_account_finish_cancels(flex_account_state())) {
        flex_account_cancel();
    }
    void (*done)(void) = A.where == FLEX_ACC_RET_OOBE ? A.done : NULL;
    close_ui();
    if (done) {
        done();   // la primera configuracion sigue (guarda la marca "oobe" y va al bloqueo)
    }
}

// Confirmado: la credencial se borra (NVS y RAM, UNA escritura), el estado pasa a UNLINKED y la nube suelta lo de esa cuenta. Sin red nada de esto habla con el servidor
static void unlink_now(void)
{
    A.ask = false;
    flex_account_forget_local();
    if (flex_cloud_account_unlinked) {
        flex_cloud_account_unlinked();
    }
    flex_notify("Flex Account", "Cuenta desvinculada de este P4");
    render();
}

static void request_link(void)
{
    char name[24];
    flex_cfg_get_str("name", name, sizeof(name), "FlexOS Ultra");
    if (flex_account_request_code(name)) {
        render();
    }
}

static void open_wifi(void)
{
    A.wifi_requests++;
    if (flex_wifi_oobe_enter) {
        flex_wifi_oobe_enter();
    }
}

// ---- toque ----
static void root_event_cb(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    if (!in || !A.open) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        A.press_x = p.x;
        A.press_y = p.y;
        A.press_moved = false;
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_PRESSING) {
        int dx = p.x - A.press_x, dy = p.y - A.press_y;
        if (dx * dx + dy * dy > TAP_SLOP * TAP_SLOP) {
            A.press_moved = true;   // CLICKED llega al soltar aunque se haya arrastrado: eso no es un toque
        }
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || A.press_moved) {
        return;
    }
    flex_account_snapshot_t now;
    flex_account_snapshot(&now);
    bool online = flex_net_online();
    flex_acc_action_t act = flex_account_hit(A.built, A.ask, A.first_boot, flex_account_needs_relink(&now), online, p.x, p.y);
    switch (act) {
    case FLEX_ACC_FINISH: finish(); break;
    case FLEX_ACC_ASK_OPEN:
        A.ask = true;   // primero se pregunta; nada se borra todavia
        render();
        break;
    case FLEX_ACC_ASK_DISMISS:
        A.ask = false;
        render();
        break;
    case FLEX_ACC_UNLINK_NOW: unlink_now(); break;
    case FLEX_ACC_WIFI: open_wifi(); break;
    case FLEX_ACC_REQUEST_CODE: request_link(); break;
    case FLEX_ACC_CANCEL_LINK:
        flex_account_cancel();
        render();
        break;
    default: break;
    }
}

// El estado cambia como mucho cada 3 s (cadencia del sondeo de la tarea de red): copiar la foto bajo cerrojo en CADA cuadro no aporta nada
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!A.open) {
        return;
    }
    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - A.poll_ms) < POLL_MS) {
        return;
    }
    A.poll_ms = now;
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    bool online = flex_net_online();
    if (!A.have_last || s.state != A.built || s.progress != A.last_progress || online != A.last_online || s.link != A.last_link || strcmp(s.stage, A.last_stage) ||
        strcmp(s.error, A.last_error)) {
        render();
    }
}

void flex_account_ui_open(flex_acc_return_t where, void (*done)(void))
{
    if (A.open) {
        close_ui();
    }
    A.where = where;
    A.first_boot = where == FLEX_ACC_RET_OOBE;
    A.done = done;
    A.ask = false;
    A.have_last = false;
    A.poll_ms = lv_tick_get();
    A.open = true;
    flex_touch_drop_all();   // el toque que abrio la pantalla no se filtra
    flex_navbar_set_ctx(FLEX_NAV_HIDDEN);   // una pantalla completa lo ocupa todo, tambien la barra
    A.root = flex_box(flex_shell_screen());
    lv_obj_set_size(A.root, W, H);
    lv_obj_set_clickable(A.root, true);
    lv_obj_set_gesture_bubble(A.root, false);
    lv_obj_set_style_bg_opa(A.root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(A.root, lv_color_hex(0x202434), 0);
    const lv_image_dsc_t *w = flex_wallmgr_image(FLEX_WALL_HOME);
    if (w) {
        lv_obj_t *img = lv_image_create(A.root);   // sobre el fondo de pantalla
        lv_image_set_src(img, w);
        lv_obj_set_clickable(img, false);
    }
    lv_obj_add_event_cb(A.root, root_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_move_foreground(A.root);
    // Entrar con una cuenta guardada y Wi-Fi pide comprobarla (no bloquea: la red va en la tarea de Flex Account, con su propio freno)
    if (flex_account_linked() && flex_net_online()) {
        flex_account_request_validation();
    }
    render();
}

bool flex_account_ui_owns_screen(void) { return A.open; }

void flex_account_ui_back(void)
{
    if (!A.open || A.first_boot) {
        return;   // en la primera configuracion se omite con la barra, como en Arduino
    }
    if (A.ask) {
        A.ask = false;   // Atras primero cierra la confirmacion
        render();
        return;
    }
    finish();
}

void flex_account_ui_close_now(void)
{
    if (!A.open || A.first_boot) {
        return;
    }
    if (flex_account_finish_cancels(flex_account_state())) {
        flex_account_cancel();
    }
    close_ui();
}

void flex_account_ui_start(void)
{
    if (!A.tmr) {
        A.tmr = lv_timer_create(tick_cb, POLL_MS, NULL);
    }
}

// ---- lo que ve el resto del sistema ----
void flex_account_settings_text(char *out, size_t n)
{
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    flex_account_settings_text_for(&s, out, n);
}
void flex_account_settings_enter(void) { flex_account_ui_open(FLEX_ACC_RET_SETTINGS, NULL); }

// ---- pruebas ----
int flex_account_ui_builds(void) { return A.builds; }
int flex_account_ui_wifi_requests(void) { return A.wifi_requests; }
bool flex_account_ui_ask_open(void) { return A.ask; }
static bool find_text(lv_obj_t *o, const char *t, bool part)
{
    if (lv_obj_check_type(o, &lv_label_class) && (part ? strstr(lv_label_get_text(o), t) != NULL : !strcmp(lv_label_get_text(o), t))) {
        return true;
    }
    for (uint32_t k = 0; k < lv_obj_get_child_count(o); k++) {
        if (find_text(lv_obj_get_child(o, (int32_t)k), t, part)) {
            return true;
        }
    }
    return false;
}
bool flex_account_ui_shown(const char *t) { return A.root && find_text(A.root, t, false); }
bool flex_account_ui_shown_part(const char *t) { return A.root && find_text(A.root, t, true); }

#ifdef FLEX_SIM
// Dibuja una foto cualquiera (la escena compara el dibujo con lo que pinta accountRender de Arduino para esa misma foto)
void flex_account_ui_test_render(const flex_account_snapshot_t *s, bool online, bool ask, flex_acc_return_t where)
{
    if (!A.open) {
        flex_account_ui_open(where, NULL);
    }
    A.where = where;
    A.first_boot = where == FLEX_ACC_RET_OOBE;
    A.ask = ask;
    render_with(s, online);
}
lv_obj_t *flex_account_ui_test_root(void) { return A.root; }
int flex_account_ui_bar_width(void)
{
    if (!A.bar_fill) {
        return -1;
    }
    lv_obj_update_layout(A.bar_fill);
    return (int)lv_obj_get_width(A.bar_fill);
}
// Huella de la pantalla: geometria, visibilidad, colores y textos de CADA objeto (los trazos van en la huella del framebuffer que calcula la escena)
static uint32_t crc_u32(uint32_t c, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        c ^= (v >> (8 * i)) & 0xffu;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return c;
}
static uint32_t tree_crc(lv_obj_t *o, uint32_t c)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    c = crc_u32(c, (uint32_t)a.x1);
    c = crc_u32(c, (uint32_t)a.y1);
    c = crc_u32(c, (uint32_t)a.x2);
    c = crc_u32(c, (uint32_t)a.y2);
    c = crc_u32(c, lv_obj_is_hidden(o) ? 1u : 0u);
    c = crc_u32(c, lv_obj_is_clickable(o) ? 1u : 0u);
    c = crc_u32(c, lv_color_to_u16(lv_obj_get_style_bg_color(o, LV_PART_MAIN)));
    c = crc_u32(c, (uint32_t)lv_obj_get_style_bg_opa(o, LV_PART_MAIN));
    c = crc_u32(c, (uint32_t)lv_obj_get_style_radius(o, LV_PART_MAIN));
    if (lv_obj_check_type(o, &lv_label_class)) {
        for (const char *t = lv_label_get_text(o); *t; t++) {
            c = crc_u32(c, (uint32_t)(unsigned char)*t);
        }
        c = crc_u32(c, lv_color_to_u16(lv_obj_get_style_text_color(o, LV_PART_MAIN)));
        c = crc_u32(c, (uint32_t)lv_font_get_line_height(lv_obj_get_style_text_font(o, LV_PART_MAIN)));
    }
    for (uint32_t k = 0; k < lv_obj_get_child_count(o); k++) {
        c = tree_crc(lv_obj_get_child(o, (int32_t)k), c);
    }
    return c;
}
uint32_t flex_account_ui_tree_crc(void)
{
    if (!A.root) {
        return 0;
    }
    lv_obj_update_layout(A.root);
    return tree_crc(A.root, 0xFFFFFFFFu);
}
#endif
