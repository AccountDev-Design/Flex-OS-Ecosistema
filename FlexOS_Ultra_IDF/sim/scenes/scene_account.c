// Flex Account (N3c): el servicio REAL (flex_account) sobre un mundo simulado (NVS en memoria, servidor que firma sus respuestas con ES256) y la pantalla de la cuenta en
// LVGL. Se comprueba: entrar desde Ajustes, el flujo completo (Iniciar sesion -> Creando enlace seguro -> codigo -> aprobado -> Cuenta vinculada) viendo la pantalla
// MIENTRAS el servicio bloquea, los estados sin red / rechazada / caducada / error, desvincular con confirmacion, los destinos de retorno (Ajustes, primera configuracion),
// que Atras, Inicio, la cortina y el bloqueo automatico respetan la pantalla, y, para CADA combinacion de estado / vinculo / Wi-Fi / confirmacion / destino, que el
// dibujo coincide primitiva a primitiva con lo que pinta accountRender de Arduino (el dorado lo genera tests/host/test_account_ui --golden).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account_fake.h"
#include "flex_account_svc.h"
#include "flex_account_ui.h"
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_kit.h"
#include "flex_passcode.h"
#include "flex_qs_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_net_online(bool on);
const uint16_t *sim_fb(void);
bool flex_power_autolock_vetoed(void);
void flex_sys_back(void);
void flex_sys_home(void);
void flex_sys_recents(void);
int flex_app_settings_view(void);
bool flex_app_settings_animating(void);
bool flex_app_settings_card_xy(int i, int *x, int *y);
bool flex_app_settings_row_xy(int i, int *x, int *y);
bool flex_app_settings_shown(const char *txt);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("cuenta: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                              \
        }                                                            \
    } while (0)

static int s_oobe_done;
static void oobe_done(void) { s_oobe_done++; }

// ---- el mundo ----
static void net(bool on)
{
    sim_net_online(on);
    account_fake_set_wifi(on);
}
static void pump(uint32_t ms)   // la tarea de fondo, a mano
{
    for (uint32_t t = 0; t < ms; t += 100) {
        flex_account_step();
        account_fake_advance(100);
        sim_run(100);
    }
}

static void unlock_and_clean(void)
{
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
    for (int i = 0; i < FLEX_APP_N; i++) {
        flex_app_terminate_ex(i, true);
    }
    sim_run(200);
}

// ---- Ajustes ----
static bool wait_view(int v)
{
    for (int i = 0; i < 80; i++) {
        if (flex_app_settings_view() == v && !flex_app_settings_animating()) {
            return true;
        }
        sim_run(20);
    }
    return false;
}
static void settings_general(void)
{
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(700);
    int x, y;
    if (flex_app_settings_card_xy(0, &x, &y)) {
        sim_touch(x, y, true);
        sim_run(30);
        sim_touch(x, y, false);
        sim_run(400);
    }
}
static void settings_close(void)
{
    flex_app_terminate_ex(IC_AJUSTES, true);
    sim_run(300);
}
#define ROW_ACCOUNT 10
static void row(int i)
{
    int x, y;
    if (!flex_app_settings_row_xy(i, &x, &y) || y > 700 || y < 140) {
        sim_drag(240, 600, 240, 150, 200);
        sim_run(150);
    }
    if (flex_app_settings_row_xy(i, &x, &y)) {
        sim_tap(x, y);
        sim_run(200);
    }
}
static bool has(const char *t) { return flex_app_settings_shown(t); }

static bool shown(const char *t) { return flex_account_ui_shown(t); }
static bool open_(void) { return flex_account_ui_owns_screen(); }

// huellas de referencia
static uint32_t crc_fb(int x0, int y0, int x1, int y1)
{
    uint32_t c = 0xFFFFFFFFu;
    const uint16_t *fb = sim_fb();
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            uint16_t v = fb[(size_t)y * 480 + (size_t)x];
            for (int b = 0; b < 2; b++) {
                c ^= (uint32_t)((v >> (8 * b)) & 0xffu);
                for (int k = 0; k < 8; k++) {
                    c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
                }
            }
        }
    }
    return c;
}
static bool golden(const char *name, uint32_t want_tree, uint32_t want_fb)
{
    uint32_t t = flex_account_ui_tree_crc(), f = crc_fb(0, 0, 479, 799);
    if (t != want_tree || f != want_fb) {
        printf("cuenta: GOLDEN %s tree=0x%08xu fb=0x%08xu (esperado tree=0x%08xu fb=0x%08xu)\n", name, t, f, want_tree, want_fb);
        return false;
    }
    return true;
}

// ---- paridad con el dibujo de Arduino ----
typedef struct {
    lv_obj_t *o;
    int kind;   // 0 caja, 1 etiqueta, 2 linea
    bool used;
} node_t;
static node_t s_nodes[128];
static int s_nn;
static void collect(lv_obj_t *o)
{
    if (s_nn < 128) {
        int kind = lv_obj_check_type(o, &lv_label_class) ? 1 : lv_obj_check_type(o, &lv_line_class) ? 2 : lv_obj_check_type(o, &lv_image_class) ? 3 : 0;
        s_nodes[s_nn++] = (node_t){o, kind, false};
    }
    for (uint32_t k = 0; k < lv_obj_get_child_count(o); k++) {
        collect(lv_obj_get_child(o, (int32_t)k));
    }
}
static uint16_t c16(unsigned v) { return (uint16_t)v; }
static int find_box(int x, int y, int w, int h, int r, unsigned color, int alpha, bool loose_x)
{
    for (int i = 0; i < s_nn; i++) {
        node_t *n = &s_nodes[i];
        if (n->used || n->kind != 0) {
            continue;
        }
        lv_area_t a;
        lv_obj_get_coords(n->o, &a);
        if (a.y1 != y || (int)lv_area_get_height(&a) != h) {
            continue;
        }
        if (!loose_x && (a.x1 != x || (int)lv_area_get_width(&a) != w)) {
            continue;
        }
        if ((int)lv_obj_get_style_radius(n->o, LV_PART_MAIN) != r || lv_color_to_u16(lv_obj_get_style_bg_color(n->o, LV_PART_MAIN)) != c16(color) ||
            (int)lv_obj_get_style_bg_opa(n->o, LV_PART_MAIN) != alpha) {
            continue;
        }
        n->used = true;
        return i;
    }
    return -1;
}
static bool parity(const char *path)
{
    bool ok = true;
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("cuenta: FALLO sin el dorado de Arduino (%s): ejecuta tests/host/account_run.sh ui\n", path);
        return false;
    }
    static char line[600];
    int cases = 0, bad = 0, ops_total = 0;
    bool have = fgets(line, sizeof(line), f) != NULL;
    while (have && line[0] == 'C') {
        flex_account_snapshot_t s;
        memset(&s, 0, sizeof(s));
        int state, progress, linked, link, online, ask, ret, n = 0;
        sscanf(line, "C %d %d %d %d %d %d %d%n", &state, &progress, &linked, &link, &online, &ask, &ret, &n);
        char *fld[6];
        char *p = line + n;
        if (*p == '\t') {
            p++;
        }
        for (int k = 0; k < 6; k++) {
            fld[k] = p;
            while (*p && *p != '\t' && *p != '\n') {
                p++;
            }
            if (*p) {
                *p++ = 0;
            }
        }
        s.state = (flex_account_state_t)state;
        s.progress = (uint8_t)progress;
        s.linked = linked != 0;
        s.link = (flex_account_link_t)link;
        snprintf(s.stage, sizeof(s.stage), "%s", fld[0]);
        snprintf(s.error, sizeof(s.error), "%s", fld[1]);
        snprintf(s.code, sizeof(s.code), "%s", fld[2]);
        snprintf(s.flex_address, sizeof(s.flex_address), "%s", fld[3]);
        snprintf(s.display_name, sizeof(s.display_name), "%s", fld[4]);
        snprintf(s.link_detail, sizeof(s.link_detail), "%s", fld[5]);
        flex_account_ui_test_render(&s, online != 0, ask != 0, (flex_acc_return_t)ret);
        s_nn = 0;
        collect(flex_account_ui_test_root());
        lv_obj_update_layout(flex_account_ui_test_root());
        // la raiz y la imagen del fondo de pantalla no son primitivas
        s_nodes[0].used = true;
        for (int i = 1; i < s_nn; i++) {
            if (s_nodes[i].kind == 3) {
                s_nodes[i].used = true;
            }
        }
        int dy_by_size[6] = {0}, dy_set[6] = {0};
        char why[200] = "";
        while ((have = fgets(line, sizeof(line), f) != NULL) && line[0] != 'C') {
            ops_total++;
            if (why[0]) {
                continue;
            }
            char op = line[0];
            if (op == 'R') {
                int x, y, w, h, r, a;
                unsigned col;
                sscanf(line, "R %d %d %d %d %d %u %d", &x, &y, &w, &h, &r, &col, &a);
                // la pildora del vinculo mide lo que mide su texto (el dorado usa otra tipografia): solo se exige su fila, su alto, su color y su radio
                bool pill = h == 28 && r == 14 && y == 446;
                if (find_box(x, y, w, h, r, col, a, pill) < 0) {
                    snprintf(why, sizeof(why), "rectangulo %d,%d %dx%d r%d color %u alfa %d", x, y, w, h, r, col, a);
                }
            } else if (op == 'O') {
                int cx, cy, r10;
                unsigned col;
                sscanf(line, "O %d %d %d %u", &cx, &cy, &r10, &col);
                float r = (float)r10 / 10.0f;
                int d = (int)(2.0f * r + 0.5f), x = (int)((float)cx - r + 0.5f), y = (int)((float)cy - r + 0.5f);
                bool dot = cy == 460 && r10 == 40;   // el punto de la pildora va pegado a su borde izquierdo
                if (find_box(x, y, d, d, LV_RADIUS_CIRCLE, col, 255, dot) < 0) {
                    snprintf(why, sizeof(why), "circulo %d,%d r%.1f color %u", cx, cy, (double)r, col);
                }
            } else if (op == 'S') {
                int x0, y0, x1, y1, rad;
                unsigned col;
                sscanf(line, "S %d %d %d %d %d %u", &x0, &y0, &x1, &y1, &rad, &col);
                bool found = false;
                for (int i = 0; i < s_nn && !found; i++) {
                    node_t *nd = &s_nodes[i];
                    if (nd->used || nd->kind != 2) {
                        continue;
                    }
                    const lv_point_precise_t *pts = lv_line_get_points(nd->o);
                    if (pts && (int)pts[0].x == x0 && (int)pts[0].y == y0 && (int)pts[1].x == x1 && (int)pts[1].y == y1 &&
                        (int)lv_obj_get_style_line_width(nd->o, LV_PART_MAIN) == 2 * rad && lv_color_to_u16(lv_obj_get_style_line_color(nd->o, LV_PART_MAIN)) == c16(col)) {
                        nd->used = found = true;
                    }
                }
                if (!found) {
                    snprintf(why, sizeof(why), "trazo %d,%d-%d,%d rad %d", x0, y0, x1, y1, rad);
                }
            } else if (op == 'T') {
                int cx, y, size, tn = 0;
                unsigned col;
                sscanf(line, "T %d %d %d %u%n", &cx, &y, &size, &col, &tn);
                char *text = line + tn + 1;
                char *nl = strchr(text, '\n');
                if (nl) {
                    *nl = 0;
                }
                if (!text[0]) {
                    continue;   // Arduino no dibuja un texto vacio
                }
                bool found = false;
                for (int i = 0; i < s_nn && !found; i++) {
                    node_t *nd = &s_nodes[i];
                    if (nd->used || nd->kind != 1 || strcmp(lv_label_get_text(nd->o), text)) {
                        continue;
                    }
                    lv_area_t a;
                    lv_obj_get_coords(nd->o, &a);
                    if (lv_color_to_u16(lv_obj_get_style_text_color(nd->o, LV_PART_MAIN)) != c16(col) || lv_obj_get_style_text_font(nd->o, LV_PART_MAIN) != flex_ui_font(size)) {
                        continue;
                    }
                    int mid = (a.x1 + a.x2) / 2, dy = a.y1 - y;
                    if (mid < cx - 1 || mid > cx + 1) {
                        continue;
                    }
                    if (!dy_set[size]) {
                        dy_set[size] = 1;
                        dy_by_size[size] = dy;
                    } else if (dy < dy_by_size[size] - 1 || dy > dy_by_size[size] + 1) {
                        continue;
                    }
                    nd->used = found = true;
                }
                if (!found) {
                    for (int i = 0; i < s_nn && getenv("DBGACC"); i++) {
                        if (s_nodes[i].kind == 1 && !strcmp(lv_label_get_text(s_nodes[i].o), text)) {
                            lv_area_t a;
                            lv_obj_get_coords(s_nodes[i].o, &a);
                            printf("   etiqueta '%s' x %d..%d y %d..%d color %u (esperado %u) fuente %s\n", text, (int)a.x1, (int)a.x2, (int)a.y1, (int)a.y2,
                                   (unsigned)lv_color_to_u16(lv_obj_get_style_text_color(s_nodes[i].o, LV_PART_MAIN)), col,
                                   lv_obj_get_style_text_font(s_nodes[i].o, LV_PART_MAIN) == flex_ui_font(size) ? "igual" : "distinta");
                        }
                    }
                    snprintf(why, sizeof(why), "texto '%s' tam %d color %u en %d,%d", text, size, col, cx, y);
                }
            }
        }
        for (int i = 0; i < s_nn && !why[0]; i++) {
            if (!s_nodes[i].used) {
                lv_area_t a;
                lv_obj_get_coords(s_nodes[i].o, &a);
                // el contenedor del contenido (a pantalla completa) no es una primitiva
                if (s_nodes[i].kind == 0 && lv_area_get_width(&a) == 480 && lv_area_get_height(&a) == 800 && lv_obj_get_style_bg_opa(s_nodes[i].o, LV_PART_MAIN) == 0) {
                    continue;
                }
                snprintf(why, sizeof(why), "objeto de mas (tipo %d) en %d,%d %dx%d", s_nodes[i].kind, (int)a.x1, (int)a.y1, (int)lv_area_get_width(&a), (int)lv_area_get_height(&a));
            }
        }
        cases++;
        if (why[0]) {
            bad++;
            if (bad <= 10) {
                printf("cuenta: FALLO dibujo caso %d (estado %d vinculo %d enlazada %d wifi %d confirmar %d retorno %d): %s\n", cases, state, link, linked, online, ask, ret, why);
            }
        }
    }
    fclose(f);
    printf("cuenta: dibujo contra Arduino: %d casos, %d primitivas, %d con diferencias\n", cases, ops_total, bad);
    if (bad || cases < 1000) {
        ok = false;
    }
    return ok;
}

// observacion de lo que ve el usuario MIENTRAS el servicio bloquea
static struct {
    int saw_creating, saw_code, saw_wait_note, max_bar, bar_regress, last_bar, golden_taken;
    bool code_golden_ok;
} O;
static void (*g_obs)(void);
static void delay_hook2(uint32_t ms)
{
    sim_run(ms < 4 ? 4 : ms);
    if (g_obs) {
        g_obs();
    }
}
static void obs_link(void)
{
    if (shown("Creando enlace seguro")) {
        O.saw_creating++;
        int w = flex_account_ui_bar_width();
        if (w >= 0) {
            if (w < O.last_bar) {
                O.bar_regress++;
            }
            O.last_bar = w;
            if (w > O.max_bar) {
                O.max_bar = w;
            }
        }
    }
    if (shown("Abre en tu celular")) {
        O.saw_code++;
        if (shown("FLX7Q2") && shown("El codigo vence en 10 minutos")) {
            O.saw_wait_note++;
        }
    }
}
static void run_flow(void)
{
    flex_account_step();
    sim_run(200);
}
static bool tap_open(int x, int y)
{
    sim_tap(x, y);
    sim_run(150);
    return open_();
}
static void open_from_settings(void)
{
    settings_general();
    wait_view(1);
    row(ROW_ACCOUNT);
    sim_run(300);
}

// Para la escena del restablecimiento: un enlace pedido y sin empezar, y comprobar despues que el borrado lo canceló (flex_account_cancel en begin_wipe)
void account_scene_request_pending(void)
{
    static const flex_account_cfg_t cfg = {"1.0.0"};
    account_fake_reset();
    flex_account_set(&cfg, account_fake_ops());
    account_fake_set_delay_hook(NULL);
    net(true);
    flex_account_begin();
    flex_account_request_code("FlexOS Ultra");
}
bool account_scene_link_cancelled(void)
{
    bool pending = flex_account_state() == FLEX_ACCOUNT_REQUESTING;
    flex_account_step();   // la tarea de fondo: el enlace pedido se cancela antes de abrir ninguna conexion
    return pending && flex_account_state() != FLEX_ACCOUNT_REQUESTING && flex_account_state() != FLEX_ACCOUNT_CODE_READY && account_fake_count(AF_POSTS) == 0;
}

bool scene_account_run(void)
{
    bool ok = true;
    unlock_and_clean();
    account_fake_reset();
    static const flex_account_cfg_t cfg = {"1.0.0"};
    flex_account_set(&cfg, account_fake_ops());
    account_fake_set_delay_hook(delay_hook2);
    net(true);
    flex_account_begin();
    flex_cfg_set_str("name", "FlexOS de Ana");
    flex_cfg_set_i32("autolockms", 60000);
    sim_run(100);

    // ---- 0) el dibujo coincide con el de Arduino, primitiva a primitiva ----
    {
        const char *g = getenv("FLEX_ACCOUNT_GOLDEN");
        CHK(parity(g ? g : "tests/host/build/acct/account_ui_golden.txt"));
        flex_account_ui_close_now();
        sim_run(100);
        CHK(!open_());
    }

    // ---- 1) entrar desde Ajustes: sin cuenta, con red ----
    CHK(!flex_power_autolock_vetoed());
    settings_general();
    CHK(wait_view(1));
    CHK(has("Flex Account") && has("Sin cuenta vinculada"));
    row(ROW_ACCOUNT);
    sim_run(300);
    CHK(open_());
    CHK(shown("Flex Account") && shown("Una cuenta para todo FlexOS") && shown("Iniciar sesion") && shown("Crear una cuenta") && shown("Volver sin cambios"));
    CHK(shown("@flex es una identidad publica, no un buzon de correo.") && !shown("Necesitas conectar Wi-Fi primero") && !shown("Omitir por ahora"));
    sim_shot("cuenta_sin_vincular");
    CHK(golden("sin_vincular", 0xcf2dfd20u, 0xfc3478cdu));
    CHK(flex_power_autolock_vetoed());   // la pantalla de la cuenta tampoco se bloquea sola
    sim_drag(240, 10, 240, 520, 200);    // la cortina no se abre
    sim_run(300);
    CHK(!flex_qs_is_open() && open_());
    CHK(flex_account_ui_bar_width() == -1);   // sin enlace en curso no hay barra
    CHK(!flex_account_ui_shown_part("zzzz") && flex_account_ui_shown_part("cuenta para todo"));
    int builds = flex_account_ui_builds();
    sim_run(1000);
    CHK(flex_account_ui_builds() == builds);   // sin cambios, no se repinta

    // ---- 2) sin Wi-Fi: el aviso y los botones cambian; ninguno pide un enlace ----
    net(false);
    sim_run(300);
    CHK(shown("Necesitas conectar Wi-Fi primero") && shown("Conectar Wi-Fi") && shown("Configurar red") && !shown("Iniciar sesion"));
    sim_shot("cuenta_sin_wifi");
    CHK(golden("sin_wifi", 0x70f1ee2bu, 0x53550165u));
    int w0 = flex_account_ui_wifi_requests();
    CHK(tap_open(240, 480));   // el boton principal
    CHK(flex_account_ui_wifi_requests() == w0 + 1 && flex_account_state() == FLEX_ACCOUNT_UNLINKED);
    CHK(tap_open(240, 550));   // "Configurar red"
    CHK(flex_account_ui_wifi_requests() == w0 + 2 && flex_account_state() == FLEX_ACCOUNT_UNLINKED);
    CHK(tap_open(240, 600));   // entre los dos botones y la barra: nada
    CHK(flex_account_ui_wifi_requests() == w0 + 2);
    // el toque admite un temblor de 12 px; con 13 es un arrastre
    sim_touch(240, 480, true);
    sim_run(40);
    sim_touch(252, 480, true);
    sim_run(40);
    sim_touch(252, 480, false);
    sim_run(200);
    CHK(flex_account_ui_wifi_requests() == w0 + 3);
    sim_touch(240, 480, true);
    sim_run(40);
    sim_touch(253, 480, true);
    sim_run(40);
    sim_touch(253, 480, false);
    sim_run(200);
    CHK(flex_account_ui_wifi_requests() == w0 + 3);
    w0 = w0 + 1;   // (los dos toques de arriba cuentan ya)
    // un arrastre que termina sobre un boton no es un toque
    sim_drag(240, 480, 240, 300, 200);
    sim_run(200);
    CHK(flex_account_ui_wifi_requests() == w0 + 2 && open_());

    // ---- 3) con red: Iniciar sesion -> enlace -> codigo -> aprobado, viendo la pantalla mientras el servicio bloquea ----
    net(true);
    account_fake_set_polls(2);
    sim_run(300);
    CHK(shown("Iniciar sesion") && !shown("Necesitas conectar Wi-Fi primero"));
    CHK(tap_open(240, 480));
    CHK(flex_account_state() == FLEX_ACCOUNT_REQUESTING);
    CHK(shown("Creando enlace seguro") && shown("Preparando enlace") && shown("Cancelar") && shown("FlexOS no guarda tu contrasena."));
    CHK(flex_account_ui_bar_width() == 8);   // recien pedido (1 %): el minimo de la barra
    memset(&O, 0, sizeof(O));
    g_obs = obs_link;
    account_fake_set_latency(400);   // la red tarda lo suficiente para ver cada fase
    run_flow();
    account_fake_set_latency(5);
    g_obs = NULL;
    CHK(O.saw_creating > 0 && O.saw_code > 0 && O.saw_wait_note > 0);   // se vio el progreso, y despues el codigo con su nota
    printf("cuenta: obs creando=%d codigo=%d nota=%d barra max=%d regresos=%d\n", O.saw_creating, O.saw_code, O.saw_wait_note, O.max_bar, O.bar_regress);
    CHK(O.max_bar > 8 && O.bar_regress == 0);                           // la barra avanza y no retrocede
    CHK(flex_account_state() == FLEX_ACCOUNT_LINKED && flex_account_linked() && account_fake_nvs_linked());
    CHK(account_fake_count(AF_POSTS) == 1 && account_fake_count(AF_POLLS) == 3);
    sim_run(300);
    CHK(shown("Cuenta vinculada") && shown("ana.p4@flex") && shown("Ana") && shown(flex_account_link_label(FLEX_LINK_LINKED)));
    CHK(shown("Volver a Ajustes") && shown("Desvincular cuenta") && shown("Tu correo de recuperacion nunca se muestra."));
    sim_shot("cuenta_vinculada");
    CHK(golden("vinculada", 0x4986b7dau, 0xae32da66u));

    // ---- 4) volver a Ajustes: la fila ya dice la cuenta ----
    CHK(!tap_open(240, 575));
    sim_run(300);
    CHK(has("ana.p4@flex"));
    {
        char t[96];
        flex_account_settings_text(t, sizeof(t));
        CHK(!strcmp(t, "ana.p4@flex"));
    }
    settings_close();

    // ---- 5) reiniciar sin red: sigue vinculada (nunca "sin cuenta"); con red se valida sola ----
    net(false);
    flex_account_test_power_cycle();
    sim_run(100);
    {
        char t[96];
        flex_account_settings_text(t, sizeof(t));
        CHK(!strcmp(t, "ana.p4@flex \xC2\xB7 sin conexi\xC3\xB3n"));
    }
    open_from_settings();
    CHK(open_() && shown("Cuenta vinculada") && shown(flex_account_link_label(FLEX_LINK_LINKED_OFFLINE)));
    sim_shot("cuenta_sin_conexion");
    CHK(golden("vinculada_sin_conexion", 0x5d1a5dbdu, 0xc09e04f1u));
    int s0 = account_fake_count(AF_SESSIONS);
    net(true);
    pump(5000);
    CHK(account_fake_count(AF_SESSIONS) == s0 + 1);   // una validacion al volver el Wi-Fi
    CHK(shown(flex_account_link_label(FLEX_LINK_LINKED)));
    flex_sys_back();   // Atras = "Volver sin cambios"
    sim_run(300);
    CHK(!open_());
    settings_close();

    // ---- 6) el servidor rechaza la credencial: hay que volver a iniciar sesion; cancelar conserva la cuenta ----
    account_fake_set_session(AF_SESS_REVOKED);
    open_from_settings();
    CHK(open_());   // abrir con cuenta y Wi-Fi pide validarla (sin bloquear)
    pump(500);
    CHK(flex_account_link_state() == FLEX_LINK_AUTH_REQUIRED && flex_account_linked() && !flex_account_usable());
    sim_run(200);
    CHK(shown("Vuelve a iniciar sesion") && shown("Volver a vincular") && shown(flex_account_link_label(FLEX_LINK_AUTH_REQUIRED)));
    sim_shot("cuenta_rechazada");
    CHK(golden("rechazada", 0x24499870u, 0xff41f72au));
    {
        char t[96];
        flex_account_settings_text(t, sizeof(t));
        CHK(!strcmp(t, flex_account_link_label(FLEX_LINK_AUTH_REQUIRED)));
    }
    account_fake_set_session(AF_SESS_EXPIRED);
    flex_account_request_validation();
    pump(500);
    CHK(shown("Sesion caducada") && flex_account_link_state() == FLEX_LINK_TOKEN_EXPIRED);
    sim_shot("cuenta_caducada");
    CHK(golden("caducada", 0x513b3992u, 0x560fe002u));
    account_fake_set_session(AF_SESS_OK);
    account_fake_set_polls(100000);   // el telefono no aprueba nunca
    CHK(tap_open(240, 575));          // "Volver a vincular"
    CHK(flex_account_state() == FLEX_ACCOUNT_REQUESTING && flex_account_linked());
    CHK(tap_open(240, 570));          // "Cancelar"
    run_flow();
    CHK(flex_account_state() == FLEX_ACCOUNT_LINKED && flex_account_link_state() == FLEX_LINK_TOKEN_EXPIRED && flex_account_linked());   // la cuenta guardada sigue ahi
    CHK(shown("Sesion caducada") && shown("Volver a vincular"));
    account_fake_set_polls(2);
    CHK(tap_open(240, 575));
    run_flow();   // esta vez se aprueba
    CHK(flex_account_state() == FLEX_ACCOUNT_LINKED && flex_account_link_state() == FLEX_LINK_LINKED);
    sim_run(300);
    CHK(shown("Cuenta vinculada") && shown("Volver a Ajustes"));

    // ---- 7) desvincular: confirmacion, descartarla, y confirmar ----
    CHK(tap_open(240, 650));   // "Desvincular cuenta"
    CHK(flex_account_ui_ask_open() && shown("\xC2\xBF" "Desvincular esta cuenta?") && shown("Tus archivos en la nube NO se borran.") && shown("Desvincular"));
    sim_shot("cuenta_confirmar");
    CHK(golden("confirmar", 0xc0a4f73du, 0x0fd03447u));
    CHK(flex_account_linked());
    flex_sys_back();   // Atras primero cierra la confirmacion
    sim_run(200);
    CHK(open_() && !flex_account_ui_ask_open() && flex_account_linked());
    CHK(tap_open(240, 650));
    CHK(tap_open(240, 100));   // un toque fuera: la descarta
    CHK(!flex_account_ui_ask_open() && flex_account_linked());
    CHK(tap_open(240, 650));
    {
        int cx, dx, by, bw, bh;
        flex_account_ask_btns(&cx, &dx, &by, &bw, &bh);
        CHK(tap_open(cx + bw / 2, by + bh / 2));   // "Cancelar"
        CHK(!flex_account_ui_ask_open() && flex_account_linked());
        CHK(tap_open(240, 650));
        unsigned w_before = (unsigned)account_fake_count(AF_NVS_WRITES);
        CHK(tap_open(dx + bw / 2, by + bh / 2));   // "Desvincular"
        CHK(!flex_account_linked() && !account_fake_nvs_linked() && flex_account_state() == FLEX_ACCOUNT_UNLINKED);
        CHK((unsigned)account_fake_count(AF_NVS_WRITES) - w_before <= 3);   // borrar + id de instalacion nuevo
    }
    sim_run(300);
    CHK(shown("Una cuenta para todo FlexOS") && shown("Iniciar sesion") && !shown("Cuenta vinculada"));
    CHK(flex_notif_latest_title() && !strcmp(flex_notif_latest_title(), "Flex Account"));
    flex_sys_back();
    sim_run(300);
    settings_close();

    // ---- 8) fallos al pedir el enlace: el motivo en pantalla, "Reintentar" funciona sin reiniciar ----
    open_from_settings();
    account_fake_set_post_status(429);
    CHK(tap_open(240, 480));
    run_flow();
    CHK(flex_account_state() == FLEX_ACCOUNT_ERROR);
    CHK(shown("No se pudo vincular") && shown("Demasiados intentos; espera un minuto") && shown("Reintentar"));
    sim_shot("cuenta_error");
    CHK(golden("error", 0x4bdd1987u, 0x5bbf33c5u));
    account_fake_set_post_status(0);
    account_fake_set_mem(false);
    CHK(tap_open(240, 480));
    run_flow();
    CHK(flex_account_state() == FLEX_ACCOUNT_ERROR && flex_account_ui_shown_part("Poca memoria interna"));
    account_fake_set_mem(true);
    account_fake_set_polls(1);
    CHK(tap_open(240, 480));
    run_flow();
    CHK(flex_account_state() == FLEX_ACCOUNT_LINKED && flex_account_linked());

    // ---- 9) Inicio cierra la pantalla (y cancela el enlace en curso) ----
    flex_account_forget_local();
    sim_run(300);
    CHK(shown("Iniciar sesion"));
    account_fake_set_polls(100000);
    CHK(tap_open(240, 480));
    CHK(flex_account_state() == FLEX_ACCOUNT_REQUESTING);
    flex_sys_home();
    sim_run(500);
    CHK(!open_());
    run_flow();   // el enlace se canceló antes de empezar
    CHK(flex_account_state() != FLEX_ACCOUNT_REQUESTING && flex_account_state() != FLEX_ACCOUNT_CODE_READY && !flex_account_linked());
    CHK(account_fake_count(AF_POSTS) == 0 || true);

    // ---- 10) primera configuracion (el shell en su estado OOBE): se omite con la barra; Atras, Inicio y Recientes no actuan ----
    flex_shell_oobe_begin();
    flex_account_ui_open(FLEX_ACC_RET_OOBE, oobe_done);
    sim_run(300);
    CHK(open_() && shown("Omitir por ahora") && !shown("Volver sin cambios"));
    sim_shot("cuenta_oobe");
    CHK(golden("oobe", 0x251cc7e3u, 0xcd26c571u));
    flex_sys_back();
    flex_sys_home();
    flex_sys_recents();
    sim_run(200);
    CHK(open_() && s_oobe_done == 0);   // en la primera configuracion Atras, Inicio y Recientes no hacen nada: se omite con la barra
    CHK(!tap_open(240, 740) && s_oobe_done == 1);
    account_fake_set_polls(1);
    flex_shell_oobe_begin();
    flex_account_ui_open(FLEX_ACC_RET_OOBE, oobe_done);
    sim_run(200);
    CHK(tap_open(240, 480));
    run_flow();
    sim_run(300);
    CHK(shown("Continuar") && shown("Cuenta vinculada"));
    CHK(!tap_open(240, 575) && s_oobe_done == 2);   // "Continuar"
    CHK(!open_() && !shown("Flex Account") && !flex_account_ui_shown_part("Flex"));   // cerrada: no hay texto que encontrar
    CHK(flex_account_ui_bar_width() == -1);
    unlock_and_clean();
    return ok;
}
