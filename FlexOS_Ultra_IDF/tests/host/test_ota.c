// OTA (N5a): el modelo y el motor frente al FlexOS_OTA.cpp de Arduino compilado tal cual (micro-lector JSON, versiones, MD5 utilizable, textos, otaCheck y
// otaInstall con la red, la flash, el reloj y las particiones sustituidos por UN mundo simulado y copiable: el servicio se ejecuta primero, el mundo se restaura y
// despues se ejecuta Arduino sobre la misma situacion), en las cuatro configuraciones del preprocesador de Arduino (CA fijado, firma obligatoria + antirretroceso,
// sin CA, sin CA con TLS sin verificar). Y lo que Arduino no tiene o cambia a proposito: la clave de manifiesto propia, la API de la interfaz.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flex_ota.h"
#include "flex_test.h"

// ---- el doble de Arduino, una copia por configuracion ----
#define DECL(v)                                                                                                                                              \
    void ref_ota_reset_##v(void);                                                                                                                            \
    void ref_ota_world_##v(unsigned, int, int);                                                                                                              \
    void ref_ota_http_##v(int, int, int, int, int, int);                                                                                                     \
    void ref_ota_body_##v(const unsigned char *, int);                                                                                                       \
    void ref_ota_sched_##v(int, const unsigned *, const unsigned *);                                                                                         \
    void ref_ota_upd_##v(int, int, int, int, int);                                                                                                           \
    void ref_ota_set_time_##v(unsigned);                                                                                                                     \
    void ref_ota_snapshot_##v(void);                                                                                                                         \
    void ref_ota_restore_##v(void);                                                                                                                          \
    void ref_ota_prime_##v(const char *, const char *, const char *, unsigned);                                                                              \
    int ref_ota_check_##v(void);                                                                                                                             \
    int ref_ota_install_##v(void);                                                                                                                           \
    void ref_ota_ard_##v(int *, int *, int *, unsigned *, unsigned *, unsigned *, int *, char *, char *, char *, char *, char *);                          \
    void ref_ota_effects_##v(unsigned *, int *, int *, int *, unsigned long long *, char *, int *, int *, unsigned long long *, char *, int *, int *, int *, int *, int *, char *, int *, int *, int *, int *); \
    int ref_ota_write_size_##v(int);                                                                                                                         \
    int ref_ota_md5_##v(const char *);                                                                                                                       \
    int ref_ota_ver_##v(const char *, const char *);                                                                                                         \
    int ref_ota_ver_ok_##v(const char *, unsigned short *);                                                                                                  \
    const char *ref_ota_errtext_##v(int);                                                                                                                    \
    const char *ref_ota_phasetext_##v(int);                                                                                                                  \
    const char *ref_ota_find_##v(const char *, const char *);                                                                                                \
    int ref_ota_block_##v(const char *, const char *, int *, int *);                                                                                         \
    int ref_ota_str_##v(const char *, int, char *, int);                                                                                                     \
    unsigned ref_ota_num_##v(const char *, int);                                                                                                             \
    unsigned ref_op_millis_##v(void);                                                                                                                        \
    void ref_op_delay_##v(unsigned);                                                                                                                         \
    int ref_op_net_##v(void);                                                                                                                                \
    unsigned ref_op_heap_##v(void);                                                                                                                          \
    int ref_op_partition_##v(void);                                                                                                                          \
    int ref_op_http_begin_##v(const char *, const char *, int);                                                                                              \
    int ref_op_http_get_##v(void);                                                                                                                           \
    int ref_op_http_size_##v(void);                                                                                                                          \
    int ref_op_http_available_##v(void);                                                                                                                     \
    int ref_op_http_read_##v(unsigned char *, int);                                                                                                          \
    int ref_op_http_connected_##v(void);                                                                                                                     \
    int ref_op_http_stream_##v(void);                                                                                                                        \
    void ref_op_http_end_##v(void);                                                                                                                          \
    int ref_op_upd_begin_##v(unsigned);                                                                                                                      \
    void ref_op_upd_md5_##v(const char *);                                                                                                                   \
    int ref_op_upd_write_##v(int);                                                                                                                           \
    int ref_op_cancel_raised_##v(void);                                                                                                                      \
    int ref_op_upd_end_##v(void);                                                                                                                            \
    int ref_op_upd_finished_##v(void);                                                                                                                       \
    void ref_op_upd_abort_##v(void);                                                                                                                         \
    void ref_op_lock_##v(void);                                                                                                                              \
    void ref_op_unlock_##v(void);                                                                                                                            \
    void ref_op_save_pending_##v(const char *);                                                                                                              \
    void ref_op_restart_##v(void);
DECL(v0)
DECL(v1)
DECL(v2)
DECL(v3)

typedef struct {
    void (*reset)(void);
    void (*world)(unsigned, int, int);
    void (*http)(int, int, int, int, int, int);
    void (*body)(const unsigned char *, int);
    void (*sched)(int, const unsigned *, const unsigned *);
    void (*upd)(int, int, int, int, int);
    void (*set_time)(unsigned);
    void (*snapshot)(void);
    void (*restore)(void);
    void (*prime)(const char *, const char *, const char *, unsigned);
    int (*check)(void);
    int (*install)(void);
    void (*ard)(int *, int *, int *, unsigned *, unsigned *, unsigned *, int *, char *, char *, char *, char *, char *);
    void (*effects)(unsigned *, int *, int *, int *, unsigned long long *, char *, int *, int *, unsigned long long *, char *, int *, int *, int *, int *, int *, char *, int *, int *, int *, int *);
    int (*write_size)(int);
    int (*md5)(const char *);
    int (*ver)(const char *, const char *);
    int (*ver_ok)(const char *, unsigned short *);
    const char *(*errtext)(int);
    const char *(*phasetext)(int);
    const char *(*find)(const char *, const char *);
    int (*block)(const char *, const char *, int *, int *);
    int (*str)(const char *, int, char *, int);
    unsigned (*num)(const char *, int);
    unsigned (*millis)(void);
    void (*delay)(unsigned);
    int (*net)(void);
    unsigned (*heap)(void);
    int (*partition)(void);
    int (*http_begin)(const char *, const char *, int);
    int (*http_get)(void);
    int (*http_size)(void);
    int (*http_available)(void);
    int (*http_read)(unsigned char *, int);
    int (*http_connected)(void);
    int (*http_stream)(void);
    void (*http_end)(void);
    int (*upd_begin)(unsigned);
    void (*upd_md5)(const char *);
    int (*upd_write)(int);
    int (*cancel_raised)(void);
    int (*upd_end)(void);
    int (*upd_finished)(void);
    void (*upd_abort)(void);
    void (*lock)(void);
    void (*unlock)(void);
    void (*save_pending)(const char *);
    void (*restart)(void);
} Ref;
#define FILL(v)                                                                                                                                                 \
    {ref_ota_reset_##v, ref_ota_world_##v, ref_ota_http_##v, ref_ota_body_##v, ref_ota_sched_##v, ref_ota_upd_##v, ref_ota_set_time_##v, ref_ota_snapshot_##v,   \
     ref_ota_restore_##v, ref_ota_prime_##v, ref_ota_check_##v, ref_ota_install_##v, ref_ota_ard_##v, ref_ota_effects_##v, ref_ota_write_size_##v, ref_ota_md5_##v, \
     ref_ota_ver_##v, ref_ota_ver_ok_##v, ref_ota_errtext_##v, ref_ota_phasetext_##v, ref_ota_find_##v, ref_ota_block_##v, ref_ota_str_##v, ref_ota_num_##v,        \
     ref_op_millis_##v, ref_op_delay_##v, ref_op_net_##v, ref_op_heap_##v, ref_op_partition_##v, ref_op_http_begin_##v, ref_op_http_get_##v, ref_op_http_size_##v,  \
     ref_op_http_available_##v, ref_op_http_read_##v, ref_op_http_connected_##v, ref_op_http_stream_##v, ref_op_http_end_##v, ref_op_upd_begin_##v,              \
     ref_op_upd_md5_##v, ref_op_upd_write_##v, ref_op_cancel_raised_##v, ref_op_upd_end_##v, ref_op_upd_finished_##v, ref_op_upd_abort_##v, ref_op_lock_##v,      \
     ref_op_unlock_##v, ref_op_save_pending_##v, ref_op_restart_##v}
static const Ref R[4] = {FILL(v0), FILL(v1), FILL(v2), FILL(v3)};
static int CV;   // la configuracion en uso

// ---- las operaciones del servicio, sobre el mundo de la configuracion en uso ----
static uint32_t o_millis(void) { return R[CV].millis(); }
static void o_delay(uint32_t ms) { R[CV].delay(ms); }
static bool o_net(void) { return R[CV].net() != 0; }
static uint32_t o_heap(void) { return R[CV].heap(); }
static bool o_part(void) { return R[CV].partition() != 0; }
static bool o_hbegin(const char *u, const char *ca, bool ins, uint32_t t) { (void)t; return R[CV].http_begin(u, ca, ins) != 0; }
static int o_hget(void) { return R[CV].http_get(); }
static int o_hsize(void) { return R[CV].http_size(); }
static int o_havail(void) { return R[CV].http_available(); }
static int o_hread(uint8_t *b, size_t n) { return R[CV].http_read(b, (int)n); }
static bool o_hconn(void) { return R[CV].http_connected() != 0; }
static bool o_hstream(void) { return R[CV].http_stream() != 0; }
static void o_hend(void) { R[CV].http_end(); }
static bool o_ubegin(uint32_t t) { return R[CV].upd_begin(t) != 0; }
static void o_umd5(const char *m) { R[CV].upd_md5(m); }
static size_t o_uwrite(const uint8_t *d, size_t n)
{
    (void)d;
    size_t w = (size_t)R[CV].upd_write((int)n);
    if (R[CV].cancel_raised()) {
        flex_ota_cancel();   // el usuario pulso "Cancelar" a mitad de la descarga
    }
    return w;
}
static bool o_uend(void) { return R[CV].upd_end() != 0; }
static bool o_ufin(void) { return R[CV].upd_finished() != 0; }
static void o_uabort(void) { R[CV].upd_abort(); }
static void o_lock(void) { R[CV].lock(); }
static void o_unlock(void) { R[CV].unlock(); }
static void o_savep(const char *v) { R[CV].save_pending(v); }
static void o_restart(void) { R[CV].restart(); }
static flex_ota_ops_t make_ops(void)
{
    flex_ota_ops_t o;
    memset(&o, 0, sizeof(o));
    o.millis = o_millis; o.delay_ms = o_delay; o.net_ready = o_net; o.free_heap = o_heap; o.next_partition = o_part;
    o.http_begin = o_hbegin; o.http_get = o_hget; o.http_size = o_hsize; o.http_available = o_havail; o.http_read = o_hread; o.http_connected = o_hconn;
    o.http_stream = o_hstream; o.http_end = o_hend; o.upd_begin = o_ubegin; o.upd_set_md5 = o_umd5; o.upd_write = o_uwrite; o.upd_end = o_uend;
    o.upd_finished = o_ufin; o.upd_abort = o_uabort; o.panel_lock = o_lock; o.panel_unlock = o_unlock; o.save_pending = o_savep; o.restart = o_restart;
    return o;
}
static flex_ota_cfg_t make_cfg(int v)
{
    flex_ota_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.local_version = "1.0.0";
    c.manifest_url = "https://raw.githubusercontent.com/AccountDev-Design/Flex-OS-Ecosistema/main/ota/manifest.json";
    c.key = "flex_os_ultra";   // la de Arduino: lo que se compara es el motor; la clave propia de IDF se prueba aparte
    c.root_ca = (v == 0 || v == 1) ? "PEM-DE-PRUEBA" : "";
    c.allow_insecure = v == 3;
    c.require_signature = v == 1;
    c.antirollback = v == 1;
    c.min_heap = 56000u;
    return c;
}
static void service_setup(int v)
{
    CV = v;
    flex_ota_cfg_t c = make_cfg(v);
    flex_ota_ops_t o = make_ops();
    flex_ota_set(&c, &o);
    flex_ota_service_reset();
}

static uint32_t s_seed = 1;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}

// ---- comparacion de un resultado ----
typedef struct {
    int state, error, pct, has;
    unsigned done, total, bps;
    char remote[16], url[192], md5[36], log[320], sig[132];
    unsigned ms;
    int restarts, aborts, begins, md5_set, nwrites, pend_set, ends, gets, ca, insecure, locks, unlocks, end_calls, begin_calls;
    unsigned long long begin_total, written;
    char md5set[48], pend[48], last_url[300];
    int wsz[24];
} Res;

static void take_ard(const Ref *r, Res *o)
{
    memset(o, 0, sizeof(*o));
    r->ard(&o->state, &o->error, &o->pct, &o->done, &o->total, &o->bps, &o->has, o->remote, o->url, o->md5, o->log, o->sig);
    r->effects(&o->ms, &o->restarts, &o->aborts, &o->begins, &o->begin_total, o->md5set, &o->md5_set, &o->nwrites, &o->written, o->pend, &o->pend_set, &o->ends, &o->gets, &o->ca,
               &o->insecure, o->last_url, &o->locks, &o->unlocks, &o->end_calls, &o->begin_calls);
    for (int i = 0; i < 24; i++) o->wsz[i] = r->write_size(i);
}
static void take_svc(const Ref *r, Res *o)
{
    memset(o, 0, sizeof(*o));
    flex_ota_info_t i;
    flex_ota_get(&i);
    flex_ota_manifest_t m;
    flex_ota_manifest_copy(&m);
    o->state = i.state; o->error = i.error; o->pct = i.percent; o->has = i.has_update; o->done = i.done; o->total = i.total; o->bps = i.bps;
    memcpy(o->remote, m.ver, sizeof(o->remote)); memcpy(o->url, m.url, sizeof(o->url)); memcpy(o->md5, m.md5, sizeof(o->md5)); memcpy(o->log, m.changelog, sizeof(o->log));
    memcpy(o->sig, m.sig, sizeof(o->sig));
    char dummy_remote[16], dummy_url[192], dummy_md5[36], dummy_log[320], dummy_sig[132];
    (void)dummy_remote; (void)dummy_url; (void)dummy_md5; (void)dummy_log; (void)dummy_sig;
    r->effects(&o->ms, &o->restarts, &o->aborts, &o->begins, &o->begin_total, o->md5set, &o->md5_set, &o->nwrites, &o->written, o->pend, &o->pend_set, &o->ends, &o->gets, &o->ca,
               &o->insecure, o->last_url, &o->locks, &o->unlocks, &o->end_calls, &o->begin_calls);
    for (int i2 = 0; i2 < 24; i2++) o->wsz[i2] = r->write_size(i2);
}
static bool same(const char *tag, int it, int v, const Res *a, const Res *s)
{
    bool ok = a->state == s->state && a->error == s->error && a->pct == s->pct && a->has == s->has && a->done == s->done && a->total == s->total && a->bps == s->bps &&
              !strcmp(a->remote, s->remote) && !strcmp(a->url, s->url) && !strcmp(a->md5, s->md5) && !strcmp(a->log, s->log) && !strcmp(a->sig, s->sig) && a->ms == s->ms &&
              a->restarts == s->restarts && a->aborts == s->aborts && a->begins == s->begins && a->md5_set == s->md5_set && a->nwrites == s->nwrites && a->pend_set == s->pend_set &&
              a->ends == s->ends && a->gets == s->gets && a->ca == s->ca && a->insecure == s->insecure && a->locks == s->locks && a->unlocks == s->unlocks &&
              a->end_calls == s->end_calls && a->begin_calls == s->begin_calls && a->begin_total == s->begin_total && a->written == s->written && !strcmp(a->md5set, s->md5set) &&
              !strcmp(a->pend, s->pend) && !strcmp(a->last_url, s->last_url) && !memcmp(a->wsz, s->wsz, sizeof(a->wsz));
    if (!ok) {
        fprintf(stderr, "  %s #%d v%d: ard st=%d err=%d pct=%d done=%u total=%u bps=%u has=%d ms=%u writes=%d ends=%d aborts=%d restarts=%d begins=%d gets=%d | svc st=%d err=%d pct=%d done=%u total=%u bps=%u has=%d ms=%u writes=%d ends=%d aborts=%d restarts=%d begins=%d gets=%d\n",
                tag, it, v, a->state, a->error, a->pct, a->done, a->total, a->bps, a->has, a->ms, a->nwrites, a->ends, a->aborts, a->restarts, a->begins, a->gets, s->state, s->error,
                s->pct, s->done, s->total, s->bps, s->has, s->ms, s->nwrites, s->ends, s->aborts, s->restarts, s->begins, s->gets);
    }
    CHECK(ok);
    return ok;
}

// el servicio y Arduino sobre la misma situacion: `run_svc` y `run_ard` hacen la comprobacion o la instalacion
typedef int (*svc_fn)(void);
static void lockstep(const char *tag, int it, int v, int install, const char *remote, const char *url, const char *md5, unsigned total)
{
    const Ref *r = &R[v];
    r->snapshot();
    service_setup(v);
    if (install) flex_ota_prime(remote, url, md5, total);
    if (install) flex_ota_run_install(); else flex_ota_run_check();
    Res svc;
    take_svc(r, &svc);
    r->restore();
    if (install) r->prime(remote, url, md5, total);
    int ok_ard = install ? r->install() : r->check();
    Res ard;
    take_ard(r, &ard);
    (void)ok_ard;
    same(tag, it, v, &ard, &svc);
}

// ---- generadores ----
static const char *pick(const char *const *a, int n) { return a[rnd() % (unsigned)n]; }

#define VERPICK (rnd() % 4 ? VER[rnd() % 5] : VER[rnd() % 18])
static void gen_manifest(char *out, size_t cap)
{
    static const char *const VER[] = {"1.2.0", "1.0.1", "2.0.0", "1.0.0", "0.9.9", "1.2", "1.2.3.4", "v1.2.3", "1.2.70000", "01.002.0003", "", "1..2", "1.2.", "1.a.2", " 1.2.3", "65535.65535.65535", "1.0.0-rc1", "9.9.9"};
    static const char *const URL[] = {"https://github.com/x/y/releases/download/v1.2.0/fw.bin", "http://insecure.example/fw.bin", "ftp://x/fw.bin", "", "https://", "https://a/b?c=d&e=f",
                                      "HTTPS://upper/fw.bin", "https://x.example/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/p/fw.bin"};
    static const char *const MD5[] = {"d41d8cd98f00b204e9800998ecf8427e", "00000000000000000000000000000000", "D41D8CD98F00B204E9800998ECF8427E", "d41d8cd98f00b204e9800998ecf8427", "zzzd8cd98f00b204e9800998ecf8427e", "", "ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12"};
    static const char *const LOG[] = {"Mejoras de rendimiento", "Linea 1\\nLinea 2\\nLinea 3", "Con \\\"comillas\\\" y barra \\\\ y tab\\t y unicode \\u00e9 fin", "", "{llaves}} dentro { de cadena", "\\/", "Linea000-Linea001-Linea002-Linea003-Linea004-Linea005-Linea006-Linea007-Linea008-Linea009-Linea010-Linea011-Linea012-Linea013-Linea014-Linea015-Linea016-Linea017-Linea018-Linea019-Linea020-Linea021-Linea022-Linea023-Linea024-Linea025-Linea026-Linea027-Linea028-Linea029-Linea030-Linea031-Linea032-Linea033-Linea034-Linea035-Linea036-Linea037-Linea038-Linea039-Linea040-Linea041-Linea042-Linea043-Linea044-Linea045-Linea046-Linea047-Linea048-Linea049-Linea050-Linea051-Linea052-Linea053-Linea054-Linea055-Linea056-Linea057-Linea058-Linea059-Linea060-Linea061-Linea062-Linea063-Linea064-Linea065-Linea066-Linea067-Linea068-Linea069-Linea070-Linea071-Linea072-Linea073-Linea074"};
    static const char *const SIG[] = {"aabbccdd", "", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    char blk[3200];
    size_t n = 0;
    n += (size_t)snprintf(blk + n, sizeof(blk) - n, "{");
    int fields = 0;
    // version
    if (rnd() % 12) {
        const char *sp = rnd() % 4 == 0 ? " " : "";
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"version\"%s:%s\"%s\"", fields++ ? "," : "", sp, sp, VERPICK);
    }
    if (rnd() % 10) {
        const char *u = rnd() % 5 ? URL[0] : pick(URL, 8);
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"url\": \"%s\"", fields++ ? "," : "", u);
    }
    if (rnd() % 3) {
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"md5\": \"%s\"", fields++ ? "," : "", pick(MD5, 7));
    }
    if (rnd() % 3) {
        long sz = rnd() % 5 == 0 ? 4294967295L : (long)(rnd() % 3000000);
        if (rnd() % 8 == 0) n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"size\": \"%ld\"", fields++ ? "," : "", sz);
        else n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"size\": %ld", fields++ ? "," : "", sz);
    }
    if (rnd() % 2) {
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"changelog\": \"%s\"", fields++ ? "," : "", pick(LOG, 7));
    }
    if (rnd() % 2) {
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"sig\": \"%s\", \"sigalg\": \"ed25519\"", fields++ ? "," : "", pick(SIG, 4));
    }
    if (rnd() % 5 == 0) {   // un objeto anidado, con sus propias claves repetidas
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"assets\": {\"url\": \"https://nested/x.bin\", \"version\": \"7.7.7\"}", fields++ ? "," : "");
    }
    if (rnd() % 6 == 0) {   // un texto largo para el changelog (se acota)
        char big[500];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        n += (size_t)snprintf(blk + n, sizeof(blk) - n, "%s\"notes\": \"%s\"", fields++ ? "," : "", big);
    }
    n += (size_t)snprintf(blk + n, sizeof(blk) - n, "}");
    // el manifiesto: los bloques de las tres placas en un orden cualquiera
    const char *mine = "flex_os_ultra";
    const char *other1 = rnd() % 2 ? "flex_os_ultra_s3" : "flex_os_ultra_idf";
    const char *other2 = "flex_os_pro";
    char other[200];
    snprintf(other, sizeof(other), "{\"version\": \"%s\", \"url\": \"https://other/x.bin\"}", VERPICK);
    size_t o = 0;
    o += (size_t)snprintf(out + o, cap - o, "%s", rnd() % 3 ? "{\n  " : "  { ");
    int order = (int)(rnd() % 3);
    if (rnd() % 14 == 0) mine = "flex_os_ultr";   // sin bloque para esta placa
    if (order == 0) o += (size_t)snprintf(out + o, cap - o, "\"%s\": %s,\n  \"%s\": %s,\n  \"%s\": %s", other1, other, mine, blk, other2, other);
    else if (order == 1) o += (size_t)snprintf(out + o, cap - o, "\"%s\": %s,\n  \"%s\": %s,\n  \"%s\": %s", mine, blk, other1, other, other2, other);
    else o += (size_t)snprintf(out + o, cap - o, "\"%s\": %s,\n  \"%s\": %s,\n  \"%s\": %s", other2, other, other1, other, mine, blk);
    o += (size_t)snprintf(out + o, cap - o, "\n}\n");
    // roturas: corte en cualquier punto, llaves desbalanceadas, vacio
    switch (rnd() % 28) {
    case 0: out[rnd() % (o ? o : 1)] = 0; break;
    case 1: { char *p = strrchr(out, '}'); if (p) *p = ' '; break; }
    case 2: out[0] = 0; break;
    case 3: { char *p = strstr(out, "\"version\""); if (p) p[3] = 'X'; break; }
    default: break;
    }
}

typedef struct { int begin_ok, code, size, stream, stay, zero, net, partition; unsigned heap; } Net;
static Net rand_net(void)
{
    Net n = {1, 200, 0, 1, 0, 0, 1, 1, 200000};
    switch (rnd() % 44) {
    case 0: n.begin_ok = 0; break;
    case 1: n.code = 404; break;
    case 2: n.code = 500; break;
    case 3: n.code = -1; break;
    case 4: n.code = -5; break;
    case 5: n.code = -11; break;
    case 6: n.stream = 0; break;
    case 7: n.net = 0; break;
    case 8: n.heap = 55999 + rnd() % 3; break;
    case 9: n.stay = 1; break;
    case 10: n.zero = 1; break;
    case 11: n.partition = 0; break;
    case 12: n.code = 0; break;
    case 13: n.code = 204; break;
    default: break;
    }
    return n;
}
static void set_net(const Ref *r, const Net *n)
{
    r->world(n->heap, n->net, n->partition);
    r->http(n->begin_ok, n->code, n->size, n->stream, n->stay, n->zero);
}
// la llegada del cuerpo: trozos de tamano y retraso variables (a veces con un hueco largo que roza el tiempo de atasco)
static void set_body(const Ref *r, const unsigned char *body, int len)
{
    r->body(body, len);
    unsigned at[40], cum[40];
    int k = 1 + (int)(rnd() % 12);
    unsigned t = (unsigned)(rnd() % 50), c = 0;
    int mode = (int)(rnd() % 9);
    if (mode == 8) t = 14994 + (unsigned)(rnd() % 12);   // el primer trozo llega justo en el limite de los 15 s sin datos
    for (int i = 0; i < k; i++) {
        c += (unsigned)((len / k) + 1);
        if (c > (unsigned)len || i == k - 1) c = (unsigned)len;
        at[i] = t;
        cum[i] = c;
        t += mode == 0 ? 14990u + rnd() % 20 : (mode == 1 ? 3000u : rnd() % 700u);
    }
    r->sched(k, at, cum);
}

static void set_body_now(const Ref *r, const unsigned char *body, int len)
{
    r->body(body, len);
    unsigned at[1] = {0}, cum[1] = {(unsigned)len};
    r->sched(1, at, cum);
}

// ---- pruebas del modelo ----
static void t_model(void)
{
    s_seed = 11;
    static const char *const FR[] = {"0", "1", "2", "65535", "65536", "99999999999", "", "-1", "a", "1a", " ", "00", "007"};
    for (int it = 0; it < 8000; it++) {
        char a[48], b[48];
        for (int k = 0; k < 2; k++) {
            char *o = k ? b : a;
            if (rnd() % 6 == 0) {
                snprintf(o, 48, "%s", rnd() % 2 ? "1.0.0" : "");
            } else if (rnd() % 7 == 0) {   // basura
                int n = (int)(rnd() % 12);
                for (int i = 0; i < n; i++) o[i] = ".0123456789abv -"[rnd() % 16];
                o[n] = 0;
            } else {
                int parts = rnd() % 9 == 0 ? (int)(rnd() % 5) : 3;
                o[0] = 0;
                for (int p = 0; p < parts; p++) {
                    char t[24];
                    snprintf(t, sizeof(t), "%s%s", p ? "." : "", pick(FR, 13));
                    strncat(o, t, 47 - strlen(o));
                }
            }
        }
        uint16_t va[3], vb[3];
        unsigned short ra[3];
        bool oka = flex_ota_ver_parse(a, va), okb = flex_ota_ver_parse(b, vb);
        int rok = R[0].ver_ok(a, ra);
        CHECK_EQ_I(oka ? 1 : 0, rok);
        if (oka) CHECK(va[0] == ra[0] && va[1] == ra[1] && va[2] == ra[2]);
        int rc = R[0].ver(a, b);
        if (!oka || !okb) CHECK_EQ_I(rc, -2);
        else CHECK_EQ_I(flex_ota_ver_cmp(va, vb), rc);
        // MD5
        char m[40];
        int mn = rnd() % 4 == 0 ? (int)(rnd() % 40) : 32;
        for (int i = 0; i < mn; i++) m[i] = rnd() % 9 == 0 ? "gz-"[rnd() % 3] : (rnd() % 3 == 0 ? '0' : "0123456789abcdefABCDEF"[rnd() % 22]);
        m[mn] = 0;
        if (rnd() % 9 == 0) memset(m, '0', (size_t)(mn < 32 ? mn : 32));
        CHECK_EQ_I(flex_ota_md5_usable(m) ? 1 : 0, R[0].md5(m));
    }
    CHECK(!flex_ota_md5_usable(NULL) && !flex_ota_ver_parse(NULL, (uint16_t[3]){0}));
    for (int e = 0; e < 40; e++) CHECK(!strcmp(flex_ota_err_text((flex_ota_err_t)e), R[0].errtext(e)));
    for (int s = 0; s < 14; s++) CHECK(!strcmp(flex_ota_phase_text((flex_ota_state_t)s), R[0].phasetext(s)));
    // el micro-lector JSON sobre manifiestos de todas las formas
    char buf[6000];
    for (int it = 0; it < 4000; it++) {
        gen_manifest(buf, sizeof(buf));
        static const char *const KEYS[] = {"flex_os_ultra", "flex_os_ultra_s3", "flex_os_ultra_idf", "version", "url", "md5", "size", "changelog", "sig", "nada"};
        const char *k = pick(KEYS, 10);
        const char *f1 = flex_ota_json_find(buf, buf + strlen(buf), k);
        const char *f2 = R[0].find(buf, k);
        CHECK(f1 == f2);
        const char *bs, *be;
        int rbs, rbe;
        int rb = R[0].block(buf, k, &rbs, &rbe);
        bool mb = flex_ota_json_block(buf, k, &bs, &be);
        CHECK_EQ_I(mb ? 1 : 0, rb);
        if (mb && rb) CHECK(bs - buf == rbs && be - buf == rbe);
        if (f1) {
            int off = (int)(f1 - buf);
            char o1[24], o2[24];
            memset(o1, 0x55, sizeof(o1));
            memset(o2, 0x55, sizeof(o2));
            int cap = 1 + (int)(rnd() % 23);
            bool s1 = flex_ota_json_str(f1, buf + strlen(buf), o1, (size_t)cap);
            int s2 = R[0].str(buf, off, o2, cap);
            CHECK_EQ_I(s1 ? 1 : 0, s2);
            CHECK(!memcmp(o1, o2, sizeof(o1)));
            CHECK_EQ_I(flex_ota_json_num(f1, buf + strlen(buf)), R[0].num(buf, off));
        }
    }
}


// El micro-lector no mira mas alla de `end`: cada clave cortada en cada punto, en un bloque EXACTO del heap (ASan vigila un byte de mas), contra Arduino; las
// comillas sueltas delante de una clave; y una cadena que empieza justo en `end` no toca el buffer de salida.
static void t_json_edges(void)
{
    static const char *const KEYS[] = {"version", "url", "md5", "size", "changelog", "sig", "flex_os_ultra_idf", "a}"};
    for (int k = 0; k < 16; k++) {
        char full[96];
        snprintf(full, sizeof(full), k < 8 ? "\"%s\" : 1," : "\"%s\":1,", KEYS[k & 7]);
        size_t fl = strlen(full);
        for (size_t L = 0; L <= fl; L++) {
            char *h = (char *)malloc(L + 1);
            memcpy(h, full, L);
            h[L] = 0;
            const char *f1 = flex_ota_json_find(h, h + L, KEYS[k & 7]);
            if ((k & 7) < 7) CHECK(f1 == R[0].find(h, KEYS[k & 7]));
            else (void)f1;   // la clave con una llave dentro: solo la memoria se vigila
            free(h);
        }
        char *h = (char *)malloc(fl + 3);   // `end` antes del final de un buffer mas largo, con la clave justo despues
        memcpy(h, full, fl);
        memcpy(h + fl, "xyz", 3);
        for (size_t L = 0; L < fl; L++) {
            const char *f1 = flex_ota_json_find(h, h + L, KEYS[k & 7]);
            CHECK(f1 == NULL || f1 <= h + L);
        }
        free(h);
    }
    static const char *const ODD[] = {"\"\"version\": 1", "x\"\"url\":\"u\"", "\"\"\"\"md5\":2", "\"a\"\"size\":3", "\"\"changelog\" : \"c\"", "\"\"sig\":\"\"", "\"\"flex_os_ultra_idf\":{}"};
    static const char *const OK[] = {"version", "url", "md5", "size", "changelog", "sig", "flex_os_ultra_idf"};
    for (int i = 0; i < 7; i++) {
        const char *f1 = flex_ota_json_find(ODD[i], ODD[i] + strlen(ODD[i]), OK[i]);
        CHECK(f1 != NULL && f1 == R[0].find(ODD[i], OK[i]));
    }
    char out[8];
    const char *str = "x\"abc\"";
    memset(out, 0x55, sizeof(out));
    CHECK(!flex_ota_json_str(str + 1, str + 1, out, sizeof(out)) && (unsigned char)out[0] == 0x55);   // p == end: ni se lee ni se escribe
    CHECK(!flex_ota_json_str(NULL, str, out, sizeof(out)) && (unsigned char)out[0] == 0x55);
    CHECK(!flex_ota_json_str(str + 1, str + 6, out, 0) && (unsigned char)out[0] == 0x55);
    // un \u cortado justo en `end` (4 digitos que no estan)
    const char *cut = "\"a\\u12";
    CHECK(!flex_ota_json_str(cut, cut + strlen(cut), out, sizeof(out)));
}

// ---- la comprobacion del manifiesto en paso con Arduino (otaCheck) ----
static void t_check(void)
{
    s_seed = 22;
    int newer = 0, uptodate = 0, errs[24] = {0};
    static char body[16000];
    for (int it = 0; it < 6000; it++) {
        int v = (int)(it % 4);
        const Ref *r = &R[v];
        r->reset();
        Net n = rand_net();
        set_net(r, &n);
        gen_manifest(body, 6000);
        int len = (int)strlen(body);
        if (rnd() % 25 == 0) {   // mas grande que el buffer del manifiesto
            memset(body + len, ' ', 5000);
            len += 5000;
            body[len] = 0;
        }
        if (rnd() % 10 == 0) {   // el cierre del bloque propio cae en cada byte alrededor del limite del buffer de Arduino (4095 utiles)
            const char *bs, *be;
            if (flex_ota_json_block(body, "flex_os_ultra", &bs, &be)) {
                int shift = 4090 + (int)(rnd() % 9) - (int)(be - body);
                if (shift > 0 && len + shift < 15000) {
                    memmove(body + shift, body, (size_t)len + 1);
                    memset(body, ' ', (size_t)shift);
                    len += shift;
                }
            }
        }
        if (n.stay && rnd() % 2) len = len / 2;   // cuelga a medias: tiempo de atasco
        set_body(r, (const unsigned char *)body, len);
        lockstep("manifiesto", it, v, 0, "", "", "", 0);
        flex_ota_info_t i;
        flex_ota_get(&i);
        newer += i.state == FLEX_OTA_AVAILABLE;
        uptodate += i.state == FLEX_OTA_UP_TO_DATE;
        if (i.state == FLEX_OTA_ERROR && i.error < 24) errs[i.error]++;
    }
    CHECK(newer > 500 && uptodate > 150);
    int kinds = 0;
    for (int e = 1; e < 22; e++) kinds += errs[e] > 0;
    CHECK(kinds >= 11);   // el reparto cubre casi todos los errores posibles del manifiesto y de la red (trece)
}

// ---- la descarga y la instalacion en paso con Arduino (otaInstall) ----
static unsigned char s_fw[300000];
static int s_ui_calls;
void flex_ota_ui_install_started(void) { s_ui_calls++; }   // la interfaz (N5b): aqui solo cuenta
// Lo de la placa (flex_ota_port.c): aqui se enciende a mano, para probar que con solo la mitad el servicio no se cae
static int g_port_cfg_on, g_port_ops_on;
static flex_ota_cfg_t g_port_cfg;
static flex_ota_ops_t g_port_ops;
const flex_ota_cfg_t *flex_ota_port_cfg(void) { return g_port_cfg_on ? &g_port_cfg : NULL; }
const flex_ota_ops_t *flex_ota_port_ops(void) { return g_port_ops_on ? &g_port_ops : NULL; }

static void t_install(void)
{
    s_seed = 33;
    static const char *const MD5[] = {"d41d8cd98f00b204e9800998ecf8427e", "00000000000000000000000000000000", "", "zz", "D41D8CD98F00B204E9800998ECF8427E"};
    int done = 0, errs[24] = {0}, rebooted = 0, cancelled = 0, stalls = 0, extra_ok = 0, max_total = 0;
    for (int it = 0; it < 3000; it++) {
        int v = (int)(it % 4);
        const Ref *r = &R[v];
        r->reset();
        Net n = rand_net();
        // tamano anunciado: lo normal, los bordes y lo ilegal
        int total;
        switch (rnd() % 12) {
        case 0: total = 0; break;
        case 1: total = -1; break;
        case 2: total = (int)FLEX_OTA_MIN_BIN - 1; break;
        case 3: total = (int)FLEX_OTA_MIN_BIN; break;
        case 4: total = (int)FLEX_OTA_MAX_BIN + 1; break;
        case 5: total = (int)FLEX_OTA_MAX_BIN; break;
        default: total = 65536 + (int)(rnd() % 200000); break;
        }
        n.size = total;
        set_net(r, &n);
        int body_len = total > 0 && total <= (int)sizeof(s_fw) ? total : 70000;
        if (rnd() % 8 == 0) body_len = body_len / 2 + 1;   // el servidor cierra antes de tiempo
        else if (rnd() % 7 == 0 && total > 0 && total + 6000 < (int)sizeof(s_fw)) body_len = total + 1 + (int)(rnd() % 5000);   // manda de mas que lo anunciado
        if (body_len > (int)sizeof(s_fw)) body_len = (int)sizeof(s_fw);
        set_body(r, s_fw, body_len);
        int cancel_after = rnd() % 9 == 0 ? (int)(rnd() % (unsigned)(body_len + 1)) : -1;
        r->upd(rnd() % 14 != 0, rnd() % 12 != 0, rnd() % 16 != 0, rnd() % 10 == 0 ? (int)(rnd() % 30) : -1, cancel_after);
        r->set_time((unsigned)(rnd() % 100000));
        const char *md5 = MD5[rnd() % 5];
        const char *url = rnd() % 20 == 0 ? "http://insegura/fw.bin" : (rnd() % 30 == 0 ? "" : "https://servidor/fw.bin");
        lockstep("instalacion", it, v, 1, "1.2.0", url, md5, (unsigned)(rnd() % 3000000));
        flex_ota_info_t i;
        flex_ota_get(&i);
        done += i.state == FLEX_OTA_REBOOTING;
        rebooted += i.state == FLEX_OTA_REBOOTING;
        if (i.state == FLEX_OTA_ERROR && i.error < 24) errs[i.error]++;
        cancelled += i.error == FLEX_OTA_ERR_CANCELLED;
        extra_ok += (body_len > total && total >= (int)FLEX_OTA_MIN_BIN && total <= (int)FLEX_OTA_MAX_BIN);
        max_total += total == (int)FLEX_OTA_MAX_BIN;
        stalls += i.error == FLEX_OTA_ERR_TIMEOUT;
    }
    CHECK(rebooted > 300 && cancelled > 40 && stalls > 40);
    CHECK(extra_ok > 20 && max_total > 20);
    int kinds = 0;
    for (int e = 1; e < 22; e++) kinds += errs[e] > 0;
    CHECK(kinds >= 14);
    (void)done;
}

// ---- la API del servicio, la interfaz y las diferencias deliberadas ----
static int g_pend_set;
static char g_pend[32];
static bool g_pend_cleared;
static bool o_loadp(char *out, size_t cap)
{
    if (!g_pend_set) return false;
    snprintf(out, cap, "%s", g_pend);
    return true;
}
static void o_clearp(void) { g_pend_cleared = true; g_pend_set = 0; }

static void t_api(void)
{
    s_seed = 44;
    const Ref *r = &R[0];
    r->reset();
    service_setup(0);
    flex_ota_info_t i;
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_IDLE && !i.has_update && i.percent == 0);
    CHECK(!strcmp(flex_ota_status_text(), "Buscar actualizacion"));
    CHECK(!strcmp(flex_ota_local_version(), "1.0.0"));
    // pedir comprobar: sin red NO lanza nada y deja el error exacto
    r->world(200000, 0, 1);
    flex_ota_request_check();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_ERROR && i.error == FLEX_OTA_ERR_NO_WIFI && !flex_ota_work_pending());
    CHECK(!strcmp(flex_ota_status_text(), "Sin conexion a internet"));
    // con red: pasa a "Buscando" y el trabajador la hace; una segunda peticion mientras tanto no se acumula
    r->world(200000, 1, 1);
    r->http(1, 200, 0, 1, 0, 0);
    const char *mf = "{\"flex_os_ultra\": {\"version\": \"1.5.0\", \"url\": \"https://x/fw.bin\", \"changelog\": \"Hola\", \"size\": 123456}}";
    set_body_now(r, (const unsigned char *)mf, (int)strlen(mf));
    flex_ota_request_check();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_CHECKING && flex_ota_busy() && flex_ota_work_pending());
    CHECK(!strcmp(flex_ota_status_text(), "Buscando actualizaciones..."));
    flex_ota_request_check();   // ocupado: no hace nada
    flex_ota_request_install();   // sin version confirmada: tampoco
    CHECK(flex_ota_work_pending());
    flex_ota_work();
    CHECK(!flex_ota_work_pending() && !flex_ota_busy());
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_AVAILABLE && i.has_update && !strcmp(i.remote, "1.5.0") && !strcmp(i.changelog, "Hola") && i.total == 123456);
    CHECK(!strcmp(flex_ota_status_text(), "1.5.0 disponible"));
    flex_ota_manifest_t m;
    CHECK(flex_ota_manifest_copy(&m) && !strcmp(m.url, "https://x/fw.bin"));
    // pedir instalar: sin red, error; con red, "Conectando" y la interfaz se entera
    r->world(200000, 0, 1);
    flex_ota_request_install();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_ERROR && i.error == FLEX_OTA_ERR_NO_WIFI && s_ui_calls == 0);
    r->world(200000, 1, 1);
    flex_ota_prime("1.5.0", "https://x/fw.bin", "", 123456);
    flex_ota_request_install();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_CONNECTING && flex_ota_work_pending() && s_ui_calls == 1);
    CHECK(!strcmp(flex_ota_status_text(), "Conectando..."));
    // una orden pendiente: la ultima gana (comprobar pisa a instalar si llega antes de que corra)
    flex_ota_cancel();   // la bandera se limpia al empezar la instalacion
    r->http(1, 200, 90000, 1, 0, 0);
    set_body_now(r, s_fw, 90000);
    r->upd(1, 1, 1, -1, -1);
    flex_ota_work();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_REBOOTING && i.percent == 100 && i.done == 90000 && i.total == 90000);
    CHECK(!strcmp(flex_ota_status_text(), "Reiniciando..."));
    {
        unsigned ms; int rs, ab, bg, mset, nw, ps, en, ge, ca, in, lk, ul, ec, bc; unsigned long long bt, wr; char md5s[48], pend[48], lu[300];
        r->effects(&ms, &rs, &ab, &bg, &bt, md5s, &mset, &nw, &wr, pend, &ps, &en, &ge, &ca, &in, lu, &lk, &ul, &ec, &bc);
        CHECK(rs == 1 && ab == 0 && ps == 1 && !strcmp(pend, "1.5.0") && wr == 90000 && ec == 1 && bc == 1 && bt == 90000);
        CHECK_EQ_I(lk, ul);
        CHECK(lk >= 20 && nw == 22);   // 90000 / 4096 = 22 bloques: cada escritura con el candado del panel tomado y soltado
    }
    // el saludo tras el reinicio: la version que se dejo apuntada coincide con la que corre
    {
        flex_ota_cfg_t c = make_cfg(0);
        c.local_version = "1.5.0";
        flex_ota_ops_t o = make_ops();
        o.load_pending = o_loadp;
        o.clear_pending = o_clearp;
        flex_ota_set(&c, &o);
        flex_ota_service_reset();
        g_pend_set = 1;
        snprintf(g_pend, sizeof(g_pend), "1.5.0");
        g_pend_cleared = false;
        flex_ota_begin();
        flex_ota_get(&i);
        CHECK(i.just_updated && g_pend_cleared);
        g_pend_set = 1;   // otra version apuntada (la instalada no es esa): no saluda, pero se borra igual
        snprintf(g_pend, sizeof(g_pend), "1.4.0");
        g_pend_cleared = false;
        flex_ota_service_reset();
        flex_ota_begin();
        flex_ota_get(&i);
        CHECK(!i.just_updated && g_pend_cleared);
        g_pend_set = 0;
        g_pend_cleared = false;
        flex_ota_begin();
        CHECK(!g_pend_cleared);
    }
    // la comprobacion automatica: una sola vez, ~20 s despues del arranque y solo con red y en reposo
    service_setup(0);
    r->reset();
    r->world(200000, 1, 1);
    r->set_time(19999);
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_work_pending());
    r->set_time(20000);
    flex_ota_auto_check_tick();
    CHECK(flex_ota_work_pending());
    flex_ota_work();   // (la red de este mundo no tiene nada: falla, da igual)
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_work_pending());   // no se reintenta sola
    service_setup(0);
    r->world(200000, 0, 1);
    r->set_time(30000);
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_work_pending());   // sin red: nada, y ya no lo vuelve a intentar
    r->world(200000, 1, 1);
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_work_pending());
    service_setup(0);
    r->world(200000, 1, 1);
    r->set_time(30000);
    flex_ota_prime("1.5.0", "https://x/fw.bin", "", 1);   // no en reposo (ya hay una version): no pisa
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_work_pending());
    // los estados: "ocupado" son exactamente los de trabajo de red, y cada uno dice lo que flexOtaStatusText de Arduino
    {
        static const struct { int st, err, busy; const char *txt; } T[] = {
            {FLEX_OTA_IDLE, 0, 0, "Buscar actualizacion"}, {FLEX_OTA_CHECKING, 0, 1, "Buscando actualizaciones..."}, {FLEX_OTA_UP_TO_DATE, 0, 0, "El sistema esta al dia"},
            {FLEX_OTA_AVAILABLE, 0, 0, "1.5.0 disponible"}, {FLEX_OTA_CONNECTING, 0, 1, "Conectando..."}, {FLEX_OTA_DOWNLOADING, 0, 1, "Descargando..."},
            {FLEX_OTA_VERIFYING, 0, 1, "Verificando..."}, {FLEX_OTA_INSTALLING, 0, 1, "Instalando..."}, {FLEX_OTA_FINALIZING, 0, 1, "Finalizando..."},
            {FLEX_OTA_REBOOTING, 0, 1, "Reiniciando..."}, {FLEX_OTA_ERROR, FLEX_OTA_ERR_TLS, 0, NULL}, {FLEX_OTA_ERROR, FLEX_OTA_ERR_CANCELLED, 0, NULL}};
        for (unsigned k = 0; k < sizeof(T) / sizeof(T[0]); k++) {
            service_setup(0);
            flex_ota_test_force((flex_ota_state_t)T[k].st, (flex_ota_err_t)T[k].err, "1.5.0");
            CHECK_EQ_I(flex_ota_busy() ? 1 : 0, T[k].busy);
            CHECK(!strcmp(flex_ota_status_text(), T[k].txt ? T[k].txt : R[0].errtext(T[k].err)));
            if (T[k].busy) {
                // ocupado: ni comprobar ni instalar hacen nada, aunque haya una version confirmada
                r->world(200000, 1, 1);
                int ui0 = s_ui_calls;
                flex_ota_request_install();
                flex_ota_request_check();
                flex_ota_get(&i);
                CHECK(i.state == (flex_ota_state_t)T[k].st && !flex_ota_work_pending() && s_ui_calls == ui0);
            }
        }
    }
    // pedir instalar necesita una version confirmada Y una direccion
    service_setup(0);
    r->world(200000, 1, 1);
    flex_ota_test_force(FLEX_OTA_UP_TO_DATE, FLEX_OTA_ERR_NONE, NULL);
    flex_ota_request_install();
    flex_ota_get(&i);
    CHECK(i.state == FLEX_OTA_UP_TO_DATE && !flex_ota_work_pending());
    // sin operaciones el servicio no hace nada ni falla
    flex_ota_set(NULL, NULL);
    flex_ota_request_check();
    flex_ota_request_install();
    flex_ota_cancel();
    flex_ota_work();
    flex_ota_begin();
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_run_check() && !flex_ota_run_install());
    // la mitad de lo de la placa (solo la configuracion, o solo las operaciones): nada se desreferencia
    service_setup(0);
    r->world(200000, 1, 1);
    flex_ota_request_check();   // una orden pendiente con las operaciones...
    CHECK(flex_ota_work_pending());
    g_port_cfg = make_cfg(0);
    g_port_cfg_on = 1;
    flex_ota_set(NULL, NULL);   // ...que desaparecen (queda solo la configuracion)
    flex_ota_work();
    CHECK(flex_ota_work_pending());   // no se ejecuta ni se pierde
    flex_ota_begin();
    flex_ota_request_check();
    flex_ota_request_install();
    flex_ota_work();
    flex_ota_auto_check_tick();
    CHECK(!flex_ota_run_check() && !flex_ota_run_install());
    CHECK(!strcmp(flex_ota_local_version(), "1.0.0"));
    g_port_cfg_on = 0;
    g_port_ops = make_ops();
    g_port_ops.load_pending = o_loadp;
    g_port_ops.clear_pending = o_clearp;
    g_port_ops_on = 1;
    g_pend_set = 1;
    snprintf(g_pend, sizeof(g_pend), "1.0.0");
    flex_ota_service_reset();
    r->world(200000, 1, 1);
    flex_ota_request_check();   // las operaciones sin configuracion: pide, pero no se puede ejecutar
    CHECK(flex_ota_work_pending());
    flex_ota_work();
    CHECK(flex_ota_work_pending());
    flex_ota_service_reset();
    flex_ota_begin();   // sin configuracion no se sabe que version corre: no saluda
    flex_ota_get(&i);
    CHECK(!i.just_updated);
    CHECK(!strcmp(flex_ota_local_version(), ""));
    g_port_ops_on = 0;
    g_pend_set = 0;
    flex_ota_service_reset();
    // flex_ota_set exige las DOS cosas: con solo una se vuelve a lo de la placa (nada se copia desde un NULL)
    {
        flex_ota_cfg_t c = make_cfg(0);
        flex_ota_ops_t o = make_ops();
        service_setup(0);
        CHECK(!strcmp(flex_ota_local_version(), "1.0.0"));
        flex_ota_set(&c, NULL);
        CHECK(!strcmp(flex_ota_local_version(), ""));
        service_setup(0);
        flex_ota_set(NULL, &o);
        CHECK(!strcmp(flex_ota_local_version(), ""));
        flex_ota_request_check();   // sin operaciones no hace nada
        CHECK(!flex_ota_work_pending());
    }
    // flex_ota_prime acota cada cadena a su buffer y la deja terminada en 0 (version 15, url 191, md5 35)
    {
        char rv[40], url[300], md5[60];
        memset(rv, 'v', sizeof(rv) - 1); rv[sizeof(rv) - 1] = 0;
        memcpy(url, "https://", 8); memset(url + 8, 'u', sizeof(url) - 9); url[sizeof(url) - 1] = 0;
        memset(md5, 'a', sizeof(md5) - 1); md5[sizeof(md5) - 1] = 0;
        service_setup(0);
        flex_ota_prime(rv, url, md5, 1234);
        flex_ota_get(&i);
        CHECK_EQ_I(strlen(i.remote), FLEX_OTA_VER_MAX - 1);
        flex_ota_manifest_t pm;
        CHECK(flex_ota_manifest_copy(&pm));
        CHECK_EQ_I(strlen(pm.ver), FLEX_OTA_VER_MAX - 1);
        CHECK_EQ_I(strlen(pm.url), FLEX_OTA_URL_MAX - 1);
        CHECK_EQ_I(strlen(pm.md5), FLEX_OTA_MD5_MAX - 1);
        CHECK(i.state == FLEX_OTA_AVAILABLE && i.total == 1234 && i.has_update);
        flex_ota_service_reset();
    }
}

// ---- la confirmacion de arranque (rollback): lo unico que Arduino no tiene, asi que aqui la referencia es la especificacion ----
static void t_boot(void)
{
    static const unsigned UP[] = {0, 1, 14999, 15000, 15001, 60000, 0xFFFFFFFFu};
    for (int pend = 0; pend < 2; pend++) {
        for (int safe = 0; safe < 2; safe++) {
            for (int ui = 0; ui < 2; ui++) {
                for (unsigned k = 0; k < sizeof(UP) / sizeof(UP[0]); k++) {
                    flex_ota_boot_t want;
                    if (!pend) want = FLEX_OTA_BOOT_NONE;                     // ya validada: nada que hacer
                    else if (safe) want = FLEX_OTA_BOOT_HOLD;                 // Modo seguro: jamas se confirma
                    else if (!ui || UP[k] < 15000u) want = FLEX_OTA_BOOT_WAIT; // aun no esta sano
                    else want = FLEX_OTA_BOOT_CONFIRM;
                    CHECK_EQ_I(flex_ota_boot_decide(pend != 0, UP[k], safe != 0, ui != 0), want);
                }
            }
        }
    }
    CHECK_EQ_I((int)FLEX_OTA_CONFIRM_MS, 15000);
}

// ---- los identificadores propios: la clave del manifiesto de este firmware no es la de Arduino ----
static void t_ids(void)
{
    CHECK(!strcmp(FLEX_OTA_KEY, "flex_os_ultra_idf"));
    const char *mf = "{\"flex_os_ultra\": {\"version\": \"9.0.0\", \"url\": \"https://arduino/fw.bin\"}, \"flex_os_ultra_idf\": {\"version\": \"1.1.0\", \"url\": \"https://idf/fw.bin\", \"md5\": "
                     "\"d41d8cd98f00b204e9800998ecf8427e\"}, \"flex_os_ultra_s3\": {\"version\": \"9.9.9\", \"url\": \"https://s3/fw.bin\"}}";
    flex_ota_cfg_t c = make_cfg(0);
    c.key = FLEX_OTA_KEY;
    flex_ota_manifest_t m;
    bool newer;
    CHECK_EQ_I(flex_ota_manifest_read(mf, &c, &m, &newer), FLEX_OTA_ERR_NONE);
    CHECK(newer && !strcmp(m.ver, "1.1.0") && !strcmp(m.url, "https://idf/fw.bin"));   // lee SU bloque, no el de Arduino (9.0.0) ni el del S3
    c.key = "flex_os_ultra";
    CHECK_EQ_I(flex_ota_manifest_read(mf, &c, &m, &newer), FLEX_OTA_ERR_NONE);
    CHECK(newer && !strcmp(m.ver, "9.0.0"));
    // un manifiesto que solo trae los bloques de las otras placas: ninguno es suyo
    const char *other = "{\"flex_os_ultra\": {\"version\": \"9.0.0\", \"url\": \"https://a/b\"}, \"flex_os_pro\": {\"version\": \"9.0.0\", \"url\": \"https://c/d\"}}";
    c.key = FLEX_OTA_KEY;
    CHECK_EQ_I(flex_ota_manifest_read(other, &c, &m, &newer), FLEX_OTA_ERR_NO_BLOCK);
    // la clave de un bloque no casa con otra que solo la empieza
    const char *prefix = "{\"flex_os_ultra_idf_beta\": {\"version\": \"9.0.0\", \"url\": \"https://a/b\"}}";
    CHECK_EQ_I(flex_ota_manifest_read(prefix, &c, &m, &newer), FLEX_OTA_ERR_NO_BLOCK);
}

void test_ota(void)
{
    t_json_edges();
    t_model();
    t_check();
    t_install();
    t_api();
    t_ids();
    t_boot();
}
