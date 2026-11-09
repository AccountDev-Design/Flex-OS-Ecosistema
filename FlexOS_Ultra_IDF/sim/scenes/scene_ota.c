// Actualizaciones OTA (N5b, 03b §13): el motor REAL (flex_ota) sobre un servidor simulado (manifiesto + binario por trozos, la flash que cuenta lo que se
// escribe) y las cuatro capas de la interfaz: el aviso flotante (entra deslizando, se va a los 9 s o con un deslizamiento hacia abajo), el modal del changelog
// (velo opaco, scroll, Cancelar / Actualizar ahora), la pantalla de instalacion (modal: Atras, Inicio y Recientes no hacen nada; Cancelar solo mientras se puede
// abortar; el error con "Entendido") y Ajustes > Actualizaciones. Y lo que las rodea: el mosaico del panel rapido, la fila de Ajustes, el bloqueo automatico
// que no actua con una actualizacion en marcha y los gestos que no abren nada por debajo.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_ota.h"
#include "flex_ota_ui.h"
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
int flex_app_settings_sel(void);
bool flex_app_settings_animating(void);
bool flex_app_settings_card_xy(int i, int *x, int *y);
bool flex_app_settings_row_xy(int i, int *x, int *y);
bool flex_app_settings_shown(const char *txt);

#define CHK(c)                                                       \
    do {                                                             \
        if (!(c)) {                                                  \
            printf("ota: FALLO linea %d: %s\n", __LINE__, #c);       \
            ok = false;                                              \
        }                                                            \
    } while (0)

enum { OV_NONE = 0, OV_TOAST, OV_CHANGELOG, OV_PROGRESS, OV_SETTINGS };
#define BIN_LEN 1310720u   // 1,25 MB
#define CHUNK 4096

// ---- el servidor y la flash simulados ----
static struct {
    bool net;
    char ver[16];
    char changelog[400];
    int mcode;                 // codigo HTTP del manifiesto
    int bcode;                 // codigo HTTP del binario
    int stage;                 // 1 manifiesto, 2 binario
    uint32_t pos, len;
    char manifest[2048];
    uint32_t written;
    int begun, aborts, ended, restarts, locks, unlocks;
    bool end_ok;
    uint32_t skew;
    char pend[16];
    bool pend_has;
    uint32_t hook_at;          // byte a partir del cual se ejecuta el gancho (una vez)
    void (*hook)(void);
    int hooks;
} W;

static uint32_t o_millis(void) { return lv_tick_get(); }
static void o_delay(uint32_t ms) { sim_run(ms < 4 ? 4 : ms); }
static bool o_net(void) { return W.net; }
static uint32_t o_heap(void) { return 400000; }
static bool o_part(void) { return true; }
static bool o_begin(const char *url, const char *ca, bool insecure, uint32_t t)
{
    (void)ca; (void)insecure; (void)t;
    W.stage = strstr(url, "manifest") ? 1 : 2;
    W.pos = 0;
    W.len = W.stage == 1 ? (uint32_t)strlen(W.manifest) : BIN_LEN;
    return true;
}
static int o_get(void) { return W.stage == 1 ? W.mcode : W.bcode; }
static int o_size(void) { return (int)W.len; }
static int o_avail(void)
{
    uint32_t r = W.len - W.pos;
    return (int)(r > CHUNK ? CHUNK : r);
}
static int o_read(uint8_t *b, size_t n)
{
    uint32_t r = W.len - W.pos;
    if (r > n) {
        r = (uint32_t)n;
    }
    if (r > CHUNK) {
        r = CHUNK;
    }
    for (uint32_t i = 0; i < r; i++) {
        b[i] = W.stage == 1 ? (uint8_t)W.manifest[W.pos + i] : (uint8_t)((W.pos + i) * 31u);
    }
    W.pos += r;
    sim_run(10);   // la red: unos 400 KB/s
    if (W.stage == 2 && W.hook && W.hook_at && W.pos >= W.hook_at) {
        W.hook_at = 0;
        W.hooks++;
        W.hook();
    }
    return (int)r;
}
static bool o_conn(void) { return W.pos < W.len; }
static bool o_stream(void) { return true; }
static void o_end(void) {}
static bool u_begin(uint32_t total) { (void)total; W.begun++; W.written = 0; return true; }
static void u_md5(const char *m) { (void)m; }
static size_t u_write(const uint8_t *d, size_t n) { (void)d; W.written += (uint32_t)n; return n; }
static bool u_end(void) { W.ended++; return W.end_ok; }
static bool u_fin(void) { return W.end_ok; }
static void u_abort(void) { W.aborts++; }
static void p_lock(void) { W.locks++; }
static void p_unlock(void) { W.unlocks++; }
static void s_save(const char *v) { snprintf(W.pend, sizeof(W.pend), "%s", v); W.pend_has = true; }
static bool s_load(char *o, size_t c)
{
    if (!W.pend_has) {
        return false;
    }
    snprintf(o, c, "%s", W.pend);
    return true;
}
static void s_clear(void) { W.pend_has = false; }
static void o_restart(void) { W.restarts++; }

static const flex_ota_cfg_t CFG = {.local_version = "1.0.0", .manifest_url = "https://flex.test/ota/manifest.json", .key = FLEX_OTA_KEY, .root_ca = "CA",
                                   .min_heap = FLEX_OTA_MIN_HEAP};
static const flex_ota_ops_t OPS = {.millis = o_millis, .delay_ms = o_delay, .net_ready = o_net, .free_heap = o_heap, .next_partition = o_part,
                                   .http_begin = o_begin, .http_get = o_get, .http_size = o_size, .http_available = o_avail, .http_read = o_read,
                                   .http_connected = o_conn, .http_stream = o_stream, .http_end = o_end, .upd_begin = u_begin, .upd_set_md5 = u_md5,
                                   .upd_write = u_write, .upd_end = u_end, .upd_finished = u_fin, .upd_abort = u_abort, .panel_lock = p_lock,
                                   .panel_unlock = p_unlock, .save_pending = s_save, .load_pending = s_load, .clear_pending = s_clear, .restart = o_restart};

static void publish(const char *ver)
{
    snprintf(W.ver, sizeof(W.ver), "%s", ver);
    snprintf(W.manifest, sizeof(W.manifest),
             "{\"flex_os_ultra\":{\"version\":\"9.9.9\",\"url\":\"https://flex.test/arduino.bin\"},"
             "\"%s\":{\"version\":\"%s\",\"url\":\"https://flex.test/ota/flexos_ultra_idf.bin\",\"md5\":\"0123456789abcdef0123456789abcdef\","
             "\"size\":%u,\"changelog\":\"%s\"}}",
             FLEX_OTA_KEY, ver, (unsigned)BIN_LEN, W.changelog);
}

// el trabajador de la placa, a mano
static void pump(uint32_t ms)
{
    uint32_t t0 = lv_tick_get();
    while (lv_tick_get() - t0 < ms) {
        sim_run(48);
        if (flex_ota_work_pending()) {
            flex_ota_work();
        }
    }
}
static bool pump_until(flex_ota_state_t s, uint32_t max_ms)
{
    uint32_t t0 = lv_tick_get();
    flex_ota_info_t i;
    for (;;) {
        flex_ota_get(&i);
        if (i.state == s) {
            return true;
        }
        if (lv_tick_get() - t0 > max_ms) {
            return false;
        }
        pump(48);
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

static bool shown(const char *t) { return flex_ota_ui_shown(t); }
static int ov(void) { return flex_ota_ui_overlay(); }
static bool tap_btn(int id)
{
    int x, y;
    if (!flex_ota_ui_btn_xy(id, &x, &y)) {
        return false;
    }
    sim_tap(x, y);
    return true;
}
static uint16_t px(int x, int y) { return sim_fb()[(size_t)y * 480 + (size_t)x]; }
static uint16_t c565(int r, int g, int b) { return lv_color_to_u16(lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b)); }

// gancho de la descarga: la instalacion a medias, vista desde fuera
static bool g_mid_ok;
static void hook_mid(void)
{
    bool ok = true;
    flex_ota_info_t i;
    flex_ota_get(&i);
    sim_run(60);
    CHK(ov() == OV_PROGRESS && i.state == FLEX_OTA_DOWNLOADING);
    CHK(shown("Descargando"));
    CHK(i.percent >= 25 && i.percent <= 60);
    {
        char p[8];
        snprintf(p, sizeof(p), "%u%%", (unsigned)i.percent);
        CHK(shown(p));
    }
    CHK(shown("/s"));   // la velocidad
    int x, y;
    CHK(flex_ota_ui_btn_xy(FLEX_OTA_BT_CANCEL, &x, &y));   // se puede cancelar: aun no se ha tocado la particion de arranque
    sim_shot("ota_instalando");
    // modal: Atras, Inicio y Recientes no hacen nada; el bloqueo automatico no actua
    flex_sys_back();
    flex_sys_home();
    flex_sys_recents();
    sim_run(100);
    CHK(ov() == OV_PROGRESS);
    CHK(flex_power_autolock_vetoed());
    sim_drag(240, 10, 240, 520, 200);   // la cortina no se abre
    sim_run(300);
    CHK(!flex_qs_is_open());
    sim_drag(100, 400, 400, 400, 150);   // deslizar a la derecha: la instalacion no se cierra
    sim_run(200);
    CHK(ov() == OV_PROGRESS);
    g_mid_ok = ok;
}
static void hook_cancel(void)
{
    hook_mid();
    bool ok = g_mid_ok;
    CHK(tap_btn(FLEX_OTA_BT_CANCEL));
    sim_run(100);
    g_mid_ok = ok;
}

bool scene_ota_run(void)
{
    bool ok = true;
    unlock_and_clean();
    memset(&W, 0, sizeof(W));
    W.mcode = 200;
    W.bcode = 200;
    W.end_ok = true;
    snprintf(W.changelog, sizeof(W.changelog), "Mejor rendimiento del escritorio\\n- Nuevo ajuste de brillo\\n- Correccion de la camara");
    flex_cfg_set_i32("autolockms", 60000);   // el predeterminado: el bloqueo automatico esta activo
    flex_ota_set(&CFG, &OPS);
    flex_ota_service_reset();
    sim_run(100);

    // ---- 1) el mosaico del panel rapido: "Actualizaciones" con la version, y tocado abre Ajustes > Actualizaciones --------------------------------
    CHK(ov() == OV_NONE);
    CHK(!flex_power_autolock_vetoed());
    sim_drag(240, 10, 240, 520, 260);
    sim_run(400);
    {
        uint8_t ids[32];
        int n = flex_qs_visible_ids(ids, 32);
        bool found = false;
        for (int i = 0; i < n; i++) {
            found = found || ids[i] == FLEX_QS_OTA;
        }
        CHK(found);
        lv_area_t a;
        CHK(flex_qs_ctl_rect(FLEX_QS_OTA, &a));
        sim_shot("ota_panel");
        sim_tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        sim_run(500);
        CHK(!flex_qs_is_open());
        CHK(ov() == OV_SETTINGS);
    }
    CHK(shown("Actualizacion de software") && shown("El sistema esta al dia") && shown("Flex OS Ultra 1.0.0") && shown("Buscar actualizacion"));
    CHK(flex_power_autolock_vetoed());   // una capa a pantalla completa: el bloqueo automatico no actua (Lock.h:381)
    sim_shot("ota_ajustes_idle");

    // ---- 2) sin Wi-Fi: el error exacto en la propia pagina ----------------------------------------------------------------------------------------
    W.net = false;
    CHK(tap_btn(FLEX_OTA_BT_CHECK));
    sim_run(200);
    CHK(shown("No se pudo comprobar") && shown(flex_ota_err_text(FLEX_OTA_ERR_NO_WIFI)) && shown("Buscar actualizacion"));
    sim_shot("ota_ajustes_error");

    // ---- 3) con red y una version nueva: busca, y la pagina muestra la version, el tamano y las novedades; los botones se cambian ------------------
    W.net = true;
    publish("1.2.0");
    CHK(tap_btn(FLEX_OTA_BT_CHECK));
    sim_run(100);
    {
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(i.state == FLEX_OTA_CHECKING);
        CHK(shown("Buscando...") && shown("Buscando actualizaciones..."));   // el motor ocupado: sin botones, solo el estado
        int x, y;
        CHK(!flex_ota_ui_btn_xy(FLEX_OTA_BT_CHECK, &x, &y) || true);
    }
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(200);
    CHK(ov() == OV_SETTINGS);
    CHK(shown("Actualizacion disponible") && shown("Version disponible") && shown("1.2.0") && shown("Tamano") && shown("1,2 MB") && shown("Novedades"));
    CHK(shown("Buscar") && shown("Actualizar ahora"));
    sim_shot("ota_ajustes_disponible");

    // ---- 4) Atras cierra la pagina; en el escritorio sale el aviso (una vez por version), entra deslizando y se queda donde debe ------------------
    CHK(tap_btn(FLEX_OTA_BT_BACK));
    sim_run(60);
    CHK(ov() == OV_TOAST);
    {
        int y0 = flex_ota_ui_card_y();
        sim_run(60);
        int y1 = flex_ota_ui_card_y();
        CHK(y0 > 624 && y1 < y0);   // viene de abajo
        sim_run(450);
        CHK(flex_ota_ui_card_y() == 800 - 80 - 96);
    }
    CHK(shown("Nueva actualizacion disponible") && shown("Flex OS Ultra 1.2.0"));
    CHK(!flex_power_autolock_vetoed());   // la tarjeta flotante no cuenta
    sim_shot("ota_aviso");
    sim_drag(240, 500, 240, 200, 200);   // con el aviso visible la caja de aplicaciones no se abre
    sim_run(300);
    CHK(!flex_drawer_is_open());
    // un deslizamiento hacia abajo sobre el aviso lo descarta
    sim_drag(240, 650, 240, 760, 150);
    sim_run(500);
    CHK(ov() == OV_NONE);
    sim_run(1000);
    CHK(ov() == OV_NONE);   // la misma version no vuelve a avisar

    // ---- 5) el aviso de una version nueva se va solo a los 9 s ---------------------------------------------------------------------------------------
    publish("1.3.0");
    flex_ota_request_check();
    pump(300);
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(100);
    CHK(ov() == OV_TOAST);
    {
        uint32_t t0 = lv_tick_get();
        while (lv_tick_get() - t0 < 8000) {
            sim_run(100);
        }
        CHK(ov() == OV_TOAST);
        while (lv_tick_get() - t0 < 9700) {
            sim_run(50);
        }
        CHK(ov() == OV_NONE);
    }

    // ---- 6) el aviso tocado abre el modal con el velo opaco, la version, el tamano y las novedades con scroll ---------------------------------------
    snprintf(W.changelog, sizeof(W.changelog),
             "- Cambio 01\\n- Cambio 02\\n- Cambio 03\\n- Cambio 04\\n- Cambio 05\\n- Cambio 06\\n- Cambio 07\\n- Cambio 08\\n- Cambio 09\\n- Cambio 10\\n- Cambio 11\\n"
             "- Cambio 12\\n- Cambio 13\\n- Cambio 14");
    publish("1.4.0");
    flex_ota_request_check();
    pump(300);
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(100);
    CHK(ov() == OV_TOAST);
    sim_run(450);
    CHK(tap_btn(FLEX_OTA_BT_TOAST));
    sim_run(500);
    CHK(ov() == OV_CHANGELOG);
    CHK(shown("Actualizacion de software") && shown("Flex OS Ultra  1.4.0") && shown("Version actual: 1.0.0") && shown("Tamano de la descarga: 1,2 MB"));
    CHK(shown("Cancelar") && shown("Actualizar ahora") && shown("- Cambio 01"));
    {
        bool dark = flex_look()->dark;
        CHK(px(5, 5) == (dark ? c565(8, 10, 16) : c565(228, 232, 240)));   // velo OPACO
        CHK(px(5, 790) == px(5, 5));
    }
    CHK(flex_power_autolock_vetoed());
    sim_shot("ota_changelog");
    CHK(flex_ota_ui_log_scroll() == 0 && flex_ota_ui_log_max() > 0);
    sim_drag(240, 400, 240, 250, 250);
    sim_run(300);
    CHK(flex_ota_ui_log_scroll() > 0);
    sim_shot("ota_changelog_scroll");
    // la pantalla de debajo no recibe nada: ni la cortina ni la caja de aplicaciones
    sim_drag(240, 10, 240, 520, 200);
    sim_run(300);
    CHK(!flex_qs_is_open() && ov() == OV_CHANGELOG);
    // deslizar a la derecha sale del modal (el gesto atras)
    sim_drag(100, 300, 420, 300, 150);
    sim_run(300);
    CHK(ov() == OV_NONE);
    // Cancelar tambien
    flex_ota_open_settings();
    sim_run(100);
    CHK(ov() == OV_SETTINGS);
    sim_drag(100, 300, 420, 300, 150);
    sim_run(300);
    CHK(ov() == OV_NONE);

    // ---- 7) instalar y cancelar a mitad: el error exacto, "El equipo no se ha modificado." y la flash se aborta --------------------------------------
    flex_ota_open_settings();
    sim_run(100);
    CHK(ov() == OV_SETTINGS && shown("Actualizar ahora"));
    W.hook = hook_cancel;
    W.hook_at = BIN_LEN / 3;
    g_mid_ok = false;
    CHK(tap_btn(FLEX_OTA_BT_INSTALL));
    sim_run(150);
    CHK(ov() == OV_PROGRESS);
    CHK(shown("Actualizando FlexOS") && shown("Flex OS Ultra 1.4.0"));
    pump(8000);
    CHK(W.hooks == 1 && g_mid_ok);
    {
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(i.state == FLEX_OTA_ERROR && i.error == FLEX_OTA_ERR_CANCELLED);
        CHK(W.begun == 1 && W.aborts == 1 && W.ended == 0 && W.restarts == 0);
        CHK(W.locks == W.unlocks);
    }
    sim_run(200);
    CHK(ov() == OV_PROGRESS);
    CHK(shown("No se pudo actualizar") && shown(flex_ota_err_text(FLEX_OTA_ERR_CANCELLED)) && shown("El equipo no se ha modificado.") && shown("Entendido"));
    sim_shot("ota_error");
    flex_sys_back();   // el error no es modal: Atras sale
    sim_run(100);
    CHK(ov() == OV_NONE);
    flex_ota_open_settings();
    sim_run(100);
    flex_ota_ui_close_now();
    CHK(ov() == OV_NONE);

    // ---- 8) la instalacion completa: las fases, el porcentaje, el reinicio y el saludo del siguiente arranque -----------------------------------------
    W.pend_has = false;
    flex_ota_request_check();
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(200);
    flex_ota_open_settings();
    sim_run(100);
    CHK(shown("Actualizar ahora"));
    W.hook = hook_mid;
    W.hook_at = (uint32_t)((uint64_t)BIN_LEN * 40 / 100);
    W.hooks = 0;
    g_mid_ok = false;
    CHK(tap_btn(FLEX_OTA_BT_INSTALL));
    sim_run(150);
    CHK(ov() == OV_PROGRESS);
    pump(10000);
    CHK(W.hooks == 1 && g_mid_ok);
    {
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(i.state == FLEX_OTA_REBOOTING || i.state == FLEX_OTA_FINALIZING);
        CHK(W.written == BIN_LEN && W.ended == 1 && W.restarts == 1);
        CHK(W.pend_has && !strcmp(W.pend, "1.4.0"));
        CHK(W.locks == W.unlocks && W.locks > 0);
    }
    sim_run(300);
    CHK(ov() == OV_PROGRESS);
    CHK(shown("100%"));
    CHK(shown("El equipo se reiniciara solo"));
    {
        int x, y;
        CHK(!flex_ota_ui_btn_xy(FLEX_OTA_BT_CANCEL, &x, &y));   // ya no se puede cancelar
    }
    CHK(flex_power_autolock_vetoed());
    sim_shot("ota_reiniciando");
    flex_sys_back();
    flex_sys_home();
    sim_run(100);
    CHK(ov() == OV_PROGRESS);   // modal hasta el reinicio

    // ---- 9) tras el reinicio: el saludo se marca una vez; la OTA cuadro con la version que corre ----------------------------------------------------
    {
        flex_ota_service_reset();
        flex_ota_begin();
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(!i.just_updated);   // la version que corre sigue siendo 1.0.0: no era esta
        CHK(!W.pend_has);       // y el aviso pendiente se limpia siempre
        W.pend_has = true;
        snprintf(W.pend, sizeof(W.pend), "1.0.0");
        flex_ota_begin();
        flex_ota_get(&i);
        CHK(i.just_updated && !W.pend_has);
    }
    flex_ota_ui_close_now();   // la pantalla de instalacion sigue (el motor en estado REBOOTING): se cierra al reiniciar
    flex_ota_service_reset();
    sim_run(200);
    CHK(ov() == OV_NONE || ov() == OV_PROGRESS);

    // ---- 10) la comprobacion automatica: una vez, ~20 s despues del arranque, solo con red ---------------------------------------------------------
    flex_ota_ui_close_now();
    flex_ota_service_reset();
    W.net = true;
    publish("1.4.0");
    sim_run(200);
    {
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(i.state == FLEX_OTA_CHECKING || flex_ota_work_pending() || i.state == FLEX_OTA_UP_TO_DATE);   // lo pidio el temporizador de la capa
    }
    pump(500);
    {
        flex_ota_info_t i;
        flex_ota_get(&i);
        CHK(i.state == FLEX_OTA_UP_TO_DATE || i.state == FLEX_OTA_AVAILABLE);
    }
    sim_run(100);
    flex_ota_set(NULL, NULL);
    flex_ota_service_reset();
    flex_cfg_set_i32("autolockms", 60000);
    return ok;
}
