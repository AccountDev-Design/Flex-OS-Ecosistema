// Referencia: la hora por red (FlexOS_Ultra_NTP.h) de la version Arduino tal cual: el cliente SNTP (ntpQuery), la tarea (ntpTask), la planificacion
// (ntpOnWifiUp, ntpRequestSync, ntpTick), la persistencia (clkSaveNvs, clkLoadNvs, clkPersistTick) y los textos de Ajustes. El Wi-Fi, el socket UDP, la
// NVS, las tareas y el reloj son dobles que fija la prueba.
#include "ref_prelude.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#define FLEXOS_ENABLE_WIFI 1
#define FLEXOS_WIFI_CORE_UNSAFE 0
#define FLEXOS_CLK_MIN_EPOCH 1767225600UL
#define FLEXOS_TZ_OFFSET_SEC (-18000L)
#define F(x) x
static struct SerialDouble { void println(const char *) {} } Serial;
static uint32_t g_ms = 0;
static uint32_t millis() { return g_ms; }
static void vTaskDelay(int ms) { g_ms += (uint32_t)ms; }
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define pdFAIL 0
typedef void (*TaskFn)(void *);
static TaskFn g_task_fn = nullptr; static bool g_task_ok = true; static int g_tasks_made = 0;
static int xTaskCreatePinnedToCore(TaskFn fn, const char *, int, void *, int, void *, int)
{
    if (!g_task_ok) return pdFAIL;
    g_task_fn = fn; g_tasks_made++; return pdPASS;
}
static void vTaskDelete(void *) {}
enum { ST_HOME = 1, ST_LOCK = 2, ST_APP = 3 };
static int gState = ST_HOME;
static bool gAirplane = false, gNetOnline = false, clkAnchored = true, gTimeNvsOk = true;
static uint32_t g_utc = 1791481500u;     // la hora del reloj (clkNowUtc)
static uint32_t clkNowUtc() { return g_utc; }
static uint32_t g_set_epoch = 0; static int g_set_n = 0;
static void clkSetEpoch(uint32_t e) { g_set_epoch = e; g_utc = e; g_set_n++; }
// Wi-Fi
enum { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
static int g_wl = WL_CONNECTED;
static struct WiFiDouble { int status() { return g_wl; } } WiFi;
// NVS (Preferences)
static bool g_ns_exists = true, g_ns_rw_ok = true;
static uint64_t g_nvs_epoch = 0, g_nvs_sync = 0; static int g_nvs_writes = 0;
struct Preferences {
    bool open = false, ro = false;
    bool begin(const char *, bool readOnly)
    {
        if (readOnly) { if (!g_ns_exists) return false; open = true; ro = true; return true; }
        if (!g_ns_rw_ok) return false;
        g_ns_exists = true; open = true; ro = false; return true;
    }
    void end() { open = false; }
    uint64_t getULong64(const char *k, uint64_t def) { return !strcmp(k, "epoch") ? (g_nvs_epoch ? g_nvs_epoch : def) : (g_nvs_sync ? g_nvs_sync : def); }
    void putULong64(const char *k, uint64_t v) { if (!strcmp(k, "epoch")) g_nvs_epoch = v; else g_nvs_sync = v; g_nvs_writes++; }
};
// UDP: cada consulta (en el orden en que llegan) tiene su guion
enum { UK_SILENT = 0, UK_PACKET, UK_SHORT, UK_FAIL_BEGIN, UK_FAIL_BEGINPKT, UK_FAIL_END };
struct UScript { int kind = UK_SILENT; int after_ms = 0; std::vector<uint8_t> bytes; };
static UScript g_script[8]; static int g_q = 0;
static std::vector<std::string> g_hosts; static std::vector<int> g_ports; static std::vector<std::vector<uint8_t>> g_sent;
static int g_stops = 0, g_begins = 0;
struct WiFiUDP {
    UScript *sc = nullptr; uint32_t t_send = 0; bool sent = false; bool begun = false; bool got = false;
    bool begin(int) { g_begins++; UScript &s = g_script[g_q < 8 ? g_q : 7]; sc = &s; if (s.kind == UK_FAIL_BEGIN) return false; begun = true; return true; }
    bool beginPacket(const char *h, int p) { g_hosts.push_back(h); g_ports.push_back(p); return sc->kind != UK_FAIL_BEGINPKT; }
    size_t write(const uint8_t *d, size_t n) { g_sent.push_back(std::vector<uint8_t>(d, d + n)); return n; }
    bool endPacket() { t_send = g_ms; sent = true; return sc->kind != UK_FAIL_END; }
    int parsePacket()
    {
        if (!sent || got) return 0;
        if ((int32_t)(g_ms - t_send) < sc->after_ms) return 0;
        if (sc->kind == UK_PACKET) return (int)sc->bytes.size();
        if (sc->kind == UK_SHORT) return (int)sc->bytes.size();
        return 0;
    }
    int read(uint8_t *b, int n) { int k = (int)sc->bytes.size() < n ? (int)sc->bytes.size() : n; memcpy(b, sc->bytes.data(), (size_t)k); got = true; return k; }
    void stop() { g_stops++; g_q++; }
};

#include "ref_ntp_clk.inc"
#include "ref_ntp.inc"

extern "C" {
void ref_ntp_reset()
{
    g_ms = 0; g_task_fn = nullptr; g_task_ok = true; g_tasks_made = 0; gState = ST_HOME; gAirplane = false; gNetOnline = false; clkAnchored = true; gTimeNvsOk = true;
    g_utc = 1791481500u; g_set_epoch = 0; g_set_n = 0; g_wl = WL_CONNECTED; g_ns_exists = true; g_ns_rw_ok = true; g_nvs_epoch = 0; g_nvs_sync = 0; g_nvs_writes = 0;
    for (auto &s : g_script) s = UScript();
    g_q = 0; g_hosts.clear(); g_ports.clear(); g_sent.clear(); g_stops = 0; g_begins = 0;
    gNtpState = NTPS_NEVER; gNtpBusy = false; gNtpDoneMs = 0; gNtpLastSyncUtc = 0; gNtpFromNvs = false; gNtpFails = 0; gNtpNextMs = 0; gNtpUserAsked = false;
}
void ref_ntp_world(int state_ready, int airplane, int online, int wl_connected, unsigned ms, unsigned utc)
{
    gState = state_ready == 1 ? ST_HOME : (state_ready == 2 ? ST_LOCK : ST_APP); gAirplane = airplane != 0; gNetOnline = online != 0; g_wl = wl_connected ? WL_CONNECTED : WL_DISCONNECTED;
    g_ms = ms; g_utc = utc;
}
void ref_ntp_task_ok(int ok) { g_task_ok = ok != 0; }
void ref_ntp_script(int idx, int kind, int after_ms, const unsigned char *bytes, int len)
{
    g_script[idx].kind = kind; g_script[idx].after_ms = after_ms; g_script[idx].bytes.assign(bytes, bytes + len);
}
void ref_ntp_wifi_up() { ntpOnWifiUp(); }
int ref_ntp_request(int user) { int before = g_tasks_made; ntpRequestSync(user != 0); return g_tasks_made - before; }
int ref_ntp_tick() { int before = g_tasks_made; ntpTick(); return g_tasks_made - before; }
// ejecuta la tarea lanzada (si la hay): devuelve cuantos servidores se consultaron
int ref_ntp_run_task() { if (!g_task_fn) return -1; TaskFn f = g_task_fn; g_task_fn = nullptr; int h0 = (int)g_hosts.size(); f(nullptr); return (int)g_hosts.size() - h0; }
void ref_ntp_get(int *state, int *busy, unsigned *done, unsigned *last, int *from_nvs, int *fails, unsigned *next, int *user, unsigned *set_epoch, int *set_n, int *nvs_ok)
{
    *state = gNtpState; *busy = gNtpBusy; *done = gNtpDoneMs; *last = gNtpLastSyncUtc; *from_nvs = gNtpFromNvs; *fails = gNtpFails; *next = gNtpNextMs; *user = gNtpUserAsked;
    *set_epoch = g_set_epoch; *set_n = g_set_n; *nvs_ok = gTimeNvsOk;
}
void ref_ntp_text(char *state, int ns, char *last, int nl)
{
    ntpStateText(state, (size_t)ns); ntpLastSyncText(last, (size_t)nl);
}
int ref_ntp_hosts(int i, char *out, int cap, int *port) { if (i >= (int)g_hosts.size()) return 0; snprintf(out, (size_t)cap, "%s", g_hosts[(size_t)i].c_str()); *port = g_ports[(size_t)i]; return 1; }
int ref_ntp_sent(int i, unsigned char *out) { if (i >= (int)g_sent.size()) return 0; memcpy(out, g_sent[(size_t)i].data(), g_sent[(size_t)i].size()); return (int)g_sent[(size_t)i].size(); }
int ref_ntp_stops() { return g_stops; }
int ref_ntp_begins() { return g_begins; }
// persistencia
void ref_ntp_nvs(int ns_exists, int rw_ok, unsigned long long epoch, unsigned long long last)
{
    g_ns_exists = ns_exists != 0; g_ns_rw_ok = rw_ok != 0; g_nvs_epoch = ns_exists ? epoch : 0; g_nvs_sync = ns_exists ? last : 0; g_nvs_writes = 0;   // un espacio que no existe no tiene claves
}
void ref_ntp_load() { clkLoadNvs(); }
void ref_ntp_save(int anchored) { clkAnchored = anchored != 0; clkSaveNvs(); }
void ref_ntp_persist() { clkPersistTick(); }
void ref_ntp_nvs_get(unsigned long long *epoch, unsigned long long *last, int *writes) { *epoch = g_nvs_epoch; *last = g_nvs_sync; *writes = g_nvs_writes; }
void ref_ntp_set_last_sync(unsigned last, int from_nvs) { gNtpLastSyncUtc = last; gNtpFromNvs = from_nvs != 0; }
void ref_ntp_set_state(int state, int busy, int fails, int from_nvs, int nvs_ok) { gNtpState = (uint8_t)state; gNtpBusy = busy != 0; gNtpFails = (uint8_t)fails; gNtpFromNvs = from_nvs != 0; gTimeNvsOk = nvs_ok != 0; }
// una instantanea del mundo de Arduino: la prueba ejecuta primero el servicio y despues la tarea de Arduino sobre EXACTAMENTE la misma situacion
struct Snap {
    uint32_t ms, utc, set_epoch; int set_n, q, tasks, stops, begins, writes; TaskFn fn;
    size_t nhosts, nsent;
    uint8_t st, fails; bool busy, from_nvs, user; uint32_t done, last, next; bool nvs_ok;
    uint64_t ep, sy;
} g_snap;
void ref_ntp_rewind() { g_q = 0; }
void ref_ntp_snapshot()
{
    g_snap = {g_ms, g_utc, g_set_epoch, g_set_n, g_q, g_tasks_made, g_stops, g_begins, g_nvs_writes, g_task_fn, g_hosts.size(), g_sent.size(),
              (uint8_t)gNtpState, (uint8_t)gNtpFails, (bool)gNtpBusy, gNtpFromNvs, gNtpUserAsked, gNtpDoneMs, gNtpLastSyncUtc, gNtpNextMs, gTimeNvsOk, g_nvs_epoch, g_nvs_sync};
}
void ref_ntp_restore()
{
    g_ms = g_snap.ms; g_utc = g_snap.utc; g_set_epoch = g_snap.set_epoch; g_set_n = g_snap.set_n; g_q = g_snap.q; g_tasks_made = g_snap.tasks; g_stops = g_snap.stops;
    g_begins = g_snap.begins; g_nvs_writes = g_snap.writes; g_task_fn = g_snap.fn; g_hosts.resize(g_snap.nhosts); g_ports.resize(g_snap.nhosts); g_sent.resize(g_snap.nsent);
    gNtpState = g_snap.st; gNtpFails = g_snap.fails; gNtpBusy = g_snap.busy; gNtpFromNvs = g_snap.from_nvs; gNtpUserAsked = g_snap.user; gNtpDoneMs = g_snap.done;
    gNtpLastSyncUtc = g_snap.last; gNtpNextMs = g_snap.next; gTimeNvsOk = g_snap.nvs_ok; g_nvs_epoch = g_snap.ep; g_nvs_sync = g_snap.sy;
}
unsigned ref_ntp_ms() { return g_ms; }
unsigned ref_ntp_utc() { return g_utc; }
void ref_ntp_clk_set(unsigned utc) { clkSetEpoch(utc); }
void ref_ntp_set_time(unsigned utc) { g_utc = utc; }   // el reloj del doble, sin contar la llamada como del Arduino
void ref_ntp_advance(unsigned ms) { g_ms += ms; }
unsigned ref_ntp_query(const char *host) { return ntpQuery(host); }
int ref_ntp_flags(int *airplane, int *online, int *ready) { *airplane = gAirplane; *online = gNetOnline; *ready = gState == ST_HOME || gState == ST_LOCK; return 0; }
void ref_ntp_civil(long days, int *y, int *m, int *d) { clkCivilFromDays(days, *y, *m, *d); }
}
