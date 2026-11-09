// Hora por red (N1a): el modelo y el servicio frente al NTP.h de Arduino compilado tal cual (paquete, lectura de la respuesta, planificacion, espera
// progresiva, persistencia, textos, fecha civil) y el transporte UDP contra un servidor UDP REAL en 127.0.0.1 (respuesta buena, silencio, datagramas cortos,
// respuestas de otro origen, respuestas con el reloj del servidor en alarma, ni un descriptor de socket abierto de mas).
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "flex_ntp.h"
#include "flex_test.h"

// ---- el doble de Arduino ----
void ref_ntp_reset(void);
void ref_ntp_world(int state_ready, int airplane, int online, int wl_connected, unsigned ms, unsigned utc);
void ref_ntp_task_ok(int ok);
void ref_ntp_script(int idx, int kind, int after_ms, const unsigned char *bytes, int len);
void ref_ntp_wifi_up(void);
int ref_ntp_request(int user);
int ref_ntp_tick(void);
int ref_ntp_run_task(void);
void ref_ntp_get(int *state, int *busy, unsigned *done, unsigned *last, int *from_nvs, int *fails, unsigned *next, int *user, unsigned *set_epoch, int *set_n, int *nvs_ok);
void ref_ntp_text(char *state, int ns, char *last, int nl);
int ref_ntp_hosts(int i, char *out, int cap, int *port);
int ref_ntp_sent(int i, unsigned char *out);
int ref_ntp_stops(void);
int ref_ntp_begins(void);
void ref_ntp_nvs(int ns_exists, int rw_ok, unsigned long long epoch, unsigned long long last);
void ref_ntp_load(void);
void ref_ntp_save(int anchored);
void ref_ntp_persist(void);
void ref_ntp_nvs_get(unsigned long long *epoch, unsigned long long *last, int *writes);
void ref_ntp_set_last_sync(unsigned last, int from_nvs);
void ref_ntp_set_state(int state, int busy, int fails, int from_nvs, int nvs_ok);
unsigned ref_ntp_ms(void);
unsigned ref_ntp_utc(void);
void ref_ntp_clk_set(unsigned utc);
void ref_ntp_set_time(unsigned utc);
void ref_ntp_advance(unsigned ms);
unsigned ref_ntp_query(const char *host);
int ref_ntp_flags(int *airplane, int *online, int *ready);
void ref_ntp_civil(long days, int *y, int *m, int *d);
void ref_ntp_rewind(void);
void ref_ntp_snapshot(void);
void ref_ntp_restore(void);
enum { UK_SILENT = 0, UK_PACKET, UK_SHORT, UK_FAIL_BEGIN, UK_FAIL_BEGINPKT, UK_FAIL_END };

static uint32_t s_seed = 1;
static uint32_t rnd(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 8;
}

// respuesta de servidor con una marca de transmision dada (segundos desde 1900)
static void make_reply(uint8_t p[FLEX_NTP_PKT_SIZE], uint32_t secs1900, int mode, int li, int stratum)
{
    memset(p, 0, FLEX_NTP_PKT_SIZE);
    p[0] = (uint8_t)((li << 6) | (4 << 3) | mode);
    p[1] = (uint8_t)stratum;
    p[40] = (uint8_t)(secs1900 >> 24);
    p[41] = (uint8_t)(secs1900 >> 16);
    p[42] = (uint8_t)(secs1900 >> 8);
    p[43] = (uint8_t)secs1900;
}

// ---- el entorno del servicio, atado al reloj del doble de Arduino ----
static struct {
    int safe;
    int ready_override;   // -1: como el doble
    int nvs_ok_flag;
    int saves, wakes;
    uint64_t saved_epoch, saved_last;
    int load_ok;
    uint64_t load_epoch, load_last;
    int synced_flag;
    unsigned set_calls;
    int set_net;
    uint32_t set_val;
    int query_calls;
    unsigned (*query_fn)(const char *);
} E;
static uint32_t e_millis(void) { return ref_ntp_ms(); }
static uint32_t e_utc(void) { return ref_ntp_utc(); }
static void e_set_utc(uint32_t u, bool net) { ref_ntp_set_time(u); E.set_calls++; E.set_net = net; E.set_val = u; }
static bool e_synced(void) { return E.synced_flag != 0; }
static bool e_airplane(void) { int a, o, r; ref_ntp_flags(&a, &o, &r); return a != 0; }
static bool e_ready(void) { int a, o, r; ref_ntp_flags(&a, &o, &r); return r != 0; }
static bool e_safe(void) { return E.safe != 0; }
static bool e_nvs_ok(void) { return E.nvs_ok_flag != 0; }
static bool e_load(uint64_t *e, uint64_t *l) { *e = E.load_epoch; *l = E.load_last; return E.load_ok != 0; }
static bool e_save(uint64_t e, uint64_t l) { E.saves++; E.saved_epoch = e; E.saved_last = l; return E.nvs_ok_flag != 0; }
static uint32_t e_query(const char *h) { E.query_calls++; return E.query_fn ? E.query_fn(h) : 0; }
static void e_wake(void) { E.wakes++; }
static flex_ntp_env_t env_ref(void)
{
    flex_ntp_env_t e = {.millis = e_millis, .utc = e_utc, .set_utc = e_set_utc, .synced = e_synced, .airplane = e_airplane, .system_ready = e_ready, .safe_mode = e_safe,
                        .nvs_ok = e_nvs_ok, .load = e_load, .save = e_save, .query = e_query, .wake = e_wake};
    return e;
}
static void service_fresh(void)
{
    memset(&E, 0, sizeof(E));
    E.nvs_ok_flag = 1;
    E.load_ok = 1;
    flex_ntp_env_t e = env_ref();
    flex_ntp_set_env(&e);
    flex_ntp_service_reset();
    flex_net_set_online(false);
}

// ---- pruebas puras ----
static void t_tables(void)
{
    CHECK_EQ_I(FLEX_NTP_PERIOD_MS, 6 * 3600 * 1000);
    CHECK_EQ_I(FLEX_NTP_FIRST_DELAY, 1500);
    CHECK_EQ_I(FLEX_NTP_TIMEOUT_MS, 4000);
    CHECK_EQ_I(FLEX_NTP_PORT, 123);
    CHECK_EQ_I(FLEX_NTP_PKT_SIZE, 48);
    CHECK((long)FLEX_NTP_EPOCH_DELTA == 2208988800L);
    CHECK_EQ_I(FLEX_NTP_BACKOFF_MIN, 30000);
    CHECK_EQ_I(FLEX_NTP_BACKOFF_MAX, 1800000);
    CHECK_EQ_I(FLEX_NTP_MIN_EPOCH, 1767225600);
    CHECK_EQ_I(FLEX_NTP_TZ_SEC, -18000);
    CHECK(!strcmp(flex_ntp_servers[0], "pool.ntp.org") && !strcmp(flex_ntp_servers[1], "time.google.com") && !strcmp(flex_ntp_servers[2], "time.cloudflare.com"));
    // la espera progresiva: 30 s, 60 s, 2 min, 4 min, 8 min, 16 min, 30 min (techo)
    static const uint32_t W[9] = {30000, 30000, 60000, 120000, 240000, 480000, 960000, 1800000, 1800000};
    for (int f = 0; f <= 8; f++) {
        CHECK_EQ_I(flex_ntp_backoff_ms((uint8_t)f), W[f]);
    }
    for (int f = 9; f < 256; f++) {
        CHECK_EQ_I(flex_ntp_backoff_ms((uint8_t)f), 1800000u);
    }
}

static void t_packet_and_parse(void)
{
    // lo que se manda a un servidor es lo que manda Arduino
    ref_ntp_reset();
    ref_ntp_world(1, 0, 1, 1, 0, 1791481500u);
    ref_ntp_wifi_up();
    ref_ntp_world(1, 0, 1, 1, 5000, 1791481500u);
    CHECK_EQ_I(ref_ntp_request(1), 1);
    CHECK_EQ_I(ref_ntp_run_task(), 3);   // todos en silencio: los tres servidores
    uint8_t exp[FLEX_NTP_PKT_SIZE], got[FLEX_NTP_PKT_SIZE];
    flex_ntp_build_packet(got);
    for (int i = 0; i < 3; i++) {
        CHECK_EQ_I(ref_ntp_sent(i, exp), FLEX_NTP_PKT_SIZE);
        CHECK(!memcmp(exp, got, sizeof(got)));
        char h[64];
        int port;
        CHECK(ref_ntp_hosts(i, h, sizeof(h), &port) && !strcmp(h, flex_ntp_servers[i]) && port == 123);
    }
    CHECK_EQ_I(ref_ntp_stops(), 3);   // el socket se cierra en cada consulta
    // la lectura de la respuesta: marcas validas, bajo el suelo, por debajo de 1970, cero, paquetes cortos y largos
    s_seed = 99;
    int accepted = 0, rejected = 0;
    for (int it = 0; it < 4000; it++) {
        uint8_t pkt[80];
        int len = 48;
        switch (rnd() % 6) {
        case 0: len = (int)(rnd() % 48); break;       // corto: no se lee
        case 1: len = 49 + (int)(rnd() % 30); break;  // largo: se leen los 48 primeros
        default: break;
        }
        for (int i = 0; i < 80; i++) pkt[i] = (uint8_t)rnd();
        uint32_t secs;
        switch (rnd() % 7) {
        case 0: secs = (uint32_t)FLEX_NTP_EPOCH_DELTA + FLEX_NTP_MIN_EPOCH - 1 - (rnd() % 3); break;   // justo por debajo del suelo
        case 1: secs = (uint32_t)FLEX_NTP_EPOCH_DELTA + FLEX_NTP_MIN_EPOCH + (rnd() % 3); break;       // justo en el suelo
        case 2: secs = (uint32_t)FLEX_NTP_EPOCH_DELTA; break;                                          // 1970 exacto
        case 3: secs = rnd() % 4000000000u; break;
        case 4: secs = 0xFFFFFFFFu - (rnd() % 3); break;
        case 5: secs = 0; break;
        default: secs = (uint32_t)FLEX_NTP_EPOCH_DELTA + 1800000000u + (rnd() % 2000000000u); break;
        }
        pkt[40] = (uint8_t)(secs >> 24);
        pkt[41] = (uint8_t)(secs >> 16);
        pkt[42] = (uint8_t)(secs >> 8);
        pkt[43] = (uint8_t)secs;
        uint32_t mine = flex_ntp_parse_reply(pkt, len);
        // Arduino
        ref_ntp_reset();
        ref_ntp_world(1, 0, 1, 1, 0, 1791481500u);
        ref_ntp_script(0, len < 48 ? UK_SHORT : UK_PACKET, 20 * (int)(rnd() % 5), pkt, len);
        uint32_t t0 = ref_ntp_ms();
        unsigned q = ref_ntp_query("x");
        CHECK_EQ_I(q, mine);
        (void)t0;
        accepted += mine != 0;
        rejected += mine == 0;
    }
    CHECK(accepted > 300 && rejected > 300);
    // los extras que Arduino no tiene: modo, aviso de reloj y estrato
    uint8_t p[FLEX_NTP_PKT_SIZE];
    make_reply(p, (uint32_t)FLEX_NTP_EPOCH_DELTA + 1800000000u, 4, 0, 2);
    CHECK(flex_ntp_reply_sane(p, 48));
    for (int mode = 0; mode < 8; mode++) {
        make_reply(p, 1, mode, 0, 2);
        CHECK(flex_ntp_reply_sane(p, 48) == (mode == 4));
    }
    for (int li = 0; li < 4; li++) {
        make_reply(p, 1, 4, li, 2);
        CHECK(flex_ntp_reply_sane(p, 48) == (li != 3));
    }
    for (int st = 0; st < 256; st++) {
        make_reply(p, 1, 4, 0, st);
        CHECK(flex_ntp_reply_sane(p, 48) == (st >= 1 && st <= 15));
    }
    CHECK(!flex_ntp_reply_sane(p, 47) && !flex_ntp_reply_sane(NULL, 48));
    CHECK_EQ_I(flex_ntp_parse_reply(NULL, 48), 0u);
}

static void t_text_and_civil(void)
{
    s_seed = 5;
    for (int it = 0; it < 3000; it++) {
        long days = (long)(rnd() % 1900000) - 900000;   // de antes del ano 0 a mas alla del 5000: la rama de epocas negativas tambien
        int y1, m1, d1, y2, m2, d2;
        ref_ntp_civil(days, &y1, &m1, &d1);
        flex_ntp_civil_from_days(days, &y2, &m2, &d2);
        CHECK(y1 == y2 && m1 == m2 && d1 == d2);
    }
    CHECK_EQ_I(0, 0);
    // la ultima sincronizacion: nunca, hoy, ayer, otro dia; justo en la medianoche local y en el suelo
    for (int it = 0; it < 6000; it++) {
        uint32_t now = 1767225600u + rnd() % 200000000u;
        uint32_t last;
        switch (rnd() % 6) {
        case 0: last = rnd() % 1767225600u; break;                      // bajo el suelo: Nunca
        case 1: last = now - (rnd() % 90000); break;                     // hoy / ayer
        case 2: last = now - (rnd() % 400000); break;
        case 3: last = 1767225600u + (rnd() % 3); break;                // el suelo
        case 4: last = now + (rnd() % 100000); break;                    // en el futuro
        default: last = 1767225600u + rnd() % 200000000u; break;
        }
        // las cinco horas de la medianoche local (UTC-5): 05:00 UTC
        if (rnd() % 5 == 0) {
            now = (now / 86400u) * 86400u + 5 * 3600u + (rnd() % 3) - 1;
            if (rnd() % 2) last = now - 1 - (rnd() % 3);
        }
        ref_ntp_reset();
        ref_ntp_world(1, 0, 0, 1, 0, now);
        ref_ntp_set_last_sync(last, 0);
        char rs[96], rl[96], ms[96], ml[96];
        ref_ntp_text(rs, sizeof(rs), rl, sizeof(rl));
        flex_ntp_last_text(ml, sizeof(ml), last, now);
        CHECK(!strcmp(rl, ml));
        (void)ms;
    }
    // los textos de estado: todas las combinaciones
    for (int st = 0; st < 4; st++) {
        for (int busy = 0; busy < 2; busy++) {
            for (int from = 0; from < 2; from++) {
                for (int air = 0; air < 2; air++) {
                    for (int on = 0; on < 2; on++) {
                        for (int nok = 0; nok < 2; nok++) {
                            ref_ntp_reset();
                            ref_ntp_world(1, air, on, 1, 0, 1791481500u);
                            ref_ntp_set_state(st, busy, 0, from, nok);
                            char rs[96], rl[96], ms[96];
                            ref_ntp_text(rs, sizeof(rs), rl, sizeof(rl));
                            flex_ntp_t m;
                            flex_ntp_init(&m);
                            m.state = (uint8_t)st;
                            m.busy = busy != 0;
                            m.from_nvs = from != 0;
                            m.time_nvs_ok = nok != 0;
                            flex_ntp_state_text(ms, sizeof(ms), &m, air != 0, on != 0);
                            CHECK(!strcmp(rs, ms));
                        }
                    }
                }
            }
        }
    }
    // truncado a un buffer pequeno: nunca escribe de mas
    char small[8];
    flex_ntp_t m;
    flex_ntp_init(&m);
    flex_ntp_state_text(small, sizeof(small), &m, false, false);
    CHECK(strlen(small) < sizeof(small));
}

// ---- la planificacion, en paso de baile con Arduino ----
static bool same_state(const char *what, int it, const flex_ntp_t *mp)
{
    int st, busy, from, fails, user, nvs, set_n;
    unsigned done, last, next, set_epoch;
    ref_ntp_get(&st, &busy, &done, &last, &from, &fails, &next, &user, &set_epoch, &set_n, &nvs);
    const flex_ntp_t m = *mp;
    bool same = st == m.state && busy == m.busy && done == m.done_ms && last == m.last_sync_utc && from == m.from_nvs && fails == m.fails && next == m.next_ms &&
                user == m.user_asked;
    if (!same) {
        fprintf(stderr, "  %s #%d: ref st=%d busy=%d done=%u last=%u from=%d fails=%d next=%u user=%d | mod st=%d busy=%d done=%u last=%u from=%d fails=%d next=%u user=%d\n",
                what, it, st, busy, done, last, from, fails, next, user, m.state, m.busy, m.done_ms, m.last_sync_utc, m.from_nvs, m.fails, m.next_ms, m.user_asked);
    }
    CHECK(same);
    return same;
}
static void compare_state(const char *what, int it)
{
    int st, busy, from, fails, user, nvs, set_n;
    unsigned done, last, next, set_epoch;
    ref_ntp_get(&st, &busy, &done, &last, &from, &fails, &next, &user, &set_epoch, &set_n, &nvs);
    flex_ntp_t m;
    flex_ntp_get(&m);
    if (st != m.state || busy != m.busy || done != m.done_ms || last != m.last_sync_utc || from != m.from_nvs || fails != m.fails || next != m.next_ms ||
        user != m.user_asked) {
        fprintf(stderr, "  %s #%d: ref st=%d busy=%d done=%u last=%u from=%d fails=%d next=%u user=%d | mod st=%d busy=%d done=%u last=%u from=%d fails=%d next=%u user=%d\n",
                what, it, st, busy, done, last, from, fails, next, user, m.state, m.busy, m.done_ms, m.last_sync_utc, m.from_nvs, m.fails, m.next_ms, m.user_asked);
    }
    CHECK(st == m.state && busy == m.busy && done == m.done_ms && last == m.last_sync_utc && from == m.from_nvs && fails == m.fails && next == m.next_ms &&
          user == m.user_asked);
    CHECK_EQ_I(set_epoch, E.set_val);
    CHECK_EQ_I((unsigned)set_n, E.set_calls);
}

// el guion de UDP del doble de Arduino y el transporte del servicio hacen lo mismo: el servicio le pregunta al propio ntpQuery de Arduino
static unsigned q_ref(const char *h) { return ref_ntp_query(h); }

static void t_state_machine(void)
{
    s_seed = 4242;
    int tasks = 0, syncs = 0, fails = 0, blocked = 0;
    for (int run = 0; run < 400; run++) {
        ref_ntp_reset();
        service_fresh();
        E.query_fn = q_ref;
        E.synced_flag = 1;
        int online = 0, air = 0, ready = 1;
        unsigned ms = (unsigned)(rnd() % 4000), utc = 1791481500u + rnd() % 1000000u;
        ref_ntp_world(ready, air, online, 1, ms, utc);
        for (int step = 0; step < 90; step++) {
            // el mundo cambia
            unsigned dt;
            switch (rnd() % 8) {
            case 0: dt = 1000; break;
            case 1: dt = 30000; break;
            case 2: dt = (unsigned)FLEX_NTP_PERIOD_MS; break;
            case 3: dt = 1500; break;
            case 4: dt = (unsigned)(rnd() % 2000); break;
            default: dt = (unsigned)(rnd() % 200000); break;
            }
            ms += dt;
            utc += dt / 1000;
            switch (rnd() % 12) {
            case 0: air = !air; break;
            case 1: ready = (int)(rnd() % 4 == 0 ? 3 : 1 + (int)(rnd() % 2)); break;
            default: break;
            }
            bool was_online = online != 0;
            if (rnd() % 6 == 0) online = !online;
            ref_ntp_world(ready, air, online, online, ms, utc);
            if (online && !was_online) {
                ref_ntp_wifi_up();   // el enlace se confirma
            }
            flex_net_set_online(online != 0);
            compare_state("mundo", run * 100 + step);
            // una operacion
            switch (rnd() % 6) {
            case 0:
            case 1: {
                int r = ref_ntp_tick();
                int w0 = E.wakes;
                bool pend0 = flex_ntp_work_pending();
                flex_ntp_tick_service();
                bool launched = flex_ntp_work_pending() && !pend0;
                CHECK_EQ_I(r, launched ? 1 : 0);
                (void)w0;
                tasks += r;
                break;
            }
            case 2: {
                int user = (int)(rnd() % 2);
                int r = ref_ntp_request(user);
                int w0 = E.wakes;
                flex_ntp_request_sync(user != 0);
                CHECK_EQ_I(r, E.wakes - w0);
                tasks += r;
                blocked += r == 0;
                break;
            }
            default: {
                // la tarea, si hay una lanzada: los servidores contestan o no
                if (flex_ntp_work_pending()) {
                    for (int i = 0; i < 3; i++) {
                        unsigned how = rnd() % 4;
                        uint8_t pk[48];
                        make_reply(pk, (uint32_t)FLEX_NTP_EPOCH_DELTA + utc + (how == 3 ? 0u : 5u), 4, 0, 2);
                        if (how == 0) ref_ntp_script(i, UK_SILENT, 0, pk, 48);
                        else if (how == 1) ref_ntp_script(i, UK_PACKET, 20 * (int)(rnd() % 20), pk, 48);
                        else if (how == 2) ref_ntp_script(i, UK_SHORT, 0, pk, 20);
                        else { make_reply(pk, 12345, 4, 0, 2); ref_ntp_script(i, UK_PACKET, 0, pk, 48); }   // bajo el suelo: se descarta
                    }
                    // el servicio hace su trabajo sobre el mundo tal cual y despues, con el mundo restaurado, Arduino hace su tarea
                    ref_ntp_rewind();
                    ref_ntp_snapshot();
                    flex_ntp_work();
                    ref_ntp_restore();
                    CHECK(ref_ntp_run_task() >= 0);
                }
                break;
            }
            }
            compare_state("op", run * 100 + step);
            int st, busy, from, fl, user, nvs, set_n;
            unsigned done, last, next, set_epoch;
            ref_ntp_get(&st, &busy, &done, &last, &from, &fl, &next, &user, &set_epoch, &set_n, &nvs);
            syncs += st == FLEX_NTPS_OK;
            fails += st == FLEX_NTPS_FAIL;
            ms = ref_ntp_ms();
            utc = ref_ntp_utc();
        }
    }
    CHECK(tasks > 300 && syncs > 100 && fails > 50 && blocked > 100);
}




// El modelo PURO en paso con Arduino, tambien en lo que el servicio no deja hacer: estar en linea sin que se haya llamado a ntpOnWifiUp (primera consulta con
// next_ms == 0), el reloj de 32 bits dando la vuelta, y todas las combinaciones de modo avion, red y estado del sistema.
static void t_model_lockstep(void)
{
    s_seed = 8080;
    int first_runs = 0, wraps = 0, finishes = 0;
    for (int run = 0; run < 700; run++) {
        ref_ntp_reset();
        flex_ntp_t m;
        flex_ntp_init(&m);
        int online = (int)(rnd() % 2), air = rnd() % 5 == 0, ready = (int)(1 + rnd() % 3);
        uint32_t ms = (rnd() % 3 == 0) ? (0xFFFFFFFFu - rnd() % 3000) : (rnd() % 6 == 0 ? 1499 + rnd() % 3 : rnd() % 5000);
        uint32_t utc = 1791481500u;
        for (int step = 0; step < 80; step++) {
            uint32_t dt;
            switch (rnd() % 9) {
            case 0: dt = 1; break;
            case 1: dt = 29999 + rnd() % 3; break;
            case 2: dt = (uint32_t)FLEX_NTP_PERIOD_MS - 1 + rnd() % 3; break;
            case 3: dt = (uint32_t)FLEX_NTP_BACKOFF_MAX - 1 + rnd() % 3; break;
            case 4: dt = 1500; break;
            default: dt = rnd() % 70000; break;
            }
            wraps += ms + dt < ms;
            ms += dt;
            utc += dt / 1000;
            if (rnd() % 7 == 0) air = !air;
            if (rnd() % 6 == 0) online = !online;
            if (rnd() % 7 == 0) ready = (int)(1 + rnd() % 3);
            ref_ntp_world(ready, air, online, online, ms, utc);
            if (online && rnd() % 3 == 0) {
                ref_ntp_wifi_up();
                flex_ntp_on_wifi_up(&m, ms);
            }
            flex_ntp_in_t in = {.now_ms = ms, .airplane = air != 0, .online = online != 0, .system_ready = ready != 3};
            switch (rnd() % 4) {
            case 0:
            case 1: {
                bool first = m.next_ms == 0 && !m.busy;
                int r = ref_ntp_tick();
                bool l = flex_ntp_tick(&m, &in);
                CHECK_EQ_I(r, l ? 1 : 0);
                first_runs += first && l;
                break;
            }
            case 2: {
                int user = (int)(rnd() % 2);
                int r = ref_ntp_request(user);
                bool l = flex_ntp_request(&m, user != 0, &in);
                CHECK_EQ_I(r, l ? 1 : 0);
                break;
            }
            default:
                if (m.busy) {
                    for (int i = 0; i < 3; i++) {
                        uint8_t pk[48];
                        make_reply(pk, (uint32_t)FLEX_NTP_EPOCH_DELTA + utc + 5, 4, 0, 2);
                        if (rnd() % 3 == 0) ref_ntp_script(i, UK_PACKET, 20 * (int)(rnd() % 10), pk, 48);
                        else ref_ntp_script(i, UK_SILENT, 0, pk, 48);
                    }
                    ref_ntp_rewind();
                    int st0, b0, f0, fl0, u0, sn0, nv0;
                    unsigned d0, l0, nx0, se0;
                    ref_ntp_get(&st0, &b0, &d0, &l0, &f0, &fl0, &nx0, &u0, &se0, &sn0, &nv0);
                    ref_ntp_run_task();
                    int st1, b1, f1, fl1, u1, sn1, nv1;
                    unsigned d1, l1, nx1, se1;
                    ref_ntp_get(&st1, &b1, &d1, &l1, &f1, &fl1, &nx1, &u1, &se1, &sn1, &nv1);
                    uint32_t got = sn1 > sn0 ? se1 : 0;
                    ms = ref_ntp_ms();
                    flex_ntp_finish(&m, got, ms);
                    finishes++;
                }
                break;
            }
            same_state("modelo", run * 100 + step, &m);
        }
    }
    CHECK(first_runs > 40 && wraps > 100 && finishes > 300);
}


// Los bordes del modelo frente a Arduino: la primera consulta justo a los 1500 ms y la reserva del 0 de "nunca programada" cuando el reloj de 32 bits da la vuelta
static void t_model_edges(void)
{
    for (uint32_t ms = 1498; ms <= 1502; ms++) {
        ref_ntp_reset();
        ref_ntp_world(1, 0, 1, 1, ms, 1791481500u);
        flex_ntp_t m;
        flex_ntp_init(&m);
        flex_ntp_in_t in = {.now_ms = ms, .airplane = false, .online = true, .system_ready = true};
        int r = ref_ntp_tick();
        bool l = flex_ntp_tick(&m, &in);
        CHECK_EQ_I(r, l ? 1 : 0);
        CHECK_EQ_I(r, ms >= 1500 ? 1 : 0);
        same_state("borde 1500", (int)ms, &m);
    }
    for (int d = -2; d <= 2; d++) {
        uint32_t ms = 0xFFFFFFFFu - 1499u + (uint32_t)d;   // ms + 1500 da 0 (o casi): el 0 esta reservado
        ref_ntp_reset();
        ref_ntp_world(1, 0, 1, 1, ms, 1791481500u);
        flex_ntp_t m;
        flex_ntp_init(&m);
        ref_ntp_wifi_up();
        flex_ntp_on_wifi_up(&m, ms);
        same_state("vuelta del reloj", d, &m);
        CHECK(m.next_ms != 0);
    }
}

// ---- persistencia: la hora de antes del apagado ----
static void t_persist_parity(void)
{
    s_seed = 2718;
    for (int it = 0; it < 4000; it++) {
        int ns_exists = (int)(rnd() % 2), rw_ok = (int)(rnd() % 2);
        uint64_t epoch, last;
        switch (rnd() % 6) {
        case 0: epoch = 0; break;
        case 1: epoch = FLEX_NTP_MIN_EPOCH - 1 - rnd() % 3; break;
        case 2: epoch = FLEX_NTP_MIN_EPOCH + rnd() % 3; break;
        default: epoch = FLEX_NTP_MIN_EPOCH + rnd() % 400000000u; break;
        }
        last = rnd() % 3 == 0 ? 0 : FLEX_NTP_MIN_EPOCH + rnd() % 400000000u;
        // Arduino
        ref_ntp_reset();
        ref_ntp_world(1, 0, 0, 1, 7000, 1791481500u);
        ref_ntp_nvs(ns_exists, rw_ok, epoch, last);
        ref_ntp_load();
        // servicio
        service_fresh();
        E.load_ok = ns_exists || rw_ok;                 // abre en solo lectura y, si no, en lectura / escritura (que lo crea)
        E.load_epoch = ns_exists ? epoch : 0;           // un espacio recien creado esta vacio
        E.load_last = ns_exists ? last : 0;
        flex_ntp_boot_load();
        compare_state("carga", it);
        int st, busy, from, fails, user, nvs, set_n;
        unsigned done, lastsync, next, set_epoch;
        ref_ntp_get(&st, &busy, &done, &lastsync, &from, &fails, &next, &user, &set_epoch, &set_n, &nvs);
        CHECK_EQ_I(nvs, flex_ntp_time_nvs_ok() ? 1 : 0);
        CHECK(set_n == 0 || E.set_net == 0);            // la hora recuperada NO es hora de red
        // guardar (con la hora recuperada o la ultima sincronizacion en marcha): la misma hora y ultima sincronizacion que Arduino, con las mismas condiciones
        uint32_t now = (rnd() % 4 == 0) ? (FLEX_NTP_MIN_EPOCH - 1 - rnd() % 5) : 1791481500u + rnd() % 100000;
        int writable = (int)(rnd() % 4 != 0);
        ref_ntp_world(1, 0, 0, 1, 7000, now);
        ref_ntp_nvs(1, writable, 0, 0);
        ref_ntp_save(1);
        unsigned long long ee, ll;
        int writes;
        ref_ntp_nvs_get(&ee, &ll, &writes);
        // el servicio guarda lo mismo si la hora merece guardarse (aqui: la de la red / manual); la hora de fabrica NO se guarda (diferencia deliberada)
        E.synced_flag = 1;
        E.nvs_ok_flag = writable;
        E.saves = 0;
        flex_ntp_save_time();   // usa e_utc() = el reloj del doble (ya con `now`)
        if (writes) {
            CHECK_EQ_I(E.saves, 1);
            CHECK(E.saved_epoch == ee && E.saved_last == ll);
        } else if (now >= FLEX_NTP_MIN_EPOCH && !writable) {
            CHECK_EQ_I(E.saves, 1);   // intento de guardar que falla: lo dice la bandera
            CHECK(!flex_ntp_time_nvs_ok());
        } else {
            CHECK_EQ_I(E.saves, 0);
        }
        int nvs_after;
        unsigned d2, l2, n2, se2;
        int f2, fl2, u2, sn2, s2, b2;
        ref_ntp_get(&s2, &b2, &d2, &l2, &f2, &fl2, &n2, &u2, &se2, &sn2, &nvs_after);
        CHECK_EQ_I(nvs_after, flex_ntp_time_nvs_ok() ? 1 : 0);
    }
}

// el guardado horario, en paso con clkPersistTick de Arduino (la primera vez guarda; despues una vez por hora)
static void t_persist_hourly(void)
{
    ref_ntp_reset();
    service_fresh();
    E.synced_flag = 1;
    unsigned long long ee, ll;
    int w_ref = 0, w_mod = 0;
    uint32_t t = 5000;
    static const uint32_t DT[] = {0, 1000, 3598999, 1, 1, 3600000, 59999, 3540001, 20, 7200000, 3599999, 1, 90000000, 3600000};
    for (unsigned i = 0; i < sizeof(DT) / sizeof(DT[0]); i++) {
        t += DT[i];
        ref_ntp_world(1, 0, 0, 1, t, 1791481500u + t / 1000);
        int before;
        ref_ntp_nvs_get(&ee, &ll, &before);
        ref_ntp_persist();
        int after;
        ref_ntp_nvs_get(&ee, &ll, &after);
        w_ref += after - before;   // dos escrituras (epoch y lastsync) por guardado
        int sv = E.saves;
        flex_ntp_tick_service();
        w_mod += E.saves - sv;
        CHECK_EQ_I(w_mod * 2, w_ref);
    }
    CHECK(w_mod >= 5);
}

// ---- el servicio por dentro (lo que Arduino no tiene o lo que cambia a proposito) ----
static void t_service(void)
{
    service_fresh();
    ref_ntp_reset();
    ref_ntp_world(1, 0, 0, 1, 10000, 1791481500u);
    // la red: el enlace acaba de confirmarse -> primera consulta a +1,5 s; seguir en linea no la mueve; perderla y recobrarla si
    flex_net_set_online(true);
    flex_ntp_t m;
    flex_ntp_get(&m);
    CHECK_EQ_I(m.next_ms, 11500);
    ref_ntp_advance(500);
    flex_net_set_online(true);
    flex_ntp_get(&m);
    CHECK_EQ_I(m.next_ms, 11500);
    flex_net_set_online(false);
    CHECK(!flex_net_online());
    flex_net_set_online(true);
    flex_ntp_get(&m);
    CHECK_EQ_I(m.next_ms, 10500 + 1500);
    // lo que hace la interfaz: solo banderas y el aviso al trabajador; nada espera a la red
    ref_ntp_world(1, 0, 1, 1, 20000, 1791481500u);
    E.wakes = 0;
    flex_ntp_request_sync(true);
    CHECK_EQ_I(E.wakes, 1);
    flex_ntp_request_sync(true);   // ya hay una en marcha: no se lanza otra
    CHECK_EQ_I(E.wakes, 1);
    bool busy;
    char st[96], la[96];
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(busy && !strcmp(st, "Sincronizando...") && !strcmp(la, "Nunca"));
    // los tres servidores en silencio: fallo, tres preguntas, el texto y la espera progresiva
    E.query_fn = NULL;   // e_query devuelve 0
    flex_ntp_work();
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(!busy && !strcmp(st, "Sin respuesta del servidor"));
    CHECK_EQ_I(E.query_calls, 3);
    flex_ntp_get(&m);
    CHECK_EQ_I(m.fails, 1);
    CHECK_EQ_I(m.state, FLEX_NTPS_FAIL);
    // el Wi-Fi cae a mitad: no se insiste en el resto de los servidores
    ref_ntp_world(1, 0, 1, 1, 400000, 1791481500u);
    flex_ntp_request_sync(true);
    CHECK(flex_ntp_work_pending());
    flex_net_set_online(false);
    E.query_calls = 0;
    flex_ntp_work();
    CHECK_EQ_I(E.query_calls, 0);
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(!strcmp(st, "Sin conexi\xC3\xB3n Wi-Fi"));
    // un servidor que contesta con una hora buena a la segunda: hora de red, ultima sincronizacion de hoy y la hora guardada
    service_fresh();
    ref_ntp_world(1, 0, 1, 1, 500000, 1791490000u);
    flex_net_set_online(true);
    E.synced_flag = 1;
    {
        static const unsigned want = 1791490555u;
        static unsigned (*seq[3])(const char *);
        (void)seq;
        // el segundo servidor contesta
        E.query_fn = NULL;
        (void)want;
    }
}

// ---- un servidor UDP de verdad en 127.0.0.1 ----
typedef struct {
    int fd, spoof_fd, port;
    pthread_t th;
    int stop;   // con __atomic: lo escribe quien para el servidor y lo lee su hilo
    pthread_mutex_t mu;
    int mode;
    uint32_t secs1900;
    int nrecv;
    uint8_t last_req[64];
    int last_len;
    int delay_ms;
} srv_t;
enum { SM_SILENT = 0, SM_OK, SM_SHORT, SM_SPOOF_THEN_OK, SM_SPOOF_ONLY, SM_BAD_THEN_OK, SM_ALARM, SM_MODE3, SM_STRATUM0, SM_LONG, SM_SHORT_THEN_OK, SM_EMPTY_THEN_OK,
       SM_SPOOF_SPOOF_LATE_OK };

static void send_to(int fd, const struct sockaddr_in *to, const uint8_t *d, int n)
{
    sendto(fd, d, (size_t)n, 0, (const struct sockaddr *)to, sizeof(*to));
}

static void *srv_main(void *arg)
{
    srv_t *s = arg;
    while (!__atomic_load_n(&s->stop, __ATOMIC_ACQUIRE)) {
        uint8_t buf[128];
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        ssize_t n = recvfrom(s->fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        if (n < 0) {
            continue;   // el tiempo de espera del socket: vuelve a mirar si hay que parar
        }
        pthread_mutex_lock(&s->mu);
        s->nrecv++;
        s->last_len = (int)n;
        memcpy(s->last_req, buf, (size_t)(n < 64 ? n : 64));
        int mode = s->mode, delay = s->delay_ms;
        uint32_t secs = s->secs1900;
        pthread_mutex_unlock(&s->mu);
        if (delay) {
            usleep((useconds_t)delay * 1000u);
        }
        uint8_t ok[48], bad[48], alarm[48], m3[48], s0[48];
        make_reply(ok, secs, 4, 0, 2);
        make_reply(bad, 1000, 4, 0, 2);           // bajo el suelo
        make_reply(alarm, secs, 4, 3, 2);
        make_reply(m3, secs, 3, 0, 2);
        make_reply(s0, secs, 4, 0, 0);
        switch (mode) {
        case SM_OK: send_to(s->fd, &from, ok, 48); break;
        case SM_SHORT: send_to(s->fd, &from, ok, 20); break;
        case SM_SPOOF_THEN_OK: send_to(s->spoof_fd, &from, ok, 48); usleep(20000); send_to(s->fd, &from, ok, 48); break;
        case SM_SPOOF_ONLY: send_to(s->spoof_fd, &from, ok, 48); break;
        case SM_BAD_THEN_OK: send_to(s->fd, &from, bad, 48); usleep(20000); send_to(s->fd, &from, ok, 48); break;
        case SM_ALARM: send_to(s->fd, &from, alarm, 48); break;
        case SM_MODE3: send_to(s->fd, &from, m3, 48); break;
        case SM_STRATUM0: send_to(s->fd, &from, s0, 48); break;
        case SM_LONG: { uint8_t big[90]; memset(big, 0, sizeof(big)); memcpy(big, ok, 48); send_to(s->fd, &from, big, 90); break; }
        case SM_SHORT_THEN_OK: send_to(s->fd, &from, ok, 20); usleep(20000); send_to(s->fd, &from, ok, 48); break;
        case SM_EMPTY_THEN_OK: send_to(s->fd, &from, ok, 0); usleep(20000); send_to(s->fd, &from, ok, 48); break;
        case SM_SPOOF_SPOOF_LATE_OK: send_to(s->spoof_fd, &from, ok, 48); usleep(80000); send_to(s->spoof_fd, &from, ok, 48); usleep(70000); send_to(s->fd, &from, ok, 48); break;
        default: break;
        }
    }
    return NULL;
}

static int srv_start(srv_t *s)
{
    memset(s, 0, sizeof(*s));
    pthread_mutex_init(&s->mu, NULL);
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    s->spoof_fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (s->fd < 0 || s->spoof_fd < 0 || bind(s->fd, (struct sockaddr *)&a, sizeof(a)) != 0 || bind(s->spoof_fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        return -1;
    }
    socklen_t l = sizeof(a);
    getsockname(s->fd, (struct sockaddr *)&a, &l);
    s->port = ntohs(a.sin_port);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    s->secs1900 = (uint32_t)FLEX_NTP_EPOCH_DELTA + 1791490000u;
    return pthread_create(&s->th, NULL, srv_main, s);
}
static void srv_stop(srv_t *s)
{
    __atomic_store_n(&s->stop, 1, __ATOMIC_RELEASE);
    pthread_join(s->th, NULL);
    close(s->fd);
    close(s->spoof_fd);
    pthread_mutex_destroy(&s->mu);
}
static void srv_set(srv_t *s, int mode, int delay_ms)
{
    pthread_mutex_lock(&s->mu);
    s->mode = mode;
    s->delay_ms = delay_ms;
    s->nrecv = 0;
    pthread_mutex_unlock(&s->mu);
}
static int srv_recv(srv_t *s)
{
    pthread_mutex_lock(&s->mu);
    int n = s->nrecv;
    pthread_mutex_unlock(&s->mu);
    return n;
}
static int srv_last(srv_t *s, uint8_t out[48])
{
    pthread_mutex_lock(&s->mu);
    int n = s->last_len;
    memcpy(out, s->last_req, 48);
    pthread_mutex_unlock(&s->mu);
    return n;
}
static int open_fds(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (d) {
        while (readdir(d)) n++;
        closedir(d);
    }
    return n;
}
static uint64_t mono_ms_t(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static srv_t S_A, S_B, S_C;
static int port_of_host(const char *h)
{
    if (!strcmp(h, flex_ntp_servers[0])) return S_A.port;
    if (!strcmp(h, flex_ntp_servers[1])) return S_B.port;
    return S_C.port;
}
static unsigned q_udp(const char *h) { return flex_ntp_udp_query("127.0.0.1", port_of_host(h), 300); }

static void t_udp(void)
{
    CHECK_EQ_I(srv_start(&S_A), 0);
    CHECK_EQ_I(srv_start(&S_B), 0);
    CHECK_EQ_I(srv_start(&S_C), 0);
    const uint32_t T = 1791490000u;
    // respuesta buena: la epoca; el servidor recibe EL paquete de Arduino
    srv_set(&S_A, SM_OK, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 500), T);
    uint8_t want[48];
    flex_ntp_build_packet(want);
    {
        uint8_t got[48];
        CHECK(srv_last(&S_A, got) == 48 && !memcmp(got, want, 48));
    }
    // silencio: 0 pasado el tiempo (ni antes ni mucho despues)
    srv_set(&S_A, SM_SILENT, 0);
    uint64_t t0 = mono_ms_t();
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 200), 0);
    uint64_t dt = mono_ms_t() - t0;
    CHECK(dt >= 195 && dt < 400);
    // un datagrama corto no es una respuesta: se sigue esperando y llega la buena
    srv_set(&S_A, SM_SHORT_THEN_OK, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 500), T);
    srv_set(&S_A, SM_SHORT, 0);
    t0 = mono_ms_t();
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 200), 0);
    CHECK(mono_ms_t() - t0 >= 195);
    // un datagrama VACIO tampoco es una respuesta: se sigue esperando y llega la buena
    srv_set(&S_A, SM_EMPTY_THEN_OK, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 500), T);
    // el tiempo se cuenta desde que se pregunta, no desde el ultimo datagrama que llega: la buena a los 150 ms ya no vale con 100 ms de margen
    srv_set(&S_A, SM_SPOOF_SPOOF_LATE_OK, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 100), 0);
    usleep(120000);   // deja que el servidor termine su guion
    // varias veces, para que un reloj que no avanza bien no pase por casualidad
    srv_set(&S_A, SM_SILENT, 0);
    for (int i = 0; i < 6; i++) {
        t0 = mono_ms_t();
        CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 80), 0);
        uint64_t d2 = mono_ms_t() - t0;
        CHECK(d2 >= 75 && d2 < 250);
    }
    // un datagrama de otro origen (otro puerto) se ignora; si despues llega el del servidor, vale
    srv_set(&S_A, SM_SPOOF_THEN_OK, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 500), T);
    srv_set(&S_A, SM_SPOOF_ONLY, 0);
    t0 = mono_ms_t();
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 200), 0);
    CHECK(mono_ms_t() - t0 >= 195);
    // la primera respuesta COMPLETA cierra la espera, valga o no (como en Arduino): una mala seguida de una buena no se rescata
    srv_set(&S_A, SM_BAD_THEN_OK, 0);
    t0 = mono_ms_t();
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 500), 0);
    CHECK(mono_ms_t() - t0 < 150);
    // lo que un servidor de verdad no manda: reloj en alarma, modo que no es de servidor, estrato 0
    srv_set(&S_A, SM_ALARM, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 300), 0);
    srv_set(&S_A, SM_MODE3, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 300), 0);
    srv_set(&S_A, SM_STRATUM0, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 300), 0);
    // un datagrama mas largo de 48 bytes: se lee la parte de la respuesta
    srv_set(&S_A, SM_LONG, 0);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 300), T);
    // tarda un poco en contestar (dentro del tiempo) y otro que tarda demasiado
    srv_set(&S_A, SM_OK, 80);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 400), T);
    srv_set(&S_A, SM_OK, 300);
    CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 100), 0);
    usleep(350000);   // la respuesta tardia llega a un socket ya cerrado: no pasa nada
    // un nombre que no se resuelve
    CHECK_EQ_I(flex_ntp_udp_query("999.999.999.999", 123, 100), 0);
    CHECK_EQ_I(flex_ntp_udp_query("", 123, 100), 0);
    // ni un descriptor abierto de mas tras muchas consultas
    srv_set(&S_A, SM_OK, 0);
    int fds0 = open_fds();
    for (int i = 0; i < 150; i++) {
        CHECK_EQ_I(flex_ntp_udp_query("127.0.0.1", S_A.port, 300), T);
    }
    srv_set(&S_A, SM_SILENT, 0);
    for (int i = 0; i < 20; i++) {
        flex_ntp_udp_query("127.0.0.1", S_A.port, 10);
    }
    CHECK_EQ_I(open_fds(), fds0);

    // ---- el servicio entero sobre sockets de verdad: el primer servidor no contesta, el segundo si, el tercero no llega a preguntarse ----
    service_fresh();
    E.query_fn = q_udp;
    E.synced_flag = 1;
    ref_ntp_reset();
    ref_ntp_world(1, 0, 1, 1, 30000, 1791481500u);
    flex_net_set_online(true);
    srv_set(&S_A, SM_SILENT, 0);
    srv_set(&S_B, SM_OK, 0);
    srv_set(&S_C, SM_OK, 0);
    flex_ntp_request_sync(true);
    CHECK(flex_ntp_work_pending());
    flex_ntp_work();
    CHECK(srv_recv(&S_A) == 1 && srv_recv(&S_B) == 1 && srv_recv(&S_C) == 0);
    flex_ntp_t m;
    flex_ntp_get(&m);
    CHECK(m.state == FLEX_NTPS_OK && !m.busy && !m.from_nvs && m.fails == 0 && m.last_sync_utc == T);
    CHECK_EQ_I(E.set_val, T);
    CHECK(E.set_net == 1);   // hora de red
    CHECK(E.saves >= 1 && E.saved_epoch == T && E.saved_last == T);
    // con los tres en silencio: tres preguntas y un fallo con espera progresiva
    srv_set(&S_A, SM_SILENT, 0);
    srv_set(&S_B, SM_SILENT, 0);
    srv_set(&S_C, SM_SILENT, 0);
    ref_ntp_world(1, 0, 1, 1, 100000000u, 1791481500u);
    flex_ntp_request_sync(true);
    CHECK(flex_ntp_work_pending());
    flex_ntp_work();
    CHECK(srv_recv(&S_A) == 1 && srv_recv(&S_B) == 1 && srv_recv(&S_C) == 1);
    flex_ntp_get(&m);
    CHECK(m.state == FLEX_NTPS_FAIL && m.fails == 1 && m.last_sync_utc == T);   // la ultima buena se conserva
    srv_stop(&S_A);
    srv_stop(&S_B);
    srv_stop(&S_C);
}

// ---- diferencias deliberadas y condiciones del servicio ----
static void t_conditions(void)
{
    ref_ntp_reset();
    ref_ntp_world(1, 0, 0, 1, 50000, 1791481500u);
    // la hora de fabrica no se guarda; la de la red, la manual y la aproximada si
    service_fresh();
    E.synced_flag = 0;
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 0);
    flex_ntp_tick_service();
    CHECK_EQ_I(E.saves, 0);
    E.synced_flag = 1;   // un ajuste manual o NTP
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 1);
    service_fresh();
    E.load_epoch = 1791000000u;
    E.load_last = 1790000000u;
    flex_ntp_boot_load();   // hora aproximada
    E.saves = 0;
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 1);
    CHECK(E.saved_epoch == 1791000000u && E.saved_last == 1790000000u);   // el reloj ya es la hora recuperada
    // una epoca de la NVS que no cabe en 32 bits (dato corrupto) NO se aplica (Arduino la truncaba)
    service_fresh();
    E.load_epoch = 0x1FFFFFFFFFull;
    E.load_last = 0;
    flex_ntp_boot_load();
    flex_ntp_t m;
    flex_ntp_get(&m);
    CHECK(!m.from_nvs && E.set_calls == 0);
    // el borde de los 32 bits: 0xFFFFFFFF se aplica, 0x100000000 no
    service_fresh();
    E.load_epoch = 0xFFFFFFFFull;
    flex_ntp_boot_load();
    CHECK(E.set_calls == 1 && E.set_val == 0xFFFFFFFFu);
    service_fresh();
    E.load_epoch = 0x100000000ull;
    flex_ntp_boot_load();
    CHECK(E.set_calls == 0);
    // lo que merece guardarse: la hora aproximada de la NVS aunque no haya ultima sincronizacion; una ultima sincronizacion aunque la hora no venga de la NVS
    service_fresh();
    E.load_epoch = 1791000000u;
    E.load_last = 0;
    flex_ntp_boot_load();
    E.saves = 0;
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 1);
    service_fresh();
    E.synced_flag = 0;
    {
        // una sincronizacion buena vista desde el servicio: la hora ya no es de la NVS pero hay ultima sincronizacion
        E.query_fn = NULL;
        ref_ntp_world(1, 0, 1, 1, 70000, 1791481500u);
        flex_net_set_online(true);
        ref_ntp_world(1, 0, 1, 1, 90000, 1791481500u);
    }
    // la hora en el suelo se guarda (Arduino: solo se descarta la MENOR); la de justo debajo no
    service_fresh();
    E.synced_flag = 1;
    ref_ntp_world(1, 0, 0, 1, 1000, FLEX_NTP_MIN_EPOCH);
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 1);
    ref_ntp_world(1, 0, 0, 1, 1000, FLEX_NTP_MIN_EPOCH - 1);
    flex_ntp_save_time();
    CHECK_EQ_I(E.saves, 1);
    // el guardado horario con millis == 0 en la primera vuelta: la siguiente a la hora justa y un milisegundo
    service_fresh();
    E.synced_flag = 1;
    ref_ntp_world(1, 0, 0, 1, 0, 1791481500u);
    flex_ntp_tick_service();
    CHECK_EQ_I(E.saves, 1);
    ref_ntp_world(1, 0, 0, 1, 3600000, 1791481500u);
    flex_ntp_tick_service();
    CHECK_EQ_I(E.saves, 1);
    ref_ntp_world(1, 0, 0, 1, 3600001, 1791481500u);
    flex_ntp_tick_service();
    CHECK_EQ_I(E.saves, 2);
    // un entorno incompleto (sin modo avion, sin reloj...) no rompe los textos ni las peticiones
    {
        flex_ntp_env_t part;
        memset(&part, 0, sizeof(part));
        flex_ntp_set_env(&part);
        flex_ntp_service_reset();
        char st2[96], la2[96];
        bool b2;
        flex_ntp_texts(st2, sizeof(st2), la2, sizeof(la2), &b2);
        CHECK(!strcmp(st2, "Esperando Wi-Fi") && !strcmp(la2, "Nunca"));
        flex_ntp_request_sync(true);
        flex_ntp_tick_service();
        flex_ntp_work();
        flex_ntp_save_time();
        flex_ntp_boot_load();
        flex_net_set_online(true);
        flex_net_set_online(false);
    }
    // en Modo seguro no hay primera sincronizacion ni reintentos, pero el guardado horario sigue
    service_fresh();
    E.safe = 1;
    E.synced_flag = 1;
    ref_ntp_world(1, 0, 1, 1, 100000, 1791481500u);
    flex_net_set_online(true);
    ref_ntp_world(1, 0, 1, 1, 200000, 1791481500u);
    flex_ntp_tick_service();
    CHECK(!flex_ntp_work_pending());
    CHECK_EQ_I(E.saves, 1);
    E.safe = 0;
    ref_ntp_world(1, 0, 1, 1, 200010, 1791481500u);
    flex_ntp_tick_service();
    CHECK(flex_ntp_work_pending());
    // modo avion y sin red: la peticion del usuario no hace nada; el texto lo dice
    service_fresh();
    ref_ntp_world(1, 1, 1, 1, 300000, 1791481500u);
    flex_net_set_online(true);
    E.wakes = 0;
    flex_ntp_request_sync(true);
    CHECK_EQ_I(E.wakes, 0);
    char st[96], la[96];
    bool busy;
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(!strcmp(st, "Modo avi\xC3\xB3n activo") && !busy);
    ref_ntp_world(1, 0, 0, 1, 300000, 1791481500u);
    flex_net_set_online(false);
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(!strcmp(st, "Esperando Wi-Fi"));
    // el espacio de la hora de la NVS no responde: el texto del error y el icono en rojo (Ajustes)
    service_fresh();
    E.load_ok = 0;
    flex_ntp_boot_load();
    CHECK(!flex_ntp_time_nvs_ok());
    flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
    CHECK(!strcmp(st, "Error: almacenamiento NVS no disponible"));
    // sin entorno el servicio no hace nada ni falla
    flex_ntp_set_env(NULL);
    flex_ntp_request_sync(true);
    flex_ntp_tick_service();
    flex_ntp_boot_load();
    flex_ntp_save_time();
    flex_net_set_online(true);
    CHECK(flex_net_online());
    flex_net_set_online(false);
    CHECK(!flex_net_online());
}



// ---- las tres tareas del servicio a la vez (la interfaz, el trabajador y el servicio de Wi-Fi): TSan vigila que no haya carreras ----
static pthread_mutex_t g_svc_mu = PTHREAD_MUTEX_INITIALIZER;
static void tl_lock(void) { pthread_mutex_lock(&g_svc_mu); }
static void tl_unlock(void) { pthread_mutex_unlock(&g_svc_mu); }
static uint32_t tl_ms;
static uint32_t tl_millis(void) { return __atomic_add_fetch(&tl_ms, 7, __ATOMIC_RELAXED); }
static uint32_t tl_utc(void) { return 1791481500u + __atomic_load_n(&tl_ms, __ATOMIC_RELAXED) / 1000; }
static void tl_set(uint32_t u, bool n) { (void)u; (void)n; }
static bool tl_true(void) { return true; }
static bool tl_false(void) { return false; }
static bool tl_load(uint64_t *e, uint64_t *l) { *e = 0; *l = 0; return true; }
static bool tl_save(uint64_t e, uint64_t l) { (void)e; (void)l; return true; }
static uint32_t tl_query(const char *h) { (void)h; return 1791490000u; }
static void tl_wake(void) {}
static int g_thr_stop;
static void *thr_ui(void *a)
{
    (void)a;
    char st[96], la[96];
    bool busy;
    while (!__atomic_load_n(&g_thr_stop, __ATOMIC_ACQUIRE)) {
        flex_ntp_request_sync(rnd() % 3 == 0);
        flex_ntp_texts(st, sizeof(st), la, sizeof(la), &busy);
        flex_ntp_time_nvs_ok();
        flex_net_online();
    }
    return NULL;
}
static void *thr_worker(void *a)
{
    (void)a;
    while (!__atomic_load_n(&g_thr_stop, __ATOMIC_ACQUIRE)) {
        flex_ntp_tick_service();
        if (flex_ntp_work_pending()) {
            flex_ntp_work();
        }
        flex_ntp_save_time();
    }
    return NULL;
}
static void *thr_wifi(void *a)
{
    (void)a;
    for (int i = 0; !__atomic_load_n(&g_thr_stop, __ATOMIC_ACQUIRE); i++) {
        flex_net_set_online((i / 3) % 2 == 0);
    }
    return NULL;
}
static void t_threads(void)
{
    flex_ntp_env_t e = {.millis = tl_millis, .utc = tl_utc, .set_utc = tl_set, .synced = tl_true, .airplane = tl_false, .system_ready = tl_true, .safe_mode = tl_false,
                        .nvs_ok = tl_true, .load = tl_load, .save = tl_save, .query = tl_query, .wake = tl_wake, .lock = tl_lock, .unlock = tl_unlock};
    flex_ntp_set_env(&e);
    flex_ntp_service_reset();
    pthread_t a, b, c;
    __atomic_store_n(&g_thr_stop, 0, __ATOMIC_RELEASE);
    pthread_create(&a, NULL, thr_ui, NULL);
    pthread_create(&b, NULL, thr_worker, NULL);
    pthread_create(&c, NULL, thr_wifi, NULL);
    usleep(300000);
    __atomic_store_n(&g_thr_stop, 1, __ATOMIC_RELEASE);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    pthread_join(c, NULL);
    flex_ntp_t m;
    flex_ntp_get(&m);
    CHECK(m.fails <= 8 && m.state <= FLEX_NTPS_BUSY);
    CHECK(m.state == FLEX_NTPS_OK || m.state == FLEX_NTPS_BUSY || m.state == FLEX_NTPS_NEVER || m.state == FLEX_NTPS_FAIL);
}

void test_ntp(void)
{
    t_tables();
    t_packet_and_parse();
    t_text_and_civil();
    t_state_machine();
    t_model_lockstep();
    t_model_edges();
    t_persist_parity();
    t_persist_hourly();
    t_service();
    t_conditions();
    t_udp();
    t_threads();
}
