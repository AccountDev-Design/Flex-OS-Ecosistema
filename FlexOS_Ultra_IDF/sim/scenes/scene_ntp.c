// Hora por red (N1a, 03b §12): el servicio REAL (modelo + servicio de flex_net) en el simulador con el transporte guionizado, y lo que ve el usuario:
// Ajustes › General (estado, ultima sincronizacion, "Sincronizar ahora") y el control "Hora" del panel rapido. Sin Wi-Fi el servicio dice "Esperando Wi-Fi"
// y no consulta nada; con red y la hora llega de un servidor, se aplica al reloj del sistema, se guarda y sobrevive a un reinicio como hora APROXIMADA.
#include <stdio.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_clock.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_ntp.h"
#include "flex_passcode.h"
#include "flex_qs_model.h"
#include "flex_set_model.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_time.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_net_online(bool on);
void flex_time_sim_unsync(void);
bool flex_net_system_ready(void);

int flex_app_settings_view(void);
int flex_app_settings_sel(void);
bool flex_app_settings_animating(void);
bool flex_app_settings_card_xy(int i, int *x, int *y);
bool flex_app_settings_row_xy(int i, int *x, int *y);
const flex_set_sys_t *flex_app_settings_sys(void);
bool flex_app_settings_shown(const char *txt);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("ntp: FALLO linea %d: %s\n", __LINE__, #c);       \
            ok = false;                                              \
        }                                                            \
    } while (0)

// ---- el transporte guionizado y el entorno del servicio ----
static struct {
    uint32_t reply[3];   // lo que contesta cada servidor (0 = silencio)
    int asked[3];
    int total;
    int wakes;
    uint32_t load_epoch, load_last;
    bool load_ok;
} N;
static uint32_t s_skew;   // saltos de reloj sin simular la interfaz: horas enteras de espera no se pueden correr cuadro a cuadro
static uint32_t n_millis(void) { return lv_tick_get() + s_skew; }
static uint32_t n_utc(void) { return flex_time_utc(); }
static void n_set(uint32_t u, bool net) { flex_time_set_utc(u, net); }
static bool n_synced(void) { return flex_time_synced(); }
static bool n_air(void) { return flex_cfg_get_bool("airpl", false); }
static bool n_ready(void) { return flex_net_system_ready(); }
static bool n_false(void) { return false; }
static bool n_true(void) { return true; }
static bool n_load(uint64_t *e, uint64_t *l)
{
    *e = N.load_epoch ? N.load_epoch : flex_kvs_get_u64("flexos_time", "epoch", 0);
    *l = N.load_epoch ? N.load_last : flex_kvs_get_u64("flexos_time", "lastsync", 0);
    return N.load_ok;
}
static bool n_save(uint64_t e, uint64_t l)
{
    return flex_kvs_set_u64("flexos_time", "epoch", e) == ESP_OK && flex_kvs_set_u64("flexos_time", "lastsync", l) == ESP_OK;
}
static uint32_t n_query(const char *h)
{
    for (int i = 0; i < 3; i++) {
        if (!strcmp(h, flex_ntp_servers[i])) {
            N.asked[i]++;
            N.total++;
            return N.reply[i];
        }
    }
    return 0;
}
static void n_wake(void) { N.wakes++; }
static void env_set(void)
{
    flex_ntp_env_t e = {.millis = n_millis, .utc = n_utc, .set_utc = n_set, .synced = n_synced, .airplane = n_air, .system_ready = n_ready, .safe_mode = n_false,
                        .nvs_ok = n_true, .load = n_load, .save = n_save, .query = n_query, .wake = n_wake};
    flex_ntp_set_env(&e);
}
// el trabajador de la placa, a mano: cada 50 ms de la simulacion
static void pump(uint32_t ms)
{
    uint32_t t0 = lv_tick_get();
    while (lv_tick_get() - t0 < ms) {   // sim_run avanza de 4 en 4 ms y se pasa un poco: se mide el reloj de verdad
        sim_run(48);
        flex_ntp_tick_service();
        if (flex_ntp_work_pending()) {
            flex_ntp_work();
        }
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
static void settings_general(void)   // abre Ajustes en General (con la pagina recien armada: los valores se leen al abrir)
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
static void row(int i)
{
    int x, y;
    if (!flex_app_settings_row_xy(i, &x, &y) || y > 700 || y < 140) {
        sim_drag(240, 600, 240, 250, 200);
        sim_run(100);
    }
    if (flex_app_settings_row_xy(i, &x, &y)) {
        sim_tap(x, y);
        sim_run(150);
    }
}
static bool has(const char *t) { return flex_app_settings_shown(t); }
static const char *state_text(void)
{
    static char b[96], l[96];
    bool busy;
    flex_ntp_texts(b, sizeof(b), l, sizeof(l), &busy);
    return b;
}

bool scene_ntp_run(void)
{
    bool ok = true;
    unlock_and_clean();
    memset(&N, 0, sizeof(N));
    s_skew = 0;
    flex_cfg_set_i32("autolockms", 0);   // "Nunca": la espera no bloquea el telefono
    N.load_ok = true;
    sim_net_online(false);
    flex_cfg_set_bool("airpl", false);
    flex_kvs_set_u64("flexos_time", "epoch", 0);
    flex_kvs_set_u64("flexos_time", "lastsync", 0);
    flex_time_set_utc(1791481500u, false);   // jue 8 oct 2026, 12:45 en Lima
    env_set();
    flex_ntp_service_reset();
    sim_run(100);

    // ---- 1) sin red: el servicio dice la verdad, no consulta nada y "Sincronizar ahora" no hace nada ----------------------------------------------
    settings_general();
    CHK(wait_view(1) && flex_app_settings_sel() == 0);
    CHK(flex_app_settings_sys()->ntp_available);
    CHK(has("Esperando Wi-Fi") && has("Nunca") && has("Consultar el servidor de hora"));
    sim_shot("ntp_sin_red");
    row(6);
    CHK(N.wakes == 0 && N.total == 0);
    pump(3000);
    CHK(N.total == 0);
    settings_close();

    // ---- 2) el sistema solo esta "listo" en el escritorio o en el bloqueo (la primera consulta sin enlace anunciado espera a eso); una hora recuperada o
    //         de fabrica no es hora de red ----------------------------------------------------------------------------------------------------------
    flex_app_open(IC_AJUSTES, NULL);
    sim_run(700);
    CHK(!flex_net_system_ready());
    settings_close();
    CHK(flex_net_system_ready());
    flex_shell_lock();   // el bloqueo tambien cuenta
    sim_run(300);
    CHK(flex_shell_state() == FLEX_SH_LOCK && flex_net_system_ready());
    sim_drag(240, 700, 240, 300, 300);
    sim_run(500);
    CHK(flex_shell_state() == FLEX_SH_HOME);
    flex_time_sim_unsync();
    flex_time_set_utc(1791481500u, false);
    CHK(!flex_time_synced() && !flex_clock_synced());   // la semilla / hora aproximada no es hora de red
    N.reply[1] = 1791490000u;
    sim_net_online(true);
    pump(1000);
    CHK(N.total == 0);   // aun no: el margen de 1,5 s tras confirmar el enlace
    pump(800);
    // ---- 2b) la primera consulta: el primer servidor no contesta y el segundo si; la hora se aplica, se guarda y queda "Sincronizado" ----------------------------
    N.reply[0] = 0;
    N.reply[2] = 1791499999u;
    pump(200);
    CHK(N.asked[0] == 1 && N.asked[1] == 1 && N.asked[2] == 0);   // en orden; gana el primero que contesta
    CHK(flex_time_utc() == 1791490000u && flex_time_synced() && flex_clock_synced());   // y ahora si es hora de red
    flex_time_sim_unsync();
    flex_clock_set_utc(1791490000u);   // un ajuste manual tambien lo es
    CHK(flex_clock_synced());
    CHK(flex_kvs_get_u64("flexos_time", "epoch", 0) == 1791490000u && flex_kvs_get_u64("flexos_time", "lastsync", 0) == 1791490000u);
    CHK(!strcmp(state_text(), "Sincronizado - UTC-5 (Lima)"));
    settings_general();
    CHK(wait_view(1));
    CHK(has("Sincronizado - UTC-5 (Lima)") && has("Hoy 15:06"));   // 1791490000 = 15:06 en Lima
    sim_shot("ntp_sincronizado");
    settings_close();
    // y la proxima, a las 6 h: ni antes ni (mucho) despues
    int t1 = N.total;
    s_skew += 5 * 3600 * 1000u;
    pump(100);
    CHK(N.total == t1);
    s_skew += 1 * 3600 * 1000u + 5000;
    pump(100);
    CHK(N.total == t1 + 2);   // otra consulta: otra vez el primero en silencio y el segundo
    CHK(flex_ntp_texts != NULL);

    // ---- 3) todos en silencio: espera progresiva (30 s, 60 s, 2 min...) y "Sin respuesta del servidor"; "Sincronizar ahora" salta la espera ------------------
    N.reply[0] = N.reply[1] = N.reply[2] = 0;
    settings_general();
    CHK(wait_view(1));
    row(6);   // "Sincronizar ahora"
    CHK(N.wakes >= 1);
    pump(200);
    CHK(!strcmp(state_text(), "Sin respuesta del servidor"));
    CHK(N.asked[0] >= 3 && N.asked[1] >= 3 && N.asked[2] >= 1);
    {
        flex_ntp_t m;
        flex_ntp_get(&m);
        CHK(m.state == FLEX_NTPS_FAIL && m.fails == 1);
        CHK(m.last_sync_utc != 0);   // la ultima buena se conserva
    }
    settings_close();
    int t2 = N.total;
    pump(29000);
    CHK(N.total == t2);              // 30 s tras el primer fallo
    pump(2500);
    CHK(N.total == t2 + 3);          // y a los 30 s pregunta de nuevo: los tres, en silencio
    {
        flex_ntp_t m;
        flex_ntp_get(&m);
        CHK(m.fails == 2);
    }
    pump(55000);
    CHK(N.total == t2 + 3);          // el segundo fallo espera 60 s
    pump(8000);
    CHK(N.total == t2 + 6);
    // el usuario salta la espera
    flex_ntp_request_sync(true);
    pump(200);
    CHK(N.total == t2 + 9);

    // ---- 4) el panel rapido: "Hora" esta y, tocado, pide la sincronizacion (no bloquea) ---------------------------------------------------------------
    {
        sim_drag(240, 10, 240, 520, 260);
        sim_run(400);
        uint8_t ids[32];
        int n = flex_qs_visible_ids(ids, 32);   // el panel carga su configuracion al abrirse por primera vez
        bool found = false;
        for (int i = 0; i < n; i++) {
            found = found || ids[i] == FLEX_QS_NTP;
        }
        CHK(found);
        lv_area_t a;
        CHK(flex_qs_ctl_rect(FLEX_QS_NTP, &a));
        sim_shot("ntp_panel");
        int before = N.wakes;
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(250);
        CHK(N.wakes == before + 1);
        flex_qs_close_now();
        sim_run(300);
    }

    // ---- 5) modo avion: ni primera consulta ni peticion; el texto lo dice; el control del panel desaparece ----------------------------------------------
    pump(200);
    flex_cfg_set_bool("airpl", true);
    int t3 = N.total, w3 = N.wakes;
    flex_ntp_request_sync(true);
    s_skew += 400000;
    pump(100);
    CHK(N.total == t3 && N.wakes == w3);
    CHK(!strcmp(state_text(), "Modo avi\xC3\xB3n activo"));
    {
        sim_drag(240, 10, 240, 520, 260);
        sim_run(400);
        lv_area_t a;
        CHK(!flex_qs_ctl_rect(FLEX_QS_NTP, &a));
        flex_qs_close_now();
        sim_run(300);
    }
    flex_cfg_set_bool("airpl", false);
    // sin enlace: "Sin conexion Wi-Fi"
    sim_net_online(false);
    CHK(!strcmp(state_text(), "Sin conexi\xC3\xB3n Wi-Fi"));
    int t4 = N.total;
    s_skew += 400000;
    pump(100);
    CHK(N.total == t4);
    sim_net_online(true);   // vuelve: otra vez a +1,5 s
    pump(1000);
    CHK(N.total == t4);
    pump(800);
    CHK(N.total > t4);

    // ---- 6) tras un reinicio: la hora de antes del apagado, APROXIMADA y dicho en Ajustes, hasta que se sincroniza ---------------------------------------
    sim_net_online(false);
    N.reply[1] = 1791600000u;
    flex_ntp_service_reset();
    flex_time_sim_unsync();
    flex_time_set_utc(1700000000u, false);   // la hora de un arranque en frio: antes del suelo, no valida
    flex_ntp_boot_load();
    CHK(flex_time_utc() == flex_kvs_get_u64("flexos_time", "epoch", 0) && flex_time_utc() >= FLEX_NTP_MIN_EPOCH);
    CHK(!flex_time_synced() && !flex_clock_synced());   // la hora recuperada de la NVS NO es hora de red
    CHK(!strcmp(state_text(), "Hora aproximada (sin sincronizar)"));
    settings_general();
    CHK(wait_view(1));
    CHK(has("Hora aproximada (sin sincronizar)"));
    sim_shot("ntp_aproximada");
    settings_close();
    sim_net_online(true);
    pump(2500);
    CHK(!strcmp(state_text(), "Sincronizado - UTC-5 (Lima)") && flex_time_utc() >= 1791600000u);

    // ---- 7) el espacio de la hora de la NVS no responde: el error, y el icono de la fila en rojo --------------------------------------------------------
    flex_ntp_service_reset();
    N.load_ok = false;
    flex_ntp_boot_load();
    CHK(!flex_ntp_time_nvs_ok() && !strcmp(state_text(), "Error: almacenamiento NVS no disponible"));
    settings_general();
    CHK(wait_view(1));
    CHK(!flex_app_settings_sys()->time_nvs_ok && has("Error: almacenamiento NVS no disponible"));
    sim_shot("ntp_error_nvs");
    settings_close();
    N.load_ok = true;
    flex_ntp_service_reset();
    sim_net_online(false);
    flex_ntp_set_env(NULL);
    return ok;
}
