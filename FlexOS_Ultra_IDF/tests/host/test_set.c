// Ajustes (docs/spec/03b §11): las pantallas (lista, categoria, cabeceras), las filas con su posicion y su rango, las acciones de cada fila con sus
// efectos, los ciclos, los textos, la sesion ajustes.bin v1 y las curvas de la transicion frente a FlexOS_Ultra_AppSettings.h de Arduino.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_set_model.h"
#include "flex_test.h"

void ref_set_load_sys(const flex_set_sys_t *s);
void ref_set_load_nums(unsigned used, unsigned total, unsigned trash, unsigned long ms, unsigned free_kb, unsigned rem_kb, unsigned psram_free_mb, unsigned psram_total_mb, unsigned rem_mb);
void ref_set_store_sys(flex_set_sys_t *s);
int ref_set_home_dirty(void);
int ref_set_touch(void);
void ref_set_clear_flags(void);
int ref_set_detail(int cat, int scroll, char *log, int cap, int *rowN, int *y0, int *y1, int *contentH);
int ref_set_list(int scroll, char *log, int cap, int *listH);
int ref_set_heads(int cat, char *log, int cap);
int ref_set_action(int cat, int idx, char *log, int cap);
int ref_set_maxscroll(int view, int contentH, int listH);
void ref_set_datetime(char *out, int n);
void ref_set_uptime(unsigned long ms, char *out, int n);
const char *ref_set_lang(int l);
int ref_set_autolock_idx(unsigned ms);
const char *ref_set_autolock_name(unsigned ms);
unsigned ref_set_autolock_norm(unsigned ms);
int ref_set_sess_save(int view, int sel, int scroll, int lscroll, unsigned char *out);
int ref_set_sess_load(const unsigned char *in, int n, int *view, int *sel, int *scroll, int *lscroll);

static uint32_t s_seed = 20260;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}
static const char *pick(const char *const *a, int n)
{
    return a[rnd() % (unsigned)n];
}

#define TXT_HI   0xF101
#define TXT_LO   0xF102
#define TXT_MUTE 0xF103

// ---- el modelo expresado en el mismo registro que los dobles de la referencia
static int expand_ops(const flex_set_page_t *p, int first, int last, int ybase, char *out, int cap, int pos)
{
    for (int i = first; i < last; i++) {
        const flex_set_op_t *o = &p->op[i];
        int y = o->y + ybase;
        int n = 0;
        switch (o->kind) {
        case FLEX_SO_CARD:
            n = snprintf(out + pos, (size_t)(cap - pos), "C%d,%d,%d,%d,%d;", o->x, y, o->w, o->h, o->r);
            break;
        case FLEX_SO_GLYPH:
            n = snprintf(out + pos, (size_t)(cap - pos), "G%d,%d,%d,%u;", o->glyph, o->x, y, (unsigned)o->color);
            break;
        case FLEX_SO_CATICON:
            n = snprintf(out + pos, (size_t)(cap - pos), "I%d,%d,%d,%d,%u;", o->glyph, o->x, y, o->w, (unsigned)o->color);
            break;
        case FLEX_SO_TEXT:
            n = snprintf(out + pos, (size_t)(cap - pos), "T%d,%d,%d,%u,%d:%s;", o->x, y, o->size, o->role == FLEX_SR_HI ? TXT_HI : o->role == FLEX_SR_LO ? TXT_LO : TXT_MUTE,
                         o->max_right, o->text);
            break;
        case FLEX_SO_TEXT_R:
            n = snprintf(out + pos, (size_t)(cap - pos), "R%d,%d,%d,%u:%s;", o->x, y, o->size, o->role == FLEX_SR_HI ? TXT_HI : o->role == FLEX_SR_LO ? TXT_LO : TXT_MUTE, o->text);
            break;
        case FLEX_SO_CHEV:
            n = snprintf(out + pos, (size_t)(cap - pos), "S%d,%d,%d,%d,2.0,%u;S%d,%d,%d,%d,2.0,%u;", o->x - 3, y - 6, o->x + 3, y, TXT_MUTE, o->x + 3, y, o->x - 3, y + 6, TXT_MUTE);
            break;
        case FLEX_SO_BACK:
            n = snprintf(out + pos, (size_t)(cap - pos), "S30,46,18,58,2.6,%u;S18,58,30,70,2.6,%u;", TXT_HI, TXT_HI);
            break;
        }
        pos += n;
    }
    return pos;
}

static void rand_str(char *dst, size_t n, const char *const *pool, int np)
{
    snprintf(dst, n, "%s", pick(pool, np));
}

static void rand_sys(flex_set_sys_t *s)
{
    static const char *const names[] = {"FlexOS Ultra", "Mi placa", "x", "", "Equipo de Ra\xC3\xBAl"};
    static const char *const ntp[] = {"Sincronizado", "Sin conexion", "Esperando red", "Error de hora", ""};
    static const char *const last[] = {"Nunca", "Hoy 10:32", "Ayer", "hace 3 dias"};
    static const char *const ota[] = {"El sistema esta al dia", "Buscando actualizaciones...", "1.2.3 disponible", "No disponible", "Buscar actualizacion"};
    static const char *const acc[] = {"Sin cuenta vinculada", "ana@flex.cloud", "Vinculacion en curso", "ana@flex.cloud \xC2\xB7 sin conexi\xC3\xB3n"};
    static const char *const wifi[] = {"(No disponible)", "(Desactivado)", "(Casa)", "Conectado", "(Conectando...)", "()", "(", ")", "(a)"};
    static const char *const ble[] = {"(No disponible)", "(Visible como \"FlexOS\")", "(Desactivado)", "Modo avion"};
    static const char *const theft[] = {"Desactivada", "Monitoreando movimiento\xE2\x80\xA6", "Protecci\xC3\xB3n pausada", "Bloqueado por seguridad"};
    static const char *const aerr[] = {"Codec no encontrado", "Sin I2S", ""};
    memset(s, 0, sizeof(*s));
    s->lang = (int)(rnd() % 6);
    s->h24 = rnd() & 1;
    s->bright = 5 + (int)(rnd() % 96);
    if (rnd() % 3 == 0) s->bright = 25 * (int)(1 + rnd() % 4);
    s->glass = rnd() & 1;
    s->dark = rnd() & 1;
    s->navmode = (int)(rnd() % 2);
    s->lockwidgets = (int)(rnd() % 16);
    s->iconstyle = (int)(rnd() % 2);
    s->animstyle = (int)(rnd() % 3);
    s->kbsize = (int)(rnd() % 3);
    s->locktype = (int)(rnd() % 3);
    static const uint32_t al[] = {30000, 60000, 300000, 600000, 1800000, 0, 120000, 45000};
    s->autolock_ms = al[rnd() % 8];
    s->poffpin = rnd() & 1;
    s->airplane = rnd() & 1;
    s->ble_enabled = rnd() & 1;
    rand_str(s->name, sizeof(s->name), names, 5);
    s->rtc_d = 1 + (int)(rnd() % 31);
    s->rtc_mo = 1 + (int)(rnd() % 12);
    s->rtc_y = 2020 + (int)(rnd() % 20);
    s->rtc_h = (int)(rnd() % 24);
    s->rtc_min = (int)(rnd() % 60);
    s->time_nvs_ok = rnd() & 1;
    s->ntp_busy = rnd() & 1;
    s->ntp_available = true;   // Arduino siempre tiene servicio de hora
    rand_str(s->ntp_state, sizeof(s->ntp_state), ntp, 5);
    rand_str(s->ntp_last, sizeof(s->ntp_last), last, 4);
    rand_str(s->ota_text, sizeof(s->ota_text), ota, 5);
    rand_str(s->account_text, sizeof(s->account_text), acc, 4);
    rand_str(s->wifi_sub, sizeof(s->wifi_sub), wifi, 9);
    rand_str(s->ble_sub, sizeof(s->ble_sub), ble, 4);
    rand_str(s->theft, sizeof(s->theft), theft, 4);
    s->audio_avail = rnd() & 1;
    s->volume = (int)(rnd() % 101);
    s->muted = rnd() & 1;
    rand_str(s->audio_error, sizeof(s->audio_error), aerr, 3);
    s->fs_ready = rnd() % 4 != 0;
}

// los numeros que el modelo recibe formateados ("B<n>") y la actividad / memoria
static void set_nums(flex_set_sys_t *s)
{
    unsigned used = rnd() % 100000, total = rnd() % 100000, trash = rnd() % 5000;
    unsigned long ms = (unsigned long)(rnd() % 90) * 3600000UL * (rnd() % 7 == 0 ? 24 : 1) + rnd() % 3600000UL;
    unsigned free_kb = rnd() % 20000, rem_kb = rnd() % 1024, pf = rnd() % 33, pt = 8 + rnd() % 33, rem_mb = rnd() % 1048576;
    ref_set_load_nums(used, total, trash, ms, free_kb, rem_kb, pf, pt, rem_mb);
    snprintf(s->fs_used, sizeof(s->fs_used), "B%u", used);
    snprintf(s->fs_total, sizeof(s->fs_total), "B%u", total);
    snprintf(s->trash_size, sizeof(s->trash_size), "B%u", trash);
    flex_set_uptime(ms, s->uptime, sizeof(s->uptime));
    s->free_kb = free_kb;
    s->psram_free_mb = pf;
    s->psram_total_mb = pt;
}

static char RB[1 << 17], MB[1 << 17];

// efectos del doble -> fichas ordenadas; lo mismo desde las banderas del modelo
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int tokens(const char *log, char out[][32], int max)
{
    int n = 0;
    const char *p = log;
    while (*p && n < max) {
        const char *e = strchr(p, ';');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l > 31) l = 31;
        memcpy(out[n], p, l);
        out[n][l] = 0;
        n++;
        p = e ? e + 1 : p + strlen(p);
    }
    return n;
}
static void sort_tokens(char t[][32], int n)
{
    char *ptr[16];
    char tmp[16][32];
    for (int i = 0; i < n; i++) ptr[i] = t[i];
    qsort(ptr, (size_t)n, sizeof(ptr[0]), cmp_str);
    for (int i = 0; i < n; i++) memcpy(tmp[i], ptr[i], 32);
    for (int i = 0; i < n; i++) memcpy(t[i], tmp[i], 32);
}

void test_set(void)
{
    int bad = 0, details = 0, lists = 0, actions = 0, effects = 0, helpers = 0;
    for (int it = 0; it < 1500; it++) {
        flex_set_sys_t s;
        rand_sys(&s);
        set_nums(&s);
        ref_set_load_sys(&s);

        // ---- pagina de detalle de cada categoria, con scroll
        for (int cat = 0; cat < FLEX_SET_NCAT; cat++) {
            int scroll = (rnd() % 3 == 0) ? 0 : (int)(rnd() % 300);
            int rowN, y0[16], y1[16], ch;
            ref_set_detail(cat, scroll, RB, sizeof(RB), &rowN, y0, y1, &ch);
            static flex_set_page_t pg;
            flex_set_detail_build(&pg, cat, &s);
            int base = FLEX_SET_DLIST_TOP - scroll;
            expand_ops(&pg, 0, pg.n, base, MB, sizeof(MB), 0);
            MB[strlen(MB)] = 0;
            bad += strcmp(RB, MB) != 0;
            bad += rowN != pg.rows || ch != pg.content_h;
            for (int i = 0; i < rowN && i < pg.rows; i++) bad += y0[i] != pg.row_y0[i] + base || y1[i] != pg.row_y1[i] + base;
            bad += pg.n >= FLEX_SET_OPS_MAX;
            if (bad) {
                size_t k = 0;
                while (RB[k] && RB[k] == MB[k]) k++;
                printf("SET detail cat %d scroll %d rows %d/%d content %d/%d first diff at %zu\n ref: %.160s\n mdl: %.160s\n", cat, scroll, rowN, pg.rows, ch, pg.content_h, k, RB + (k > 40 ? k - 40 : 0), MB + (k > 40 ? k - 40 : 0));
                break;
            }
            details++;
            // la cabecera de la categoria y la de la lista
            static flex_set_page_t hd, lh;
            flex_set_detail_head(&hd, cat);
            flex_set_list_head(&lh);
            int pos = expand_ops(&lh, 0, lh.n, 0, MB, sizeof(MB), 0);
            MB[pos++] = '|';
            MB[pos] = 0;
            pos = expand_ops(&hd, 0, hd.n, 0, MB, sizeof(MB), pos);
            MB[pos] = 0;
            ref_set_heads(cat, RB, sizeof(RB));
            if (strcmp(RB, MB) != 0) printf("SET heads cat %d\n ref: %.200s\n mdl: %.200s\n", cat, RB, MB);
            bad += strcmp(RB, MB) != 0;
        }
        // ---- la lista de categorias con scroll: las tarjetas fuera de la ventana no se dibujan
        {
            int scroll = (int)(rnd() % 300);
            int listH;
            ref_set_list(scroll, RB, sizeof(RB), &listH);
            static flex_set_page_t lp;
            flex_set_list_build(&lp);
            int pos = 0;
            MB[0] = 0;
            for (int i = 0; i < FLEX_SET_NCAT; i++) {
                if (!flex_set_card_visible(i, scroll)) continue;
                int end = (i + 1 < FLEX_SET_NCAT) ? lp.card_op[i + 1] : lp.n;
                pos = expand_ops(&lp, lp.card_op[i], end, FLEX_SET_LIST_TOP - scroll, MB, sizeof(MB), pos);
            }
            MB[pos] = 0;
            if (strcmp(RB, MB) != 0 || listH != lp.content_h) printf("SET list scroll %d H %d/%d\n ref: %.300s\n mdl: %.300s\n", scroll, listH, lp.content_h, RB, MB);
            bad += strcmp(RB, MB) != 0 || listH != lp.content_h;
            lists++;
        }
        // ---- acciones de fila: el estado y los efectos
        for (int k = 0; k < 6; k++) {
            static const int acats[5] = {0, 1, 3, 5, 6};
            int cat = (rnd() % 5 < 4) ? acats[rnd() % 5] : (int)(rnd() % FLEX_SET_NCAT), idx = (int)(rnd() % 12);
            flex_set_sys_t before = s;
            ref_set_clear_flags();
            ref_set_action(cat, idx, RB, sizeof(RB));
            flex_set_act_t act;
            flex_set_row_action(&s, cat, idx, &act);
            flex_set_sys_t r = before;
            ref_set_store_sys(&r);
            if (r.lang != s.lang || r.h24 != s.h24 || r.bright != s.bright || r.glass != s.glass || r.dark != s.dark || r.navmode != s.navmode || r.lockwidgets != s.lockwidgets || r.iconstyle != s.iconstyle || r.animstyle != s.animstyle || r.autolock_ms != s.autolock_ms || r.poffpin != s.poffpin) printf("SET action state cat %d idx %d\n", cat, idx);
            bad += r.lang != s.lang || r.h24 != s.h24 || r.bright != s.bright || r.glass != s.glass || r.dark != s.dark || r.navmode != s.navmode ||
                   r.lockwidgets != s.lockwidgets || r.iconstyle != s.iconstyle || r.animstyle != s.animstyle || r.autolock_ms != s.autolock_ms ||
                   r.poffpin != s.poffpin;
            // fichas esperadas
            char exp[16][32], got[16][32];
            int ne = 0;
            uint32_t savemask = FLEX_SF_SAVE_LANG | FLEX_SF_SAVE_H24 | FLEX_SF_SAVE_BRIGHT | FLEX_SF_SAVE_LOCKW | FLEX_SF_SAVE_NAV | FLEX_SF_SAVE_ICON |
                                FLEX_SF_SAVE_ANIM | FLEX_SF_SAVE_AUTOLOCK | FLEX_SF_SAVE_POFFPIN;
            if (act.flags & savemask) snprintf(exp[ne++], 32, "SAVE");
            if (act.flags & FLEX_SF_THEME) snprintf(exp[ne++], 32, "THEME");
            if (act.flags & FLEX_SF_BACKLIGHT) snprintf(exp[ne++], 32, "BACKLIGHT%d", s.bright);
            if (act.render == FLEX_SRD_BAND) snprintf(exp[ne++], 32, "BAND");
            if (act.render == FLEX_SRD_FULL) snprintf(exp[ne++], 32, "RENDER");
            static const char *const open_name[] = {"", "NTP", "OTA", "FACTORY", "ACCOUNT", "CONN", "WIFI", "BLE", "KEYBOARD", "LOCKSETUP", "THEFT"};
            if (act.open) snprintf(exp[ne++], 32, "%s", open_name[act.open]);
            int ng = tokens(RB, got, 16);
            sort_tokens(exp, ne);
            sort_tokens(got, ng);
            int tok_bad = ne != ng;
            for (int i = 0; !tok_bad && i < ne; i++) tok_bad = strcmp(exp[i], got[i]) != 0;
            bad += tok_bad;
            if ((act.flags & FLEX_SF_HOME_DIRTY ? 1 : 0) != ref_set_home_dirty()) printf("SET home dirty cat %d idx %d\n", cat, idx);
            bad += (act.flags & FLEX_SF_HOME_DIRTY ? 1 : 0) != ref_set_home_dirty();
            // el temporizador de inactividad se reinicia con el reloj de la referencia (millis fijado por set_nums)
            if (((act.flags & FLEX_SF_TOUCH_TIMER) != 0) != (ref_set_touch() != 0)) printf("SET touch timer cat %d idx %d millis %d\n", cat, idx, ref_set_touch());
            bad += ((act.flags & FLEX_SF_TOUCH_TIMER) != 0) != (ref_set_touch() != 0);
            if (tok_bad && effects == 0) printf("SET action cat %d idx %d ref=[%s]\n", cat, idx, RB);
            actions++;
            effects += act.flags != 0 || act.open != 0;
            ref_set_load_sys(&s);   // la referencia sigue el estado del modelo para la siguiente
        }
        // ---- ayudas de texto
        {
            char a[64], b[64];
            ref_set_datetime(a, sizeof(a));
            flex_set_datetime_str(b, sizeof(b), s.rtc_d, s.rtc_mo, s.rtc_y, s.rtc_h, s.rtc_min);
            bad += strcmp(a, b) != 0;
            unsigned long ms = (unsigned long)rnd() * (rnd() % 3 + 1) / 3;
            ref_set_uptime(ms, a, sizeof(a));
            flex_set_uptime(ms, b, sizeof(b));
            bad += strcmp(a, b) != 0;
            bad += strcmp(ref_set_lang(s.lang), flex_set_lang_name(s.lang)) != 0;
            unsigned alms = (rnd() % 3) ? flex_set_autolock_ms[rnd() % 6] : rnd() % 3000000;
            bad += ref_set_autolock_idx(alms) != flex_set_autolock_idx(alms) || strcmp(ref_set_autolock_name(alms), flex_set_autolock_name[flex_set_autolock_idx(alms)]) != 0 ||
                   ref_set_autolock_norm(alms) != flex_set_autolock_norm(alms);
            int view = (int)(rnd() % 2), ch2 = (int)(rnd() % 1500), lh = (int)(rnd() % 1500);
            bad += ref_set_maxscroll(view, ch2, lh) != flex_set_max_scroll(view == 0 ? lh : ch2, view == 0);
            if (bad) printf("SET helpers: datetime/uptime/lang/autolock/maxscroll\n");
            helpers++;
        }
        // ---- sesion (con el almacenamiento montado: sin el, Arduino ni lee ni escribe)
        {
            s.fs_ready = true;
            ref_set_load_sys(&s);
            int view = (int)(rnd() % 4) - 1, sel = (int)(rnd() % 16) - 2, sc = (int)(rnd() % 3000) - 200, ls = (int)(rnd() % 3000) - 200;
            unsigned char ref[16];
            int rn = ref_set_sess_save(view, sel, sc, ls, ref);
            flex_set_sess_t ss;
            flex_set_sess_pack(&ss, view, sel, sc, ls);
            bad += rn != (int)sizeof(ss) || memcmp(ref, &ss, sizeof(ss)) != 0 || sizeof(flex_set_sess_t) != 8;
            unsigned char raw[8];
            for (int i = 0; i < 8; i++) raw[i] = (unsigned char)(rnd() % 256);
            if (rnd() % 2) { raw[2] = (unsigned char)(rnd() % 14); raw[3] = 0; }   // sel en rango muchas veces
            if (rnd() % 2) { raw[0] = (unsigned char)(rnd() % 3); raw[1] = 0; }
            int rv, rs, rsc, rls;
            int loaded = ref_set_sess_load(raw, 8, &rv, &rs, &rsc, &rls);
            flex_set_sess_t m;
            memcpy(&m, raw, 8);
            int ok = flex_set_sess_normalize(&m);
            bad += loaded != ok;
            if (ok) bad += rv != m.view || rs != m.sel || rsc != m.scroll || rls != m.list_scroll;
        }
        if (bad) {
            printf("SET diverged at iteration %d\n", it);
            break;
        }
    }
    CHECK(bad == 0 && details == 1500 * 12 && actions == 9000 && effects > 2500);

    // ---- sin servicio de hora (aqui aun no esta migrado): la fila lo dice y tocarla no hace nada
    {
        flex_set_sys_t z;
        memset(&z, 0, sizeof(z));
        static flex_set_page_t pg;
        flex_set_detail_build(&pg, 0, &z);
        int found = 0;
        for (int i = 0; i < pg.n; i++) {
            found += pg.op[i].kind == FLEX_SO_TEXT && !strcmp(pg.op[i].text, "No disponible");
        }
        CHECK(found == 1);
        flex_set_act_t a;
        flex_set_row_action(&z, 0, 6, &a);
        CHECK(a.flags == 0 && a.open == FLEX_SX_NONE && a.render == FLEX_SRD_NONE);
        z.ntp_available = true;
        flex_set_row_action(&z, 0, 6, &a);
        CHECK(a.open == FLEX_SX_NTP_SYNC && a.render == FLEX_SRD_BAND);
    }
    // ---- la transicion (formulas de la especificacion y de settingsAnimate): sin referencia ejecutable, por sus valores
    {
        int o, in;
        flex_set_anim_offsets(+1, 0, &o, &in);
        CHECK(o == 0 && in == 480);                                 // abrir: la lista en su sitio, la categoria fuera por la derecha
        flex_set_anim_offsets(+1, FLEX_SET_NAV_MS, &o, &in);
        CHECK(o == -134 && in == 0);                                // abrir al final: la lista 28 % a la izquierda, la categoria en su sitio
        flex_set_anim_offsets(-1, 0, &o, &in);
        CHECK(o == 0 && in == -134);                                // volver: la categoria en su sitio, la lista 28 % a la izquierda
        flex_set_anim_offsets(-1, FLEX_SET_NAV_MS, &o, &in);
        CHECK(o == 480 && in == 0);
        flex_set_anim_offsets(+1, 5000, &o, &in);
        CHECK(o == -134 && in == 0);                                // el tiempo no pasa de 270 ms
        flex_set_anim_offsets(+1, FLEX_SET_NAV_MS / 2, &o, &in);
        CHECK(o == -67 && in == 240);                               // la mitad: ease-in-out simetrica
        float prev = -1.0f;
        int mono = 1;
        for (int ms = 0; ms <= FLEX_SET_NAV_MS; ms += 10) {
            float e = flex_set_ease((float)ms / FLEX_SET_NAV_MS);
            mono &= e >= prev && e >= 0.0f && e <= 1.0f;
            prev = e;
        }
        CHECK(mono && fabsf(flex_set_ease(0.25f) - 0.0625f) < 1e-6f && fabsf(flex_set_ease(0.75f) - 0.9375f) < 1e-6f);
        CHECK(flex_set_shadow_alpha(1) == 62 && flex_set_shadow_alpha(8) == 6);
        // el gesto de volver: se arma pegado al borde y se decide al soltar
        CHECK(flex_set_back_swipe_armed(0) && flex_set_back_swipe_armed(27) && !flex_set_back_swipe_armed(28));
        CHECK(flex_set_back_swipe_done(10, 300, 81, 300) && !flex_set_back_swipe_done(10, 300, 80, 300) && flex_set_back_swipe_done(10, 300, 200, 389) &&
              !flex_set_back_swipe_done(10, 300, 200, 390) && !flex_set_back_swipe_done(10, 300, 200, 210) && flex_set_back_swipe_done(10, 300, 200, 211));
    }
    printf("ajustes: pantallas, filas, acciones, ciclos, textos y sesion iguales a Arduino (%d pantallas, %d listas, %d acciones, %d con efecto, %d ayudas)\n", details, lists,
           actions, effects, helpers);
}
