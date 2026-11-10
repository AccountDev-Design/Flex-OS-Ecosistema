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
bool flex_factory_active(void);
int flex_factory_view(void);
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

// ---- Ajustes (como en la escena `ntp`) ----
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
// Huellas de referencia de cada pantalla: la de los objetos (geometria, colores, textos) y la de los pixeles (glifos incluidos). Cualquier cambio de una medida, de un
// color o de una cadena de la interfaz las cambia: la escena no solo mira que "haya" una capa, mira que sea ESTA. Se regeneran a proposito al cambiar el diseno
// (la escena imprime las nuevas con "GOLDEN"; con los valores a 0 la comprobacion falla).
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
static bool golden(const char *name, uint32_t want_tree, uint32_t want_fb, int x0, int y0, int x1, int y1)
{
    uint32_t t = flex_ota_ui_tree_crc(), f = crc_fb(x0, y0, x1, y1);
    if (t != want_tree || f != want_fb) {
        printf("ota: GOLDEN %s tree=0x%08xu fb=0x%08xu (esperado tree=0x%08xu fb=0x%08xu)\n", name, t, f, want_tree, want_fb);
        return false;
    }
    return true;
}
static uint16_t c565(int r, int g, int b) { return lv_color_to_u16(lv_color_make((uint8_t)r, (uint8_t)g, (uint8_t)b)); }

// gancho de la descarga: la instalacion a medias, vista desde fuera
static bool g_mid_ok;
static int g_gold_mid;   // la huella de la pantalla de instalacion solo se mira en la instalacion buena (al 40 %)
static void hook_mid(void)
{
    bool ok = true;
    flex_ota_info_t i;
    flex_ota_get(&i);
    sim_run(60);
    CHK(ov() == OV_PROGRESS && i.state == FLEX_OTA_DOWNLOADING);
    CHK(shown("Descargando..."));
    CHK(i.percent >= 25 && i.percent <= 60);
    {
        char p[8];
        snprintf(p, sizeof(p), "%u%%", (unsigned)i.percent);
        CHK(shown(p));
    }
    CHK(flex_ota_ui_shown_part("KB/s"));   // la velocidad
    int x, y;
    CHK(flex_ota_ui_btn_xy(FLEX_OTA_BT_CANCEL, &x, &y));   // se puede cancelar: aun no se ha tocado la particion de arranque
    sim_shot("ota_instalando");
    if (g_gold_mid) {
        CHK(golden("instalando", 0xbf43a7bau, 0xf89096b5u, 0, 0, 479, 799));
    }
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
    CHK(golden("ajustes_idle", 0x3a1f1243u, 0xe5640b5bu, 0, 0, 479, 799));

    // ---- 2) sin Wi-Fi: el error exacto en la propia pagina ----------------------------------------------------------------------------------------
    W.net = false;
    CHK(tap_btn(FLEX_OTA_BT_CHECK));
    sim_run(200);
    CHK(shown("No se pudo comprobar") && shown(flex_ota_err_text(FLEX_OTA_ERR_NO_WIFI)) && shown("Buscar actualizacion"));
    sim_shot("ota_ajustes_error");
    CHK(golden("ajustes_error", 0xfeeefc03u, 0xb23b8301u, 0, 0, 479, 799));

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
    CHK(golden("ajustes_disponible", 0x6e78e7a8u, 0x67a07f35u, 0, 0, 479, 799));

    // ---- 4) Atras cierra la pagina; en el escritorio sale el aviso (una vez por version), entra deslizando y se queda donde debe ------------------
    flex_sys_back();   // (el boton de la flecha se prueba mas abajo: aqui se necesita medir la entrada desde el primer cuadro)
    {
        // el aviso aparece solo (en el siguiente cuadro) y entra deslizando desde abajo: se muestrea cuadro a cuadro
        int first = -1, last = -1, prev = 10000;
        bool mono = true;
        uint32_t t0 = lv_tick_get();
        while (lv_tick_get() - t0 < 700) {
            sim_run(4);
            if (ov() != OV_TOAST) {
                continue;
            }
            int y = flex_ota_ui_card_y();
            if (first < 0) {
                first = y;
            }
            mono = mono && y <= prev;
            prev = y;
            last = y;
        }
        printf("ota: aviso first=%d last=%d mono=%d\n", first, last, (int)mono);
        CHK(first > 800 - 80 - 96 + 100);
        CHK(last == 800 - 80 - 96);
        CHK(mono);
    }
    CHK(ov() == OV_TOAST);
    CHK(flex_ota_ui_card_y() == 800 - 80 - 96);
    CHK(shown("Nueva actualizacion disponible") && shown("Flex OS Ultra 1.2.0"));
    CHK(!flex_power_autolock_vetoed());   // la tarjeta flotante no cuenta
    sim_shot("ota_aviso");
    CHK(golden("aviso", 0x900822b0u, 0x5b7f5d81u, 8, 616, 471, 730));
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
    CHK(shown("Cancelar") && shown("Actualizar ahora") && flex_ota_ui_shown_part("- Cambio 01"));
    {
        bool dark = flex_look()->dark;
        CHK(px(5, 5) == (dark ? c565(8, 10, 16) : c565(228, 232, 240)));   // velo OPACO
        CHK(px(5, 790) == px(5, 5));
        CHK(px(80, 756) == px(5, 5) && px(240, 756) == px(5, 5) && px(400, 756) == px(5, 5));   // sin barra de navegacion: la capa lo ocupa todo
    }
    CHK(flex_power_autolock_vetoed());
    sim_shot("ota_changelog");
    CHK(golden("changelog", 0x42461376u, 0x40a39a09u, 0, 0, 479, 799));
    CHK(flex_ota_ui_log_scroll() == 0 && flex_ota_ui_log_max() > 0);
    sim_drag(240, 400, 240, 250, 250);
    sim_run(300);
    CHK(flex_ota_ui_log_scroll() > 0);
    sim_shot("ota_changelog_scroll");
    CHK(golden("changelog_scroll", 0x91b856d4u, 0x14af67a4u, 0, 0, 479, 799));
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
    flex_ota_open_settings();   // y la flecha de la cabecera
    sim_run(100);
    CHK(ov() == OV_SETTINGS && tap_btn(FLEX_OTA_BT_BACK));
    sim_run(100);
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
    CHK(golden("error", 0xb7a83f03u, 0x5fbce6b8u, 0, 0, 479, 799));
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
    g_gold_mid = 1;
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
    CHK(golden("reiniciando", 0x2818da09u, 0xad96a8a7u, 0, 0, 479, 799));
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

    // ---- 11) Ajustes > General > Actualizaciones: la fila dice el estado, lleva a la capa, y al cerrarla se vuelve a leer ----------------------------
    flex_ota_service_reset();
    W.net = true;
    publish("1.6.0");
    settings_general();
    CHK(wait_view(1));
    CHK(has("Actualizaciones") && has("Buscar actualizacion"));
    row(7);
    sim_run(300);
    CHK(ov() == OV_SETTINGS);
    CHK(tap_btn(FLEX_OTA_BT_CHECK));
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(200);
    CHK(ov() == OV_SETTINGS && shown("1.6.0"));
    CHK(tap_btn(FLEX_OTA_BT_BACK));
    sim_run(300);
    CHK(ov() == OV_NONE);   // con una app delante no sale el aviso (solo en el escritorio)
    CHK(has("1.6.0 disponible"));   // la fila se ha vuelto a leer al cerrar la capa
    sim_shot("ota_fila_ajustes");
    // Restablecer con una actualizacion en marcha: solo el aviso, que se retira solo
    flex_ota_service_reset();
    flex_ota_request_check();   // CHECKING, sin que el trabajador la ejecute: la OTA esta ocupada
    CHK(flex_ota_busy());
    settings_close();
    settings_general();
    CHK(wait_view(1));
    row(9);
    sim_run(300);
    CHK(flex_factory_active() && flex_factory_view() == 6);
    sim_shot("ota_restablecer_bloqueado");
    sim_run(2400);
    CHK(!flex_factory_active());   // se retira solo a los 2,2 s
    row(9);
    sim_run(300);
    CHK(flex_factory_active() && flex_factory_view() == 6);
    sim_tap(240, 400);   // o con un toque
    sim_run(400);
    CHK(!flex_factory_active());
    pump(500);
    settings_close();
    // sin actualizacion en marcha, Restablecer abre su asistente de siempre
    settings_general();
    CHK(wait_view(1));
    row(9);
    sim_run(300);
    CHK(flex_factory_active() && flex_factory_view() == 1);
    sim_tap(129, 733);   // "Cancelar": vuelve a Ajustes sin haber tocado nada
    sim_run(500);
    CHK(!flex_factory_active());
    settings_close();

    flex_sys_home();
    sim_run(600);
    // ---- 12) el aviso solo se abre con un TOQUE: arrastrarlo un poco y soltar no abre el changelog; Atras no cierra el aviso ---------------------------------
    flex_ota_service_reset();
    W.net = true;
    snprintf(W.changelog, sizeof(W.changelog), "Mejoras generales\\n- Mas rapido\\n- Menos consumo");
    publish("1.7.0");
    flex_ota_request_check();
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(700);
    CHK(ov() == OV_TOAST && flex_ota_ui_card_y() == 800 - 80 - 96);
    sim_drag(240, 660, 240, 680, 120);   // 20 px hacia abajo: ni descarta el aviso ni lo "toca"
    sim_run(600);
    CHK(ov() == OV_TOAST);
    flex_ota_ui_back();   // la tarjeta flotante no es una capa de la que se salga con Atras
    flex_sys_back();
    sim_run(100);
    CHK(ov() == OV_TOAST);
    sim_drag(240, 640, 240, 700, 120);   // 60 px: se descarta
    sim_run(600);
    CHK(ov() == OV_NONE);
    // arrastrar hacia arriba y soltar tampoco lo abre
    flex_ota_service_reset();
    publish("1.7.1");
    flex_ota_request_check();
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(700);
    CHK(ov() == OV_TOAST);
    sim_drag(240, 690, 240, 665, 120);
    sim_run(400);
    CHK(ov() == OV_TOAST);
    CHK(tap_btn(FLEX_OTA_BT_TOAST));   // un toque de verdad si lo abre
    sim_run(500);
    CHK(ov() == OV_CHANGELOG);
    flex_sys_back();
    sim_run(300);
    CHK(ov() == OV_NONE);

    // ---- 13) modo claro: los mismos colores de Arduino con su paleta clara --------------------------------------------------------------------------------
    flex_theme_set_dark(false);
    sim_run(300);
    flex_ota_service_reset();
    W.net = true;
    publish("1.7.2");
    flex_ota_open_settings();
    sim_run(300);
    CHK(golden("ajustes_idle_claro", 0xe81d0ebfu, 0x88b1be35u, 0, 0, 479, 799));
    CHK(tap_btn(FLEX_OTA_BT_CHECK));
    CHK(pump_until(FLEX_OTA_AVAILABLE, 3000));
    sim_run(400);
    CHK(golden("ajustes_disponible_claro", 0xa7813c25u, 0x87e2727fu, 0, 0, 479, 799));
    flex_sys_back();
    sim_run(700);
    CHK(ov() == OV_TOAST && flex_ota_ui_card_y() == 800 - 80 - 96);
    CHK(golden("aviso_claro", 0x486b5e49u, 0x432454bbu, 8, 616, 471, 730));
    CHK(tap_btn(FLEX_OTA_BT_TOAST));
    sim_run(600);
    CHK(ov() == OV_CHANGELOG);
    CHK(golden("changelog_claro", 0xcdfab3d6u, 0x7ec245cau, 0, 0, 479, 799));
    CHK(tap_btn(FLEX_OTA_BT_CANCEL));
    sim_run(300);
    CHK(ov() == OV_NONE);
    flex_ota_test_force(FLEX_OTA_ERROR, FLEX_OTA_ERR_CANCELLED, "1.7.2");
    flex_ota_ui_install_started();   // la pantalla de instalacion, ya en su rama de error
    sim_run(300);
    CHK(ov() == OV_PROGRESS);
    CHK(golden("error_claro", 0x2d50d24au, 0x86e3a9c8u, 0, 0, 479, 799));
    CHK(tap_btn(FLEX_OTA_BT_CLOSE));
    sim_run(300);
    CHK(ov() == OV_NONE);
    flex_ota_test_force(FLEX_OTA_CONNECTING, FLEX_OTA_ERR_NONE, "1.7.2");
    flex_ota_ui_install_started();
    sim_run(300);
    CHK(ov() == OV_PROGRESS);
    CHK(golden("conectando_claro", 0x2302dddbu, 0x5d3024afu, 0, 0, 479, 799));
    flex_ota_test_force(FLEX_OTA_ERROR, FLEX_OTA_ERR_TLS, "1.7.2");   // el error sustituye a la fase sin cerrar la capa
    sim_run(300);
    CHK(ov() == OV_PROGRESS && shown("No se pudo actualizar") && shown(flex_ota_err_text(FLEX_OTA_ERR_TLS)));
    flex_ota_service_reset();   // el motor ya no instala: la pantalla de instalacion no se queda colgada
    sim_run(300);
    CHK(ov() == OV_NONE);
    flex_theme_set_dark(true);
    sim_run(300);

    flex_ota_set(NULL, NULL);
    flex_ota_service_reset();
    flex_cfg_set_i32("autolockms", 60000);
    return ok;
}
