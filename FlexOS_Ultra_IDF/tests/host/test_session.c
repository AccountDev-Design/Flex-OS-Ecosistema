// Sesiones de app (docs/spec/01c §14): formato del archivo (cabecera + CRC-32) y guardado diferido,
// comparados con FlexOS_Ultra_Session.h / AppFramework.h extraidos.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "flex_session_model.h"
#include "flex_test.h"

int ref_sess_write(uint16_t ver, uint16_t app, const void *d, size_t n, uint8_t *out);
void ref_sess_file(const uint8_t *buf, uint32_t sz);
int ref_sess_read(uint16_t ver, uint16_t app, void *out, size_t max);
uint32_t ref_crc32(const uint8_t *d, size_t n);
void ref_sd_reset(const int *mode);
int ref_sd_op(int op, int a, int b, uint32_t now, bool finger, bool reset_pending, int *dirty_app, uint32_t *need_mask);
int ref_sd_calls(int *out);
void ref_sd_cur(int a);

static uint32_t s_seed = 9001;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

// ---- guardado diferido: el gancho de cada app ------------------------------------------------
static int g_mode[19];
static int g_calls[256], g_ncalls;
static int my_save(void *ctx, int app)
{
    (void)ctx;
    if (g_mode[app] == 0) {
        return FLEX_SESS_NOHOOK;
    }
    g_calls[g_ncalls++ & 255] = app;
    return g_mode[app] == 1 ? FLEX_SESS_OK : FLEX_SESS_FAIL;
}

void test_session(void)
{
    // 1. CRC-32: el vector de zlib y buffers al azar
    CHECK_EQ_I(flex_crc32u((const uint8_t *)"123456789", 9, 0), 0xCBF43926u);
    int bad = 0;
    for (int it = 0; it < 2000; it++) {
        uint8_t b[300];
        size_t n = rnd() % 300;
        for (size_t i = 0; i < n; i++) b[i] = (uint8_t)rnd();
        bad += flex_crc32u(b, n, 0) != ref_crc32(b, n);
    }
    CHECK_EQ_I(bad, 0);

    // 2. Formato: lo que escribe una implementacion lo lee la otra, y los archivos danados se rechazan igual
    int wd = 0, rd = 0, accepted = 0;
    for (int it = 0; it < 20000; it++) {
        uint8_t data[1100], rfile[2048], mfile[2048], out_r[1100], out_m[1100];
        size_t len = rnd() % 4 == 0 ? 990 + rnd() % 110 : rnd() % 200;   // tambien en el borde de 1008 B
        for (size_t i = 0; i < len; i++) data[i] = (uint8_t)rnd();
        uint16_t ver = (uint16_t)(1 + rnd() % 3), app = (uint16_t)(rnd() % 19);
        int rn = ref_sess_write(ver, app, data, len, rfile);
        size_t mn = flex_sess_pack(mfile, sizeof(mfile), ver, app, data, len);
        if (rn < 0 ? mn != 0 : ((size_t)rn != mn || memcmp(rfile, mfile, mn))) {
            wd++;
            continue;
        }
        if (mn == 0) continue;
        // estropear: byte alterado, truncado, otra version u otra app, tamano de mas
        size_t sz = mn;
        uint16_t wv = ver, wa = app;
        switch (rnd() % 6) {
        case 0: break;
        case 1: mfile[rnd() % sz] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 2: sz = rnd() % sz; break;
        case 3: wv = (uint16_t)(ver + 1); break;
        case 4: wa = (uint16_t)(app + 1); break;
        case 5: mfile[sz] = 0; sz++; break;
        }
        size_t max = rnd() % 8 == 0 ? len / 2 : 1008;
        ref_sess_file(mfile, (uint32_t)sz);
        int rr = ref_sess_read(wv, wa, out_r, max);
        size_t mr = flex_sess_unpack(mfile, sz, wv, wa, out_m, max);
        if ((size_t)rr != mr || (mr && memcmp(out_r, out_m, mr))) rd++;
        accepted += mr > 0;
    }
    CHECK_EQ_I(wd, 0);
    CHECK_EQ_I(rd, 0);
    CHECK(accepted > 1500);

    // 3. Guardado diferido: marcar, tick, vaciar y terminar con ganchos que fallan o no existen
    int dd = 0, refused = 0, saved_total = 0;
    for (int it = 0; it < 6000; it++) {
        int mode[19];
        for (int i = 0; i < 19; i++) mode[i] = (int)(rnd() % 3);
        memcpy(g_mode, mode, sizeof(mode));
        ref_sd_reset(mode);
        g_ncalls = 0;
        flex_sess_t st;
        flex_sess_init(&st);
        uint32_t now = 1000 + rnd() % 500;
        for (int k = 0; k < 40; k++) {
            now += rnd() % 3 == 0 ? rnd() % 40000 : rnd() % 900;
            int op = (int)(rnd() % 4), a = (int)(rnd() % 6), b = (int)(rnd() % 2);   // pocas apps: se pisan
            bool finger = rnd() % 5 == 0, rst = rnd() % 12 == 0;
            ref_sd_cur(a);
            int rdirty;
            uint32_t rneed;
            int rr = ref_sd_op(op, a, b, now, finger, rst, &rdirty, &rneed);
            int mr = 0;
            switch (op) {
            case 0: flex_sess_mark_dirty(&st, a, now, my_save, NULL); break;
            case 1: flex_sess_tick(&st, now, finger || rst, my_save, NULL); break;
            case 2: flex_sess_flush_now(&st, my_save, NULL); break;
            case 3: mr = flex_sess_terminate_begin(&st, a, b != 0, my_save, NULL); break;
            }
            uint32_t mneed = 0;
            for (int i = 0; i < 19; i++) if (st.need[i]) mneed |= 1u << i;
            int rc[256], rn = ref_sd_calls(rc);
            bool same_calls = rn == g_ncalls;
            for (int i = 0; same_calls && i < rn; i++) same_calls = rc[i & 255] == g_calls[i & 255];
            if (rdirty != st.dirty_app || rneed != mneed || !same_calls || (op == 3 && rr != mr)) {
                dd++;
                break;
            }
            refused += op == 3 && !mr;
        }
        saved_total += g_ncalls;
    }
    if (dd) fprintf(stderr, "sesiones: %d guiones distintos de la version Arduino\n", dd);
    CHECK_EQ_I(dd, 0);
    CHECK(refused > 200 && saved_total > 10000);   // se ejercito el "no se cierra si no se pudo guardar"
    printf("sesiones: formato y guardado diferido iguales a Arduino (%d cierres rechazados, %d volcados)\n", refused,
           saved_total);
}
