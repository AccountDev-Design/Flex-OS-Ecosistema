// Memoria (01c §17): admision al abrir (las pesadas se niegan con poca PSRAM, las ligeras nunca), soltar lo
// reconstruible de las apps suspendidas, desalojo por presupuesto con la sesion ya guardada, alivio automatico
// con su aviso y "Optimizar Flex OS" (cinco etapas, modal, modo visual eficiente temporal).
#include <stdio.h>
#include <string.h>
#include "FlexOS_Mem.h"
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void flex_app_test_set_ops(int id, const flex_app_ops_t *ops);
void flex_mem_sim_set(const FlexMemSnap *s);
int flex_optimize_stage(void);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("memoria: FALLO linea %d: %s\n", __LINE__, #c);   \
            ok = false;                                              \
        }                                                            \
    } while (0)
#define MB(x) ((uint32_t)((x) * 1024u * 1024u))

static FlexMemSnap g_snap;
static int g_shed_calls, g_closed;
static int g_done_cb;
static void set_free(uint32_t free_b, uint32_t largest)
{
    g_snap.psFree = free_b;
    g_snap.psLargest = largest;
    flex_mem_sim_set(&g_snap);
}
static void t_create(lv_obj_t *root, const lv_area_t *area) { (void)root; (void)area; }
static void t_close(void)
{
    g_closed++;
    set_free(g_snap.psFree + MB(3), g_snap.psLargest + MB(2));   // cerrar la app devuelve su memoria
}
static size_t t_shed(void)
{
    g_shed_calls++;
    set_free(g_snap.psFree + MB(1), g_snap.psLargest + MB(1));
    return 1;
}
static const flex_app_ops_t k_ops = {.on_create = t_create, .on_close = t_close, .shed = t_shed};
static void on_done(void) { g_done_cb++; }

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
}

bool scene_memory_run(void)
{
    bool ok = true;
    flex_lock_clear();
    if (!flex_shell_screen()) {
        flex_i18n_init();
        flex_theme_init();
        flex_wallmgr_init();
        flex_shell_start();
    }
    to_home();
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    flex_theme_set_eff_mode(false);
    memset(&g_snap, 0, sizeof(g_snap));
    g_snap.psTotal = MB(32);
    g_snap.inTotal = 400 * 1024;
    g_snap.inFree = 180 * 1024;
    g_snap.inMin = 120 * 1024;
    g_snap.fsTotal = MB(8);
    g_snap.fsUsed = MB(2);
    g_snap.fsValid = 1;
    set_free(MB(14), MB(12));
    static const int apps[3] = {IC_NOTAS, IC_PAINT, IC_CALC};
    for (int i = 0; i < 3; i++) flex_app_test_set_ops(apps[i], &k_ops);

    // 1) con holgura se abre todo; la huella medida de una app sale de la PSRAM que desaparece al crearla
    flex_app_open(IC_GALERIA, NULL);   // pesada
    sim_run(500);
    CHK(flex_app_is_open(IC_GALERIA));
    flex_sys_home();
    sim_run(400);

    // 2) con poca PSRAM (4 MB libres: critica) una app CERRADA pesada no se abre y se avisa; la ligera si
    flex_app_terminate_ex(IC_GALERIA, true);
    int n0 = flex_notif_count();
    set_free(MB(4), MB(3));
    flex_app_open(IC_MULTIMEDIA, NULL);   // pesada
    CHK(!flex_app_is_open(IC_MULTIMEDIA));
    sim_run(60);                                                                   // el aviso pasa por el buzon de la UI
    CHK(strstr(flex_notif_latest_title(), "Multimedia no se abre ahora") != NULL);
    (void)n0;
    sim_run(440);
    flex_app_open(IC_NOTAS, NULL);        // ligera: siempre
    sim_run(500);
    CHK(flex_app_is_open(IC_NOTAS));
    flex_sys_home();
    sim_run(300);

    // 3) volver a una app suspendida nunca se bloquea, aunque la memoria este critica
    flex_app_open(IC_NOTAS, NULL);
    sim_run(500);
    CHK(flex_app_current() == IC_NOTAS);
    flex_sys_home();
    sim_run(300);

    // 4) desalojo por presupuesto: critica al suspender -> se suelta lo de las suspendidas y, si sigue, se
    //    cierra la MENOS reciente (su sesion ya esta guardada: estas apps no tienen sesion)
    flex_app_terminate_ex(IC_NOTAS, true);
    set_free(MB(14), MB(12));
    flex_app_open(IC_PAINT, NULL);
    sim_run(400);
    flex_sys_home();
    sim_run(300);
    flex_app_open(IC_CALC, NULL);
    sim_run(400);
    flex_app_open(IC_NOTAS, NULL);        // Calculadora pasa a suspendida
    sim_run(400);
    flex_sys_home();
    sim_run(300);
    CHK(flex_app_is_open(IC_PAINT) && flex_app_is_open(IC_CALC) && flex_app_is_open(IC_NOTAS));
    int s0 = g_shed_calls, c0 = g_closed;
    set_free(MB(1), MB(1));               // critica: tras soltar (+3 MB) sigue por debajo de 5 MB
    flex_app_open(IC_CALC, NULL);         // vuelve a una suspendida (no se bloquea)...
    sim_run(400);
    flex_sys_home();                      // ...y al suspenderla se aplica el presupuesto
    sim_run(500);
    CHK(g_shed_calls > s0);               // primero se suelta lo reconstruible
    CHK(g_closed > c0);                   // y luego se cierra la suspendida menos reciente
    CHK(!flex_app_is_open(IC_PAINT));     // Paint era la menos reciente

    // 5) alivio automatico: al empeorar a WARN se suelta solo y, si sigue, UN aviso; con holgura, silencio
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    set_free(MB(14), MB(12));
    sim_run(2500);
    flex_notify("(marca)", "");
    sim_run(2500);
    CHK(strcmp(flex_notif_latest_title(), "(marca)") == 0);   // OK/NOTICE: silencio absoluto
    set_free(MB(5.5), MB(4));             // WARN
    sim_run(3500);
    CHK(strstr(flex_notif_latest_title(), "Memoria casi llena") != NULL);
    flex_notify("(marca 2)", "");
    sim_run(5000);
    CHK(strcmp(flex_notif_latest_title(), "(marca 2)") == 0);   // sin repetirse (el aviso solo sale al empeorar)

    // 6) Optimizar Flex OS: modal, cinco etapas a 420 ms, resultado y cierre con un toque
    set_free(MB(14), MB(12));
    sim_run(2500);
    flex_app_open(IC_PAINT, NULL);
    sim_run(400);
    flex_sys_home();
    sim_run(400);
    flex_app_open(IC_NOTAS, NULL);        // Paint suspendida, Notas delante (no se toca)
    sim_run(400);
    int s1 = g_shed_calls;
    flex_optimize_open(on_done);
    CHK(flex_optimize_active() && flex_optimize_stage() == 1);
    sim_shot("optimizar_1");
    flex_sys_home();                      // Optimizar es modal mientras trabaja: Inicio no hace nada
    sim_run(100);
    CHK(flex_optimize_active() && flex_app_current() == IC_NOTAS);
    sim_run(900);
    CHK(flex_optimize_stage() >= 2 && flex_optimize_stage() < 5);
    sim_run(1200);
    CHK(flex_optimize_stage() == 5);
    CHK(g_shed_calls == s1 + 1);          // solo la suspendida: la app de delante no se toca
    CHK(strcmp(flex_recents_state_text(IC_PAINT), "Estado guardado") == 0);   // se le solto lo pesado
    CHK(strcmp(flex_recents_state_text(IC_NOTAS), "Activa") == 0);
    {
        char c[24];
        flex_recents_cost_text(IC_PAINT, c, sizeof(c));
        CHK(strstr(c, "MB") != NULL || strstr(c, "KB") != NULL || strstr(c, "B") != NULL);   // huella medida
    }
    sim_shot("optimizar_fin");
    sim_tap(240, 545);                    // "Hecho"
    sim_run(300);
    CHK(!flex_optimize_active() && g_done_cb == 1 && flex_app_current() == IC_NOTAS);
    flex_sys_home();
    sim_run(400);

    // 7) con la presion alta tras soltar, Optimizar enciende el modo visual eficiente (temporal)
    set_free(MB(3), MB(2));
    flex_optimize_open(NULL);
    sim_run(2600);
    CHK(flex_optimize_stage() == 5 && flex_look()->eff_mode);
    sim_tap(60, 700);                     // un toque fuera del panel tambien cierra
    sim_run(300);
    CHK(!flex_optimize_active());
    set_free(MB(14), MB(12));             // la presion baja: el modo eficiente se apaga solo
    sim_run(9000);
    CHK(!flex_look()->eff_mode);

    for (int i = 0; i < 3; i++) flex_app_test_set_ops(apps[i], NULL);
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);
    flex_shell_lock();
    sim_run(200);
    printf("memoria de las apps: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
