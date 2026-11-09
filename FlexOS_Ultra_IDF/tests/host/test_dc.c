// Device Care (docs/spec/03b §15): las ocho metricas, la puntuacion, los colores y las palabras, la tabla de textos, el historial persistente (carga de archivos
// validos, truncados y ajenos; insercion; lo que se escribe), la altura de la lista y el motor de diagnostico, frente a FlexOS_Ultra_DeviceCare.h y
// FlexOS_Ultra_DeviceTests.h de Arduino compilados tal cual.
#include <stdio.h>
#include <string.h>
#include "flex_dc_model.h"
#include "flex_dc_screens.h"
#include "flex_i18n.h"
#include "flex_test.h"

void ref_dc_li(int li);
void ref_dc_inputs(const FlexMemSnap *m, int boot_reason, unsigned up_ms, int wdt, unsigned loop, int fs_ready, int anchored, int nvs_ok, int fault, int recover,
                   unsigned abnormal, unsigned fs_total, unsigned fs_used, unsigned mhz, unsigned fw);
void ref_dc_collect(unsigned char *level, signed char *pct, char *detail, int *ok, int *score);
int ref_dc_level_color(int lv);
int ref_dc_score_color(int s);
const char *ref_dc_score_word(int s);
const char *ref_dc_t(int id);
void ref_dc_file_set(const void *d, int n);
void ref_dc_fs(int ready);
void ref_dc_hist_reset(void);
void ref_dc_hist_load(void);
void ref_dc_wr_ok(int ok);
void ref_dc_hist_add(unsigned utc, int kind, int score, int flags, int extra);
void ref_dc_hist_save(void);
int ref_dc_hist_get(unsigned char *recs, int *dirty, int *loaded, int *last_score, unsigned *last_check);
int ref_dc_written(unsigned char *out, int cap);
int ref_dc_content_h(int pad);
void ref_dc_imu(int avail, int checks);
void ref_dc_sys_test(char *label, char *value, unsigned char *level, int *ok, int *score);
void ref_dc_result(int sys, int sys_ok, int imu_mask, int imu_avail, int screen, int touch, int post, unsigned utc, int *final, int *final_ok);

void ref_dc_k(int k);
int ref_dc_hit(int x, int y);
const char *ref_dc_mo(int li, int m);
int ref_dc_box(int *bx, int *by, int *bw, int *bh, int w, int bot);
void ref_dc_state(int fall_on, unsigned last_check, int anim_ms);
int ref_dc_home(int w, int bot, int li, char *log, int cap);
int ref_dc_status(int w, int bot, int li, char *log, int cap);
int ref_dc_health(int w, int bot, int li, char *log, int cap);
int ref_dc_optim(int w, int bot, int li, char *log, int cap);
int ref_dc_diag(int w, int bot, int li, int stage, int post, char *log, int cap);
int ref_dc_result_render(int w, int bot, int li, int sys, int sys_ok, int imu_mask, int imu_avail, int screen, int touch, int final, int final_ok, int post, int evt_ok, int conf,
                         float peak, const char *rl, const char *rv, const unsigned char *rlv, char *log, int cap);
int ref_dc_touch_render(int w, int bot, int li, const unsigned char *cells, int hit, int lx, int ly, char *log, int cap);
int ref_dc_touch_geom(int w, int bot, int *gx, int *gy, int *cw, int *ch, int *foot);
int ref_dc_scr_ask(int w, int bot, int li, char *log, int cap);
void ref_dc_hist_fill(const unsigned char *recs, int n);
int ref_dc_hist_render(int w, int bot, int li, int scroll, char *log, int cap);

static uint32_t s_seed = 17;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static flex_dc_sys_t rand_sys(int li)
{
    flex_dc_sys_t y;
    memset(&y, 0, sizeof(y));
    y.li = li;
    FlexMemSnap *m = &y.m;
    if (rnd() % 5 != 0) {
        m->psTotal = (8u + rnd() % 25) << 20;
        m->psFree = rnd() % (m->psTotal + 1);
        m->psLargest = rnd() % (m->psFree + 1);
        m->psPeakUsed = rnd() % (m->psTotal + 1);
    }
    if (rnd() % 5 != 0) {
        m->inTotal = 100000 + rnd() % 400000;
        m->inFree = rnd() % (m->inTotal + 1);
        m->inMin = rnd() % (m->inFree + 1);
        if (rnd() % 4 == 0) {   // justo en los cortes de la SRAM
            static const unsigned th[] = {FLEXMEM_SRAM_LOW_BYTES - 1, FLEXMEM_SRAM_LOW_BYTES, FLEXMEM_SRAM_LOW_BYTES + 1, FLEXMEM_SRAM_MIN_BYTES - 1,
                                          FLEXMEM_SRAM_MIN_BYTES, FLEXMEM_SRAM_MIN_BYTES + 1};
            m->inFree = th[rnd() % 6];
            if (m->inTotal < m->inFree) m->inTotal = m->inFree + 100;
        }
    }
    if (m->psTotal && rnd() % 3 == 0) {   // presion de PSRAM en los cortes de aviso / critico
        static const unsigned mbs[] = {4, 5, 6, 10, 11, 12};
        m->psFree = (mbs[rnd() % 6] << 20) + (rnd() % 3) - 1u;
        if (m->psFree > m->psTotal) m->psFree = m->psTotal;
        m->psLargest = rnd() % (m->psFree + 1);
    }
    y.boot_reason = (uint8_t)(rnd() % 11);
    static const unsigned ups[] = {0, 59000, 60000, 3599000, 3600000, 3661000, 86400000};
    y.uptime_ms = rnd() % 3 == 0 ? ups[rnd() % 7] : rnd() % 300000000u;
    y.wdt_ok = rnd() % 4 != 0;
    y.loop_rate = rnd() % 5 == 0 ? 0 : rnd() % 400;
    y.fs_ready = rnd() % 6 != 0;
    y.clk_anchored = rnd() % 3 != 0;
    y.time_nvs_ok = rnd() % 4 != 0;
    y.flush_fault = rnd() % 8 == 0;
    y.recover_n = rnd() % 5 == 0 ? (int)(1 + rnd() % 3) : 0;
    y.boot_abnormal = rnd() % 3 == 0 ? rnd() % 6 : 0;
    y.fs_total = rnd() % 6 == 0 ? 0 : 1000000u + rnd() % 8000000u;
    y.fs_used = y.fs_total ? rnd() % (y.fs_total + 1) : 0;
    if (y.fs_total && rnd() % 3 == 0) {   // en los cortes de llenado (85 % y 95 %)
        static const unsigned pc[] = {84, 85, 86, 94, 95, 96};
        y.fs_used = (uint32_t)((uint64_t)y.fs_total * pc[rnd() % 6] / 100);
    }
    y.cpu_mhz = rnd() % 5 == 0 ? 0 : 400;
    y.fw_size = rnd() % 5 == 0 ? 0 : 500000 + rnd() % 2000000;
    return y;
}
static void feed_ref(const flex_dc_sys_t *y)
{
    ref_dc_li(y->li);
    ref_dc_inputs(&y->m, y->boot_reason, y->uptime_ms, y->wdt_ok, y->loop_rate, y->fs_ready, y->clk_anchored, y->time_nvs_ok, y->flush_fault, y->recover_n,
                  y->boot_abnormal, y->fs_total, y->fs_used, y->cpu_mhz, y->fw_size);
}

static void t_text(void)
{
    for (int li = 0; li < FLEX_DC_LANGS; li++) {
        ref_dc_li(li);
        for (int id = 0; id < DCS_N; id++) {
            CHECK(!strcmp(ref_dc_t(id), flex_dc_t(id, li)));
        }
    }
    CHECK(!strcmp(flex_dc_t(DCS_APPTITLE, 9), flex_dc_t(DCS_APPTITLE, 1)));   // un idioma fuera de tabla cae en EN
    CHECK(!strcmp(flex_dc_t(-1, 0), "") && !strcmp(flex_dc_t(DCS_N, 0), ""));
    for (int id = 0; id < DCS_N; id++) {   // el borde de la tabla: el idioma 5 (chino) y el -1 caen en EN, sin leer la fila de al lado
        CHECK(!strcmp(flex_dc_t(id, FLEX_DC_LANGS), flex_dc_t(id, 1)) && !strcmp(flex_dc_t(id, -1), flex_dc_t(id, 1)));
    }
}

static void t_metrics(void)
{
    s_seed = 99;
    int na = 0, bad = 0, warn = 0;
    for (int it = 0; it < 12000; it++) {
        flex_dc_sys_t y = rand_sys((int)(rnd() % 5));
        feed_ref(&y);
        unsigned char rl[DCH_N];
        signed char rp[DCH_N];
        char rd[DCH_N * 40];
        int rok, rsc;
        ref_dc_collect(rl, rp, rd, &rok, &rsc);
        flex_dc_metric_t m[DCH_N];
        flex_dc_collect(&y, m);
        uint8_t sc = 0;
        bool ok = flex_dc_score_from(m, &sc);
        for (int i = 0; i < DCH_N; i++) {
            if (rl[i] != m[i].level || rp[i] != m[i].pct || strcmp(rd + i * 40, m[i].detail) != 0) {
                fprintf(stderr, "  metrica %d #%d: ref nivel %d pct %d [%s] mod nivel %d pct %d [%s]\n", i, it, rl[i], rp[i], rd + i * 40, m[i].level, m[i].pct, m[i].detail);
            }
            CHECK_EQ_I(rl[i], m[i].level);
            CHECK_EQ_I(rp[i], m[i].pct);
            CHECK(!strcmp(rd + i * 40, m[i].detail));
            na += m[i].level == DCL_NA;
            bad += m[i].level == DCL_BAD;
            warn += m[i].level == DCL_WARN;
        }
        CHECK_EQ_I(rok, ok);
        CHECK_EQ_I(rsc, ok ? sc : -1);
    }
    CHECK(na > 2000 && bad > 2000 && warn > 2000);   // las tres clases se ejercitan de verdad
    // todos los niveles y todas las puntuaciones
    for (int lv = 0; lv < 4; lv++) CHECK_EQ_I(ref_dc_level_color(lv), flex_dc_level_color(lv));
    for (int li = 0; li < 5; li++) {
        ref_dc_li(li);
        for (int s = 0; s <= 100; s++) {
            CHECK_EQ_I(ref_dc_score_color(s), flex_dc_score_color(s));
            CHECK(!strcmp(ref_dc_score_word(s), flex_dc_score_word(s, li)));
        }
    }
    // sin medir nada no hay puntuacion
    flex_dc_metric_t none[DCH_N];
    memset(none, 0, sizeof(none));
    for (int i = 0; i < DCH_N; i++) none[i].level = DCL_NA;
    uint8_t sc;
    CHECK(!flex_dc_score_from(none, &sc));
}

static void blob_cmp(const char *what, int it)
{
    unsigned char rrec[16 * 8];
    int rd, rl, rs;
    unsigned rc;
    int rn = ref_dc_hist_get(rrec, &rd, &rl, &rs, &rc);
    (void)what;
    (void)it;
    (void)rn;
    (void)rd;
    (void)rl;
    (void)rs;
    (void)rc;
}

static void t_hist(void)
{
    s_seed = 2024;
    // un archivo mas corto que la cabecera NO se interpreta (y no se lee mas alla de lo que trae: el buffer es del tamano justo y ASan vigila)
    for (int len = 0; len < 8; len++) {
        unsigned char *blob = malloc((size_t)(len ? len : 1));
        memset(blob, 0xA5, (size_t)(len ? len : 1));
        flex_dc_hist_t h;
        memset(&h, 0, sizeof(h));
        memset(h.rec, 0x11, sizeof(h.rec));
        h.n = 7;
        flex_dc_hist_load(&h, blob, len);
        CHECK(h.n == 0 && h.last_check == 0 && h.last_score == 0 && !h.dirty && h.loaded);
        free(blob);
    }
    int loaded_ok = 0, rejected = 0;
    for (int it = 0; it < 6000; it++) {
        // el archivo: valido, truncado, de otra version, con cuenta de mas, o ausente
        flex_dc_file_t f;
        memset(&f, 0, sizeof(f));
        f.magic = FLEX_DC_HIST_MAGIC;
        f.ver = FLEX_DC_HIST_VER;
        f.n = (uint16_t)(rnd() % 20);
        for (int i = 0; i < FLEX_DC_HIST_MAX; i++) {
            f.rec[i].utc = 1700000000u + rnd() % 90000000u;
            f.rec[i].kind = (uint8_t)(rnd() % 4);
            f.rec[i].score = (uint8_t)(rnd() % 101);
            f.rec[i].flags = (uint8_t)(rnd() % 4);
            f.rec[i].extra = (uint8_t)(rnd() % 256);
        }
        int len = (int)sizeof(f);
        switch (rnd() % 8) {
        case 0: f.magic ^= 1; break;
        case 1: f.ver = 2; break;
        case 2: len = 8 + (int)(rnd() % ((unsigned)sizeof(f) - 8)); break;   // truncado en un registro cualquiera
        case 3: len = (int)(rnd() % 8); break;                               // menos que la cabecera
        case 4: len = -1; break;                                             // no hay archivo
        default: break;
        }
        bool fs_ready = rnd() % 8 != 0;
        ref_dc_hist_reset();
        ref_dc_file_set(&f, len);
        ref_dc_fs(fs_ready);
        ref_dc_hist_load();
        flex_dc_hist_t h;
        memset(&h, 0, sizeof(h));
        flex_dc_hist_load(&h, fs_ready && len >= 0 ? &f : NULL, fs_ready ? len : -1);
        unsigned char rrec[16 * 8];
        int rd, rl, rs;
        unsigned rc;
        int rn = ref_dc_hist_get(rrec, &rd, &rl, &rs, &rc);
        CHECK_EQ_I(rn, h.n);
        CHECK(!memcmp(rrec, h.rec, (size_t)rn * 8));
        CHECK_EQ_I(rl, h.loaded);
        CHECK_EQ_I(rs, h.last_score);
        CHECK_EQ_I((int)rc, (int)h.last_check);
        CHECK_EQ_I(rd, h.dirty);
        loaded_ok += rn > 0;
        rejected += rn == 0;
        CHECK_EQ_I(ref_dc_content_h(20), flex_dc_hist_content_h(&h, 20));
        // se anotan eventos: lo mas reciente arriba, tope de 16, y lo que se escribe es lo mismo
        int adds = 1 + (int)(rnd() % 4);
        for (int k = 0; k < adds; k++) {
            unsigned utc = rnd() % 8 == 0 ? 3 : 1700000000u + rnd() % 90000000u;
            int kind = (int)(rnd() % 4), score = (int)(rnd() % 101), flags = (int)(rnd() % 4), extra = (int)(rnd() % 256);
            bool wr_ok = rnd() % 5 != 0;
            ref_dc_wr_ok(wr_ok);
            ref_dc_hist_add(utc, kind, score, flags, extra);
            flex_dc_hist_add(&h, utc, (uint8_t)kind, (uint8_t)score, (uint8_t)flags, (uint8_t)extra);
            unsigned char wr[sizeof(flex_dc_file_t) + 8];
            int wn = ref_dc_written(wr, sizeof(wr));
            flex_dc_file_t out;
            bool has = flex_dc_hist_file(&h, &out) && fs_ready;
            CHECK_EQ_I(has, wn >= 0 ? 1 : 0);
            if (has && wn >= 0) {
                CHECK_EQ_I(wn, (int)sizeof(out));
                CHECK(!memcmp(wr, &out, sizeof(out)));
                if (wr_ok) flex_dc_hist_saved(&h);
            }
            rn = ref_dc_hist_get(rrec, &rd, &rl, &rs, &rc);
            CHECK_EQ_I(rn, h.n);
            CHECK(!memcmp(rrec, h.rec, (size_t)rn * 8));
            CHECK_EQ_I(rs, h.last_score);
            CHECK_EQ_I((int)rc, (int)h.last_check);
            if (fs_ready) CHECK_EQ_I(rd, h.dirty);
            CHECK_EQ_I(ref_dc_content_h(20), flex_dc_hist_content_h(&h, 20));
            // lo que la ref ya escribio, el modelo lo descarta para el siguiente giro
            if (!fs_ready) break;
        }
        blob_cmp("hist", it);
    }
    CHECK(loaded_ok > 2500 && rejected > 1000);
    // sin cambios no hay nada que escribir
    flex_dc_hist_t h;
    memset(&h, 0, sizeof(h));
    flex_dc_file_t out;
    CHECK(!flex_dc_hist_file(&h, &out));
    CHECK_EQ_I(flex_dc_hist_content_h(&h, 20), 40);
    // un dia local: la hora UTC 04:59 y 05:01 caen en dias distintos (UTC-5): tres tarjetas en dos dias
    flex_dc_hist_add(&h, 86400u * 3 + 5 * 3600 - 60, DC_EV_DIAG, 90, 0, 0);
    flex_dc_hist_add(&h, 86400u * 3 + 5 * 3600 + 60, DC_EV_DIAG, 91, 0, 0);
    flex_dc_hist_add(&h, 86400u * 3 + 5 * 3600 + 120, DC_EV_OPT, 0, 0, 0);
    CHECK_EQ_I(flex_dc_hist_content_h(&h, 20), 40 + 2 * 26 + 3 * 66);
}

static void t_diag(void)
{
    s_seed = 555;
    for (int it = 0; it < 6000; it++) {
        flex_dc_sys_t y = rand_sys((int)(rnd() % 5));
        feed_ref(&y);
        char rlab[9 * 32], rval[9 * 28];
        unsigned char rlv[9];
        int rok, rsc;
        ref_dc_sys_test(rlab, rval, rlv, &rok, &rsc);
        flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS];
        uint8_t sc = 0;
        bool ok;
        flex_dc_run_system_test(&y, rows, &sc, &ok);
        for (int i = 0; i < FLEX_DC_SYS_ROWS; i++) {
            const char *lab = rows[i].label >= 0 ? flex_dc_t(rows[i].label, y.li) : rows[i].lit;
            if (strcmp(rlab + i * 32, lab) != 0 || strcmp(rval + i * 28, rows[i].value) != 0 || rlv[i] != rows[i].level) {
                fprintf(stderr, "  fila %d #%d: ref [%s=%s] %d mod [%s=%s] %d\n", i, it, rlab + i * 32, rval + i * 28, rlv[i], lab, rows[i].value, rows[i].level);
            }
            CHECK(!strcmp(rlab + i * 32, lab));
            CHECK(!strcmp(rval + i * 28, rows[i].value));
            CHECK_EQ_I(rlv[i], rows[i].level);
        }
        CHECK_EQ_I(rok, ok);
        if (ok) CHECK_EQ_I(rsc, sc);
    }
    // el resultado final: media de lo que SE PUDO medir y su anotacion en el historial
    s_seed = 808;
    int with = 0, without = 0;
    for (int it = 0; it < 8000; it++) {
        flex_dc_res_t r;
        flex_dc_res_reset(&r);
        r.sys_ok = rnd() % 3 != 0;
        r.sys = (uint8_t)(rnd() % 101);
        r.imu_avail = rnd() % 3 == 0;
        r.imu_mask = (uint8_t)(rnd() % 256);   // solo cuentan las cinco comprobaciones
        r.screen = rnd() % 3 == 0 ? -1 : (int8_t)(rnd() % 2);
        r.touch = rnd() % 3 == 0 ? -1 : (int8_t)(rnd() % 101);
        bool post = rnd() % 2;
        unsigned utc = 1700000000u + rnd() % 1000000u;
        ref_dc_hist_reset();
        ref_dc_file_set(NULL, -1);
        ref_dc_fs(1);
        int rf, rfok;
        ref_dc_result(r.sys, r.sys_ok, r.imu_mask, r.imu_avail, r.screen, r.touch, post, utc, &rf, &rfok);
        flex_dc_hist_t h;
        memset(&h, 0, sizeof(h));
        flex_dc_hist_load(&h, NULL, -1);
        flex_dc_result_compute(&r, post, &h, utc);
        CHECK_EQ_I(rfok, r.final_ok);
        CHECK_EQ_I(rf, r.final);
        unsigned char rrec[16 * 8];
        int rd, rl, rs;
        unsigned rc;
        int rn = ref_dc_hist_get(rrec, &rd, &rl, &rs, &rc);
        CHECK_EQ_I(rn, h.n);
        CHECK(!memcmp(rrec, h.rec, (size_t)rn * 8));
        CHECK_EQ_I(rs, h.last_score);
        with += rfok;
        without += !rfok;
    }
    CHECK(with > 3000 && without > 100);
    CHECK_EQ_I(flex_dc_touch_coverage(0), 0);
    CHECK_EQ_I(flex_dc_touch_coverage(48), 100);
    CHECK_EQ_I(flex_dc_touch_coverage(23), 47);
}

// ---- pantallas: el registro de primitivas del modelo, con la misma gramatica que el del doble de Arduino ----
static const unsigned ROLE_TOK[DCC_N] = {0xF101, 0xF102, 0xF103, 0xF110, 0xF111, 0xF112, 0xF109, 0xF10A, 0xF113, 0xF114, 0xF115, 0xF105, 0xF10D, 0xF11A, 0xF11B, 0xF200, 0xF10E};
typedef struct { char *s; size_t cap, n; } sb_t;
static void sb(sb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void sb(sb_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(b->s + b->n, b->cap - b->n, fmt, ap);
    va_end(ap);
    if (k > 0 && b->n + (size_t)k < b->cap) b->n += (size_t)k;
}
static void expand(const flex_dc_page_t *pg, sb_t *b)
{
    for (int i = 0; i < pg->n; i++) {
        const flex_dc_op_t *o = &pg->op[i];
        unsigned c = ROLE_TOK[o->role];
        switch (o->kind) {
        case DCO_SURF: sb(b, "U%d,%d,%d,%d,%d,%d;", o->x, o->y, o->w, o->h, o->r, o->surf); break;
        case DCO_RECT: sb(b, "R%d,%d,%d,%d,%u;", o->x, o->y, o->w, o->h, c); break;
        case DCO_RRECT: sb(b, "Q%d,%d,%d,%d,%d,%u;", o->x, o->y, o->w, o->h, o->r, c); break;
        case DCO_OUTLINE: sb(b, "L%d,%d,%d,%d,%d,%u;", o->x, o->y, o->w, o->h, o->r, c); break;
        case DCO_DISC: sb(b, "O%d,%d,%d,%u;", o->x, o->y, o->r, c); break;
        case DCO_CIRCLE: sb(b, "C%d,%d,%d,%u;", o->x, o->y, o->r, c); break;
        case DCO_RING: sb(b, "I%d,%d,%d,%d,%u;", o->x, o->y, o->r, o->th, c); break;
        case DCO_ARC: sb(b, "A%.3f,%.3f,%.3f,%.3f,%.3f,%d,%u;", (double)o->f[0], (double)o->f[1], (double)o->f[2], (double)o->f[3], (double)o->f[4], o->th, c); break;
        case DCO_STROKE: sb(b, "S%.3f,%.3f,%.3f,%.3f,%.3f,%u;", (double)o->f[0], (double)o->f[1], (double)o->f[2], (double)o->f[3], (double)o->f[4], c); break;
        case DCO_TRI: sb(b, "V%d,%d,%d,%d,%d,%d,%u;", o->p[0], o->p[1], o->p[2], o->p[3], o->p[4], o->p[5], c); break;
        case DCO_TEXT: sb(b, "T%d,%d,%d,%u:%s;", o->x, o->y, o->size, c, o->text); break;
        case DCO_TEXT_CLIP: sb(b, "K%d,%d,%d,%u,%d:%s;", o->x, o->y, o->size, c, o->mr, o->text); break;
        case DCO_TEXT_C: sb(b, "M%d,%d,%d,%u:%s;", o->x, o->y, o->size, c, o->text); break;
        case DCO_TEXT_R: sb(b, "N%d,%d,%d,%u:%s;", o->x, o->y, o->size, c, o->text); break;
        default: break;
        }
    }
    for (int i = 0; i < pg->nhit; i++) sb(b, "H%d,%d,%d,%d,%d;", pg->hit[i].x0, pg->hit[i].y0, pg->hit[i].x1, pg->hit[i].y1, pg->hit[i].id);
}
static const char *mo_cb(int li, int m) { return flex_i18n_mo_short[li][m]; }
static int tw_cb(const char *t, int fs, void *ctx)
{
    return (int)strlen(t) * *(int *)ctx * fs;
}
static char s_got[60000], s_exp[60000];
static flex_dc_page_t s_pg;
static int s_k = 6;

static void cmp_screen(const char *what, int it, int w, int bot, int li)
{
    if (strcmp(s_got, s_exp) != 0) {
        size_t i = 0;
        while (s_got[i] && s_got[i] == s_exp[i]) i++;
        fprintf(stderr, "  %s #%d w=%d bot=%d li=%d: difieren en %zu\n   ref: ...%.140s\n   mod: ...%.140s\n", what, it, w, bot, li, i, s_got + (i > 60 ? i - 60 : 0), s_exp + (i > 60 ? i - 60 : 0));
    }
    CHECK(strcmp(s_got, s_exp) == 0);
}
static flex_dc_ctx_t ctx_for(int w, int bot, int li)
{
    flex_dc_ctx_t c;
    int bx, by, bw, bh;
    ref_dc_box(&bx, &by, &bw, &bh, w, bot);
    c.bx = bx; c.by = by; c.bw = bw; c.bh = bh;
    c.li = li;
    c.tw = tw_cb;
    c.tctx = &s_k;
    c.mo_short = mo_cb;
    return c;
}
static void hit_cmp(const flex_dc_page_t *pg)
{
    for (int k = 0; k < 6; k++) {
        int x = (int)(rnd() % 520) - 20, y = (int)(rnd() % 820) - 10;
        if (pg->nhit && k < 3) {   // alrededor de las zonas
            int i = (int)(rnd() % (unsigned)pg->nhit);
            x = (rnd() % 2 ? pg->hit[i].x0 : pg->hit[i].x1) + (int)(rnd() % 3) - 1;
            y = (rnd() % 2 ? pg->hit[i].y0 : pg->hit[i].y1) + (int)(rnd() % 3) - 1;
        }
        CHECK_EQ_I(ref_dc_hit(x, y), flex_dc_hit_test(pg, x, y));
    }
}
static void box_pick(int *w, int *bot)
{
    static const int ws[] = {480, 400, 300};
    static const int bots[] = {736, 800, 640, 560, 500, 460, 420, 380, 340};
    *w = ws[rnd() % 3];
    *bot = rnd() % 2 ? bots[rnd() % 9] : 330 + (int)(rnd() % 470);
}

static void t_screens(void)
{
    s_seed = 31337;
    for (int it = 0; it < 4000; it++) {
        int w, bot;
        box_pick(&w, &bot);
        int li = (int)(rnd() % 5);
        s_k = rnd() % 4 == 0 ? 5 : 6;
        ref_dc_k(s_k);
        flex_dc_sys_t y = rand_sys(li);
        feed_ref(&y);
        flex_dc_ctx_t c = ctx_for(w, bot, li);
        flex_dc_metric_t m[DCH_N];
        flex_dc_collect(&y, m);
        uint8_t sc = 0;
        bool ok = flex_dc_score_from(m, &sc);
        int anim = (int)(rnd() % 3 == 0 ? rnd() % 700 : 800);
        unsigned last = rnd() % 3 == 0 ? 0 : 1700000000u + rnd() % 90000000u;
        int fall_on = (int)(rnd() % 2), bno = (int)(rnd() % 2);
        ref_dc_state(fall_on, last, anim);
        ref_dc_imu(bno, 0);
        sb_t e;

        // inicio
        flex_dc_home_t hm = {.score_ok = ok, .score = sc, .last_check = last, .fall_on = fall_on != 0, .bno_avail = bno != 0};
        ref_dc_home(w, bot, li, s_got, sizeof(s_got));
        flex_dc_home(&s_pg, &c, &hm);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("inicio", it, w, bot, li);
        hit_cmp(&s_pg);

        // estado
        ref_dc_status(w, bot, li, s_got, sizeof(s_got));
        flex_dc_status(&s_pg, &c, m, y.loop_rate);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("estado", it, w, bot, li);
        hit_cmp(&s_pg);

        // salud
        ref_dc_health(w, bot, li, s_got, sizeof(s_got));
        flex_dc_health(&s_pg, &c, m, flex_dc_ring_p((uint32_t)anim));
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("salud", it, w, bot, li);
        hit_cmp(&s_pg);

        // optimizar
        ref_dc_optim(w, bot, li, s_got, sizeof(s_got));
        flex_dc_optim(&s_pg, &c, y.m.psTotal, y.m.psFree);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("optimizar", it, w, bot, li);
        hit_cmp(&s_pg);

        // motor de diagnostico
        int stage = (int)(rnd() % DG_N), post = (int)(rnd() % 2);
        ref_dc_diag(w, bot, li, stage, post, s_got, sizeof(s_got));
        flex_dc_diag(&s_pg, &c, stage, post != 0, (uint32_t)anim);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("diagnostico", it, w, bot, li);

        // test de pantalla: la pregunta final
        ref_dc_scr_ask(w, bot, li, s_got, sizeof(s_got));
        flex_dc_scr_ask(&s_pg, &c);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("pantalla", it, w, bot, li);
        hit_cmp(&s_pg);
    }
}

static void t_screens2(void)
{
    s_seed = 4242;
    for (int it = 0; it < 4000; it++) {
        int w, bot;
        box_pick(&w, &bot);
        int li = (int)(rnd() % 5);
        s_k = rnd() % 4 == 0 ? 5 : 6;
        ref_dc_k(s_k);
        flex_dc_sys_t y = rand_sys(li);
        feed_ref(&y);
        flex_dc_ctx_t c = ctx_for(w, bot, li);
        int anim = (int)(rnd() % 3 == 0 ? rnd() % 700 : 800);
        ref_dc_state(0, 0, anim);
        sb_t e;

        // resultado: lo medido + las nueve filas del sistema
        flex_dc_res_t r;
        flex_dc_res_reset(&r);
        flex_dc_sysrow_t rows[FLEX_DC_SYS_ROWS];
        flex_dc_run_system_test(&y, rows, &r.sys, &r.sys_ok);
        r.imu_avail = rnd() % 3 == 0;
        r.imu_mask = (uint8_t)(rnd() % 256);   // solo cuentan las cinco comprobaciones
        r.screen = rnd() % 3 == 0 ? -1 : (int8_t)(rnd() % 2);
        r.touch = rnd() % 3 == 0 ? -1 : (int8_t)(rnd() % 101);
        flex_dc_hist_t hh;
        memset(&hh, 0, sizeof(hh));
        flex_dc_result_compute(&r, false, &hh, 1700000000u);
        bool post = rnd() % 2, evt_ok = rnd() % 2;
        unsigned conf = rnd() % 101;
        float peak = (float)(rnd() % 400) / 10.0f;
        char rl[9 * 32], rv[9 * 28];
        unsigned char rlv[9];
        for (int i = 0; i < FLEX_DC_SYS_ROWS; i++) {
            snprintf(rl + i * 32, 32, "%s", rows[i].label >= 0 ? flex_dc_t(rows[i].label, li) : rows[i].lit);
            memcpy(rv + i * 28, rows[i].value, 28);
            rlv[i] = rows[i].level;
        }
        ref_dc_result_render(w, bot, li, r.sys, r.sys_ok, r.imu_mask, r.imu_avail, r.screen, r.touch, r.final, r.final_ok, post, evt_ok, (int)conf, peak, rl, rv, rlv, s_got,
                             sizeof(s_got));
        flex_dc_result_t rr = {.res = &r, .rows = rows, .post = post, .last_evt_ok = evt_ok, .evt_conf = conf, .evt_peak_g = (double)peak};
        flex_dc_result(&s_pg, &c, &rr, (uint32_t)anim);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("resultado", it, w, bot, li);
        hit_cmp(&s_pg);

        // test tactil
        unsigned char cells[FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS];
        int hit = 0;
        for (int i = 0; i < FLEX_DC_TT_COLS * FLEX_DC_TT_ROWS; i++) {
            cells[i] = rnd() % 3 == 0;
            hit += cells[i];
        }
        if (rnd() % 5 == 0) hit = (int)(rnd() % 49);   // la cifra de cobertura en todos sus tramos (60, 95)
        int lx = rnd() % 4 == 0 ? -1 : (int)(rnd() % 480), ly = (int)(rnd() % 800);
        ref_dc_touch_render(w, bot, li, cells, hit, lx, ly, s_got, sizeof(s_got));
        flex_dc_touch_test(&s_pg, &c, cells, hit, lx, lx < 0 ? -1 : ly);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("tactil", it, w, bot, li);
        hit_cmp(&s_pg);
        int gx, gy, cw, ch, foot;
        ref_dc_touch_geom(w, bot, &gx, &gy, &cw, &ch, &foot);
        flex_dc_ttgeom_t tg;
        flex_dc_touch_geom(&c, &tg);
        CHECK(gx == tg.gx && gy == tg.gy && cw == tg.cw && ch == tg.ch && foot == tg.foot);

        // historial: 0..16 registros y la lista desplazada
        flex_dc_hist_t h;
        memset(&h, 0, sizeof(h));
        h.n = (int)(rnd() % 3 == 0 ? rnd() % 4 : rnd() % 17);
        uint32_t base = 1700000000u + rnd() % 5000000u;
        for (int i = 0; i < h.n; i++) {
            base -= rnd() % 3 == 0 ? 90000 + rnd() % 100000 : rnd() % 20000;   // el mas reciente arriba, con cambios de dia
            h.rec[i].utc = base;
            h.rec[i].kind = (uint8_t)(rnd() % 4);
            h.rec[i].score = (uint8_t)(rnd() % 101);
            h.rec[i].flags = (uint8_t)(rnd() % 4);
            h.rec[i].extra = (uint8_t)(rnd() % 256);
        }
        int mx = flex_dc_hist_content_h(&h, flex_ui_pad(c.bw, c.bh)) - c.bh;
        int scroll = mx > 0 && rnd() % 3 != 0 ? (int)(rnd() % (unsigned)(mx + 1)) : 0;
        ref_dc_hist_fill((const unsigned char *)h.rec, h.n);
        ref_dc_hist_render(w, bot, li, scroll, s_got, sizeof(s_got));
        flex_dc_hist(&s_pg, &c, &h, scroll);
        e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&s_pg, &e);
        cmp_screen("historial", it, w, bot, li);
        CHECK_EQ_I(ref_dc_content_h(0), flex_dc_hist_content_h(&h, 20));   // el doble mide con la caja por defecto (pad 20)
    }
}

// Lo que no existe en Arduino (no hay referencia que comparar): la tarjeta animada con el anillo a medio barrer y la pantalla de caidas SIN el controlador
// del sensor. Se comprueba lo que se promete: que home_p(1) es la pantalla de inicio de siempre, que a medio barrer solo cambia la tarjeta, que todo lo que
// pinta cae dentro de la caja, que dice lo que debe (y no finge un modulo) y que su unica zona pulsable es "Entendido".
static bool op_in(const flex_dc_op_t *o, int x0, int y0, int x1, int y1)
{
    const int tol = 3;
    switch (o->kind) {
    case DCO_SURF: case DCO_RECT: case DCO_RRECT: case DCO_OUTLINE:
        return o->x >= x0 - tol && o->y >= y0 - tol && o->x + o->w <= x1 + tol && o->y + o->h <= y1 + tol;
    case DCO_DISC: case DCO_CIRCLE: case DCO_RING:
        return o->x - o->r >= x0 - tol && o->x + o->r <= x1 + tol && o->y - o->r >= y0 - tol && o->y + o->r <= y1 + tol;
    case DCO_ARC:
        return o->f[0] - o->f[2] >= x0 - tol && o->f[0] + o->f[2] <= x1 + tol && o->f[1] - o->f[2] >= y0 - tol && o->f[1] + o->f[2] <= y1 + tol;
    case DCO_STROKE:
        return o->f[0] >= x0 - tol && o->f[2] <= x1 + tol && o->f[1] >= y0 - tol && o->f[3] <= y1 + tol && o->f[2] >= x0 - tol && o->f[0] <= x1 + tol;
    case DCO_TRI:
        for (int k = 0; k < 3; k++) {
            if (o->p[2 * k] < x0 - tol || o->p[2 * k] > x1 + tol || o->p[2 * k + 1] < y0 - tol || o->p[2 * k + 1] > y1 + tol) return false;
        }
        return true;
    default:   // textos: el punto de anclaje
        return o->x >= x0 - tol && o->x <= x1 + tol && o->y >= y0 - tol && o->y <= y1 + tol;
    }
}
static bool has_text(const flex_dc_page_t *pg, const char *s)
{
    for (int i = 0; i < pg->n; i++) {
        if (pg->op[i].kind >= DCO_TEXT && strcmp(pg->op[i].text, s) == 0) return true;
    }
    return false;
}

static void t_extra(void)
{
    s_seed = 90210;
    static flex_dc_page_t a, b;
    // las notas de caidas: una por idioma, distintas, y el idioma fuera de rango cae en ingles
    for (int i = 0; i < FLEX_DC_LANGS; i++) {
        CHECK(flex_dc_fall_note(i)[0] != 0);
        for (int j = i + 1; j < FLEX_DC_LANGS; j++) CHECK(strcmp(flex_dc_fall_note(i), flex_dc_fall_note(j)) != 0);
    }
    CHECK(strcmp(flex_dc_fall_note(-1), flex_dc_fall_note(1)) == 0 && strcmp(flex_dc_fall_note(9), flex_dc_fall_note(1)) == 0);
    for (int it = 0; it < 600; it++) {
        int w, bot;
        box_pick(&w, &bot);
        int li = (int)(rnd() % 5);
        flex_dc_ctx_t c = ctx_for(w, bot, li);
        // inicio: con el anillo entero es la pantalla de siempre; a medio barrer solo cambia la tarjeta (lo demas, igual)
        flex_dc_home_t h = {.score_ok = rnd() % 4 != 0, .score = (uint8_t)(rnd() % 101), .last_check = rnd() % 2 ? 1791481500u : 0u, .fall_on = rnd() % 2, .bno_avail = rnd() % 2};
        flex_dc_home(&a, &c, &h);
        flex_dc_home_p(&b, &c, &h, 1.0f);
        CHECK(a.n == b.n && a.nhit == b.nhit && memcmp(a.op, b.op, sizeof(a.op[0]) * (size_t)a.n) == 0 && memcmp(a.hit, b.hit, sizeof(a.hit[0]) * (size_t)a.nhit) == 0);
        float p = (float)(rnd() % 1000) / 1000.0f;
        flex_dc_home_p(&b, &c, &h, p);
        int cx, cy, cw, ch;
        flex_dc_home_card_geom(&c, &cx, &cy, &cw, &ch);
        static flex_dc_page_t card;
        card.n = card.nhit = 0;
        flex_dc_home_card(&card, &c, &h, cx, cy, cw, ch, p);
        CHECK(b.op[0].kind == DCO_RECT && memcmp(&b.op[1], &card.op[0], sizeof(card.op[0]) * (size_t)card.n) == 0);   // tras el fondo, la tarjeta con ese p
        int tail = b.n - 1 - card.n;
        CHECK(tail > 0 && b.n == a.n);
        CHECK(memcmp(&b.op[1 + card.n], &a.op[1 + card.n], sizeof(a.op[0]) * (size_t)tail) == 0);   // y los seis accesos no se enteran del anillo
        // caidas sin controlador
        flex_dc_fall_unavailable(&a, &c);
        flex_dc_fall_unavailable(&b, &c);
        CHECK(a.n == b.n && memcmp(a.op, b.op, sizeof(a.op[0]) * (size_t)a.n) == 0);   // determinista
        CHECK(a.n > 0 && a.n < FLEX_DC_OPS_MAX);
        CHECK(a.op[0].kind == DCO_RECT && a.op[0].role == DCC_WINBG);
        bool inside = true;
        for (int i = 1; i < a.n; i++) {
            if (!op_in(&a.op[i], c.bx, c.by, c.bx + c.bw, c.by + c.bh)) {
                if (c.bh >= 420) fprintf(stderr, "  fuera: op %d kind %d x %d y %d w %d h %d r %d en caja %d,%d %dx%d\n", i, a.op[i].kind, a.op[i].x, a.op[i].y, a.op[i].w, a.op[i].h, a.op[i].r, c.bx, c.by, c.bw, c.bh);
                inside = false;
            }
        }
        CHECK(inside || c.bh < 420);   // en cajas diminutas ni Arduino cabe
        CHECK(has_text(&a, flex_dc_t(DCS_FALL, li)) && has_text(&a, flex_dc_t(DCS_UNAVAILABLE, li)) && has_text(&a, "TENSTAR GY-BNO085") &&
              has_text(&a, flex_dc_t(DCS_NEEDHW, li)) && has_text(&a, flex_dc_t(DCS_NEEDHW_3, li)) && has_text(&a, flex_dc_fall_note(li)) &&
              has_text(&a, flex_dc_t(DCS_UNDERSTOOD, li)));
        // nada que finja el modulo ni un estado: ni "Activar", ni "Probar el modulo", ni "Disponible", ni "Reintentar"
        CHECK(!has_text(&a, flex_dc_t(DCS_ENABLE, li)) && !has_text(&a, flex_dc_t(DCS_TESTMODULE, li)) && !has_text(&a, flex_dc_t(DCS_AVAILABLE, li)) &&
              !has_text(&a, flex_dc_t(DCS_REPEAT, li)) && !has_text(&a, flex_dc_t(DCS_MONITORING, li)));
        CHECK(a.nhit == 1 && a.hit[0].id == DCH_FALLOK);
        CHECK(a.hit[0].x0 >= c.bx && a.hit[0].x1 <= c.bx + c.bw && a.hit[0].y1 <= c.by + c.bh && a.hit[0].y0 > c.by);
        int mx = (a.hit[0].x0 + a.hit[0].x1) / 2, my = (a.hit[0].y0 + a.hit[0].y1) / 2;
        CHECK_EQ_I(flex_dc_hit_test(&a, mx, my), DCH_FALLOK);
        CHECK_EQ_I(flex_dc_hit_test(&a, c.bx + 1, c.by + 1), -1);
    }
}

static unsigned crc_of(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h;
}

// Fijados a mano (la pantalla de caidas sin controlador no existe en Arduino: no hay referencia). Si cambia a proposito, se regeneran mirando la salida.
static const unsigned GOLD_FALL[FLEX_DC_LANGS] = {366060304u, 498466310u, 431808522u, 3965019636u, 3969767253u};

static void t_extra2(void)
{
    s_seed = 777;
    static flex_dc_page_t a, b;
    // el anillo de puntuacion acota el barrido a 0..1
    for (int it = 0; it < 300; it++) {
        int w, bot;
        box_pick(&w, &bot);
        flex_dc_ctx_t c = ctx_for(w, bot, (int)(rnd() % 5));
        int score = (int)(rnd() % 101), cx = 100 + (int)(rnd() % 200), cy = 100 + (int)(rnd() % 300), r = 20 + (int)(rnd() % 60);
        a.n = a.nhit = 0;
        flex_dc_score_ring_c(&a, &c, cx, cy, r, score, 1.0f, DCC_OK);
        b.n = b.nhit = 0;
        flex_dc_score_ring_c(&b, &c, cx, cy, r, score, 1.0f + 0.01f + (float)(rnd() % 90) / 100.0f, DCC_OK);
        CHECK(a.n == b.n && memcmp(a.op, b.op, sizeof(a.op[0]) * (size_t)a.n) == 0);
        a.n = a.nhit = 0;
        flex_dc_score_ring_c(&a, &c, cx, cy, r, score, 0.0f, DCC_OK);
        b.n = b.nhit = 0;
        flex_dc_score_ring_c(&b, &c, cx, cy, r, score, -0.01f - (float)(rnd() % 90) / 100.0f, DCC_OK);
        CHECK(a.n == b.n && memcmp(a.op, b.op, sizeof(a.op[0]) * (size_t)a.n) == 0);
    }
    // la tarjeta SIN nota (no hay ninguna metrica medible): anillo vacio de pista con grosor r / 6 y "--" (inalcanzable hoy, pero es la salida honesta)
    {
        flex_dc_ctx_t c = ctx_for(480, 736, 0);
        flex_dc_home_t h = {.score_ok = false};
        int cx, cy, cw, ch;
        flex_dc_home_card_geom(&c, &cx, &cy, &cw, &ch);
        a.n = a.nhit = 0;
        flex_dc_home_card(&a, &c, &h, cx, cy, cw, ch, 1.0f);
        int rr = ch / 2 - 16;
        rr = rr > 54 ? 54 : (rr < 26 ? 26 : rr);
        bool seen = false, dash = false;
        for (int i = 0; i < a.n; i++) {
            if (a.op[i].kind == DCO_RING && a.op[i].role == DCC_TRACK) {
                seen = a.op[i].r == rr && a.op[i].th == rr / 6;
            }
            dash = dash || (a.op[i].kind == DCO_TEXT_C && strcmp(a.op[i].text, "--") == 0);
        }
        CHECK(seen && dash);
    }
    // la pantalla de caidas sin controlador, en la caja de la app, fijada
    for (int li = 0; li < FLEX_DC_LANGS; li++) {
        s_k = 6;
        flex_dc_ctx_t c = ctx_for(480, 736, li);
        flex_dc_fall_unavailable(&a, &c);
        sb_t e = (sb_t){s_exp, sizeof(s_exp), 0};
        s_exp[0] = 0;
        expand(&a, &e);
        unsigned crc = crc_of(s_exp);
        if (crc != GOLD_FALL[li]) fprintf(stderr, "  GOLD_FALL li=%d crc=%uu (esperado %uu)\n", li, crc, GOLD_FALL[li]);
        CHECK(crc == GOLD_FALL[li]);
        // y los numeros que importan al usuario: la caja es (0, 96, 480, 640), el boton "Entendido" mide 200 x 46 y queda centrado y pegado al margen de abajo
        CHECK(a.nhit == 1 && a.hit[0].x1 - a.hit[0].x0 == 200 && a.hit[0].y1 - a.hit[0].y0 == 46 && a.hit[0].x0 == 140 && a.hit[0].y1 == 96 + 640 - 20);
    }
}

void test_dc(void)
{
    t_extra();
    t_extra2();
    t_screens();
    t_screens2();
    t_text();
    t_metrics();
    t_hist();
    t_diag();
}
