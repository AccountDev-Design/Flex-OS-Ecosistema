// Device Care (03b §15): la app real en el simulador, con un DISCO REAL para el historial y el dedo. Se prueba lo que pone el marco: los seis accesos
// y sus pantallas, la cabecera propia y "atras", los servicios REALES que alimentan las metricas (cada entrada cambiada una a una y comparada con el
// modelo de Arduino), las animaciones (solo cambian dibujos y cifras, sin reconstruir la pantalla), el motor de diagnostico de punta a punta (etapas
// de 620 ms, ocho pasadas del test de pantalla con los pixeles de verdad, la pregunta, el test tactil con su division entera de Arduino, el
// resultado y el historial), el historial en disco (escritura de una en una, lectura asincrona, archivo ajeno o truncado, lista larga con
// desplazamiento) y la honestidad de lo que aun no existe (caidas / IMU: A17h).
#include <stdio.h>
#include <string.h>
#include "FlexOS_Mem.h"
#include "flex_app.h"
#include "flex_app_ids.h"
#include "flex_clock.h"
#include "flex_dc_model.h"
#include "flex_dc_screens.h"
#include "flex_frame.h"
#include "flex_i18n.h"
#include "flex_passcode.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_wallmgr.h"
#include "lvgl.h"
#include "ui_sim.h"

void sim_disk_enable(bool on);
bool sim_disk_put(const char *rel, const void *d, size_t l);
size_t sim_disk_get(const char *rel, void *out, size_t cap);
bool sim_disk_exists(const char *rel);
void sim_system_set(uint32_t fw_size, int reset_reason);
void sim_system_abnormal(uint32_t n);
void sim_metrics_set_loops(float n);
void sim_fs_mounted(bool on);
void sim_fs_fail_writes(bool queue_fail, bool disk_fail);
void sim_fs_enable(bool on);
void sim_fs_defer(bool on);
void sim_fs_complete(bool fail);
int sim_fs_writes(void);
size_t sim_fs_get(const char *p, uint8_t *out, size_t cap);
void sim_i2c_recoveries(uint32_t n);
void sim_read_hold(bool on);
void sim_explore_release(void);
void sim_task_wdt(bool ok);
void flex_mem_sim_set(const FlexMemSnap *s);
int flex_optimize_stage(void);
uint16_t *sim_fb(void);

int flex_app_devcare_screen(void);
int flex_app_devcare_stage(void);
int flex_app_devcare_scr_step(void);
int flex_app_devcare_builds(void);
int flex_app_devcare_taps(void);
int flex_app_devcare_cells(void);
bool flex_app_devcare_result(flex_dc_res_t *out);
bool flex_app_devcare_hit_xy(int id, int *x, int *y);
bool flex_app_devcare_touch_cell_xy(int col, int row, int *x, int *y);
const flex_dc_hist_t *flex_app_devcare_hist(void);
bool flex_app_devcare_hist_loaded(void);
int flex_app_devcare_writes_ok(void);
int flex_app_devcare_writes_failed(void);
bool flex_app_devcare_writing(void);
int flex_app_devcare_scroll(void);
bool flex_app_devcare_shown(const char *txt);
void flex_app_devcare_reset_for_test(void);
int flex_dc_ui_builds(void);
int flex_dc_ui_vec_updates(void);

enum { S_HOME = 0, S_STATUS, S_FALL, S_HIST, S_HEALTH, S_OPTIM, S_DIAG, S_POST, S_SCRTEST, S_TOUCHTEST, S_RESULT };

#define CHK(c)                                                         \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("devcare: FALLO linea %d: %s\n", __LINE__, #c);     \
            ok = false;                                                \
        }                                                              \
    } while (0)

#define HIST "/System/devcare.bin"

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

// el banner de un aviso cubre la parte de arriba y se queda con los toques
static void calm(int y)
{
    for (int i = 0; y < 110 && i < 80 && flex_notif_banner_visible(); i++) {
        sim_run(100);
    }
}
static void tap(int x, int y)
{
    calm(y);
    sim_touch(x, y, true);
    sim_run(60);
    sim_touch(x, y, false);
    sim_run(160);
}
static bool tap_hit(int id)
{
    int x, y;
    if (!flex_app_devcare_hit_xy(id, &x, &y)) {
        return false;
    }
    tap(x, y);
    return true;
}
static void tap_back(void) { tap(30, 54); }   // el chevron de la cabecera

static const char *T(int id) { return flex_dc_t(id, flex_li()); }

static bool open_dc(void)
{
    flex_app_open(IC_DEVCARE, NULL);
    sim_run(900);
    return flex_shell_state() == FLEX_SH_APP && flex_shell_fg_app() == IC_DEVCARE;
}
static void close_dc(void)
{
    flex_app_terminate_ex(IC_DEVCARE, true);
    sim_run(300);
}
static bool wait_screen(int s, int ms)
{
    for (int t = 0; t < ms && flex_app_devcare_screen() != s; t += 20) {
        sim_run(20);
    }
    return flex_app_devcare_screen() == s;
}
static bool wait_stage(int g, int ms)
{
    for (int t = 0; t < ms && flex_app_devcare_stage() != g; t += 20) {
        sim_run(20);
    }
    return flex_app_devcare_stage() == g;
}

static uint16_t px(int x, int y) { return sim_fb()[(size_t)y * 480 + (size_t)x]; }
static int red5(uint16_t c) { return (c >> 11) & 31; }

static FlexMemSnap g_mem;
static void mem_healthy(void)
{
    FlexMemSnap m = {.psTotal = 32u << 20, .psFree = 20u << 20, .psLargest = 16u << 20, .psPeakUsed = 12u << 20,
                     .inTotal = 400u << 10, .inFree = 250u << 10, .inMin = 200u << 10, .fsTotal = 0xAE0000, .fsUsed = 3u << 20, .fsValid = 1};
    g_mem = m;
    flex_mem_sim_set(&g_mem);
}

// lo que Device Care DEBE medir con los servicios en este estado (la misma asignacion que hace la app, escrita aqui aparte)
static struct {
    bool mounted, wdt;
    uint32_t recov, abn;
    int reset;
    float loops;
} g_svc;
static void svc_apply(void)
{
    sim_fs_mounted(g_svc.mounted);
    sim_i2c_recoveries(g_svc.recov);
    sim_system_abnormal(g_svc.abn);
    sim_task_wdt(g_svc.wdt);
    sim_system_set(1564656, g_svc.reset);
    sim_metrics_set_loops(g_svc.loops);
}
static void svc_default(void)
{
    g_svc.mounted = true;
    g_svc.wdt = true;
    g_svc.recov = 0;
    g_svc.abn = 0;
    g_svc.reset = 1;   // encendido
    g_svc.loops = 58.0f;
    svc_apply();
}
static void expected_sys(flex_dc_sys_t *y)
{
    memset(y, 0, sizeof(*y));
    y->li = flex_li();
    y->m = g_mem;
    y->boot_reason = (uint8_t)g_svc.reset;
    y->wdt_ok = g_svc.wdt;
    y->loop_rate = (unsigned)(g_svc.loops + 0.5f);
    y->fs_ready = g_svc.mounted;
    y->clk_anchored = true;
    y->time_nvs_ok = true;
    y->flush_fault = false;
    y->recover_n = (int)g_svc.recov;
    y->boot_abnormal = g_svc.abn;
    y->fs_total = 0xAE0000;
    y->fs_used = 3u << 20;
    y->cpu_mhz = 400;
    y->fw_size = 1564656;
}

// los detalles de la pantalla de estado frente al modelo con las mismas entradas (no la hora encendida: cambia con el reloj)
static bool status_matches(void)
{
    flex_dc_sys_t y;
    expected_sys(&y);
    flex_dc_metric_t m[DCH_N];
    flex_dc_collect(&y, m);
    bool ok = true;
    static const int LBL[DCH_N] = {DCS_MEMORY, DCS_PSRAM, DCS_STABILITY, DCS_WATCHDOG, DCS_SERVICES, DCS_ERRORS, DCS_REBOOTS, DCS_STORAGE};
    for (int i = 0; i < DCH_N; i++) {
        CHK(flex_app_devcare_shown(T(LBL[i])));
        if (i != DCH_STAB) {
            CHK(flex_app_devcare_shown(m[i].detail));
        }
    }
    return ok;
}

// fichero de historial con n registros (el mas reciente primero)
static void put_hist(int n, uint32_t magic, uint16_t ver, size_t len_override)
{
    flex_dc_file_t f;
    memset(&f, 0, sizeof(f));
    f.magic = magic;
    f.ver = ver;
    f.n = (uint16_t)n;
    uint32_t t = 1791481500u - 60;
    for (int i = 0; i < n && i < FLEX_DC_HIST_MAX; i++) {
        f.rec[i].utc = t - (uint32_t)i * 37000u;   // varios dias
        f.rec[i].kind = (uint8_t)(i % 4);
        f.rec[i].score = (uint8_t)(60 + i * 2);
        f.rec[i].flags = (uint8_t)(i & 3);
    }
    sim_disk_put(HIST, &f, len_override ? len_override : sizeof(f));
}

bool scene_devcare_run(void)
{
    bool ok = true;
    unlock_and_clean();
    sim_disk_enable(true);   // disco real y vacio
    mem_healthy();
    svc_default();
    flex_clock_set_utc(1791481500u);   // jue 8 oct 2026, 12:45 en Lima
    flex_app_devcare_reset_for_test();
    sim_fs_fail_writes(false, false);
    int b0 = flex_dc_ui_builds(), v0 = flex_dc_ui_vec_updates();

    // ---- 1) abrir: la cabecera propia, la tarjeta de estado y los seis accesos; el anillo barre SIN reconstruir la pantalla ---------------------------
    CHK(open_dc());
    CHK(flex_app_devcare_screen() == S_HOME);
    sim_run(300);
    CHK(flex_dc_ui_vec_updates() > v0 + 3);       // el anillo cuenta: cuadros de vectores y de cifras...
    CHK(flex_dc_ui_builds() <= b0 + 2);           // ...sin construir la pantalla de nuevo
    sim_run(700);
    sim_shot("dc_inicio");
    CHK(flex_app_devcare_shown(T(DCS_APPTITLE)));
    static const int TILE[6] = {DCS_STATUS, DCS_DIAG, DCS_FALL, DCS_HEALTH, DCS_OPT, DCS_HIST};
    for (int i = 0; i < 6; i++) {
        CHK(flex_app_devcare_shown(T(TILE[i])));
    }
    {
        flex_dc_sys_t y;
        expected_sys(&y);
        flex_dc_metric_t m[DCH_N];
        flex_dc_collect(&y, m);
        uint8_t sc = 0;
        CHK(flex_dc_score_from(m, &sc));
        char b[16];
        snprintf(b, sizeof(b), "%d", sc);
        CHK(flex_app_devcare_shown(b));                                       // la nota REAL de ahora, no una de adorno
        CHK(flex_app_devcare_shown(flex_dc_score_word(sc, flex_li())));
        CHK(sc == 95 || sc >= 80);
    }
    CHK(flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 0);   // disco vacio: historial vacio
    char nev[80];
    snprintf(nev, sizeof(nev), "%s: %s", T(DCS_LASTCHECK), T(DCS_NEVER));
    CHK(flex_app_devcare_shown(nev));

    // ---- 2) Estado: cada servicio cambiado una vez y comparado con el modelo de Arduino ---------------------------------------------------------
    CHK(tap_hit(DCH_T0));
    CHK(flex_app_devcare_screen() == S_STATUS);
    sim_shot("dc_estado");
    CHK(flex_app_devcare_shown("Flex OS Ultra \xC2\xB7 ESP32-P4 \xC2\xB7 480x800"));
    CHK(status_matches());
    {
        char b[48];
        snprintf(b, sizeof(b), "%s: 58/s", T(DCS_LOOPRATE));
        CHK(flex_app_devcare_shown(b));
    }
    tap(80, 54);   // x >= 72: ya no es el chevron
    CHK(flex_app_devcare_screen() == S_STATUS);
    tap(71, 95);   // x < 72 e y <= WIN_TOP: es el chevron
    CHK(flex_app_devcare_screen() == S_HOME);
    // sin particion de datos, con recuperaciones del bus, reinicios inesperados, el vigilante sin ver la tarea y el ritmo a cero
    g_svc.mounted = false;
    g_svc.recov = 2;
    g_svc.abn = 3;
    g_svc.wdt = false;
    g_svc.reset = 4;   // ESP_RST_PANIC
    g_svc.loops = 0.0f;
    svc_apply();
    CHK(tap_hit(DCH_T0));
    CHK(status_matches());
    {
        char b[48];
        snprintf(b, sizeof(b), "%s: %s", T(DCS_LOOPRATE), T(DCS_NOTAVAIL));
        CHK(flex_app_devcare_shown(b));   // con cero vueltas no hay medida: no se ensena un cero que parece medido
    }
    sim_shot("dc_estado_mal");
    tap_back();
    // cada servicio por separado cambia la nota de la tarjeta de inicio: la nota REAL, comprobada contra el modelo con la misma entrada
    {
        flex_dc_sys_t y0;
        expected_sys(&y0);
        flex_dc_metric_t m0[DCH_N];
        flex_dc_collect(&y0, m0);
        uint8_t good = 0;
        CHK(flex_dc_score_from(m0, &good));
        for (int k = 0; k < 5; k++) {
            svc_default();
            switch (k) {
            case 0: g_svc.wdt = false; break;
            case 1: g_svc.reset = 4; break;      // ESP_RST_PANIC
            case 2: g_svc.recov = 2; break;
            case 3: g_svc.abn = 3; break;
            default: g_svc.mounted = false; break;
            }
            svc_apply();
            flex_dc_sys_t y;
            expected_sys(&y);
            flex_dc_metric_t m[DCH_N];
            flex_dc_collect(&y, m);
            uint8_t sc = 0;
            CHK(flex_dc_score_from(m, &sc) && sc != good);   // la entrada importa: la nota cambia
            CHK(tap_hit(DCH_T0));
            tap_back();
            sim_run(700);
            char b[16];
            snprintf(b, sizeof(b), "%d", sc);
            CHK(flex_app_devcare_screen() == S_HOME && flex_app_devcare_shown(b) && flex_app_devcare_shown(flex_dc_score_word(sc, flex_li())));
        }
        svc_default();
    }
    // memoria baja
    {
        FlexMemSnap m = g_mem;
        m.inFree = 30u << 10;
        m.psFree = 2u << 20;
        m.psLargest = 256u << 10;
        g_mem = m;
        flex_mem_sim_set(&g_mem);
    }
    CHK(tap_hit(DCH_T0));
    CHK(status_matches());
    tap_back();
    mem_healthy();
    svc_default();

    // ---- 3) Salud: el anillo barre y despues se refresca una vez por segundo ------------------------------------------------------------------------
    CHK(tap_hit(DCH_T3));
    CHK(flex_app_devcare_screen() == S_HEALTH);
    sim_run(700);
    sim_shot("dc_salud");
    CHK(status_matches());
    {
        int bb = flex_app_devcare_builds();
        sim_fs_mounted(false);   // algo cambia: a lo sumo un segundo despues la pantalla lo sabe
        sim_run(1300);
        CHK(flex_app_devcare_builds() >= bb);
        CHK(flex_app_devcare_shown(T(DCS_NOTAVAIL)));
        sim_fs_mounted(true);
        sim_run(1300);
        CHK(!flex_app_devcare_shown(T(DCS_NOTAVAIL)));
    }
    tap_back();

    // ---- 4) Caidas: no finge un modulo ni un estado ----------------------------------------------------------------------------------------------
    CHK(tap_hit(DCH_T2));
    CHK(flex_app_devcare_screen() == S_FALL);
    sim_shot("dc_caidas");
    CHK(flex_app_devcare_shown(T(DCS_UNAVAILABLE)));
    CHK(flex_app_devcare_shown("TENSTAR GY-BNO085"));
    CHK(flex_app_devcare_shown(flex_dc_fall_note(flex_li())));
    CHK(!flex_app_devcare_shown(T(DCS_ENABLE)) && !flex_app_devcare_shown(T(DCS_TESTMODULE)) && !flex_app_devcare_shown(T(DCS_AVAILABLE)));
    CHK(tap_hit(DCH_FALLOK));
    CHK(flex_app_devcare_screen() == S_HOME);

    // ---- 5) Optimizar: el panel del sistema, y al terminar se anota en el historial y vuelve a esta pantalla ----------------------------------
    CHK(tap_hit(DCH_T4));
    CHK(flex_app_devcare_screen() == S_OPTIM);
    sim_shot("dc_optim");
    {
        char f[24], b[48];
        flexMemFmt(g_mem.psFree, f, sizeof(f));
        snprintf(b, sizeof(b), "PSRAM: %s", f);
        CHK(flex_app_devcare_shown(b));
    }
    CHK(flex_app_devcare_shown(T(DCS_OPT1)) && flex_app_devcare_shown(T(DCS_OPT4)) && flex_app_devcare_shown(T(DCS_OPTRUN)));
    CHK(tap_hit(DCH_OPT_RUN));
    sim_run(300);
    CHK(flex_optimize_stage() != 0);
    for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
        sim_run(100);
    }
    CHK(flex_optimize_stage() == 5);
    sim_tap(240, 545);   // "Hecho"
    sim_run(500);
    CHK(flex_app_devcare_screen() == S_OPTIM);
    CHK(flex_app_devcare_hist()->n == 1 && flex_app_devcare_hist()->rec[0].kind == DC_EV_OPT);
    sim_run(300);
    CHK(flex_app_devcare_writes_ok() == 1 && !flex_app_devcare_writing());
    {
        flex_dc_file_t f;
        CHK(sim_disk_get(HIST, &f, sizeof(f)) == sizeof(f));
        CHK(f.magic == 0x44434152u && f.ver == 1 && f.n == 1 && f.rec[0].kind == DC_EV_OPT && f.rec[0].utc >= 1791481500u);
    }
    tap_back();

    // ---- 6) Diagnostico completo: sistema, IMU, pantalla (ocho pasadas), pregunta, tactil, resultado -------------------------------------------
    CHK(tap_hit(DCH_T1));
    CHK(flex_app_devcare_screen() == S_DIAG && flex_app_devcare_stage() == DG_SYS);
    sim_run(200);
    sim_shot("dc_diag");
    CHK(flex_app_devcare_shown(T(DCS_SYSTEM)) && flex_app_devcare_shown(T(DCS_IMUNAME)) && flex_app_devcare_shown(T(DCS_SCREEN)) &&
        flex_app_devcare_shown(T(DCS_TOUCH)));
    CHK(wait_stage(DG_IMU, 1000));
    {
        flex_dc_res_t r;
        CHK(flex_app_devcare_result(&r) && r.sys_ok);
        flex_dc_sys_t y;
        expected_sys(&y);
        flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS];
        uint8_t sc = 0;
        bool sok = false;
        flex_dc_run_system_test(&y, rows, &sc, &sok);
        CHK(r.sys == sc && r.sys_ok == sok);   // la nota del sistema de esta pasada = la del modelo con las mismas entradas
    }
    CHK(wait_screen(S_SCRTEST, 3000));
    {
        flex_dc_res_t r;
        CHK(flex_app_devcare_result(&r) && !r.imu_avail && r.imu_mask == 0);   // sin controlador del IMU: no disponible, no puntua
    }
    sim_run(100);
    CHK(flex_app_devcare_scr_step() == 0);
    CHK(px(240, 300) == 0x0000 && px(10, 10) == 0x0000);                       // negro a pantalla completa, tambien bajo la barra de estado
    {
        bool clean = true;   // ni los glifos de la barra de estado (la hora, la red, la bateria) se ven encima del patron
        for (int y = 8; y < 34; y++) {
            for (int x = 10; x < 480; x++) {
                clean = clean && px(x, y) == 0x0000;
            }
        }
        CHK(clean);
    }
    static const uint16_t SOL[6] = {0x0000, 0xFFFF, 0xF800, 0x07E0, 0x001F, 0x8410};
    for (int s = 1; s <= 5; s++) {
        tap(240, 200);
        CHK(flex_app_devcare_scr_step() == s);
        CHK(px(240, 300) == SOL[s] && px(5, 5) == SOL[s] && px(470, 640) == SOL[s]);
    }
    tap(240, 200);   // degradado
    CHK(flex_app_devcare_scr_step() == 6);
    sim_shot("dc_pant_degradado");
    CHK(red5(px(100, 20)) < red5(px(100, 220)) && red5(px(100, 220)) < red5(px(100, 500)) && red5(px(100, 500)) < red5(px(100, 720)));
    CHK(px(100, 310 + 59) != px(100, 310) || red5(px(100, 310 + 59)) > red5(px(100, 310)));   // las rampas de color
    tap(240, 200);   // rejilla y tablero
    CHK(flex_app_devcare_scr_step() == 7);
    sim_shot("dc_pant_rejilla");
    CHK(px(2, 100) == 0xFFFF && px(3, 100) == 0x0000 && px(5, 100) == 0x0000);   // lineas verticales cada dos pixeles
    CHK(px(50, 20) == 0xFFFF && px(51, 20) == 0xFFFF);                            // casilla blanca del tablero (1, 0)
    CHK(px(0, 400) == 0xF800 && px(1, 400) == 0xF800 && px(479, 400) == 0xF800);  // borde rojo de dos pixeles
    tap(240, 200);   // la pregunta
    CHK(flex_app_devcare_scr_step() == 8);
    sim_shot("dc_pant_pregunta");
    CHK(flex_app_devcare_shown(T(DCS_SCRASK1)) && flex_app_devcare_shown(T(DCS_YES)) && flex_app_devcare_shown(T(DCS_NO)));
    CHK(flex_app_devcare_screen() == S_SCRTEST);
    // la esquina del chevron NO sale en esta prueba (en Arduino el test va primero)
    tap(30, 54);
    CHK(flex_app_devcare_screen() == S_SCRTEST);
    CHK(tap_hit(DCH_SCR_NO));
    CHK(flex_app_devcare_screen() == S_DIAG && flex_app_devcare_stage() == DG_TOUCH);   // 620 ms a la vista y entra el test tactil
    CHK(wait_screen(S_TOUCHTEST, 2000));
    {
        flex_dc_res_t r;
        CHK(flex_app_devcare_result(&r) && r.screen == 0);
    }
    sim_run(200);
    sim_shot("dc_tactil");
    CHK(flex_app_devcare_shown("0%  \xC2\xB7  0/48"));

    // ---- 7) Test tactil: cada celda que toca el dedo, tocando y arrastrando; la division ENTERA de Arduino --------------------------------------
    {
        int x, y;
        CHK(flex_app_devcare_touch_cell_xy(0, 0, &x, &y));
        tap(x, y);
        CHK(flex_app_devcare_cells() == 1);
        tap(x, y);   // la misma celda no cuenta dos veces
        CHK(flex_app_devcare_cells() == 1);
        CHK(flex_app_devcare_shown("2%  \xC2\xB7  1/48"));
        char xy[32];
        snprintf(xy, sizeof(xy), "X %d  Y %d", x, y);
        CHK(flex_app_devcare_shown(xy));
        // arrastrar por la segunda fila: seis celdas con un solo gesto
        int x1, y1, x2, y2;
        flex_app_devcare_touch_cell_xy(0, 1, &x1, &y1);
        flex_app_devcare_touch_cell_xy(5, 1, &x2, &y2);
        sim_drag(x1, y1, x2, y2, 400);
        sim_run(100);
        CHK(flex_app_devcare_cells() == 7);
        // fuera de la rejilla por abajo no enciende nada
        flex_app_devcare_touch_cell_xy(0, 7, &x1, &y1);
        tap(x1, y1 + 60);
        CHK(flex_app_devcare_cells() == 7);
        // a menos de una celda a la IZQUIERDA de la rejilla la division trunca hacia cero: cae en la columna 0 (como en Arduino)
        flex_app_devcare_touch_cell_xy(0, 3, &x1, &y1);
        tap(x1 - 40, y1);
        CHK(flex_app_devcare_cells() == 8);
        // a menos de una celda por ENCIMA de la rejilla pasa lo mismo con la fila (division entera de Arduino): cae en la fila 0
        flex_app_devcare_touch_cell_xy(4, 0, &x1, &y1);
        tap(x1, y1 - 40);
        CHK(flex_app_devcare_cells() == 9);
        // el resto de celdas, una a una
        for (int r = 0; r < FLEX_DC_TT_ROWS; r++) {
            for (int c = 0; c < FLEX_DC_TT_COLS; c++) {
                flex_app_devcare_touch_cell_xy(c, r, &x1, &y1);
                tap(x1, y1);
            }
        }
        CHK(flex_app_devcare_cells() == 48);
        CHK(flex_app_devcare_shown("100%  \xC2\xB7  48/48"));
        sim_shot("dc_tactil_completo");
    }
    CHK(tap_hit(DCH_TOUCH_END));
    CHK(wait_screen(S_DIAG, 200) || flex_app_devcare_screen() == S_DIAG);
    CHK(wait_screen(S_RESULT, 2500));
    sim_run(700);
    sim_shot("dc_resultado");
    {
        flex_dc_res_t r;
        CHK(flex_app_devcare_result(&r));
        CHK(r.touch == 100 && r.screen == 0 && r.final_ok);
        flex_dc_res_t e = r;
        flex_dc_hist_t sc;
        memset(&sc, 0, sizeof(sc));
        flex_dc_result_compute(&e, false, &sc, 1);
        CHK(r.final == e.final && sc.n == 1);                          // la media de lo que SE PUDO medir (sistema, pantalla, tactil)
        const flex_dc_hist_t *h = flex_app_devcare_hist();
        CHK(h->n == 2 && h->rec[0].kind == DC_EV_DIAG && h->rec[0].score == r.final && h->rec[0].flags == 0);
        CHK(h->last_score == r.final && h->last_check == h->rec[0].utc);
    }
    CHK(flex_app_devcare_shown(T(DCS_DONE)) && flex_app_devcare_shown(T(DCS_REPEAT)));
    {
        // las nueve filas del sistema que mide el diagnostico (CPU, RAM, PSRAM, Flash, almacenamiento, ...) tal como las da el modelo con las mismas entradas
        flex_dc_sys_t y;
        expected_sys(&y);
        flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS];
        uint8_t sc = 0;
        bool sok = false;
        flex_dc_run_system_test(&y, rows, &sc, &sok);
        for (int i = 0; i < FLEX_DC_SYS_ROWS; i++) {
            CHK(rows[i].value[0] == 0 || flex_app_devcare_shown(rows[i].value));
        }
        CHK(flex_app_devcare_shown("400 MHz"));
    }
    sim_run(400);
    CHK(flex_app_devcare_writes_ok() == 2);
    {
        flex_dc_file_t f;
        CHK(sim_disk_get(HIST, &f, sizeof(f)) == sizeof(f) && f.n == 2 && f.rec[0].kind == DC_EV_DIAG && f.rec[1].kind == DC_EV_OPT);
    }
    // "Repetir" empieza otro desde el principio y esta vez "Si" a la pantalla
    CHK(tap_hit(DCH_RES_AGAIN));
    CHK(flex_app_devcare_screen() == S_DIAG && flex_app_devcare_stage() == DG_SYS);
    CHK(wait_screen(S_SCRTEST, 4000));
    for (int i = 0; i < 8; i++) {
        tap(240, 200);
    }
    CHK(flex_app_devcare_scr_step() == 8);
    CHK(tap_hit(DCH_SCR_YES));
    CHK(wait_screen(S_TOUCHTEST, 2000));
    {
        int x, y;
        for (int c = 0; c < FLEX_DC_TT_COLS; c++) {
            flex_app_devcare_touch_cell_xy(c, 0, &x, &y);
            tap(x, y);
        }
    }
    CHK(flex_app_devcare_cells() == 6);
    CHK(tap_hit(DCH_TOUCH_END));
    CHK(wait_screen(S_RESULT, 3000));
    sim_run(300);
    {
        flex_dc_res_t r;
        CHK(flex_app_devcare_result(&r) && r.screen == 1 && r.touch == 12);   // 6 * 100 / 48
        const flex_dc_hist_t *h = flex_app_devcare_hist();
        CHK(h->n == 3 && h->rec[0].kind == DC_EV_DIAG && h->rec[0].flags == 1);
    }
    CHK(tap_hit(DCH_RES_OK));
    CHK(flex_app_devcare_screen() == S_HOME);
    sim_run(700);
    CHK(!flex_app_devcare_shown(nev));   // la ultima comprobacion ya no es "Nunca"

    // ---- 8) atras: de cada pantalla vuelve al inicio; las pruebas y el resultado a donde se lanzaron; desde el inicio sale de la app ----------------
    CHK(tap_hit(DCH_T0));
    flex_sys_back();
    sim_run(300);
    CHK(flex_app_devcare_screen() == S_HOME);
    CHK(tap_hit(DCH_T1));
    sim_run(100);
    flex_sys_back();   // el diagnostico en marcha se cancela: al inicio
    sim_run(300);
    CHK(flex_app_devcare_screen() == S_HOME);
    CHK(tap_hit(DCH_T1));
    CHK(wait_screen(S_SCRTEST, 4000));
    flex_sys_back();   // la barra de navegacion sale del test de pantalla
    sim_run(300);
    CHK(flex_app_devcare_screen() == S_HOME && flex_app_devcare_hist()->n == 3);   // una prueba cancelada no anota nada
    tap_back();   // desde el inicio la flecha sale de la app
    sim_run(500);
    CHK(flex_shell_state() != FLEX_SH_APP || flex_shell_fg_app() != IC_DEVCARE);

    // ---- 9) el historial sobrevive a cerrar la app (RAM) y a un arranque en frio (disco) ---------------------------------------------------------
    CHK(open_dc());
    CHK(flex_app_devcare_hist()->n == 3 && !flex_app_devcare_shown(nev));
    close_dc();
    flex_app_devcare_reset_for_test();   // arranque en frio
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 3 && flex_app_devcare_hist()->last_check != 0);
    CHK(!flex_app_devcare_shown(nev));
    CHK(tap_hit(DCH_T5));
    CHK(flex_app_devcare_screen() == S_HIST);
    sim_shot("dc_historial");
    CHK(flex_app_devcare_shown(T(DCS_EVDIAG)) && flex_app_devcare_shown(T(DCS_EVOPT)));
    tap_back();
    close_dc();

    // ---- 10) historial largo: 16 registros, la lista entera con desplazamiento nativo ------------------------------------------------------------
    flex_app_devcare_reset_for_test();
    put_hist(16, 0x44434152u, 1, 0);
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist()->n == 16);
    CHK(tap_hit(DCH_T5));
    sim_run(200);
    CHK(flex_app_devcare_screen() == S_HIST);
    {
        int mx = flex_dc_hist_content_h(flex_app_devcare_hist(), 20) - 640;
        CHK(mx > 200);
        CHK(flex_app_devcare_scroll() == 0);
        sim_shot("dc_historial_largo");
        sim_drag(240, 640, 240, 150, 300);
        sim_run(300);
        CHK(flex_app_devcare_scroll() > 100);
        for (int i = 0; i < 6; i++) {
            sim_drag(240, 700, 240, 120, 250);
            sim_run(100);
        }
        CHK(flex_app_devcare_scroll() == mx);   // no pasa del final
        sim_shot("dc_historial_final");
        {
            sim_drag(240, 300, 240, 400, 250);   // un poco hacia arriba: a medio camino
            sim_run(200);
            int mid = flex_app_devcare_scroll();
            CHK(mid > 0 && mid < mx);
            flex_sys_home();   // suspender y volver: la lista sigue donde estaba
            sim_run(500);
            flex_app_open(IC_DEVCARE, NULL);
            sim_run(800);
            CHK(flex_app_devcare_screen() == S_HIST && flex_app_devcare_scroll() == mid);
            for (int i = 0; i < 6; i++) {
                sim_drag(240, 700, 240, 120, 250);
                sim_run(100);
            }
            CHK(flex_app_devcare_scroll() == mx);
        }
        sim_drag(240, 120, 240, 700, 250);
        sim_drag(240, 120, 240, 700, 250);
        sim_drag(240, 120, 240, 700, 250);
        sim_drag(240, 120, 240, 700, 250);
        sim_drag(240, 120, 240, 700, 250);
        sim_run(200);
        CHK(flex_app_devcare_scroll() == 0);
        // los cuatro tipos de evento estan en la lista entera (tambien los que quedan fuera de la ventana)
        CHK(flex_app_devcare_shown(T(DCS_EVDIAG)) && flex_app_devcare_shown(T(DCS_EVFALL)) && flex_app_devcare_shown(T(DCS_EVPOST)) &&
            flex_app_devcare_shown(T(DCS_EVOPT)));
    }
    tap_back();
    close_dc();

    // ---- 11) archivos que no son suyos: otra version, otro formato, truncado, mas grande de lo debido -------------------------------------------------
    flex_app_devcare_reset_for_test();
    put_hist(5, 0x12345678u, 1, 0);   // otro formato: no se interpreta
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 0);
    CHK(tap_hit(DCH_T5));
    CHK(flex_app_devcare_shown(T(DCS_NOEVENTS)) && flex_app_devcare_shown(T(DCS_EMPTYHIST)));
    sim_shot("dc_historial_vacio");
    tap_back();
    close_dc();
    flex_app_devcare_reset_for_test();
    put_hist(5, 0x44434152u, 2, 0);   // otra version
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist()->n == 0);
    close_dc();
    flex_app_devcare_reset_for_test();
    put_hist(16, 0x44434152u, 1, 8 + 3 * 8);   // dice 16 pero solo trae 3: solo se aceptan los que de verdad venian
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist()->n == 3);
    close_dc();
    flex_app_devcare_reset_for_test();
    {
        uint8_t big[300];
        memset(big, 0, sizeof(big));
        sim_disk_put(HIST, big, sizeof(big));   // mas grande que el formato: el almacenamiento no lo entrega
    }
    CHK(open_dc());
    sim_run(300);
    CHK(flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 0);
    close_dc();

    // ---- 12) escribir: un fallo no pierde el registro (queda en RAM y se reintenta), y sin particion no se escribe --------------------------------
    flex_app_devcare_reset_for_test();
    sim_disk_put(HIST, "", 0);
    CHK(open_dc());
    sim_run(200);
    sim_fs_fail_writes(false, true);   // el disco falla al terminar
    CHK(tap_hit(DCH_T4));
    CHK(tap_hit(DCH_OPT_RUN));
    for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
        sim_run(100);
    }
    sim_tap(240, 545);
    sim_run(500);
    CHK(flex_app_devcare_hist()->n == 1 && flex_app_devcare_hist()->dirty);
    CHK(flex_app_devcare_writes_failed() == 1 && !flex_app_devcare_writing());
    sim_fs_fail_writes(false, false);
    close_dc();   // cerrar reintenta
    sim_run(400);
    {
        flex_dc_file_t f;
        CHK(sim_disk_get(HIST, &f, sizeof(f)) == sizeof(f) && f.n == 1 && f.rec[0].kind == DC_EV_OPT);
    }
    CHK(!flex_app_devcare_hist()->dirty);
    // sin particion: el registro vive en RAM y no se escribe
    int w0 = flex_app_devcare_writes_ok();
    sim_fs_mounted(false);
    CHK(open_dc());
    CHK(tap_hit(DCH_T4));
    CHK(tap_hit(DCH_OPT_RUN));
    for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
        sim_run(100);
    }
    sim_tap(240, 545);
    sim_run(500);
    CHK(flex_app_devcare_hist()->n == 2 && flex_app_devcare_hist()->dirty && flex_app_devcare_writes_ok() == w0);
    sim_fs_mounted(true);
    flex_sys_home();   // al suspender ya hay particion: se guarda (dcSuspend)
    sim_run(500);
    CHK(!flex_app_devcare_hist()->dirty);
    {
        flex_dc_file_t f;
        CHK(sim_disk_get(HIST, &f, sizeof(f)) == sizeof(f) && f.n == 2);
    }
    close_dc();

    // ---- 12b) un registro que llega mientras aun se lee el archivo va ENCIMA de lo leido, y no se pierde -----------------------------------------------
    flex_app_devcare_reset_for_test();
    put_hist(3, 0x44434152u, 1, 0);
    sim_read_hold(true);
    CHK(open_dc());
    sim_run(200);
    CHK(!flex_app_devcare_hist_loaded());
    CHK(tap_hit(DCH_T4));
    CHK(tap_hit(DCH_OPT_RUN));
    for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
        sim_run(100);
    }
    sim_tap(240, 545);
    sim_run(500);
    CHK(!flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 0);   // sigue leyendose: lo anotado espera
    sim_read_hold(false);
    sim_explore_release();
    sim_run(500);
    CHK(flex_app_devcare_hist_loaded() && flex_app_devcare_hist()->n == 4 && flex_app_devcare_hist()->rec[0].kind == DC_EV_OPT);
    {
        flex_dc_file_t f;
        CHK(sim_disk_get(HIST, &f, sizeof(f)) == sizeof(f) && f.n == 4 && f.rec[0].kind == DC_EV_OPT);
    }
    close_dc();
    sim_disk_enable(false);

    // ---- 13) una escritura a la vez: el registro que llega mientras se escribe se escribe al terminar; un fallo no pisa lo nuevo -------------------
    flex_app_devcare_reset_for_test();
    sim_fs_enable(true);
    sim_fs_defer(true);
    CHK(open_dc());
    sim_run(200);
    for (int k = 0; k < 2; k++) {
        CHK(tap_hit(DCH_T4));
        CHK(tap_hit(DCH_OPT_RUN));
        for (int i = 0; i < 80 && flex_optimize_stage() != 5; i++) {
            sim_run(100);
        }
        sim_tap(240, 545);
        sim_run(500);
        if (k == 0) {
            CHK(flex_app_devcare_writing() && sim_fs_writes() == 1);   // la primera esta en vuelo
        }
        tap_back();
    }
    CHK(flex_app_devcare_hist()->n == 2 && sim_fs_writes() == 1);      // la segunda NO se escribe encima: espera
    sim_fs_complete(false);                                            // termina la primera
    sim_run(200);
    CHK(sim_fs_writes() == 2 && flex_app_devcare_writing());           // y sale la segunda con los dos registros
    sim_fs_complete(false);
    sim_run(200);
    CHK(!flex_app_devcare_writing() && !flex_app_devcare_hist()->dirty);
    {
        flex_dc_file_t f;
        uint8_t raw[200];
        CHK(sim_fs_get(HIST, raw, sizeof(raw)) == sizeof(f));
        memcpy(&f, raw, sizeof(f));
        CHK(f.n == 2);
    }
    close_dc();
    sim_fs_defer(false);
    sim_fs_enable(false);

    // ---- 14) el idioma: los textos salen de la tabla de Arduino en cada idioma y el cambio se ve al volver a la pantalla ------------------------------
    flex_set_lang(1);
    sim_disk_enable(true);
    flex_app_devcare_reset_for_test();
    CHK(open_dc());
    CHK(flex_app_devcare_shown("Device status") && flex_app_devcare_shown("Full diagnostics") && flex_app_devcare_shown("Fall detection"));
    CHK(tap_hit(DCH_T0));
    CHK(status_matches());   // los detalles de las filas tambien en ingles ("System rate: 58/s")
    {
        char b[48];
        snprintf(b, sizeof(b), "%s: 58/s", T(DCS_LOOPRATE));
        CHK(flex_app_devcare_shown("System rate: 58/s") && flex_app_devcare_shown(b));
        CHK(!flex_app_devcare_shown("Ritmo del sistema: 58/s"));   // ni una fila en el idioma de antes
    }
    tap_back();
    flex_set_lang(0);
    sim_run(100);
    tap_back();   // sale
    sim_run(300);
    close_dc();

    // ---- 15) suspender y volver: repinta sin perder la pantalla, sin avanzar un diagnostico a medias en segundo plano ----------------------------------
    flex_app_devcare_reset_for_test();
    CHK(open_dc());
    CHK(tap_hit(DCH_T1));
    sim_run(200);
    CHK(flex_app_devcare_stage() == DG_SYS);
    flex_sys_home();
    sim_run(1500);
    CHK(flex_app_devcare_stage() == DG_SYS);   // el reloj de la app esta parado en segundo plano
    flex_app_open(IC_DEVCARE, NULL);
    sim_run(800);
    CHK(flex_app_devcare_screen() == S_DIAG);
    CHK(wait_screen(S_SCRTEST, 4000));
    flex_sys_home();   // suspender en el test de pantalla
    sim_run(500);
    flex_app_open(IC_DEVCARE, NULL);
    sim_run(800);
    CHK(flex_app_devcare_screen() == S_SCRTEST && flex_app_devcare_scr_step() == 0);
    CHK(px(240, 300) == 0x0000);   // vuelve a pantalla completa
    flex_sys_back();
    sim_run(300);
    CHK(flex_app_devcare_screen() == S_HOME);
    close_dc();
    CHK(flex_app_devcare_screen() == -1);
    sim_disk_enable(false);
    return ok;
}
