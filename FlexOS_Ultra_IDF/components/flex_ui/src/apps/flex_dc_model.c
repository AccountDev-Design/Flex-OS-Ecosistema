// Flex OS Ultra · Device Care: metricas, puntuacion, historial y motor de diagnostico (ver flex_dc_model.h).
#include "flex_dc_model.h"

#include <stdio.h>
#include <string.h>
#include "flex_clock.h"
#include "flex_fs_names.h"

#include "flex_dc_text.inc"

const char *flex_dc_t(int id, int li)
{
    if (id < 0 || id >= DCS_N) {
        return "";
    }
    if (li < 0 || li >= FLEX_DC_LANGS) {
        li = 1;   // el chino cae en EN, como en el resto de las tablas
    }
    return DCH[id][li];
}

// dcMetricSet: el porcentaje se acota a [-1, 100]
static void mset(flex_dc_metric_t *m, uint8_t lv, int pct, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
#include <stdarg.h>
static void mset(flex_dc_metric_t *m, uint8_t lv, int pct, const char *fmt, ...)
{
    m->level = lv;
    m->pct = (int8_t)(pct < -1 ? -1 : (pct > 100 ? 100 : pct));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->detail, sizeof(m->detail), fmt, ap);
    va_end(ap);
}

#define RST_POWERON 1
#define RST_SW 3
#define RST_DEEPSLEEP 8

static bool boot_bad(uint8_t r)
{
    return !(r == RST_POWERON || r == RST_SW || r == RST_DEEPSLEEP);
}

void flex_dc_collect(const flex_dc_sys_t *y, flex_dc_metric_t m[DCH_N])
{
    const FlexMemSnap *s = &y->m;
    const int li = y->li;
    char b1[24], b2[24];

    // 1) SRAM interna: la que se agota primero y la que tumba la radio
    if (s->inTotal) {
        int used = (int)(100 - (uint64_t)s->inFree * 100 / s->inTotal);
        flexMemFmt(s->inFree, b1, sizeof(b1));
        mset(&m[DCH_MEM], s->inFree < FLEXMEM_SRAM_MIN_BYTES ? DCL_BAD : (s->inFree < FLEXMEM_SRAM_LOW_BYTES ? DCL_WARN : DCL_OK), 100 - used,
             "%s %s", b1, "libres");
    } else {
        mset(&m[DCH_MEM], DCL_NA, -1, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }

    // 2) PSRAM: presion y fragmentacion, con los mismos cortes que el presupuesto de memoria del sistema
    if (s->psTotal) {
        int lv = flexMemLevel(s);
        flexMemFmt(s->psFree, b1, sizeof(b1));
        flexMemFmt(s->psLargest, b2, sizeof(b2));
        mset(&m[DCH_PSRAM], lv >= FLEXMEM_LV_CRITICAL ? DCL_BAD : (lv >= FLEXMEM_LV_WARN ? DCL_WARN : DCL_OK), 100 - flexMemUsedPct(s),
             "%s (bloque %s)", b1, b2);
    } else {
        mset(&m[DCH_PSRAM], DCL_NA, -1, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }

    // 3) Estabilidad: como arranco esta vez y cuanto lleva encendido
    {
        bool bad = boot_bad(y->boot_reason);
        uint32_t up = y->uptime_ms / 1000u;
        if (up >= 3600) {
            snprintf(b1, sizeof(b1), "%luh %lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
        } else {
            snprintf(b1, sizeof(b1), "%lum %lus", (unsigned long)(up / 60), (unsigned long)(up % 60));
        }
        mset(&m[DCH_STAB], bad ? DCL_WARN : DCL_OK, -1, "%s: %s", flex_dc_t(DCS_UPTIME, li), b1);
    }

    // 4) Watchdog: se le pregunta al SDK si esta tarea sigue vigilada
    mset(&m[DCH_WDT], y->wdt_ok ? DCL_OK : DCL_WARN, -1, "%s: %lu/s", flex_dc_t(DCS_LOOPRATE, li), (unsigned long)y->loop_rate);

    // 5) Servicios: los que el sistema sabe comprobar de verdad
    {
        int okn = 0, tot = 3;
        okn += y->fs_ready ? 1 : 0;
        okn += y->clk_anchored ? 1 : 0;
        okn += y->time_nvs_ok ? 1 : 0;
        mset(&m[DCH_SVC], okn == tot ? DCL_OK : (okn >= tot - 1 ? DCL_WARN : DCL_BAD), okn * 100 / tot, "%d/%d", okn, tot);
    }

    // 6) Errores recientes: volcado del panel, NVS de la hora, particion y recuperaciones del bus I2C
    {
        int errs = 0;
        errs += y->flush_fault ? 1 : 0;
        errs += !y->time_nvs_ok ? 1 : 0;
        errs += !y->fs_ready ? 1 : 0;
        errs += y->recover_n ? 1 : 0;
        if (errs == 0) {
            mset(&m[DCH_ERR], DCL_OK, -1, "0");
        } else {
            mset(&m[DCH_ERR], errs == 1 ? DCL_WARN : DCL_BAD, -1, "%d", errs);
        }
    }

    // 7) Reinicios inesperados acumulados (contados en cada arranque)
    mset(&m[DCH_BOOT], y->boot_abnormal == 0 ? DCL_OK : (y->boot_abnormal < 3 ? DCL_WARN : DCL_BAD), -1, "%lu", (unsigned long)y->boot_abnormal);

    // 8) Almacenamiento: la particion de datos, que es la que se llena
    if (y->fs_ready && y->fs_total) {
        uint32_t tot = y->fs_total, usd = y->fs_used;
        int pct = (int)((uint64_t)usd * 100 / tot);
        char f[24];
        flex_fsn_fmt_size(tot - usd, f, sizeof(f));
        mset(&m[DCH_STO], pct >= 95 ? DCL_BAD : (pct >= 85 ? DCL_WARN : DCL_OK), 100 - pct, "%s %s", f, "libres");
    } else {
        mset(&m[DCH_STO], DCL_NA, -1, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
}

bool flex_dc_score_from(const flex_dc_metric_t m[DCH_N], uint8_t *out)
{
    int sum = 0, n = 0;
    for (int i = 0; i < DCH_N; i++) {
        if (m[i].level == DCL_NA) {
            continue;   // no medida: no puntua
        }
        int v;
        if (m[i].pct >= 0) {
            v = m[i].pct;   // metrica con porcentaje: su propio valor, penalizado segun su nivel
            if (m[i].level == DCL_WARN) {
                v = v * 70 / 100;
            }
            if (m[i].level == DCL_BAD) {
                v = v * 35 / 100;
            }
        } else {
            v = (m[i].level == DCL_OK) ? 100 : (m[i].level == DCL_WARN ? 62 : 25);
        }
        sum += v;
        n++;
    }
    if (n == 0) {
        return false;
    }
    int sc = sum / n;
    if (sc < 0) {
        sc = 0;
    }
    if (sc > 100) {
        sc = 100;
    }
    if (out) {
        *out = (uint8_t)sc;
    }
    return true;
}

int flex_dc_level_color(int level)
{
    switch (level) {
    case DCL_OK:
        return FDC_COL_OK;
    case DCL_WARN:
        return FDC_COL_WARN;
    case DCL_BAD:
        return FDC_COL_ERR;
    default:
        return FDC_COL_MUTE;
    }
}

int flex_dc_score_color(int sc)
{
    if (sc >= 80) {
        return FDC_COL_OK;
    }
    if (sc >= 55) {
        return FDC_COL_WARN;
    }
    return FDC_COL_ERR;
}

const char *flex_dc_score_word(int sc, int li)
{
    if (sc >= 80) {
        return flex_dc_t(DCS_ALLGOOD, li);
    }
    if (sc >= 55) {
        return flex_dc_t(DCS_ATTENTION, li);
    }
    return flex_dc_t(DCS_CRITICAL, li);
}

// ---- historial ----
#define HDR_BYTES ((int)(sizeof(uint32_t) + 2 * sizeof(uint16_t)))

void flex_dc_hist_load(flex_dc_hist_t *h, const void *blob, int n)
{
    if (h->loaded) {
        return;
    }
    h->loaded = true;
    h->n = 0;
    if (!blob || n < HDR_BYTES) {
        return;
    }
    flex_dc_file_t f;
    memset(&f, 0, sizeof(f));
    memcpy(&f, blob, (size_t)n < sizeof(f) ? (size_t)n : sizeof(f));
    if (f.magic != FLEX_DC_HIST_MAGIC || f.ver != FLEX_DC_HIST_VER) {
        return;   // otra version: se ignora, no se interpreta
    }
    int cnt = f.n;
    if (cnt > FLEX_DC_HIST_MAX) {
        cnt = FLEX_DC_HIST_MAX;
    }
    int avail = (n - HDR_BYTES) / (int)sizeof(flex_dc_rec_t);   // solo los registros que de verdad venian en el archivo
    if (cnt > avail) {
        cnt = avail;
    }
    for (int i = 0; i < cnt; i++) {
        h->rec[i] = f.rec[i];
    }
    h->n = cnt;
    for (int i = 0; i < h->n; i++) {
        if (h->rec[i].kind == DC_EV_DIAG || h->rec[i].kind == DC_EV_POST) {
            h->last_score = h->rec[i].score;
            h->last_check = h->rec[i].utc;
            break;   // el mas reciente esta el primero
        }
    }
}

void flex_dc_hist_add(flex_dc_hist_t *h, uint32_t utc, uint8_t kind, uint8_t score, uint8_t flags, uint8_t extra)
{
    for (int i = FLEX_DC_HIST_MAX - 1; i > 0; i--) {
        h->rec[i] = h->rec[i - 1];
    }
    h->rec[0].utc = utc;
    h->rec[0].kind = kind;
    h->rec[0].score = score;
    h->rec[0].flags = flags;
    h->rec[0].extra = extra;
    if (h->n < FLEX_DC_HIST_MAX) {
        h->n++;
    }
    h->dirty = true;
    if (kind == DC_EV_DIAG || kind == DC_EV_POST) {
        h->last_score = score;
        h->last_check = utc;
    }
}

bool flex_dc_hist_file(const flex_dc_hist_t *h, flex_dc_file_t *out)
{
    if (!h->dirty) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->magic = FLEX_DC_HIST_MAGIC;
    out->ver = FLEX_DC_HIST_VER;
    out->n = (uint16_t)h->n;
    for (int i = 0; i < h->n; i++) {
        out->rec[i] = h->rec[i];
    }
    return true;
}

void flex_dc_hist_saved(flex_dc_hist_t *h)
{
    h->dirty = false;
}

static long local_day(uint32_t utc)
{
    long local = (long)utc + FLEX_TZ_OFFSET_SEC;
    long days = local / 86400L;
    if (local % 86400L < 0) {
        days--;
    }
    return days;
}

int flex_dc_hist_content_h(const flex_dc_hist_t *h, int pad)
{
    int c = pad * 2;
    long last = -1;
    for (int i = 0; i < h->n; i++) {
        long d = local_day(h->rec[i].utc);
        if (d != last) {
            c += 26;
            last = d;
        }
        c += 66;
    }
    return c;
}

// ---- diagnostico ----
static void sys_set(flex_dc_sysrow_t *r, int label, const char *lit, uint8_t lv, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
static void sys_set(flex_dc_sysrow_t *r, int label, const char *lit, uint8_t lv, const char *fmt, ...)
{
    r->label = label;
    r->lit = lit;
    r->level = lv;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->value, sizeof(r->value), fmt, ap);
    va_end(ap);
}

void flex_dc_run_system_test(const flex_dc_sys_t *y, flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS], uint8_t *score, bool *score_ok)
{
    const FlexMemSnap *s = &y->m;
    const int li = y->li;
    char b[24];
    int i = 0;

    if (y->cpu_mhz) {
        sys_set(&rows[i++], -1, "CPU", DCL_OK, "%lu MHz", (unsigned long)y->cpu_mhz);
    } else {
        sys_set(&rows[i++], -1, "CPU", DCL_NA, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
    if (s->inTotal) {
        flexMemFmt(s->inFree, b, sizeof(b));
        sys_set(&rows[i++], -1, "RAM", s->inFree < FLEXMEM_SRAM_MIN_BYTES ? DCL_BAD : (s->inFree < FLEXMEM_SRAM_LOW_BYTES ? DCL_WARN : DCL_OK), "%s", b);
    } else {
        sys_set(&rows[i++], -1, "RAM", DCL_NA, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
    if (s->psTotal) {
        flexMemFmt(s->psFree, b, sizeof(b));
        int lv = flexMemLevel(s);
        sys_set(&rows[i++], -1, "PSRAM", lv >= FLEXMEM_LV_CRITICAL ? DCL_BAD : (lv >= FLEXMEM_LV_WARN ? DCL_WARN : DCL_OK), "%s", b);
    } else {
        sys_set(&rows[i++], -1, "PSRAM", DCL_NA, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
    if (y->fw_size) {
        flexMemFmt(y->fw_size, b, sizeof(b));
        sys_set(&rows[i++], -1, "Flash", DCL_OK, "%s", b);
    } else {
        sys_set(&rows[i++], -1, "Flash", DCL_NA, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
    if (y->fs_ready && y->fs_total) {
        int pct = (int)((uint64_t)y->fs_used * 100 / y->fs_total);
        sys_set(&rows[i++], DCS_STORAGE, NULL, pct >= 95 ? DCL_BAD : (pct >= 85 ? DCL_WARN : DCL_OK), "%d%%", pct);
    } else {
        sys_set(&rows[i++], DCS_STORAGE, NULL, DCL_NA, "%s", flex_dc_t(DCS_NOTAVAIL, li));
    }
    {
        int okn = (y->fs_ready ? 1 : 0) + (y->clk_anchored ? 1 : 0) + (y->time_nvs_ok ? 1 : 0);
        sys_set(&rows[i++], DCS_SERVICES, NULL, okn == 3 ? DCL_OK : (okn >= 2 ? DCL_WARN : DCL_BAD), "%d/3", okn);
    }
    sys_set(&rows[i++], DCS_WATCHDOG, NULL, y->wdt_ok ? DCL_OK : DCL_WARN, "%s", y->wdt_ok ? flex_dc_t(DCS_NORMAL, li) : flex_dc_t(DCS_UNAVAILABLE, li));
    {
        int errs = (y->flush_fault ? 1 : 0) + (!y->time_nvs_ok ? 1 : 0) + (!y->fs_ready ? 1 : 0);   // aqui no cuenta el bus I2C (como en Arduino)
        sys_set(&rows[i++], DCS_ERRORS, NULL, errs == 0 ? DCL_OK : (errs == 1 ? DCL_WARN : DCL_BAD), "%d", errs);
    }
    sys_set(&rows[i++], DCS_REBOOTS, NULL, y->boot_abnormal == 0 ? DCL_OK : (y->boot_abnormal < 3 ? DCL_WARN : DCL_BAD), "%lu",
            (unsigned long)y->boot_abnormal);

    flex_dc_metric_t m[DCH_N];
    flex_dc_collect(y, m);
    *score_ok = flex_dc_score_from(m, score);
}

void flex_dc_res_reset(flex_dc_res_t *r)
{
    memset(r, 0, sizeof(*r));
    r->screen = -1;
    r->touch = -1;
}

void flex_dc_result_compute(flex_dc_res_t *r, bool post, flex_dc_hist_t *h, uint32_t utc)
{
    int sum = 0, n = 0;
    if (r->sys_ok) {
        sum += r->sys;
        n++;
    }
    if (r->imu_avail) {
        int bits = 0;
        for (int i = 0; i < 5; i++) {
            if (r->imu_mask & (1 << i)) {
                bits++;
            }
        }
        sum += bits * 100 / 5;
        n++;
    }
    if (r->screen >= 0) {
        sum += r->screen ? 100 : 0;
        n++;
    }
    if (r->touch >= 0) {
        sum += r->touch;
        n++;
    }
    r->final_ok = (n > 0);
    r->final = r->final_ok ? (uint8_t)(sum / n) : 0;
    if (r->final_ok) {
        flex_dc_hist_add(h, utc, post ? DC_EV_POST : DC_EV_DIAG, r->final, (uint8_t)((r->screen == 1 ? 1 : 0) | (r->imu_avail ? 2 : 0)), 0);
    }
}

int flex_dc_touch_coverage(int hit)
{
    return hit * 100 / (FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS);
}
