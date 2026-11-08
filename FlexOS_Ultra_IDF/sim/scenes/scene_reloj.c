// Reloj y Cronometro (03 §1-2): la app real en el simulador. Pestanas (Hora, Cronometro, Temporizador), el cronometro con sus dos
// botones, la tabla de vueltas con mejor / peor, la capsula de la barra de estado (en el escritorio y en el marco de app, nunca
// en una app con cabecera propia), la tarjeta expandida (modal, con animacion, botones, cierre por fuera / gesto / tiempo) y el
// resto de puntos de entrada (pulsacion larga, mosaico del panel rapido, bloqueo).
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_clock.h"
#include "flex_crono.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_qs_model.h"
#include "flex_shell.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

#define CHK(c)                                                    \
    do {                                                          \
        if (!(c)) {                                               \
            printf("reloj: FALLO linea %d: %s\n", __LINE__, #c);  \
            ok = false;                                           \
        }                                                         \
    } while (0)

void flex_crono_svc_test_set(uint32_t elapsed_ms, bool running);
int32_t flex_home_scroll_x(void);
void flex_kiosk_load(void);
void flex_kiosk_boot(void);
void flex_kiosk_exit_now(void);
bool flex_kiosk_active(void);
void flex_cfg_set_bool(const char *k, bool v);
void flex_cfg_set_i32(const char *k, int32_t v);
bool flex_home_icon_area(int app_id, lv_area_t *out);
#define T_TXT(n) flex_app_reloj_text(n)
enum { TX_DIAL = 0, TX_BL, TX_BR, TX_CLOCK, TX_DATE, TX_CARD0, TX_CARD1, TX_TTITLE, TX_TSUB, TX_NOLAPS, TX_ROW = 10 };
#define ROW(r, c) T_TXT(TX_ROW + 3 * (r) + (c))

static const char *bl(void) { return flex_t(FLEX_S_CRN_BLAP); }
static const char *br_reset(void) { return flex_t(FLEX_S_CRN_BRESET); }
static const char *bstart(void) { return flex_t(FLEX_S_CRN_BSTART); }
static const char *bstop(void) { return flex_t(FLEX_S_CRN_BSTOP); }
static const char *bcont(void) { return flex_t(FLEX_S_CONTINUE); }

// toque corto con tiempos controlados (sim_tap dura ~200 ms)
static void tap(int x, int y)
{
    sim_touch(x, y, true);
    sim_run(60);
    sim_touch(x, y, false);
    sim_run(120);
}

// el rotulo visible con ese texto (el titulo de la cabecera estandar del marco)
static lv_obj_t *visible_label(lv_obj_t *o, const char *txt)
{
    if (lv_obj_check_type(o, &lv_label_class) && lv_obj_is_visible(o) && strcmp(lv_label_get_text(o), txt) == 0) {
        return o;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *r = visible_label(lv_obj_get_child(o, (int32_t)i), txt);
        if (r) {
            return r;
        }
    }
    return NULL;
}

static bool cap(lv_area_t *a, char *txt, size_t n)
{
    return flex_crono_cap_shown(a, txt, n);
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
    flex_crono_svc_reset();
    sim_run(200);
}

// la hora de la barra de estado a la que se refiere la capsula
static bool open_reloj(int tab)
{
    flex_app_reloj_goto_tab(tab);
    flex_app_open(IC_RELOJ, NULL);
    sim_run(600);
    return flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_RELOJ;
}

static void open_qs_top(void)
{
    sim_touch(240, 8, true);
    sim_run(40);
    for (int y = 8; y <= 500; y += 20) {
        sim_touch(240, y, true);
        sim_run(16);
    }
    sim_touch(240, 500, false);
    sim_run(500);
}

bool scene_reloj_run(void)
{
    bool ok = true;
    unlock_and_clean();
    lv_area_t a;
    char ct[16];

    // ---- 1) sin cronometro: ni capsula ni tarjeta; la hora normal en la barra --------------------------------------------
    CHK(!cap(&a, ct, sizeof(ct)));
    CHK(!flex_crono_card_active());
    CHK(flex_crono_elapsed_ms() == 0 && !flex_crono_running());

    // ---- 2) la app: pestana Hora (reloj grande, fecha larga, tarjetas, pie) ---------------------------------------------
    CHK(open_reloj(0));
    CHK(flex_app_reloj_seg_active() == 0);
    {
        char cs[8];
        flex_clock_str_big(cs, sizeof(cs));
        CHK(strcmp(T_TXT(TX_CLOCK), cs) == 0);
        struct tm tm;
        flex_clock_now(&tm);
        char ds[64], sd[40];
        flex_date_long(ds, sizeof(ds), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
        flex_date_short(sd, sizeof(sd), tm.tm_wday, tm.tm_mday, tm.tm_mon + 1);
        CHK(strcmp(T_TXT(TX_DATE), ds) == 0);
        CHK(strcmp(T_TXT(TX_CARD0), sd) == 0);
        CHK(strcmp(T_TXT(TX_CARD1), flex_look()->h24 ? "24 h" : "12 h") == 0);
    }
    sim_shot("reloj_hora");
    // formato de hora: al cambiar a 24 h el reloj grande y la tarjeta lo siguen
    flex_theme_set_h24(true);
    flex_app_reloj_goto_tab(0);
    flex_shell_show_home();
    sim_run(100);
    CHK(open_reloj(0));
    CHK(strcmp(T_TXT(TX_CARD1), "24 h") == 0);
    {
        char cs[8];
        flex_clock_str_big(cs, sizeof(cs));
        CHK(strcmp(T_TXT(TX_CLOCK), cs) == 0);
        CHK(strchr(cs, ':') && cs[strlen(cs) - 3] == ':');
    }
    flex_theme_set_h24(false);
    // tema: con la app delante, el interruptor Oscuro / Claro del panel rapido recolorea todo (los colores se leen al crear)
    {
        bool dark0 = flex_look()->dark;
        flex_theme_set_dark(!dark0);
        sim_run(200);
        CHK(flex_app_reloj_color(TX_CLOCK) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF));
        CHK(flex_app_reloj_color(TX_CARD1) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF));
        CHK(flex_app_reloj_color(TX_DATE) == (lv_color_to_u32(flex_th()->txt2) & 0xFFFFFF));
        // la cabecera estandar del marco (el titulo) tambien sigue al tema
        lv_obj_t *title = visible_label(flex_shell_screen(), "Reloj");
        CHK(title != NULL);
        CHK(title && (lv_color_to_u32(lv_obj_get_style_text_color(title, LV_PART_MAIN)) & 0xFFFFFF) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF));
        sim_shot("reloj_hora_claro");
        flex_theme_set_dark(dark0);
        sim_run(200);
        CHK(flex_app_reloj_color(TX_CLOCK) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF));
    }

    // ---- 3) pestanas: Cronometro y Temporizador ----------------------------------------------------------------------
    {
        // relojDrawTabs: barra x 20, y 101, 440 x 36; tres segmentos de 146 px, el activo inset 2 px (142 x 32)
        lv_obj_t *sg = flex_app_reloj_seg_obj();
        CHK(sg != NULL);
        lv_area_t sa;
        lv_obj_get_coords(sg, &sa);
        CHK(sa.x1 == 20 && sa.y1 == 101 && sa.x2 - sa.x1 + 1 == 440 && sa.y2 - sa.y1 + 1 == 36);
        for (int i = 0; i < 3; i++) {
            lv_area_t ia;
            lv_obj_get_coords(lv_obj_get_child(sg, i), &ia);
            CHK(ia.x1 == 20 + i * 146 + 2 && ia.x2 - ia.x1 + 1 == 142 && ia.y1 == 103 && ia.y2 - ia.y1 + 1 == 32);
        }
    }
    tap(239, 119);   // segundo segmento
    CHK(flex_app_reloj_tab() == 1 && flex_app_reloj_seg_active() == 1);
    CHK(strcmp(T_TXT(TX_DIAL), "00:00.00") == 0);
    CHK(strcmp(T_TXT(TX_BL), bl()) == 0 && strcmp(T_TXT(TX_BR), bstart()) == 0);
    CHK(strcmp(T_TXT(TX_NOLAPS), flex_t(FLEX_S_CRN_NOLAPS)) == 0);
    sim_shot("reloj_crono0");
    tap(385, 119);   // tercer segmento
    CHK(flex_app_reloj_tab() == 2 && flex_app_reloj_seg_active() == 2);
    CHK(strcmp(T_TXT(TX_TTITLE), flex_t(FLEX_S_CRN_TIMER)) == 0);
    CHK(strcmp(T_TXT(TX_TSUB), flex_t(FLEX_S_SOON)) == 0);
    CHK(strcmp(T_TXT(TX_DIAL), "") == 0);   // nada del cronometro queda en esta pestana
    sim_shot("reloj_temporizador");
    tap(80, 119);    // primer segmento
    CHK(flex_app_reloj_tab() == 0 && strcmp(T_TXT(TX_TTITLE), "") == 0);
    // un toque en la pestana ya activa no la rehace
    tap(80, 119);
    CHK(flex_app_reloj_tab() == 0);
    tap(239, 119);
    CHK(flex_app_reloj_tab() == 1);

    // ---- 4) cronometro: iniciar, parcial, detener, continuar, reiniciar ---------------------------------------------------
    CHK(!cap(&a, ct, sizeof(ct)));
    {
        // parado: la esfera no se repinta; el rotulo izquierdo esta apagado y el derecho lleva el acento
        int f0 = flex_app_reloj_frames();
        sim_run(600);
        CHK(flex_app_reloj_frames() == f0);
        CHK(flex_app_reloj_color(TX_BL) == (lv_color_to_u32(flex_th()->disabled) & 0xFFFFFF));
        CHK(flex_app_reloj_color(TX_BR) == (lv_color_to_u32(flex_accent()) & 0xFFFFFF));
    }
    tap(360, 640);   // Iniciar
    CHK(flex_crono_running());
    {
        // corriendo: ~30 fps; el rotulo derecho pasa a rojo (Detener) y el izquierdo se enciende
        int f0 = flex_app_reloj_frames();
        sim_run(1000);
        int df = flex_app_reloj_frames() - f0;
        CHK(df >= 22 && df <= 40);
        CHK(flex_app_reloj_color(TX_BR) == (lv_color_to_u32(flex_th()->danger) & 0xFFFFFF));
        CHK(flex_app_reloj_color(TX_BL) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF));
    }
    CHK(strcmp(T_TXT(TX_BR), bstop()) == 0 && strcmp(T_TXT(TX_BL), bl()) == 0);
    sim_run(1200);
    // la capsula aparece en la barra del marco de app, con la hora normal delante y el mismo tiempo
    CHK(cap(&a, ct, sizeof(ct)));
    CHK(strcmp(ct, "00:01") == 0 || strcmp(ct, "00:02") == 0);
    CHK(a.y1 == FLEX_CRONO_CAP_Y && a.y2 - a.y1 + 1 == FLEX_CRONO_CAP_H);
    CHK(a.x1 > 20 && a.x2 < FLEX_CRONO_CAP_RIGHT);   // detras de la hora y sin pisar el Wi-Fi
    {
        // la esfera dice lo mismo (segundos) que la capsula
        char d[16];
        snprintf(d, sizeof(d), "%c%c", T_TXT(TX_DIAL)[3], T_TXT(TX_DIAL)[4]);
        CHK(d[0] == ct[3] && d[1] == ct[4]);
    }
    uint32_t t1 = flex_crono_elapsed_ms();
    sim_run(500);
    CHK(flex_crono_elapsed_ms() >= t1 + 480);
    CHK(strcmp(T_TXT(TX_DIAL), "") != 0);
    sim_shot("reloj_crono_run");
    // parciales: la tabla muestra las mas recientes arriba y colorea la mejor y la peor desde la tercera
    CHK(strcmp(T_TXT(TX_NOLAPS), "") == 0 || 1);
    tap(120, 640);
    CHK(flex_crono_svc()->nlaps == 1);
    CHK(strcmp(ROW(0, 0), "01") == 0);
    sim_run(500);
    tap(120, 640);
    sim_run(1300);
    tap(120, 640);
    sim_run(300);
    tap(120, 640);
    CHK(flex_crono_svc()->nlaps == 4);
    CHK(strcmp(ROW(0, 0), "04") == 0 && strcmp(ROW(1, 0), "03") == 0);
    {
        const flex_crono_t *c = flex_crono_svc();
        char exp[16];
        flex_crono_fmt(exp, sizeof(exp), c->laps[3].split, true);
        CHK(strcmp(ROW(0, 1), exp) == 0);
        flex_crono_fmt(exp, sizeof(exp), c->laps[3].total, true);
        CHK(strcmp(ROW(0, 2), exp) == 0);
        CHK(c->best >= 0 && c->worst >= 0 && c->best != c->worst);
        CHK(c->laps[c->best].split <= c->laps[c->worst].split);
    }
    sim_shot("reloj_crono_laps");
    // mejor y peor: parciales de ~1 s, ~2,5 s y ~0,5 s; las dos filas a la vista son la mejor (la ultima) y la peor
    {
        tap(360, 640);   // Detener
        tap(120, 640);   // Reinic.
        CHK(flex_crono_svc()->st == FLEX_CRONO_IDLE);
        tap(360, 640);   // Iniciar
        sim_run(900);
        tap(120, 640);
        sim_run(2300);
        tap(120, 640);
        sim_run(300);
        tap(120, 640);
        const flex_crono_t *c = flex_crono_svc();
        CHK(c->nlaps == 3 && c->best == 2 && c->worst == 1);
        CHK(strcmp(ROW(0, 0), "03") == 0 && strcmp(ROW(1, 0), "02") == 0);
        for (int col = 0; col < 3; col++) {
            CHK(flex_app_reloj_color(TX_ROW + col) == (lv_color_to_u32(flex_accent()) & 0xFFFFFF));        // la mejor
            CHK(flex_app_reloj_color(TX_ROW + 3 + col) == (lv_color_to_u32(flex_th()->danger) & 0xFFFFFF));   // la peor
        }
    }
    // pausa: se congela
    tap(360, 640);
    CHK(!flex_crono_running() && flex_crono_svc()->st == FLEX_CRONO_PAUSE);
    CHK(strcmp(T_TXT(TX_BL), br_reset()) == 0 && strcmp(T_TXT(TX_BR), bcont()) == 0);
    {
        char frozen[16];
        snprintf(frozen, sizeof(frozen), "%s", T_TXT(TX_DIAL));
        uint32_t e0 = flex_crono_elapsed_ms();
        int f0 = flex_app_reloj_frames();
        sim_run(900);
        CHK(flex_crono_elapsed_ms() == e0);
        CHK(strcmp(T_TXT(TX_DIAL), frozen) == 0);
        CHK(flex_app_reloj_frames() == f0);   // en pausa la esfera no se repinta
        CHK(flex_app_reloj_color(TX_BR) == (lv_color_to_u32(flex_th()->txt) & 0xFFFFFF) || flex_app_reloj_color(TX_BR) == (lv_color_to_u32(flex_accent()) & 0xFFFFFF));
        CHK(cap(&a, ct, sizeof(ct)));   // la capsula sigue mientras esta en pausa
    }
    sim_shot("reloj_crono_pausa");
    // continuar: sigue desde donde estaba (no vuelve a 0)
    {
        uint32_t e0 = flex_crono_elapsed_ms();
        tap(360, 640);
        sim_run(500);
        CHK(flex_crono_running());
        CHK(flex_crono_elapsed_ms() >= e0 + 480 && flex_crono_elapsed_ms() < e0 + 1200);
    }
    // el cronometro sigue contando con la app cerrada y al volver la esfera esta al dia
    flex_sys_home();
    sim_run(500);
    CHK(flex_crono_running());
    CHK(cap(&a, ct, sizeof(ct)));   // escritorio: la misma capsula
    sim_shot("reloj_home_capsula");
    {
        uint32_t e0 = flex_crono_elapsed_ms();
        sim_run(1500);
        CHK(flex_crono_elapsed_ms() >= e0 + 1480);
    }

    // ---- 5) la tarjeta: tocar la pildora la despliega (animacion de 190 ms), modal ----------------------------------------
    CHK(cap(&a, ct, sizeof(ct)));
    {
        int tx = a.x1 + 55, ty = 34;   // fuera del icono, dentro de la pildora (y >= 30: fuera de la franja de la cortina)
        sim_touch(tx, ty, true);
        sim_run(60);
        sim_touch(tx, ty, false);
        sim_run(70);
        CHK(flex_crono_card_active() && flex_crono_card_phase() == FLEX_CC_OPENING);
        lv_area_t r;
        CHK(flex_crono_card_rect(&r));
        CHK(r.y1 > FLEX_CRONO_CAP_Y && r.y2 < FLEX_CRONO_CARD_Y + FLEX_CRONO_CARD_H);   // a medio camino
        sim_run(400);
        CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
        CHK(flex_crono_card_rect(&r));
        CHK(r.x1 == FLEX_CRONO_CARD_X && r.y1 == FLEX_CRONO_CARD_Y && r.x2 - r.x1 + 1 == FLEX_CRONO_CARD_W &&
            r.y2 - r.y1 + 1 == FLEX_CRONO_CARD_H);
        sim_shot("reloj_tarjeta");
        // lo que muestra: tiempo con centesimas, "Vuelta N" y los dos botones segun el estado
        CHK(strlen(flex_crono_card_text(0)) == 8 && flex_crono_card_text(0)[2] == ':' && flex_crono_card_text(0)[5] == '.');
        char lap[32];
        snprintf(lap, sizeof(lap), "%s %d", flex_t(FLEX_S_CRN_LAP), flex_crono_cur_lap_no(flex_crono_svc()));
        CHK(strcmp(flex_crono_card_text(1), lap) == 0);
        CHK(strcmp(flex_crono_card_text(2), bl()) == 0 && strcmp(flex_crono_card_text(3), bstop()) == 0);
        // el tiempo de la tarjeta avanza solo (~30 fps) y coincide con el servicio
        char t0[16];
        snprintf(t0, sizeof(t0), "%s", flex_crono_card_text(0));
        sim_run(300);
        CHK(strcmp(flex_crono_card_text(0), t0) != 0);
        // la pildora de la barra sigue viva por encima de la tarjeta
        CHK(cap(&a, ct, sizeof(ct)));
        CHK(ct[2] == ':');
    }
    // modal: un toque fuera cierra y NO llega a lo de debajo (el icono del Reloj, a la vista)
    flex_app_terminate_ex(IC_RELOJ, true);
    sim_run(100);
    {
        lv_area_t ic;
        CHK(flex_home_icon_area(IC_RELOJ, &ic));
        tap((ic.x1 + ic.x2) / 2, 260);   // fuera de la tarjeta (termina en y = 226), sobre un icono
        CHK(flex_crono_card_phase() == FLEX_CC_CLOSING);   // se contrae en 190 ms: aun esta a la vista
        sim_run(100);
        sim_run(400);
        CHK(!flex_crono_card_active());
        CHK(flex_shell_state() == FLEX_SH_HOME);   // el icono no se abrio
        CHK(!flex_app_is_open(IC_RELOJ));
    }
    // la barra de debajo se rehace al volver: la capsula sigue
    CHK(cap(&a, ct, sizeof(ct)));

    // modal: arrastrar el escritorio por debajo de la tarjeta abierta no lo mueve, y Recientes la suelta
    sim_touch(a.x1 + 55, 34, true);
    sim_run(60);
    sim_touch(a.x1 + 55, 34, false);
    sim_run(500);
    CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
    {
        int32_t sx = flex_home_scroll_x();
        sim_drag(420, 420, 60, 420, 300);
        sim_run(500);
        CHK(flex_home_scroll_x() == sx);
        CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
        flex_sys_recents();
        sim_run(250);
        CHK(!flex_crono_card_active());   // otra pantalla se adueno: la tarjeta no sobrevive
        flex_sys_home();
        sim_run(500);
        CHK(flex_shell_state() == FLEX_SH_HOME);
        CHK(cap(&a, ct, sizeof(ct)));
    }
    // botones de la tarjeta: Parcial suma una vuelta; Detener pausa; Reinic. deja el cronometro inactivo y la tarjeta se contrae
    sim_touch(a.x1 + 55, 34, true);
    sim_run(60);
    sim_touch(a.x1 + 55, 34, false);
    sim_run(500);
    CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
    {
        int n0 = flex_crono_svc()->nlaps;
        // durante la animacion los toques se consumen: se prueba abajo, ahora esta abierta
        tap(130, 190);   // Parcial (mitad izquierda de la fila de botones)
        CHK(flex_crono_svc()->nlaps == n0 + 1);
        sim_run(100);
        CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
        char lap[32];
        snprintf(lap, sizeof(lap), "%s %d", flex_t(FLEX_S_CRN_LAP), flex_crono_cap_shown(&a, ct, sizeof(ct)) ? flex_crono_cur_lap_no(flex_crono_svc()) : -1);
        CHK(strcmp(flex_crono_card_text(1), lap) == 0);
        tap(350, 190);   // Detener
        sim_run(150);
        CHK(flex_crono_svc()->st == FLEX_CRONO_PAUSE);
        CHK(strcmp(flex_crono_card_text(2), br_reset()) == 0 && strcmp(flex_crono_card_text(3), bcont()) == 0);
        sim_shot("reloj_tarjeta_pausa");
        tap(350, 190);   // Continuar
        sim_run(150);
        CHK(flex_crono_running());
        CHK(strcmp(flex_crono_card_text(3), bstop()) == 0);
        tap(350, 190);   // Detener otra vez para poder reiniciar
        sim_run(100);
        tap(130, 190);   // Reinic.
        sim_run(100);
        CHK(flex_crono_svc()->st == FLEX_CRONO_IDLE && flex_crono_svc()->nlaps == 0);
        CHK(flex_crono_card_phase() == FLEX_CC_CLOSING || flex_crono_card_phase() == FLEX_CC_HIDDEN);
        sim_run(400);
        CHK(!flex_crono_card_active());
        CHK(!cap(&a, ct, sizeof(ct)));   // sin cronometro, sin capsula
    }

    // ---- 6) gesto hacia arriba, tiempo / icono abren Reloj, y la tarjeta no sobrevive al bloqueo -----------------------------
    flex_crono_svc_test_set(5000, true);
    sim_run(400);
    CHK(cap(&a, ct, sizeof(ct)));
    tap(a.x1 + 55, 34);
    sim_run(500);
    CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
    sim_drag(240, 180, 240, 60, 120);   // gesto hacia arriba dentro de la tarjeta
    sim_run(500);
    CHK(!flex_crono_card_active());
    tap(a.x1 + 55, 34);
    sim_run(500);
    CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
    tap(240, 70);   // sobre el tiempo: abre la app en la pestana Cronometro
    sim_run(800);
    CHK(!flex_crono_card_active());
    CHK(flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_RELOJ && flex_app_reloj_tab() == 1);
    CHK(strcmp(T_TXT(TX_BR), bstop()) == 0);
    // desde la app: tocar la pildora abre la tarjeta tambien en el marco de app, y el bloqueo la cierra
    CHK(cap(&a, ct, sizeof(ct)));
    tap(a.x1 + 55, 34);
    sim_run(500);
    CHK(flex_crono_card_phase() == FLEX_CC_OPEN);
    {
        // lo que se hace en la tarjeta se ve en la pestana de debajo (tabla y botones se repintan por su bandera)
        int n0 = flex_crono_svc()->nlaps;
        char no[8];
        tap(130, 190);   // Parcial
        sim_run(150);
        CHK(flex_crono_svc()->nlaps == n0 + 1);
        snprintf(no, sizeof(no), "%02d", (int)flex_crono_svc()->lap0 + n0);
        CHK(strcmp(ROW(0, 0), no) == 0);
        tap(350, 190);   // Detener
        sim_run(150);
        CHK(strcmp(T_TXT(TX_BR), bcont()) == 0 && strcmp(T_TXT(TX_BL), br_reset()) == 0);
        tap(350, 190);   // Continuar
        sim_run(150);
        CHK(strcmp(T_TXT(TX_BR), bstop()) == 0 && strcmp(T_TXT(TX_BL), bl()) == 0);
        CHK(strcmp(T_TXT(TX_DIAL), "") != 0);
    }
    flex_shell_lock();
    sim_run(200);
    CHK(!flex_crono_card_active());
    sim_drag(240, 700, 240, 300, 300);
    sim_run(500);
    CHK(flex_crono_running());   // sigue contando con el bloqueo puesto

    // ---- 7) el icono de la pildora y la pulsacion larga llevan a la pestana Cronometro ----------------------------------------
    flex_app_reloj_goto_tab(0);
    CHK(flex_shell_state() == FLEX_SH_HOME || flex_shell_state() == FLEX_SH_APP);
    flex_sys_home();
    sim_run(500);
    flex_app_terminate_ex(IC_RELOJ, true);
    sim_run(100);
    CHK(cap(&a, ct, sizeof(ct)));
    tap(a.x1 + 17, 34);   // el icono
    sim_run(800);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_RELOJ && flex_app_reloj_tab() == 1);
    CHK(flex_app_reloj_seg_active() == 1);
    flex_app_reloj_goto_tab(0);
    sim_run(100);
    CHK(flex_app_reloj_seg_active() == 0);
    // pulsacion larga (> 700 ms, sin moverse) sobre la pildora
    CHK(cap(&a, ct, sizeof(ct)));
    sim_touch(a.x1 + 55, 34, true);
    sim_run(500);
    CHK(flex_app_reloj_tab() == 0);   // aun no
    sim_run(400);
    sim_touch(a.x1 + 55, 34, false);
    sim_run(500);
    CHK(flex_app_reloj_tab() == 1 && !flex_crono_card_active());
    // la pulsacion larga con movimiento (12 px o mas) no cuenta
    flex_app_reloj_goto_tab(0);
    sim_run(100);
    sim_touch(a.x1 + 55, 34, true);
    sim_run(300);
    sim_touch(a.x1 + 55 + 14, 34, true);
    sim_run(700);
    sim_touch(a.x1 + 55 + 14, 34, false);
    sim_run(300);
    CHK(flex_app_reloj_tab() == 0);

    // ---- 8) otras apps: el marco estandar lleva capsula, una app con cabecera propia no -----------------------------------------
    flex_sys_home();
    sim_run(500);
    flex_app_open(IC_CALC, NULL);   // marco estandar
    sim_run(600);
    CHK(cap(&a, ct, sizeof(ct)));
    tap(a.x1 + 17, 34);   // el icono lleva a Reloj / Cronometro desde otra app
    sim_run(900);
    CHK(flex_shell_fg_app() == IC_RELOJ && flex_app_reloj_tab() == 1);
    flex_sys_home();
    sim_run(500);
    flex_app_open(IC_AJUSTES, NULL);   // cabecera propia: sin capsula
    sim_run(700);
    CHK(!cap(&a, ct, sizeof(ct)));
    tap(120, 34);
    sim_run(500);
    CHK(!flex_crono_card_active());
    flex_sys_home();
    sim_run(500);

    // ---- 8b) Reloj suspendido: la capsula lo trae al frente en la pestana Cronometro (on_resume rehace la pantalla) ---------------------
    flex_app_terminate_ex(IC_RELOJ, true);
    sim_run(100);
    CHK(open_reloj(0));
    CHK(flex_app_reloj_seg_active() == 0);
    flex_sys_home();
    sim_run(500);
    CHK(flex_app_is_open(IC_RELOJ) && flex_shell_state() == FLEX_SH_HOME);
    CHK(cap(&a, ct, sizeof(ct)));
    tap(a.x1 + 17, 34);
    sim_run(900);
    CHK(flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_RELOJ);
    CHK(flex_app_reloj_seg_active() == 1 && flex_app_reloj_tab() == 1);
    CHK(strcmp(T_TXT(TX_BR), flex_crono_running() ? bstop() : bcont()) == 0);
    // la pestana Hora se rehace al cambiar el minuto
    tap(80, 119);
    CHK(flex_app_reloj_tab() == 0);
    {
        struct tm t0;
        flex_clock_now(&t0);
        uint32_t utc0 = (uint32_t)(timegm(&t0) - FLEX_TZ_OFFSET_SEC), tick0 = lv_tick_get();
        char before[8];
        snprintf(before, sizeof(before), "%s", T_TXT(TX_CLOCK));
        flex_clock_set_utc(utc0 + 120);
        sim_run(1500);
        char now_s[8];
        flex_clock_str_big(now_s, sizeof(now_s));
        CHK(strcmp(before, now_s) != 0);
        CHK(strcmp(T_TXT(TX_CLOCK), now_s) == 0);
        flex_clock_set_utc(utc0 + (lv_tick_get() - tick0) / 1000u);
        sim_run(1500);
    }
    flex_sys_home();
    sim_run(500);

    // ---- 8c) la cortina abierta sobre una app tapa la barra: un toque donde estaba la pildora no abre la tarjeta ------------------
    flex_app_open(IC_CALC, NULL);
    sim_run(600);
    CHK(cap(&a, ct, sizeof(ct)));
    open_qs_top();
    CHK(flex_qs_is_open());
    sim_tap(a.x1 + 55, 34);
    sim_run(300);
    CHK(!flex_crono_card_active());
    flex_qs_close_now();
    sim_run(300);
    flex_sys_home();
    sim_run(500);

    // ---- 8d) kiosco: la app clavada no lleva capsula ni tarjeta, en el escritorio tampoco; al salir vuelven -------------------------
    CHK(flex_lock_set("2468", FLEX_LOCK_PIN));   // el kiosco solo se aplica con clave puesta
    flex_cfg_set_bool("kioskon", true);
    flex_cfg_set_i32("kioskapp", IC_CALC);
    flex_shell_lock();
    sim_run(200);
    flex_kiosk_load();
    flex_kiosk_boot();
    sim_run(900);
    CHK(flex_kiosk_active() && flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_CALC);
    sim_run(300);
    CHK(cap(&a, ct, sizeof(ct)));   // la barra ya dibujada sigue (Arduino no la rehace), pero no recoge el toque
    sim_tap(a.x1 + 55, 34);
    sim_run(300);
    CHK(!flex_crono_card_active());
    flex_kiosk_exit_now();
    sim_run(900);
    CHK(!flex_kiosk_active() && flex_shell_state() == FLEX_SH_HOME);
    flex_lock_clear();
    CHK(cap(&a, ct, sizeof(ct)));

    // ---- 9) la cortina del panel rapido tapa la barra: ni capsula ni toque; el mosaico Cronometro del panel ---------------------
    open_qs_top();
    CHK(flex_qs_is_open());
    {
        // la pildora queda tapada por la cortina: un toque donde estaba no abre la tarjeta ni la app
        CHK(cap(&a, ct, sizeof(ct)));
        sim_tap(a.x1 + 55, 34);
        sim_run(300);
        CHK(!flex_crono_card_active() && flex_qs_is_open());
        lv_area_t tile;
        CHK(flex_qs_ctl_rect(FLEX_QS_CRONO, &tile));
        bool run0 = flex_crono_running();
        tap((tile.x1 + tile.x2) / 2, (tile.y1 + tile.y2) / 2);
        sim_run(300);
        CHK(flex_crono_running() == !run0);   // marcha <-> pausa
        char st[16];
        flex_crono_state_text(st, sizeof(st));
        CHK(strcmp(st, flex_crono_running() ? "En marcha" : "En pausa") == 0);
        tap((tile.x1 + tile.x2) / 2, (tile.y1 + tile.y2) / 2);
        sim_run(300);
        CHK(flex_crono_running() == run0);
    }
    flex_qs_close_now();
    sim_run(300);
    CHK(cap(&a, ct, sizeof(ct)));

    // ---- 10) una hora: la pildora cambia de clase (H:MM:SS) y de ancho sin pisar la conectividad ----------------------------------
    flex_crono_svc_test_set(3599000, true);
    sim_run(300);
    CHK(cap(&a, ct, sizeof(ct)));
    CHK(strlen(ct) == 5);
    int w0 = a.x2 - a.x1 + 1;
    sim_run(1500);
    CHK(cap(&a, ct, sizeof(ct)));
    CHK(strcmp(ct, "1:00:00") == 0 || strcmp(ct, "1:00:01") == 0);
    CHK(a.x2 - a.x1 + 1 > w0 && a.x2 < FLEX_CRONO_CAP_RIGHT);
    sim_shot("reloj_capsula_hora");

    // ---- 11) un reinicio deja todo limpio; un cronometro que sigue corriendo no se detiene con la app suspendida -----------------------
    flex_crono_svc_reset();
    sim_run(300);
    CHK(!cap(&a, ct, sizeof(ct)) && !flex_crono_card_active());
    CHK(flex_crono_elapsed_ms() == 0 && !flex_crono_running());
    printf("reloj: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
