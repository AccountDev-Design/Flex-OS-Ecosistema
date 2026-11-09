// Referencia: el motor OTA de FlexOS_OTA.cpp (Arduino) tal cual: el micro-lector JSON, las versiones, otaCheck, otaInstall, otaMd5Usable y los textos. La red
// (HTTPClient, WiFiClientSecure), la flash (Update), las particiones, el reloj, las preferencias y las tareas son dobles que fija la prueba, y viven en UN mundo
// copiable: la prueba ejecuta primero el servicio nuevo, restaura el mundo y despues ejecuta este codigo sobre EXACTAMENTE la misma situacion.
//
// Se compila varias veces (REFV = v0..v3, con REF_* para la configuracion): las opciones de FlexOS_OTA.h (CA fijado, firma obligatoria, antirretroceso, TLS sin verificar)
// son del preprocesador en Arduino.
#include "ref_prelude.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#define CONFIG_IDF_TARGET_ESP32P4 1
#ifndef REF_CA
#define REF_CA "PEM-DE-PRUEBA"
#endif
#define FLEXOS_OTA_ROOT_CA REF_CA
#ifdef REF_SIG
#define FLEXOS_OTA_REQUIRE_SIGNATURE 1
#endif
#ifdef REF_ROLLBACK
#define FLEXOS_OTA_ANTIROLLBACK 1
#endif
#ifdef REF_INSECURE
#define FLEXOS_OTA_ALLOW_INSECURE 1
#endif
#define FLEXOS_FW_VERSION "1.0.0"
#define REFCAT2(a, b) a##b
#define REFCAT(a, b) REFCAT2(a, b)
#define X(n) REFCAT(n, REFV)
// las funciones del anfitrion que declara la cabecera son globales: cada variante tiene las suyas
#define otaHostNetworkReady REFCAT(otaHostNetworkReady, REFV)
#define otaHostLockPanel REFCAT(otaHostLockPanel, REFV)
#define otaHostUnlockPanel REFCAT(otaHostUnlockPanel, REFV)
#include "../../../../FlexOS_Ultra/FlexOS_OTA.h"

// Todo lo que sigue es de ESTA copia (espacio de nombres anonimo): las funciones en linea de los dobles referencian el mundo `W` de su propia configuracion
namespace {
// ---- el mundo ----
struct World {
    uint32_t ms = 0, heap = 200000;
    bool net = true, partition = true;
    // red
    bool begin_ok = true; int code = 200, size = 0; bool has_stream = true, stay_open = false, zero_reads = false;
    std::vector<uint8_t> body; std::vector<std::pair<uint32_t, size_t>> sched; uint32_t t_get = 0; size_t read_pos = 0;
    int ends = 0, begins = 0, gets = 0; std::string last_url; bool tls_ca = false, tls_insecure = false; std::string ca_given;
    // flash
    bool upd_begin_ok = true, upd_end_ok = true, upd_finished = true; int short_write_call = -1; int cancel_after_bytes = -1;
    size_t upd_total = 0, written = 0; int nwrites = 0, aborts = 0, begin_calls = 0, end_calls = 0; std::string md5set; bool md5_set = false;
    std::vector<size_t> write_sizes; std::string pend_saved; bool pend_saved_set = false; int restarts = 0; int panel_locks = 0, panel_unlocks = 0;
    bool cancel_flag_raised = false;
};
static World W;
static World g_snap;

// ---- Arduino ----
static volatile bool gCancel_unused = false;
struct SerialDouble { void printf(const char *, ...) {} void println(const char *) {} } Serial;
#define F(x) x
static uint32_t millis() { return W.ms; }
static void vTaskDelay(uint32_t t) { W.ms += t; }
#define pdMS_TO_TICKS(x) (x)
static int xPortGetCoreID() { return 1; }
static uint32_t esp_get_free_heap_size() { return W.heap; }
static uint32_t esp_get_minimum_free_heap_size() { return W.heap; }
typedef uint32_t StackType_t;
static uint32_t uxTaskGetStackHighWaterMark(void *) { return 1000; }
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
typedef void *TaskHandle_t;
struct esp_partition_t { int x; };
static const esp_partition_t *esp_ota_get_next_update_partition(const void *) { static esp_partition_t p; return W.partition ? &p : nullptr; }
static void esp_restart() { W.restarts++; }
#define HTTP_CODE_OK 200
#define HTTPC_ERROR_READ_TIMEOUT (-11)
#define HTTPC_ERROR_CONNECTION_LOST (-5)
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1
#define U_FLASH 0
// WiFiClient: el flujo
static size_t arrived() { size_t a = 0; uint32_t t = W.ms - W.t_get; for (auto &s : W.sched) if (s.first <= t) a = s.second; return a < W.body.size() ? a : W.body.size(); }
static int w_available() { size_t a = arrived(); return a > W.read_pos ? (int)(a - W.read_pos) : 0; }
static int w_read(uint8_t *b, size_t n)
{
    if (W.zero_reads) return 0;
    int av = w_available(); size_t k = (size_t)av < n ? (size_t)av : n;
    memcpy(b, W.body.data() + W.read_pos, k); W.read_pos += k; return (int)k;
}
static bool w_connected() { return W.stay_open || W.read_pos < W.body.size(); }
struct WiFiClient { int available() { return w_available(); } size_t readBytes(uint8_t *b, size_t n) { return (size_t)w_read(b, n); } bool connected() { return w_connected(); } };
struct WiFiClientSecure : WiFiClient {
    void setCACert(const char *ca) { W.tls_ca = true; W.ca_given = ca; }
    void setInsecure() { W.tls_insecure = true; }
    void setTimeout(int) {}
};
static WiFiClient g_stream;
struct HTTPClient {
    void setTimeout(int) {} void setConnectTimeout(int) {} void setFollowRedirects(int) {} void setReuse(bool) {} void useHTTP10(bool) {}
    bool begin(WiFiClientSecure &, const char *url) { W.begins++; W.last_url = url; return W.begin_ok; }
    int GET() { W.gets++; W.t_get = W.ms; W.read_pos = 0; return W.code; }
    int getSize() { return W.size; }
    WiFiClient *getStreamPtr() { return W.has_stream ? &g_stream : nullptr; }
    void end() { W.ends++; }
};
static void raise_cancel();   // gCancel de Arduino: se define tras incluir su codigo
static size_t do_write(size_t n)
{
    int call = W.nwrites++; W.write_sizes.push_back(n);
    size_t r = (call == W.short_write_call && n > 0) ? n - 1 : n;
    W.written += r;
    if (W.cancel_after_bytes >= 0 && (int)W.written >= W.cancel_after_bytes) W.cancel_flag_raised = true;   // el usuario pulsa "Cancelar" a mitad de la descarga
    return r;
}
struct UpdateDouble {
    bool begin(size_t total, int) { W.begin_calls++; W.upd_total = total; W.written = 0; return W.upd_begin_ok; }
    void setMD5(const char *m) { W.md5_set = true; W.md5set = m; }
    size_t write(const uint8_t *, size_t n)
    {
        size_t r = do_write(n);
        if (W.cancel_flag_raised) { raise_cancel(); W.cancel_flag_raised = false; }
        return r;
    }
    bool end(bool) { W.end_calls++; return W.upd_end_ok; }
    bool isFinished() { return W.upd_finished; }
    void abort() { W.aborts++; }
    int getError() { return 0; }
    const char *errorString() { return "error"; }
} Update;
struct Preferences {
    bool begin(const char *, bool) { return true; }
    void putString(const char *, const char *v) { W.pend_saved = v; W.pend_saved_set = true; }
    void end() {}
};
static Preferences gPrefs;
static portMUX_TYPE gProgMux = portMUX_INITIALIZER_UNLOCKED;

#include "ref_ota.inc"

static void raise_cancel() { gCancel = true; }
}   // namespace

bool otaHostNetworkReady() { return W.net; }
void otaHostLockPanel() { W.panel_locks++; }
void otaHostUnlockPanel() { W.panel_unlocks++; }

extern "C" {
// ---- las operaciones del servicio nuevo: las MISMAS del mundo ----
unsigned X(ref_op_millis)() { return W.ms; }
void X(ref_op_delay)(unsigned ms) { W.ms += ms; }
int X(ref_op_net)() { return W.net; }
unsigned X(ref_op_heap)() { return W.heap; }
int X(ref_op_partition)() { return W.partition; }
int X(ref_op_http_begin)(const char *url, const char *ca, int insecure) { W.begins++; W.last_url = url; if (ca && ca[0]) { W.tls_ca = true; W.ca_given = ca; } if (insecure) W.tls_insecure = true; return W.begin_ok; }
int X(ref_op_http_get)() { W.gets++; W.t_get = W.ms; W.read_pos = 0; return W.code; }
int X(ref_op_http_size)() { return W.size; }
int X(ref_op_http_available)() { return w_available(); }
int X(ref_op_http_read)(unsigned char *b, int n) { return w_read(b, (size_t)n); }
int X(ref_op_http_connected)() { return w_connected(); }
int X(ref_op_http_stream)() { return W.has_stream; }
void X(ref_op_http_end)() { W.ends++; }
int X(ref_op_upd_begin)(unsigned total) { W.begin_calls++; W.upd_total = total; W.written = 0; return W.upd_begin_ok; }
void X(ref_op_upd_md5)(const char *m) { W.md5_set = true; W.md5set = m; }
int X(ref_op_upd_write)(int n) { return (int)do_write((size_t)n); }
int X(ref_op_cancel_raised)() { int r = W.cancel_flag_raised; W.cancel_flag_raised = false; return r; }
int X(ref_op_upd_end)() { W.end_calls++; return W.upd_end_ok; }
int X(ref_op_upd_finished)() { return W.upd_finished; }
void X(ref_op_upd_abort)() { W.aborts++; }
void X(ref_op_lock)() { W.panel_locks++; }
void X(ref_op_unlock)() { W.panel_unlocks++; }
void X(ref_op_save_pending)(const char *v) { W.pend_saved = v; W.pend_saved_set = true; }
void X(ref_op_restart)() { W.restarts++; }
void X(ref_ota_reset)() { W = World(); g_snap = World(); gState = OTA_IDLE; gError = OTA_ERR_NONE; gPercent = 0; gDone = 0; gTotal = 0; gBps = 0; gCancel = false; gHasUpd = false;
    gRemoteVer[0] = gUrl[0] = gMd5[0] = gChangelog[0] = gSig[0] = 0; }
void X(ref_ota_world)(unsigned heap, int net, int partition) { W.heap = heap; W.net = net != 0; W.partition = partition != 0; }
void X(ref_ota_http)(int begin_ok, int code, int size, int has_stream, int stay_open, int zero_reads) { W.begin_ok = begin_ok != 0; W.code = code; W.size = size; W.has_stream = has_stream != 0; W.stay_open = stay_open != 0; W.zero_reads = zero_reads != 0; }
void X(ref_ota_body)(const unsigned char *b, int n) { W.body.assign(b, b + n); }
void X(ref_ota_sched)(int n, const unsigned *at, const unsigned *cum) { W.sched.clear(); for (int i = 0; i < n; i++) W.sched.push_back({at[i], cum[i]}); }
void X(ref_ota_upd)(int begin_ok, int end_ok, int finished, int short_write_call, int cancel_after) { W.upd_begin_ok = begin_ok != 0; W.upd_end_ok = end_ok != 0; W.upd_finished = finished != 0; W.short_write_call = short_write_call; W.cancel_after_bytes = cancel_after; }
void X(ref_ota_set_time)(unsigned ms) { W.ms = ms; }
void X(ref_ota_snapshot)() { g_snap = W; }
void X(ref_ota_restore)() { W = g_snap; }
// el motor de Arduino
void X(ref_ota_prime)(const char *remote, const char *url, const char *md5, unsigned total)   // el estado tras un otaCheck bueno
{
    strncpy(gRemoteVer, remote, sizeof(gRemoteVer) - 1); strncpy(gUrl, url, sizeof(gUrl) - 1); strncpy(gMd5, md5, sizeof(gMd5) - 1); gTotal = total; gHasUpd = true; gState = OTA_AVAILABLE;
}
int X(ref_ota_check)() { gCancel = false; return otaCheck() ? 1 : 0; }
int X(ref_ota_install)() { return otaInstall() ? 1 : 0; }
void X(ref_ota_ard)(int *state, int *error, int *pct, unsigned *done, unsigned *total, unsigned *bps, int *has, char *remote, char *url, char *md5, char *log, char *sig)
{
    *state = gState; *error = gError; *pct = gPercent; *done = gDone; *total = gTotal; *bps = gBps; *has = gHasUpd;
    strcpy(remote, gRemoteVer); strcpy(url, gUrl); strcpy(md5, gMd5); strcpy(log, gChangelog); strcpy(sig, gSig);
}
void X(ref_ota_effects)(unsigned *ms, int *restarts, int *aborts, int *begins, unsigned long long *begin_total, char *md5set, int *md5_set, int *nwrites, unsigned long long *written, char *pend,
                        int *pend_set, int *ends, int *gets, int *ca, int *insecure, char *last_url, int *locks, int *unlocks, int *end_calls, int *begin_calls)
{
    *ms = W.ms; *restarts = W.restarts; *aborts = W.aborts; *begins = W.begins; *begin_total = W.upd_total; strcpy(md5set, W.md5set.c_str()); *md5_set = W.md5_set; *nwrites = W.nwrites;
    *written = W.written; strcpy(pend, W.pend_saved.c_str()); *pend_set = W.pend_saved_set; *ends = W.ends; *gets = W.gets; *ca = W.tls_ca; *insecure = W.tls_insecure;
    strcpy(last_url, W.last_url.c_str()); *locks = W.panel_locks; *unlocks = W.panel_unlocks; *end_calls = W.end_calls; *begin_calls = W.begin_calls;
}
int X(ref_ota_write_size)(int i) { return i < (int)W.write_sizes.size() ? (int)W.write_sizes[(size_t)i] : -1; }
// el modelo
int X(ref_ota_md5)(const char *s) { return otaMd5Usable(s) ? 1 : 0; }
int X(ref_ota_ver)(const char *a, const char *b)   // -2: alguna no se parsea; si no, el signo de la comparacion
{
    uint16_t va[3], vb[3]; if (!verParse(a, va) || !verParse(b, vb)) return -2; return verCmp(va, vb);
}
int X(ref_ota_ver_ok)(const char *a, unsigned short *v) { return verParse(a, v) ? 1 : 0; }
const char *X(ref_ota_errtext)(int e) { return otaErrText((FlexOtaError)e); }
const char *X(ref_ota_phasetext)(int s) { return otaPhaseText((FlexOtaState)s); }
const char *X(ref_ota_find)(const char *buf, const char *key) { const char *r = jFind(buf, buf + strlen(buf), key); return r; }
int X(ref_ota_block)(const char *buf, const char *key, int *bs, int *be) { const char *s, *e; if (!jBlock(buf, key, &s, &e)) return 0; *bs = (int)(s - buf); *be = (int)(e - buf); return 1; }
int X(ref_ota_str)(const char *buf, int off, char *out, int cap) { return jStr(buf + off, buf + strlen(buf), out, (size_t)cap) ? 1 : 0; }
unsigned X(ref_ota_num)(const char *buf, int off) { return jNum(buf + off, buf + strlen(buf)); }
}
