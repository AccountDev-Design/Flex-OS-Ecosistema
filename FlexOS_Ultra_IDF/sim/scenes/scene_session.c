// Sesiones de app (01c §14): una app de prueba guarda su estado de interfaz con el guardado diferido
// (1200 ms, tope 30 s, nunca con el dedo apoyado), el archivo tiene el formato y la version de
// Arduino, se restaura la primera vez que se abre (y una sesion danada se ignora), y cerrar la app
// desde Recientes no la cierra si no se pudo guardar.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_session_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void flex_app_test_set_ops(int id, const flex_app_ops_t *ops);
void flex_session_test_preload(int id, const uint8_t *file, size_t len);
void flex_session_forget_loaded(int id);
void sim_fs_enable(bool on);
void sim_fs_defer(bool on);
void sim_fs_complete(bool fail);
void sim_fs_fail_writes(bool queue_fail, bool disk_fail);
int sim_fs_writes(void);
size_t sim_fs_get(const char *p, uint8_t *out, size_t cap);

#define CHK(c)                                                         \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("sesiones: FALLO linea %d: %s\n", __LINE__, #c);    \
            ok = false;                                                \
        }                                                              \
    } while (0)

// "Ajustes" de prueba: la sesion de Arduino son {int16 view, sel, scroll, listScroll} (8 B)
typedef struct { int16_t view, sel, scroll, list; } sess_t;
static sess_t g_state;
static int g_loads, g_creates, g_loaded_valid, g_saves1, g_saves2;
static void t_create(lv_obj_t *root, const lv_area_t *area)
{
    (void)root;
    (void)area;
    g_creates++;
}
static bool t_save(void *out, size_t cap, size_t *len)
{
    g_saves1++;
    if (cap < sizeof(g_state)) return false;
    memcpy(out, &g_state, sizeof(g_state));
    *len = sizeof(g_state);
    return true;
}
static void t_load(const void *data, size_t len)
{
    g_loads++;
    memset(&g_state, 0, sizeof(g_state));
    if (data && len == sizeof(g_state)) {
        memcpy(&g_state, data, len);
        g_loaded_valid++;
    }
}
static sess_t g_state2;
static bool t2_save(void *out, size_t cap, size_t *len)
{
    g_saves2++;
    memcpy(out, &g_state2, sizeof(g_state2));
    *len = sizeof(g_state2);
    return cap >= sizeof(g_state2);
}
static void t2_load(const void *data, size_t len) { (void)data; (void)len; memset(&g_state2, 0, sizeof(g_state2)); }
static const flex_app_ops_t k_ops = {.on_create = t_create, .save_session = t_save, .load_session = t_load};
static const flex_app_ops_t k_ops2 = {.on_create = t_create, .save_session = t2_save, .load_session = t2_load};

static void to_home(void)
{
    flex_shell_lock();
    sim_run(100);
    sim_drag(240, 700, 240, 300, 300);
    sim_run(400);
}

bool scene_session_run(void)
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
    for (int i = 0; i < FLEX_APP_N; i++) flex_app_terminate_ex(i, true);   // otras escenas pudieron dejar apps abiertas
    sim_fs_enable(true);
    flex_app_test_set_ops(IC_AJUSTES, &k_ops);
    flex_session_forget_loaded(IC_AJUSTES);   // otras escenas la abrieron antes
    const char *path = "/System/Sessions/ajustes.bin";

    // 1) sin sesion en disco: abre en estado seguro (load con NULL), una sola vez
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    CHK(g_creates == 1 && g_loads == 1 && g_loaded_valid == 0);
    flex_sys_home();
    sim_run(400);
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    CHK(g_loads == 1);   // appLoadSessionOnce

    // 2) cambios: se agrupan y se vuelcan 1200 ms despues de la ultima marca
    int w0 = sim_fs_writes();
    g_state = (sess_t){1, 3, 120, 40};
    flex_session_mark_dirty(IC_AJUSTES);
    sim_run(600);
    CHK(sim_fs_writes() == w0 && flex_session_pending(IC_AJUSTES));      // aun no
    g_state.scroll = 200;
    flex_session_mark_dirty(IC_AJUSTES);                                    // reinicia la espera
    sim_run(900);
    CHK(sim_fs_writes() == w0);
    sim_run(500);
    CHK(sim_fs_writes() == w0 + 1 && !flex_session_pending(IC_AJUSTES));
    uint8_t file[1100];
    size_t n = sim_fs_get(path, file, sizeof(file));
    sess_t back;
    CHK(n == FLEX_SESS_HDR_SIZE + sizeof(sess_t));
    CHK(flex_sess_unpack(file, n, 1, IC_AJUSTES, &back, sizeof(back)) == sizeof(back));
    CHK(back.view == 1 && back.sel == 3 && back.scroll == 200 && back.list == 40);
    // sin cambios no se reescribe (desgaste de la flash)
    flex_session_flush_now();
    flex_session_mark_dirty(IC_AJUSTES);
    flex_session_flush_now();
    sim_run(300);
    CHK(sim_fs_writes() == w0 + 2);   // la marca nueva si se escribe; despues, nada mas
    sim_run(3000);
    CHK(sim_fs_writes() == w0 + 2);

    // 3) con el dedo apoyado no se vuelca, aunque pase el tiempo
    g_state.sel = 5;
    flex_session_mark_dirty(IC_AJUSTES);
    sim_touch(240, 300, true);
    sim_run(2500);
    CHK(sim_fs_writes() == w0 + 2);
    sim_touch(240, 300, false);
    sim_run(400);
    CHK(sim_fs_writes() == w0 + 3);

    // 3b) un volcado que fallo se rearma al suspender la app (appSuspend) y se reintenta
    g_state.sel = 9;
    flex_session_mark_dirty(IC_AJUSTES);
    sim_fs_fail_writes(true, false);
    sim_run(1500);                                     // el intento falla: la marca sigue puesta
    CHK(flex_session_pending(IC_AJUSTES));
    int wf = sim_fs_writes();
    sim_fs_fail_writes(false, false);
    sim_run(3000);
    CHK(sim_fs_writes() == wf);                        // nadie lo reintenta con la app delante
    flex_sys_home();
    sim_run(450);                                      // armado y vencido: el siguiente tick lo vuelca
    CHK(sim_fs_writes() == wf + 1 && !flex_session_pending(IC_AJUSTES));

    // 4) Recientes: cerrar la app con el disco lleno (no se puede poner en cola) NO la cierra
    flex_sys_home();
    sim_run(500);
    g_state.sel = 7;
    flex_session_mark_dirty(IC_AJUSTES);
    sim_fs_fail_writes(true, false);
    flex_app_terminate(IC_AJUSTES);
    CHK(flex_app_is_open(IC_AJUSTES));        // sigue viva en RAM
    CHK(!flex_app_terminate_ex(IC_AJUSTES, false) && flex_app_is_open(IC_AJUSTES));
    CHK(flex_app_terminate_ex(IC_AJUSTES, true) && !flex_app_is_open(IC_AJUSTES));   // el restablecimiento fuerza
    sim_fs_fail_writes(false, false);

    // 5) cerrar con el disco bien guarda en el acto y la sesion se restaura al abrirla de nuevo
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    g_state = (sess_t){2, 4, 77, 12};
    flex_session_mark_dirty(IC_AJUSTES);
    int w1 = sim_fs_writes();
    flex_sys_home();
    sim_run(300);
    flex_app_terminate(IC_AJUSTES);
    CHK(sim_fs_writes() == w1 + 1 && !flex_app_is_open(IC_AJUSTES));
    // otra "placa": se relee el archivo del disco (arranque) y se aplica al abrir por primera vez
    n = sim_fs_get(path, file, sizeof(file));
    flex_session_forget_loaded(IC_AJUSTES);   // "primer arranque" de esta app: ahora llega lo leido del disco
    flex_session_test_preload(IC_AJUSTES, file, n);
    int loads = g_loads, valid = g_loaded_valid;
    memset(&g_state, 0, sizeof(g_state));
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    CHK(g_loads == loads + 1 && g_loaded_valid == valid + 1);
    CHK(g_state.view == 2 && g_state.sel == 4 && g_state.scroll == 77 && g_state.list == 12);   // restaurada
    flex_sys_home();
    sim_run(300);

    // 5b) lo leido del disco que llega CUANDO la app ya abrio sin ello no se retiene: la proxima "primera apertura"
    //     no recibe un dato viejo
    flex_app_terminate_ex(IC_AJUSTES, true);
    flex_session_test_preload(IC_AJUSTES, file, n);     // llega tarde (la app sigue marcada como cargada)
    flex_session_forget_loaded(IC_AJUSTES);
    valid = g_loaded_valid;
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(400);
    CHK(g_loaded_valid == valid);                       // se abre en estado seguro (load con NULL)
    flex_sys_home();
    sim_run(200);

    // 6) una sesion danada o de otra version se ignora (no se repara): la app abre en estado seguro
    flex_app_terminate_ex(IC_AJUSTES, true);
    file[n - 1] ^= 0x55;                                  // CRC no cuadra
    // (la carga perezosa ya ocurrio: se prueba la validacion directamente con el modelo)
    CHK(flex_sess_unpack(file, n, 1, IC_AJUSTES, &back, sizeof(back)) == 0);
    CHK(flex_sess_unpack(file, n, 2, IC_AJUSTES, &back, sizeof(back)) == 0);   // otra version
    (void)valid;

    // 7) Recientes: cerrar una tarjeta con cambios sin guardar y el disco lleno NO la cierra; con disco, si
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(500);
    g_state.sel = 11;
    sim_fs_fail_writes(true, false);                    // el disco no acepta la escritura
    flex_session_mark_dirty(IC_AJUSTES);
    flex_sys_home();
    sim_run(300);
    CHK(flex_session_pending(IC_AJUSTES));              // el volcado fallo: sigue desfasada
    flex_recents_open();
    sim_run(400);
    CHK(flex_recents_count() == 1 && flex_recents_app_at(0) == IC_AJUSTES);
    sim_drag(240, 400, 240, 260, 250);                 // deslizar la tarjeta hacia arriba: cerrar
    sim_run(500);
    CHK(flex_app_is_open(IC_AJUSTES) && flex_recents_count() == 1);   // "Sin espacio para guardar"
    sim_fs_fail_writes(false, false);
    int w7 = sim_fs_writes();
    sim_drag(240, 400, 240, 260, 250);
    sim_run(500);
    CHK(!flex_app_is_open(IC_AJUSTES) && flex_recents_count() == 0 && sim_fs_writes() == w7 + 1);
    flex_recents_close_now();
    sim_run(300);

    // 8) el disco falla DESPUES de encolar mientras otra app tiene cambios pendientes: el reintento guarda ya a la
    //    otra con el gancho real (antes saltaba a un puntero nulo), y una app cerrada no se reintenta
    flex_app_test_set_ops(IC_NOTAS, &k_ops2);
    flex_session_forget_loaded(IC_NOTAS);
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(400);
    flex_sys_home();
    sim_run(200);
    flex_app_open(IC_NOTAS, NULL);
    sim_run(400);
    flex_sys_home();
    sim_run(200);
    sim_fs_fail_writes(true, false);                    // Ajustes: la escritura ni se encola (su marca se queda)
    g_state.sel = 3;
    flex_session_mark_dirty(IC_AJUSTES);
    sim_run(300);
    CHK(flex_session_pending(IC_AJUSTES));
    g_state2.sel = 5;
    flex_session_mark_dirty(IC_NOTAS);                  // la unica app "sucia" ahora es Notas
    sim_fs_fail_writes(false, true);                    // se encola bien y el disco falla despues
    int sv1 = g_saves1, sv2 = g_saves2;
    flex_app_terminate_ex(IC_AJUSTES, true);            // sin el arreglo: salto a NULL
    CHK(g_saves2 > sv2);                                // Notas se guardo ya, con su gancho
    sim_run(300);
    int sv1b = g_saves1;
    CHK(sv1b >= sv1 + 1);
    sim_run(8000);
    CHK(g_saves1 == sv1b);                              // cerrada: nadie la reintenta
    sim_fs_fail_writes(false, false);
    flex_app_terminate_ex(IC_NOTAS, true);
    flex_app_test_set_ops(IC_NOTAS, NULL);

    // 9) el fallo llega con la app ya CERRADA: se registra y no se reintenta (no hay interfaz de la que leer)
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(400);
    g_state.sel = 6;
    flex_session_mark_dirty(IC_AJUSTES);
    flex_session_flush_now();                           // una escritura buena: la cuenta de reintentos vuelve a 0
    g_state.sel = 7;
    flex_session_mark_dirty(IC_AJUSTES);
    sim_fs_defer(true);
    flex_session_flush_now();                           // en cola, sin resultado todavia
    flex_sys_home();
    sim_run(100);
    flex_app_terminate_ex(IC_AJUSTES, true);            // la app se cierra con la escritura sin confirmar
    CHK(!flex_app_is_open(IC_AJUSTES));
    int sv9 = g_saves1;
    sim_fs_complete(true);                              // ahora el disco dice que fallo
    sim_run(8000);
    CHK(g_saves1 == sv9);                               // nadie vuelve a llamar a su save_session
    sim_fs_defer(false);

    flex_app_test_set_ops(IC_AJUSTES, NULL);
    sim_fs_enable(false);
    flex_shell_lock();
    sim_run(200);
    printf("sesiones de app: %s\n", ok ? "estados correctos" : "ESTADO INESPERADO");
    return ok;
}
