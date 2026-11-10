// #############################################################
//  FLEX ACCOUNT · EL SERVICIO EN C, PASO A PASO CONTRA EL DE ARDUINO
//  ------------------------------------------------------------
//  Cada guion (acciones: pasos de la tarea, Wi-Fi, servidor, reinicios, memoria, cortes de NVS...) se ejecuta DOS veces sobre un mundo recien creado: una con
//  FlexOS_Account.cpp (Arduino, tal cual) y otra con components/flex_account/src/flex_account.c. Tras CADA accion se registra todo lo observable (la foto, lo que
//  devuelve la API, la credencial, el reloj, las peticiones HTTP con sus cabeceras, la NVS entera y su contador de escrituras, las tareas creadas) y los dos
//  registros tienen que ser identicos. Luego lo mismo con miles de guiones aleatorios (servidor que miente, corta, firma mal; Wi-Fi que se cae a mitad del flujo...).
// #############################################################
#include <algorithm>
#include <functional>
#include <map>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "account_ops_netstub.h"
#include "flex_account_svc.h"
#include "netstub.h"
#include "freertos/task.h"
#include "pkgbuild.h"
#include <cJSON.h>

// ---- la referencia (ref/ref_account_svc.cpp) ----
extern "C" {
typedef struct {
    int state, progress, linked;
    char stage[72], error[144], code[9], activation_url[192], flex_address[48], display_name[64];
    int link;
    char link_detail[96];
    unsigned verified_age_s;
} ref_snap_t;
void ref_acct_set_random(const unsigned char *bytes, int n);
void ref_acct_set_key(const unsigned char *pub65);
void ref_acct_on_random(void (*fn)());
void ref_svc_begin();
int ref_svc_request_code(const char *label);
void ref_svc_cancel();
int ref_svc_linked();
int ref_svc_usable();
int ref_svc_state();
int ref_svc_link_state();
void ref_svc_snapshot(ref_snap_t *o);
void ref_svc_request_validation();
void ref_svc_report_rejected();
int ref_svc_copy_bearer(char *out, unsigned long cap);
void ref_svc_forget_local();
void ref_svc_step();
void ref_svc_power_cycle();
}
const char *flexCloudRootCA();

static int gChecks = 0, gFails = 0;
#define CHECK(cond, ...) do { gChecks++; if (!(cond)) { gFails++; printf("  FALLO: "); printf(__VA_ARGS__); printf("  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

// ------------------------------ las dos implementaciones, con la misma interfaz ------------------------------
struct Snap {
    int state, progress, linked, link;
    unsigned age;
    std::string stage, error, code, url, addr, name, detail;
};
struct Impl {
    const char *name;
    void (*begin)();
    bool (*request_code)(const char *);
    void (*cancel)();
    bool (*linked)();
    bool (*usable)();
    int (*state)();
    int (*link_state)();
    Snap (*snap)();
    void (*req_validation)();
    void (*report_rejected)();
    bool (*copy_bearer)(char *, size_t);
    void (*forget)();
    void (*step)();
    void (*power_cycle)();
};
static Snap from_ref()
{
    ref_snap_t r;
    ref_svc_snapshot(&r);
    return Snap{r.state, r.progress, r.linked, r.link, r.verified_age_s, r.stage, r.error, r.code, r.activation_url, r.flex_address, r.display_name, r.link_detail};
}
static Snap from_port()
{
    flex_account_snapshot_t r;
    flex_account_snapshot(&r);
    return Snap{(int)r.state, r.progress, r.linked ? 1 : 0, (int)r.link, r.verified_age_s, r.stage, r.error, r.code, r.activation_url, r.flex_address, r.display_name, r.link_detail};
}
static const Impl kRef = {
    "Arduino", ref_svc_begin, [](const char *l) { return ref_svc_request_code(l) != 0; }, ref_svc_cancel, []() { return ref_svc_linked() != 0; }, []() { return ref_svc_usable() != 0; },
    ref_svc_state, ref_svc_link_state, from_ref, ref_svc_request_validation, ref_svc_report_rejected, [](char *o, size_t c) { return ref_svc_copy_bearer(o, c) != 0; },
    ref_svc_forget_local, ref_svc_step, ref_svc_power_cycle};
static const Impl kPort = {
    "C", flex_account_begin, flex_account_request_code, flex_account_cancel, flex_account_linked, flex_account_usable, []() { return (int)flex_account_state(); },
    []() { return (int)flex_account_link_state(); }, from_port, flex_account_request_validation, flex_account_report_rejected, flex_account_copy_bearer, flex_account_forget_local,
    flex_account_step, []() { account_ops_netstub_power_cycle(); flex_account_test_power_cycle(); }};
static flex_account_cfg_t g_cfg = {"1.0.0"};
static flex_account_ops_t g_ops_nl;   // las operaciones sin cerrojo (lock / unlock / log a NULL: una sola tarea)
static Impl kPortNL;
static const Impl *cur;   // la que corre ahora (los ganchos del servidor la usan)

// ------------------------------ servidor simulado ------------------------------
static pkgb::Key gKey;
static const char *KEY_ID = "69b91cf7a9246cc2084dda73ccef77b2dc1a372b18fec19b49cf980efd68af69";
static const char *CODE_URL = FLEX_ACCOUNT_CODE_URL;
static const char *SESSION_URL = FLEX_ACCOUNT_SESSION_URL;

enum SessionMode { SM_OK, SM_DOWN, SM_500, SM_EXPIRED, SM_REVOKED, SM_HTML401, SM_BADBODY, SM_TLSFAIL, SM_COUNT };
struct Server {
    std::string registeredHash, pendingHash, code = "FLX7Q2";
    int pendingPolls = 2;
    bool expireCode = false;
    int session = SM_OK;
    std::string address = "ana.p4@flex";
    std::string display = "Ana \xC3\x91" "u\xC3\xB1" "ez";
    int sessionCalls = 0, codeCalls = 0;
    int tlsError = 0;
    unsigned long latencyMs = 0;
    int codePostFail = 0, codePollFail = 0, codeFailError = -1;
    int postStatus = 0;   // != 0: el POST contesta este codigo sin cuerpo (409, 429, 500 ...)
    int postConflicts = 0;   // las N primeras peticiones de codigo reciben 409
    int pollStatus = 0;   // != 0: la consulta contesta este codigo sin cuerpo
    int envFault = 0;     // defecto de los sobres firmados (ver envelope_faulty)
    int sessFault = 0;    // defecto de la respuesta de la sesion
    unsigned long stallMs = 0;   // el cuerpo de la respuesta tarda tanto en llegar (0xFFFFFFFF = nunca)
    int bodyLen = 0;      // != 0: los cuerpos miden EXACTAMENTE esto (el sobre firmado se ajusta con un campo de relleno)
    int hookSession = 0;  // 1 = desvincular dentro de la peticion de sesion, 2 = cancelar, 3 = pedir validacion
    int hookPoll = 0;     // 1 = cancelar dentro de la consulta, 2 = desvincular
};
static Server S;

static std::string b64url(const pkgb::Bytes &b)
{
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (uint8_t c : b) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += t[(acc >> bits) & 63];
        }
    }
    if (bits) {
        out += t[(acc << (6 - bits)) & 63];
    }
    return out;
}
static std::string envelope(const std::string &payload, int fault = 0)
{
    pkgb::Bytes p(payload.begin(), payload.end());
    pkgb::Bytes sig = gKey.sign(pkgb::sha256(p));
    if (fault == 1) {
        sig[5] ^= 0x10;   // firma mala
    }
    std::string kid = fault == 2 ? std::string(64, '0') : KEY_ID;
    std::string alg = fault == 3 ? "ES384" : "ES256";
    std::string payload64 = b64url(p);
    if (fault == 4) {
        payload64 += "!";   // base64url roto
    }
    std::string out = std::string("{\"schema\":2,\"algorithm\":\"") + alg + "\",\"keyId\":\"" + kid + "\",\"payload\":\"" + payload64 + "\",\"signature\":\"" + b64url(sig) + "\"}";
    if (fault == 5) {
        out = "{\"schema\":1,\"payload\":\"x\"}";
    }
    return out;
}
static std::string sha256hex(const std::string &s) { return pkgb::hex(pkgb::sha256((const uint8_t *)s.data(), s.size())); }
static std::string queryParam(const std::string &url, const char *k)
{
    std::string key = std::string(k) + "=";
    size_t p = url.find(key);
    if (p == std::string::npos) {
        return "";
    }
    size_t e = url.find('&', p);
    return url.substr(p + key.size(), e == std::string::npos ? std::string::npos : e - p - key.size());
}
// Defectos del cuerpo (1-9: sobre; 10: truncado; 11: sin Content-Length; 12: demasiado grande; 13: basura; 14: vacio; 15: carga con otro proposito; 16: carga con codigo
// de otra huella; 17: carga sin codigo)
static std::string fit_envelope(const std::string &payload, int target);
static void apply_body_fault(NetResponse &rs, int fault, const std::string &payload)
{
    rs.body = S.bodyLen && !fault ? fit_envelope(payload, S.bodyLen) : envelope(payload, fault >= 1 && fault <= 5 ? fault : 0);
    rs.stallMs = S.stallMs;
    if (fault == 10) {
        rs.cutAt = rs.body.size() / 2;
    } else if (fault == 11) {
        rs.chunked = true;
    } else if (fault == 12) {
        std::string big = payload;
        big.insert(big.size() - 1, std::string(",\"pad\":\"") + std::string(9000, 'x') + "\"");
        rs.body = envelope(big);
    } else if (fault == 13) {
        rs.body = "<html>no es JSON</html>";
    } else if (fault == 14) {
        rs.body.clear();
    } else if (fault == 18) {   // demasiado grande Y sin Content-Length
        std::string big = payload;
        big.insert(big.size() - 1, std::string(",\"pad\":\"") + std::string(9000, 'x') + "\"");
        rs.body = envelope(big);
        rs.chunked = true;
    }
}
// Un sobre firmado que mida exactamente `target` bytes (si no se puede, el mas cercano por arriba)
static std::string fit_envelope(const std::string &payload, int target)
{
    for (int pad = 0; pad < 9500; pad++) {
        std::string pl = payload;
        pl.insert(pl.size() - 1, pad ? std::string(",\"pad\":\"") + std::string((size_t)pad > 9 ? (size_t)pad - 9 : 0, 'x') + "\"" : "");
        std::string e = envelope(pl);
        if ((int)e.size() >= target) {
            return e;
        }
    }
    return envelope(payload);
}
static void obs_prog(const char *what);
static NetResponse serve(const NetRequest &rq)
{
    obs_prog("net");
    NetResponse rs;
    if (!rq.url.compare(0, strlen(CODE_URL), CODE_URL)) {
        S.codeCalls++;
        int &failLeft = rq.method == "POST" ? S.codePostFail : S.codePollFail;
        if (failLeft > 0) {
            failLeft--;
            rs.status = HTTPC_ERROR_CONNECTION_REFUSED;
            rs.tlsError = S.codeFailError;
            return rs;
        }
        if (rq.method == "POST") {
            if (S.postConflicts > 0) {
                S.postConflicts--;
                rs.status = 409;
                return rs;
            }
            if (S.postStatus) {
                rs.status = S.postStatus;
                return rs;
            }
            cJSON *j = cJSON_Parse(rq.body.c_str());
            cJSON *h = cJSON_GetObjectItem(j, "tokenHash");
            S.pendingHash = cJSON_IsString(h) ? h->valuestring : "";
            if (getenv("DBG")) { fprintf(stderr, "POST %s body=%s hash=%s\n", cur->name, rq.body.c_str(), S.pendingHash.c_str()); }
            cJSON_Delete(j);
            rs.status = 201;
            std::string hash = S.envFault == 16 ? std::string(64, 'a') : S.pendingHash;
            std::string purpose = S.envFault == 15 ? "flex-device-status" : "flex-device-code";
            std::string payload = "{\"schema\":1,\"purpose\":\"" + purpose + "\"" + (S.envFault == 17 ? "" : ",\"code\":\"" + S.code + "\"") + ",\"tokenHash\":\"" + hash +
                                  "\",\"activationUrl\":\"https://flex-developer-studio.ralvarezsantos980.chatgpt.site/activate?code=" + S.code + "\"}";
            apply_body_fault(rs, S.envFault, payload);
            return rs;
        }
        if (S.hookPoll == 1) {
            S.hookPoll = 0;
            cur->cancel();
        } else if (S.hookPoll == 2) {
            S.hookPoll = 0;
            cur->forget();
        }
        if (S.pollStatus) {
            rs.status = S.pollStatus;
            return rs;
        }
        if (queryParam(rq.url, "tokenHash") != S.pendingHash) {
            rs.status = 404;
            return rs;
        }
        std::string st = "pending";
        if (S.expireCode) {
            st = "expired";
        } else if (S.pendingPolls > 0) {
            S.pendingPolls--;
        } else {
            st = "approved";
            S.registeredHash = S.pendingHash;
        }
        std::string extra = st == "approved" ? ",\"flexAddress\":\"" + S.address + "\",\"displayName\":\"" + S.display + "\"" : "";
        apply_body_fault(rs, S.envFault >= 10 && S.envFault <= 14 ? S.envFault : (S.envFault >= 1 && S.envFault <= 5 ? S.envFault : 0),
                         "{\"schema\":1,\"purpose\":\"flex-device-status\",\"state\":\"" + st + "\"" + extra + "}");
        return rs;
    }
    if (rq.url == SESSION_URL) {
        S.sessionCalls++;
        rs.latencyMs = S.latencyMs;
        rs.stallMs = S.stallMs;
        if (S.hookSession == 1) {
            S.hookSession = 0;
            cur->forget();
        } else if (S.hookSession == 2) {
            S.hookSession = 0;
            cur->cancel();
        } else if (S.hookSession == 3) {
            S.hookSession = 0;
            cur->req_validation();
        }
        switch (S.session) {
        case SM_DOWN:
            rs.status = HTTPC_ERROR_CONNECTION_REFUSED;
            return rs;
        case SM_TLSFAIL:
            rs.status = HTTPC_ERROR_CONNECTION_REFUSED;
            rs.tlsError = S.tlsError;
            return rs;
        case SM_500:
            rs.status = 503;
            rs.body = "<html>bad gateway</html>";
            return rs;
        case SM_HTML401:
            rs.status = 401;
            rs.body = "<html>Access denied</html>";
            return rs;
        case SM_EXPIRED:
            rs.status = 401;
            rs.body = "{\"ok\":false,\"error\":{\"code\":\"token_expired\",\"message\":\"x\"}}";
            return rs;
        case SM_REVOKED:
            rs.status = 401;
            rs.body = "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}";
            return rs;
        case SM_BADBODY:
            rs.status = 200;
            rs.body = "{\"ok\":true}";
            return rs;
        default:
            break;
        }
        auto it = rq.headers.find("authorization");
        std::string bearer = (it != rq.headers.end() && it->second.size() > 7) ? it->second.substr(7) : "";
        if (sha256hex(bearer) != S.registeredHash) {
            rs.status = 401;
            rs.body = "{\"ok\":false,\"error\":{\"code\":\"device_revoked\",\"message\":\"x\"}}";
            return rs;
        }
        std::string addr = S.sessFault == 4 ? "sin-arroba" : (S.sessFault == 7 ? std::string(60, 'a') + "@flex" : S.address);
        std::string disp = S.sessFault == 6 ? std::string(70, 'n') : S.display;
        std::string dispJson = S.sessFault == 5 ? "null" : "\"" + disp + "\"";
        rs.body = "{\"ok\":true,\"account\":{\"id\":\"acc_1\",\"flexAddress\":\"" + addr + "\",\"displayName\":" + dispJson + "}}";
        if (S.sessFault == 8) {
            rs.body = "{\"ok\":true,\"account\":\"ana\"}";
        } else if (S.sessFault == 3) {
            rs.body.clear();
        } else if (S.sessFault == 9) {
            rs.body.insert(rs.body.size() - 1, std::string(",\"pad\":\"") + std::string(9000, 'x') + "\"");
        } else if (S.sessFault == 1) {
            rs.chunked = true;
        } else if (S.sessFault == 2) {
            rs.cutAt = rs.body.size() / 2;
        } else if (S.sessFault == 10) {
            rs.body += "  trailing";
        } else if (S.sessFault == 11) {   // demasiado grande Y sin Content-Length
            rs.body.insert(rs.body.size() - 1, std::string(",\"pad\":\"") + std::string(9000, 'x') + "\"");
            rs.chunked = true;
        }
        if (S.bodyLen && !S.sessFault && (int)rs.body.size() < S.bodyLen) {
            size_t extra = (size_t)S.bodyLen - rs.body.size();
            rs.body.insert(rs.body.size() - 1, extra > 9 ? std::string(",\"pad\":\"") + std::string(extra - 9, 'x') + "\"" : std::string());
        }
        return rs;
    }
    rs.status = 404;
    return rs;
}

// ------------------------------ guiones ------------------------------
enum Kind {
    K_STEP, K_WIFI, K_REQ, K_LINK, K_CANCEL, K_REQVAL, K_REJECTED, K_FORGET, K_POWER, K_SRV, K_MEM, K_DNS, K_TASKFAIL, K_NVSBROKEN, K_NVSPUT, K_NVSWIPE, K_CLOCK, K_BEARER, K_PLAN,
    K_OBSERVE, K_BEGIN
};
enum Srv {
    SK_SESSION, SK_PENDING, SK_EXPIRE, SK_POSTFAIL, SK_POLLFAIL, SK_FAILERR, SK_TLSERR, SK_LATENCY, SK_POSTSTATUS, SK_POLLSTATUS, SK_ENVFAULT, SK_SESSFAULT, SK_HOOKSESS, SK_HOOKPOLL,
    SK_ADDR, SK_DISPLAY, SK_CONFLICTS, SK_STALL, SK_BODYLEN
};
enum PlanAct { PA_WIFI_OFF, PA_WIFI_ON, PA_MEM_LOW, PA_MEM_OK, PA_CANCEL, PA_FORGET, PA_REQVAL };
struct Act {
    Kind k;
    long a = 0, b = 0;
    std::string s;
};
struct Plan {
    unsigned long at;   // ms desde que se armo
    PlanAct what;
    bool done = false;
};
struct Script {
    std::string name;
    std::vector<Act> acts;
    std::vector<unsigned char> rnd;   // el aleatorio exacto (vacio: una secuencia fija)
    Script &random(std::vector<unsigned char> v) { rnd = v; return *this; }
    Script &step(long n, long ms = 100) { acts.push_back({K_STEP, n, ms, ""}); return *this; }
    Script &wifi(bool on) { acts.push_back({K_WIFI, on, 0, ""}); return *this; }
    Script &req(const char *label) { acts.push_back({K_REQ, 0, 0, label ? label : "\x01"}); return *this; }   // "\x01" = NULL
    Script &link() { acts.push_back({K_LINK, 0, 0, ""}); return *this; }
    Script &cancel() { acts.push_back({K_CANCEL, 0, 0, ""}); return *this; }
    Script &reqval() { acts.push_back({K_REQVAL, 0, 0, ""}); return *this; }
    Script &rejected() { acts.push_back({K_REJECTED, 0, 0, ""}); return *this; }
    Script &forget() { acts.push_back({K_FORGET, 0, 0, ""}); return *this; }
    Script &power() { acts.push_back({K_POWER, 0, 0, ""}); return *this; }
    Script &srv(Srv f, long v, const char *s = "") { acts.push_back({K_SRV, f, v, s}); return *this; }
    Script &mem(long free_kb, long block_kb) { acts.push_back({K_MEM, free_kb, block_kb, ""}); return *this; }
    Script &dns(bool ok) { acts.push_back({K_DNS, ok, 0, ""}); return *this; }
    Script &taskfail(long n) { acts.push_back({K_TASKFAIL, n, 0, ""}); return *this; }
    Script &nvsbroken(bool b) { acts.push_back({K_NVSBROKEN, b, 0, ""}); return *this; }
    Script &nvsput(const char *kind, const char *key, const char *val) { acts.push_back({K_NVSPUT, kind[0], 0, std::string(key) + "=" + val}); return *this; }
    Script &nvswipe() { acts.push_back({K_NVSWIPE, 0, 0, ""}); return *this; }
    Script &clock(long ms) { acts.push_back({K_CLOCK, ms, 0, ""}); return *this; }
    Script &bearer(long cap) { acts.push_back({K_BEARER, cap, 0, ""}); return *this; }
    Script &plan(PlanAct w, unsigned long at) { acts.push_back({K_PLAN, w, (long)at, ""}); return *this; }
    Script &observe() { acts.push_back({K_OBSERVE, 0, 0, ""}); return *this; }
    Script &begin() { acts.push_back({K_BEGIN, 0, 0, ""}); return *this; }
};

// ------------------------------ registro ------------------------------
struct Trace {
    std::vector<std::string> lines;
    size_t net_seen = 0;
    unsigned long t0 = 0;
    std::vector<Plan> plans;
    unsigned long armed_at = 0;
    bool in_plan = false;
    unsigned creates0 = 0;
    std::string last_d;
};
static Trace *gTrace;
static char *fmt(const char *f, ...)
{
    static char buf[4096];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}
static void log_line(const std::string &s) { gTrace->lines.push_back(s); }
static std::string nvs_dump()
{
    std::string out;
    for (auto &kv : netstubNvs()) {
        const NetNvsValue &v = kv.second;
        std::string k = kv.first;
        std::replace(k.begin(), k.end(), '\x1f', '/');
        if (v.type == 1) {
            out += fmt("%s=n%llu;", k.c_str(), v.num);
        } else if (v.type == 2) {
            out += k + "=s'" + v.str + "';";
        } else {
            out += k + "=b" + std::to_string(v.blob.size()) + ";";
        }
    }
    return out + fmt(" w=%u broken=%d", netstubNvsWriteCount(), (int)netstubNvsBroken());
}
static void observe(const std::string &tag)
{
    Trace &t = *gTrace;
    Snap s = cur->snap();
    char b[64] = "";
    bool ok = cur->copy_bearer(b, sizeof(b));
    log_line(fmt("%s | st=%d pr=%d ln=%d lk=%d age=%u stage='%s' err='%s' code='%s' url='%s' addr='%s' name='%s' det='%s' | api L=%d U=%d S=%d K=%d | bearer=%d:%s | t=%lu", tag.c_str(), s.state,
                 s.progress, s.link, s.linked, s.age, s.stage.c_str(), s.error.c_str(), s.code.c_str(), s.url.c_str(), s.addr.c_str(), s.name.c_str(), s.detail.c_str(), (int)cur->linked(),
                 (int)cur->usable(), cur->state(), cur->link_state(), (int)ok, b, gNetNowMs - t.t0));
    for (; t.net_seen < gNetLog.size(); t.net_seen++) {
        const NetRequest &r = gNetLog[t.net_seen];
        std::string h;
        for (auto &kv : r.headers) {
            h += kv.first + ":" + kv.second + ";";
        }
        log_line(fmt("  NET %s %s body[%zu]='%s' h=%s https=%d insecure=%d ca=%d at=%lu", r.method.c_str(), r.url.c_str(), r.body.size(), r.body.c_str(), h.c_str(), (int)r.https,
                     (int)r.tlsInsecure, r.tlsCa == nullptr ? 0 : (r.tlsCa == flexCloudRootCA() ? 1 : 2), r.at - t.t0));
    }
    log_line("  NVS " + nvs_dump());
    std::string tasks;
    for (auto &x : gNetTasks) {
        tasks += fmt("%s/%d/%u;", x.name.c_str(), x.core, x.stack);
    }
    log_line(fmt("  TASKS creates=%u %s | srv code=%d sess=%d", gNetTaskCreates - t.creates0, tasks.c_str(), S.codeCalls, S.sessionCalls));
}

static int g_no_obs;   // dentro de begin / reinicio el servicio tiene el cerrojo: no se puede pedir la foto
static void obs_prog(const char *what)
{
    if (g_no_obs || !gTrace) {
        return;
    }
    Snap s = cur->snap();
    log_line(fmt("  PRG %s st=%d pr=%d lk=%d stage='%s' err='%s'", what, s.state, s.progress, s.linked, s.stage.c_str(), s.error.c_str()));
}
static void obs_random() { obs_prog("random"); }
static void obs_nvs() { obs_prog("nvs"); }

static void plan_apply(PlanAct w)
{
    switch (w) {
    case PA_WIFI_OFF: gNetWifi = false; break;
    case PA_WIFI_ON: gNetWifi = true; break;
    case PA_MEM_LOW: gNetInternalFree = 24u << 10; gNetInternalBlock = 10u << 10; break;
    case PA_MEM_OK: gNetInternalFree = 200u << 10; gNetInternalBlock = 100u << 10; break;
    case PA_CANCEL: cur->cancel(); break;
    case PA_FORGET: cur->forget(); break;
    case PA_REQVAL: cur->req_validation(); break;
    }
}
static void on_delay()
{
    Trace &t = *gTrace;
    if (t.in_plan) {
        return;
    }
    t.in_plan = true;
    for (Plan &p : t.plans) {
        if (!p.done && gNetNowMs - t.armed_at >= p.at) {
            p.done = true;
            plan_apply(p.what);
            observe(fmt("plan%d", (int)p.what));
        }
    }
    // la foto cambia DURANTE el flujo bloqueante (la pantalla la lee mientras tanto): se registra cuando cambia
    Snap s = cur->snap();
    std::string now = fmt("d st=%d pr=%d ln=%d stage='%s' err='%s' code='%s'", s.state, s.progress, s.link, s.stage.c_str(), s.error.c_str(), s.code.c_str());
    if (now != t.last_d) {
        t.last_d = now;
        log_line(now);
    }
    t.in_plan = false;
}

static void world_reset(const Script &sc, unsigned long clock0)
{
    netstubNvsWipe();
    netstubReset();
    gNetNowMs = clock0;
    gNetHandler = serve;
    S = Server();
    unsigned char rnd[96];
    uint32_t x = 0x9E3779B9u ^ (uint32_t)sc.acts.size();
    for (size_t i = 0; i < sizeof(rnd); i++) {
        x = x * 1664525u + 1013904223u;
        rnd[i] = (unsigned char)(x >> 24);
    }
    if (sc.rnd.empty()) {
        ref_acct_set_random(rnd, sizeof(rnd));
    } else {
        ref_acct_set_random(sc.rnd.data(), (int)sc.rnd.size());
    }
    ref_acct_on_random(obs_random);
    gNetOnNvsBegin = obs_nvs;
    account_ops_netstub_power_cycle();
}

static void run(const Impl &im, const Script &sc, Trace &tr, unsigned long clock0)
{
    cur = &im;
    gTrace = &tr;
    flex_account_set(&g_cfg, &im == &kPortNL ? &g_ops_nl : account_ops_netstub());
    world_reset(sc, clock0);
    tr.t0 = gNetNowMs;
    tr.creates0 = gNetTaskCreates;
    tr.last_d.clear();
    tr.net_seen = 0;
    tr.plans.clear();
    gNetOnDelay = on_delay;
    g_no_obs++;
    im.power_cycle();   // arranque de fabrica de esta ejecucion
    g_no_obs--;
    observe("boot");
    for (size_t i = 0; i < sc.acts.size(); i++) {
        const Act &a = sc.acts[i];
        switch (a.k) {
        case K_STEP:
            for (long n = 0; n < a.a; n++) {
                im.step();
                netstubAdvance((unsigned long)a.b);
            }
            break;
        case K_WIFI: gNetWifi = a.a != 0; break;
        case K_REQ: {
            bool ok = im.request_code(a.s == "\x01" ? nullptr : a.s.c_str());
            log_line(fmt("  req -> %d", (int)ok));
            break;
        }
        case K_LINK: {
            S.pendingPolls = 2;
            S.expireCode = false;
            bool ok = im.request_code("FlexOS Ultra de Ana");
            log_line(fmt("  link req -> %d", (int)ok));
            tr.armed_at = gNetNowMs;
            im.step();
            break;
        }
        case K_CANCEL: im.cancel(); break;
        case K_REQVAL: im.req_validation(); break;
        case K_REJECTED: im.report_rejected(); break;
        case K_FORGET: im.forget(); break;
        case K_POWER: g_no_obs++; im.power_cycle(); g_no_obs--; break;
        case K_BEGIN: g_no_obs++; im.begin(); g_no_obs--; break;
        case K_SRV:
            switch ((Srv)a.a) {
            case SK_SESSION: S.session = (int)a.b; break;
            case SK_PENDING: S.pendingPolls = (int)a.b; break;
            case SK_EXPIRE: S.expireCode = a.b != 0; break;
            case SK_POSTFAIL: S.codePostFail = (int)a.b; break;
            case SK_POLLFAIL: S.codePollFail = (int)a.b; break;
            case SK_FAILERR: S.codeFailError = (int)a.b; break;
            case SK_TLSERR: S.tlsError = (int)a.b; break;
            case SK_LATENCY: S.latencyMs = (unsigned long)a.b; break;
            case SK_POSTSTATUS: S.postStatus = (int)a.b; break;
            case SK_POLLSTATUS: S.pollStatus = (int)a.b; break;
            case SK_ENVFAULT: S.envFault = (int)a.b; break;
            case SK_SESSFAULT: S.sessFault = (int)a.b; break;
            case SK_HOOKSESS: S.hookSession = (int)a.b; break;
            case SK_HOOKPOLL: S.hookPoll = (int)a.b; break;
            case SK_ADDR: S.address = a.s; break;
            case SK_DISPLAY: S.display = a.s; break;
            case SK_CONFLICTS: S.postConflicts = (int)a.b; break;
            case SK_STALL: S.stallMs = (unsigned long)a.b; break;
            case SK_BODYLEN: S.bodyLen = (int)a.b; break;
            }
            break;
        case K_MEM:
            gNetInternalFree = (size_t)a.a << 10;
            gNetInternalBlock = (size_t)a.b << 10;
            break;
        case K_DNS: gNetDnsOk = a.a != 0; break;
        case K_TASKFAIL: gNetTaskFail = (unsigned)a.a; break;
        case K_NVSBROKEN: netstubNvsBroken() = a.a != 0; break;
        case K_NVSPUT: {
            Preferences p;
            p.begin("flexacct", false);
            size_t eq = a.s.find('=');
            std::string key = a.s.substr(0, eq), val = a.s.substr(eq + 1);
            if (a.a == 's') {
                p.putString(key.c_str(), val.c_str());
            } else if (a.a == 'b') {
                p.putBool(key.c_str(), val == "1");
            } else if (a.a == 'u') {
                p.putUChar(key.c_str(), (uint8_t)atoi(val.c_str()));
            } else if (a.a == 'i') {
                p.putUInt(key.c_str(), (uint32_t)strtoul(val.c_str(), nullptr, 10));
            }
            p.end();
            break;
        }
        case K_NVSWIPE: netstubNvsWipe(); break;
        case K_CLOCK: netstubAdvance((unsigned long)a.a); break;
        case K_BEARER: {
            // buffer del tamano EXACTO en el monton: escribir un byte de mas es un desbordamiento que AddressSanitizer ve
            char *b = (char *)malloc(a.a > 0 ? (size_t)a.a : 1);
            memset(b, 0x55, a.a > 0 ? (size_t)a.a : 1);
            bool ok = im.copy_bearer(b, (size_t)a.a);
            log_line(fmt("  bearer cap=%ld -> %d '%.64s' [%d]", a.a, (int)ok, ok && a.a > 0 ? b : "(sin copia)", a.a > 0 ? (int)(unsigned char)b[0] : -1));
            free(b);
            break;
        }
        case K_PLAN:
            tr.plans.push_back({(unsigned long)a.b, (PlanAct)a.a});
            tr.armed_at = gNetNowMs;
            break;
        case K_OBSERVE: break;
        }
        observe(fmt("#%zu k=%d", i, (int)a.k));
    }
    gNetOnDelay = nullptr;
    gNetOnNvsBegin = nullptr;
    ref_acct_on_random(nullptr);
}

// Cobertura de los guiones: pares (estado del flujo, estado del vinculo) alcanzados y cuantas peticiones de cada tipo salieron
static std::map<std::pair<int, int>, int> gPairs;
static std::map<std::string, int> gEvents;
static void tally(const Trace &t)
{
    for (const std::string &l : t.lines) {
        int st, ln;
        const char *p = strstr(l.c_str(), "| st=");
        if (p && sscanf(p, "| st=%d pr=%*d ln=%d", &st, &ln) == 2) {
            gPairs[{st, ln}]++;
        }
        if (l.compare(0, 6, "  NET ") == 0) {
            gEvents[l.substr(6, l.find(' ', 6) - 6) + (l.find("/devices/code") != std::string::npos ? " code" : " sesion")]++;
        }
    }
}

// Compara las dos ejecuciones; devuelve false (y dice la primera diferencia) si no coinciden
static bool compare(const Script &sc, unsigned long clock0 = 1000, bool nolock = false)
{
    Trace a, b;
    run(kRef, sc, a, clock0);
    run(nolock ? kPortNL : kPort, sc, b, clock0);
    tally(b);
    size_t n = std::min(a.lines.size(), b.lines.size());
    for (size_t i = 0; i < n; i++) {
        if (a.lines[i] != b.lines[i]) {
            printf("  FALLO: %s: las trazas divergen en la linea %zu\n", sc.name.c_str(), i);
            for (size_t j = i > 3 ? i - 3 : 0; j < i; j++) {
                printf("      = %s\n", a.lines[j].c_str());
            }
            printf("    Arduino: %s\n", a.lines[i].c_str());
            printf("    C      : %s\n", b.lines[i].c_str());
            gFails++;
            gChecks++;
            return false;
        }
    }
    if (a.lines.size() != b.lines.size()) {
        printf("  FALLO: %s: distinto numero de lineas (%zu Arduino, %zu C)\n", sc.name.c_str(), a.lines.size(), b.lines.size());
        gFails++;
        gChecks++;
        return false;
    }
    gChecks++;
    return true;
}

// ------------------------------ escenarios con nombre (los de la prueba de Arduino) ------------------------------
static std::vector<Script> named()
{
    std::vector<Script> v;
    auto add = [&](const char *n) -> Script & { v.push_back(Script()); v.back().name = n; return v.back(); };

    add("arranque de fabrica").wifi(true).step(100);
    add("vincular, apagar y encender, volver el Wi-Fi")
        .wifi(true).link().bearer(64).bearer(10).bearer(44).bearer(43).bearer(0).wifi(false).power().step(600).wifi(true).step(10).step(40).step(3000)
        .power().power().power().wifi(false).power().step(60).wifi(true).power().step(60).wifi(false).power().step(60);
    add("servidor caido, 401 de un proxy, respuesta rara")
        .wifi(true).link().power().srv(SK_SESSION, SM_DOWN).step(40).step(2600).step(36000).srv(SK_SESSION, SM_HTML401).reqval().step(5).srv(SK_SESSION, SM_BADBODY).reqval().step(5)
        .srv(SK_SESSION, SM_500).reqval().step(5).srv(SK_SESSION, SM_OK).reqval().step(5).wifi(false).step(3);
    {
        Script &s = add("wifi intermitente");
        s.wifi(true).link().power().step(60);
        for (int i = 0; i < 20; i++) {
            s.wifi((i % 2) == 1).step(20);
        }
        s.wifi(true).step(400);
    }
    add("caducada, cancelar y volver a vincular")
        .wifi(true).link().power().srv(SK_SESSION, SM_EXPIRED).step(40).step(600).power().wifi(false).step(10).wifi(true).srv(SK_PENDING, 1000).req("FlexOS").cancel().step(1)
        .srv(SK_EXPIRE, 1).req("FlexOS").step(1).srv(SK_SESSION, SM_OK).srv(SK_EXPIRE, 0).link().power().wifi(false).step(5);
    add("revocada, desvincular y volver a vincular")
        .wifi(true).link().power().srv(SK_SESSION, SM_REVOKED).step(40).srv(SK_SESSION, SM_DOWN).reqval().step(5).srv(SK_SESSION, SM_OK).forget().power().step(100).link().power().wifi(true).step(50);
    add("desvincular este aparato")
        .wifi(true).link().step(50).forget().forget().forget().step(200).power().wifi(false).step(20).wifi(true).step(200).link().power().step(50).srv(SK_SESSION, SM_DOWN).reqval().step(5).forget();
    add("desvincular durante una validacion rechazada")
        .wifi(true).link().step(50).srv(SK_SESSION, SM_REVOKED).srv(SK_HOOKSESS, 1).reqval().step(5).power();
    add("desvincular durante una validacion correcta con el perfil cambiado")
        .wifi(true).link().step(50).srv(SK_ADDR, 0, "otra.cuenta@flex").srv(SK_HOOKSESS, 1).reqval().step(5).power();
    add("cancelar y revalidar durante la validacion")
        .wifi(true).link().step(50).srv(SK_HOOKSESS, 2).reqval().step(5).srv(SK_HOOKSESS, 3).reqval().step(5).step(100);
    add("matriz de estados")
        .wifi(false).step(30).wifi(true).step(300).link().step(30).power().wifi(false).step(30).srv(SK_SESSION, SM_DOWN).wifi(true).step(80).srv(SK_SESSION, SM_OK).step(400).wifi(false)
        .step(20).wifi(true).step(400).srv(SK_SESSION, SM_REVOKED).reqval().step(5).srv(SK_SESSION, SM_EXPIRED).reqval().step(5).srv(SK_SESSION, SM_DOWN).reqval().step(5)
        .srv(SK_SESSION, SM_OK).forget().wifi(false).step(30).wifi(true).srv(SK_SESSION, SM_DOWN).step(300).power().step(100);
    {
        Script &s = add("un 401 de Flex Cloud pide revalidar con freno");
        s.wifi(true).link().power().step(50);
        for (int i = 0; i < 10; i++) {
            s.rejected().step(2);
        }
        s.step(610).rejected().step(2).step(7000).rejected().step(2);
    }
    add("NVS incompleta: sin token").nvsput("b", "linked", "1").nvsput("s", "address", "ana@flex").power().nvsbroken(true).power().nvsbroken(false).power();
    add("NVS incompleta: token sin direccion").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ").power().wifi(true).step(100);
    add("NVS: token con longitud mala").nvsput("b", "linked", "1").nvsput("s", "token", "abc").nvsput("s", "address", "ana@flex").power().step(50);
    add("NVS: solo la marca linked").nvsput("b", "linked", "1").power().step(50);
    add("NVS: direccion no valida").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ").nvsput("s", "address", "sin-arroba").power();
    add("NVS: authst en la NVS").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ").nvsput("s", "address", "ana@flex").nvsput("u", "authst", "1")
        .power().wifi(true).step(100).nvsput("u", "authst", "2").power().step(100).nvsput("u", "authst", "7").power().step(100);
    add("NVS: el id de instalacion").nvsput("i", "installId", "12345").power().wifi(true).link().step(10).forget().link();
    {
        Script &s = add("30 dias encendido");
        s.wifi(true).link();
        for (int day = 0; day < 30; day++) {
            s.step(24 * 60, 60000);
        }
        s.srv(SK_SESSION, SM_DOWN);
        for (int day = 0; day < 8; day++) {
            s.step(24 * 60, 60000);
        }
        s.srv(SK_SESSION, SM_OK).step(16 * 60, 60000);
    }
    add("tareas: al arrancar nace la tarea").power();
    add("tareas: sin memoria para la pila al arrancar").wifi(true).taskfail(1).power().link();
    add("tareas: sin memoria tampoco al pedir el enlace").wifi(true).taskfail(2).power().req("FlexOS Ultra").link();
    add("tareas: validar recrea la tarea").wifi(true).link().taskfail(1).power().reqval().step(60);
    add("tareas: volver a vincular sin memoria conserva la cuenta").wifi(true).link().taskfail(1).power().taskfail(1).req("FlexOS Ultra").step(1);
    add("memoria interna baja al validar")
        .wifi(true).link().power().mem(24, 10).step(60).mem(200, 100).step(400).reqval().srv(SK_SESSION, SM_TLSFAIL).srv(SK_TLSERR, -0x2700).step(5).step(310);
    for (int c = 0; c < 6; c++) {
        static const struct { bool dns; long ms; } k[] = {{false, 4000}, {true, 200}, {true, 12400}, {true, 15000}, {true, 7000}, {true, 0}};
        add(fmt("-1 de TLS caso %d", c)).wifi(true).link().power().step(60).dns(k[c].dns).srv(SK_SESSION, SM_TLSFAIL).srv(SK_TLSERR, -1).srv(SK_LATENCY, k[c].ms).reqval().step(3).step(310);
    }
    for (int e : {-0x7F00, -0x6800, -0x2700, -0x7780, -0x3000, -1, -0x4290, 5, 0, -0x1234}) {
        add(fmt("error de TLS %d", e)).wifi(true).link().power().step(60).srv(SK_SESSION, SM_TLSFAIL).srv(SK_TLSERR, e).reqval().step(3);
    }
    add("enlace: sin memoria para el handshake").wifi(true).mem(24, 10).req("FlexOS Ultra").step(1).mem(200, 100).link();
    add("enlace: fallo de transporte -1").wifi(true).srv(SK_POSTFAIL, 1).srv(SK_FAILERR, -1).req("FlexOS Ultra").step(1).srv(SK_POSTFAIL, 1).srv(SK_FAILERR, -0x7F00).req("FlexOS Ultra").step(1).link();
    add("enlace: la consulta falla con el codigo en pantalla").wifi(true).srv(SK_PENDING, 0).srv(SK_POLLFAIL, 3).srv(SK_FAILERR, -0x6800).req("FlexOS Ultra").step(1);
    add("enlace: sin memoria a mitad de la espera").wifi(true).srv(SK_PENDING, 2).plan(PA_MEM_LOW, 4000).plan(PA_MEM_OK, 14000).req("FlexOS Ultra").step(1);
    add("enlace: el Wi-Fi se va con el codigo en pantalla").wifi(true).srv(SK_PENDING, 3).plan(PA_WIFI_OFF, 4000).plan(PA_WIFI_ON, 12000).req("FlexOS Ultra").step(1);
    add("enlace: codigo que caduca sin poder consultarlo").wifi(true).srv(SK_POLLFAIL, 100000).srv(SK_FAILERR, -1).req("FlexOS Ultra").step(1).srv(SK_POLLFAIL, 0).link();
    add("enlace: 409 y recuperacion con id nuevo").wifi(true).srv(SK_CONFLICTS, 1).req("FlexOS Ultra").step(1);
    add("enlace: 409 dos veces").wifi(true).srv(SK_CONFLICTS, 2).req("FlexOS Ultra").step(1).link();
    add("enlace: 409 con la NVS rota").wifi(true).srv(SK_CONFLICTS, 1).nvsbroken(true).req("FlexOS Ultra").step(1).nvsbroken(false);
    add("enlace: 429").wifi(true).srv(SK_POSTSTATUS, 429).req("FlexOS Ultra").step(1).srv(SK_POSTSTATUS, 500).req("FlexOS Ultra").step(1).srv(SK_POSTSTATUS, 0).link();
    add("enlace: etiquetas raras").wifi(true).req("").step(1).forget().req("\"comillas\" y \\ barras").step(1).forget().req("tilde \xC3\xA9 \xC3\xB1").step(1)
        .forget().req(std::string(60, 'x').c_str()).step(1).req(nullptr).step(1);
    add("enlace: dos peticiones seguidas").wifi(true).req("A").req("B").step(1).req("C").forget().req("D").step(1);
    add("enlace: sin Wi-Fi").req("FlexOS Ultra").step(1).wifi(true).link();
    add("enlace: cancelar antes del primer paso").wifi(true).req("FlexOS Ultra").cancel().step(1).link();
    add("enlace: cancelar en la consulta").wifi(true).srv(SK_PENDING, 1000).srv(SK_HOOKPOLL, 1).req("FlexOS Ultra").step(1).link();
    add("enlace: desvincular en la consulta").wifi(true).link().srv(SK_PENDING, 1000).srv(SK_HOOKPOLL, 2).req("FlexOS Ultra").step(1);
    add("enlace: estado 404 de la consulta").wifi(true).srv(SK_POLLSTATUS, 404).req("FlexOS Ultra").step(1).srv(SK_POLLSTATUS, 503).req("FlexOS Ultra").step(1);
    add("enlace: se guarda sin NVS").wifi(true).srv(SK_PENDING, 1).plan(PA_FORGET, 2500).req("FlexOS Ultra").step(1);
    for (int f = 1; f <= 18; f++) {
        add(fmt("sobre defectuoso %d (enlace)", f)).wifi(true).srv(SK_ENVFAULT, f).req("FlexOS Ultra").step(1).srv(SK_ENVFAULT, 0).link();
    }
    for (int f = 1; f <= 18; f++) {
        add(fmt("sobre defectuoso %d (consulta)", f)).wifi(true).srv(SK_PENDING, 0).req("FlexOS Ultra").srv(SK_ENVFAULT, 0).step(1).srv(SK_ENVFAULT, f).srv(SK_PENDING, 1).req("FlexOS Ultra").step(1);
    }
    for (int f = 0; f <= 11; f++) {
        add(fmt("respuesta de sesion defectuosa %d", f)).wifi(true).link().power().srv(SK_SESSFAULT, f).step(60).step(400).srv(SK_SESSFAULT, 0).reqval().step(5);
    }
    add("direccion y nombre cambian en la validacion")
        .wifi(true).link().step(50).srv(SK_ADDR, 0, "nueva@flex").srv(SK_DISPLAY, 0, "Otro Nombre").reqval().step(5).srv(SK_DISPLAY, 0, "").reqval().step(5);

    // ---- las esperas tras un fallo de red y el Wi-Fi que vuelve a mitad (flanco): lo que queda de la espera no se acorta, ni se alarga ----
    {
        static const long EL[] = {2000, 20000, 29000, 29900, 30000, 30100, 45000, 90000};
        for (long el : EL) {
            // un fallo (espera de 30 s) y el Wi-Fi se va y vuelve cuando han pasado `el` ms desde el fallo
            Script &sc = add(fmt("flanco tras 1 fallo, a los %ld ms", el));
            sc.wifi(true).link().power().srv(SK_SESSION, SM_DOWN).step(45).step((el - 1000) / 100).wifi(false).step(10).wifi(true).step(1200);
            // tres fallos seguidos (espera de 2 min) y el mismo flanco
            Script &sc3 = add(fmt("flanco tras 3 fallos, a los %ld ms", el));
            sc3.wifi(true).link().power().srv(SK_SESSION, SM_DOWN).step(1000).step((el - 1000) / 100).wifi(false).step(10).wifi(true).step(2500);
        }
        // el Wi-Fi que entra y sale con una validacion reciente: el intervalo minimo entre intentos
        for (long gap : {1000L, 25000L, 29900L, 30000L, 30100L}) {
            add(fmt("intervalo minimo entre intentos, flanco a los %ld ms", gap))
                .wifi(true).link().power().step(60).wifi(false).step(5).wifi(true).step((gap - 500) / 100).wifi(false).step(5).wifi(true).step(1200);
        }
    }
    // ---- el freno de los 401 de Flex Cloud, en sus fronteras ----
    add("rechazos: 60 s exactos").wifi(true).link().power().step(50).rejected().step(600).rejected().step(3);
    add("rechazos: 59,9 s").wifi(true).link().power().step(50).rejected().step(599).rejected().step(3);
    add("rechazos: 60,1 s").wifi(true).link().power().step(50).rejected().step(601).rejected().step(3);
    add("rechazos con el reloj ya avanzado").wifi(true).link().power().step(400).rejected().step(100).rejected().step(3).step(550).rejected().step(3);
    // ---- la NVS en sus fronteras ----
    add("NVS: direccion de 6 caracteres").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ").nvsput("s", "address", "a@flex").power().wifi(true).step(100);
    add("NVS: direccion de 5 caracteres").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ").nvsput("s", "address", "@flex").power();
    add("NVS: token de 42 caracteres").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNO").nvsput("s", "address", "ana@flex").power();
    add("NVS: token de 44 caracteres").nvsput("b", "linked", "1").nvsput("s", "token", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR").nvsput("s", "address", "ana@flex").power();
    add("NVS: id de instalacion a cero").nvsput("i", "installId", "0").power().wifi(true).link();
    // ---- direccion y nombre en el borde de sus buffers (48 y 64): una cota mal puesta escribe un byte de mas ----
    for (int len = 44; len <= 50; len++) {
        std::string addr = std::string((size_t)len - 5, 'a') + "@flex";
        add(fmt("direccion de %d caracteres", len)).wifi(true).srv(SK_ADDR, 0, addr.c_str()).link().power().step(60).srv(SK_ADDR, 0, "otra@flex").reqval().step(5);
        add(fmt("direccion de %d caracteres en la validacion", len)).wifi(true).link().step(30).srv(SK_ADDR, 0, addr.c_str()).reqval().step(5);
    }
    for (int len = 60; len <= 66; len++) {
        std::string nm((size_t)len, 'n');
        add(fmt("nombre de %d caracteres", len)).wifi(true).srv(SK_DISPLAY, 0, nm.c_str()).link().power().step(60);
        add(fmt("nombre de %d caracteres en la validacion", len)).wifi(true).link().step(30).srv(SK_DISPLAY, 0, nm.c_str()).reqval().step(5);
    }
    // ---- cuerpos de EXACTAMENTE el tamano limite (8 192 bytes) y sus vecinos ----
    for (int t = 8189; t <= 8195; t++) {
        add(fmt("cuerpo de %d bytes (enlace)", t)).wifi(true).srv(SK_BODYLEN, t).req("FlexOS Ultra").step(1);
        add(fmt("cuerpo de %d bytes (validacion)", t)).wifi(true).link().power().srv(SK_BODYLEN, t).step(60).step(40);
    }
    // ---- el cuerpo que no llega: tarda, o no llega nunca (la lectura espera de 10 en 10 ms y se rinde a los 15 s) ----
    for (long ms : {2000L, 14990L, 15000L, 15005L, 15500L, 0xFFFFFFFFL}) {
        add(fmt("cuerpo de la sesion tarda %ld ms", ms)).wifi(true).link().power().srv(SK_STALL, ms).step(60).step(40);
        add(fmt("cuerpo del enlace tarda %ld ms", ms)).wifi(true).srv(SK_STALL, ms).req("FlexOS Ultra").step(1);
    }
    // ---- el aleatorio exacto: un id de instalacion repetido o a cero se vuelve a sortear ----
    {
        std::vector<unsigned char> v = {1, 0, 0, 0};
        for (int i = 0; i < 32; i++) v.push_back((unsigned char)(i * 3 + 7));
        for (unsigned char b : std::vector<unsigned char>{1, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0}) v.push_back(b);
        Script &sc = add("id de instalacion: 409 con aleatorios repetidos y a cero");
        sc.random(v).wifi(true).srv(SK_CONFLICTS, 1).req("FlexOS Ultra").step(1);
        Script &sc2 = add("id de instalacion: arranque con aleatorio a cero");
        sc2.random({0, 0, 0, 0, 5, 0, 0, 0}).wifi(true).step(5);
        Script &sc3 = add("id de instalacion: desvincular con aleatorios repetidos");
        sc3.random(v).wifi(true).link().forget().link();
    }
    add("reloj: saltos de tiempo").wifi(true).link().clock(1000).step(5).clock(7 * 3600 * 1000).step(5).clock(1000).step(30);
    return v;
}

// ------------------------------ guiones aleatorios ------------------------------
static uint64_t gR = 88172645463325252ull;
static uint32_t rnd(void)
{
    gR ^= gR << 13;
    gR ^= gR >> 7;
    gR ^= gR << 17;
    return (uint32_t)(gR >> 16);
}
static Script random_script(int idx)
{
    Script s;
    s.name = fmt("aleatorio %d", idx);
    static const char *labels[] = {"FlexOS Ultra", "", "Mi \"P4\"", "\xC3\x91" "and\xC3\xBA", "una etiqueta bastante larga para el nombre del dispositivo, mas de cuarenta y ocho"};
    static const char *addrs[] = {"ana.p4@flex", "otra@flex", "x@y", "sin-arroba", "a@b.c"};
    static const char *disps[] = {"Ana", "", "Ana \xC3\x91" "u\xC3\xB1" "ez", "N"};
    int len = 12 + (int)(rnd() % 40);
    switch (rnd() % 5) {   // la mitad de los guiones empiezan con la cuenta ya vinculada (si no, la validacion apenas se ejerce)
    case 0: s.wifi(true); break;
    case 1: case 2: s.wifi(true).link(); break;
    case 3: s.wifi(true).link().power().step(1 + rnd() % 80); break;
    default: break;
    }
    for (int i = 0; i < len; i++) {
        switch (rnd() % 40) {
        case 0: case 1: s.wifi(rnd() % 2); break;
        case 2: case 3: case 4: s.step(1 + rnd() % 8); break;
        case 5: case 6: s.step(20 + rnd() % 700, 100 + rnd() % 3 * 400); break;
        case 7: s.step(1 + rnd() % 20, 60000); break;
        case 8: case 9: s.req(rnd() % 8 == 0 ? nullptr : labels[rnd() % 5]); break;
        case 10: case 11: s.link(); break;
        case 12: s.cancel(); break;
        case 13: case 14: s.reqval(); break;
        case 15: s.rejected(); break;
        case 16: s.forget(); break;
        case 17: case 18: s.power(); break;
        case 19: case 20: s.srv(SK_SESSION, rnd() % SM_COUNT); break;
        case 21: s.srv(SK_PENDING, rnd() % 4 == 0 ? 1000 : (long)(rnd() % 4)); break;
        case 22: s.srv(SK_EXPIRE, rnd() % 4 == 0); break;
        case 23: s.srv(SK_POSTFAIL, rnd() % 3); s.srv(SK_FAILERR, rnd() % 2 ? -1 : -0x7F00 + (long)(rnd() % 4) * 0x1000); break;
        case 24: s.srv(SK_POLLFAIL, rnd() % 5); break;
        case 25: s.srv(SK_TLSERR, rnd() % 2 ? -1 : -0x2700); break;
        case 26: s.srv(SK_LATENCY, rnd() % 3 ? 0 : (long)(rnd() % 16000)); break;
        case 27: s.srv(SK_POSTSTATUS, rnd() % 3 ? 0 : (long)(rnd() % 2 ? 429 : 500)); s.srv(SK_CONFLICTS, rnd() % 3 == 0); break;
        case 28: s.srv(SK_POLLSTATUS, rnd() % 4 ? 0 : (long)(rnd() % 2 ? 404 : 503)); break;
        case 29: s.srv(SK_ENVFAULT, rnd() % 3 ? 0 : (long)(rnd() % 18)); break;
        case 30: s.srv(SK_SESSFAULT, rnd() % 3 ? 0 : (long)(rnd() % 11)); break;
        case 31: s.srv(SK_HOOKSESS, rnd() % 3 ? 0 : (long)(1 + rnd() % 3)); s.srv(SK_HOOKPOLL, rnd() % 3 ? 0 : (long)(1 + rnd() % 2)); break;
        case 32: s.srv(SK_ADDR, 0, addrs[rnd() % 5]); s.srv(SK_DISPLAY, 0, disps[rnd() % 4]); break;
        case 33: if (rnd() % 2) { s.mem(24, 10); } else { s.mem(200, 100); } break;
        case 34: s.dns(rnd() % 2); break;
        case 35: s.taskfail(rnd() % 3); break;
        case 36: s.bearer(rnd() % 3 == 0 ? (long)(rnd() % 50) : 64); break;
        case 37: s.plan((PlanAct)(rnd() % 7), 100 + rnd() % 20000); break;
        case 38: s.clock(rnd() % 3 ? (long)(rnd() % 5000) : (long)(rnd() % 20000000)); break;
        case 39:
            if (rnd() % 4 == 0) {
                s.nvsbroken(rnd() % 2);
            } else if (rnd() % 3 == 0) {
                s.nvsput("u", "authst", fmt("%u", rnd() % 4));
            } else if (rnd() % 3 == 0) {
                s.nvswipe();
            } else {
                s.nvsput("b", "linked", rnd() % 2 ? "1" : "0");
            }
            break;
        }
    }
    s.step(5).wifi(true).step(300);
    return s;
}

// Antes de arrancar (ni flexAccountBegin ni flex_account_begin se han llamado) las dos dicen "sin cuenta" y rechazan las peticiones; sin operaciones el servicio no hace nada
static void t_prebegin()
{
    for (int phase = 0; phase < 3; phase++) {   // 0: operaciones puestas; 1: set(NULL, ops); 2: set(cfg, NULL)
        if (phase == 1) {
            flex_account_set(NULL, account_ops_netstub());
        } else if (phase == 2) {
            flex_account_set(&g_cfg, NULL);
        } else {
            flex_account_set(&g_cfg, account_ops_netstub());
        }
        char b1[48], b2[48];
        memset(b1, 'x', sizeof(b1));
        memset(b2, 'x', sizeof(b2));
        bool r1 = kRef.request_code("x"), r2 = kPort.request_code("x");
        bool l1 = kRef.linked(), l2 = kPort.linked(), u1 = kRef.usable(), u2 = kPort.usable();
        int s1 = kRef.state(), s2 = kPort.state(), k1 = kRef.link_state(), k2 = kPort.link_state();
        Snap a = kRef.snap(), c = kPort.snap();
        bool c1 = kRef.copy_bearer(b1, sizeof(b1)), c2 = kPort.copy_bearer(b2, sizeof(b2));
        kRef.cancel();
        kPort.cancel();
        kRef.req_validation();
        kPort.req_validation();
        kRef.report_rejected();
        kPort.report_rejected();
        CHECK(!r1 && !r2 && !l1 && !l2 && !u1 && !u2, "antes de arrancar (fase %d): rechaza, sin cuenta, no usable", phase);
        CHECK(s1 == s2 && k1 == k2 && s2 == FLEX_ACCOUNT_UNLINKED && k2 == FLEX_LINK_UNLINKED, "antes de arrancar (fase %d): UNLINKED", phase);
        CHECK(a.state == c.state && a.link == c.link && a.progress == c.progress && a.linked == c.linked && !c.linked && a.stage == c.stage && a.error == c.error && a.addr == c.addr,
              "antes de arrancar (fase %d): la foto es la de 'sin cuenta' (%d/%d)", phase, a.state, c.state);
        CHECK(!c1 && !c2, "antes de arrancar: no hay credencial que copiar");
        flex_account_snapshot_t z;
        memset(&z, 0x55, sizeof(z));
        flex_account_snapshot(&z);
        CHECK(z.state == FLEX_ACCOUNT_UNLINKED && z.link == FLEX_LINK_UNLINKED && !z.linked && !z.stage[0] && !z.code[0] && !z.flex_address[0] && z.progress == 0,
              "antes de arrancar (fase %d): la foto esta a cero", phase);
        flex_account_snapshot(NULL);   // no revienta
        flex_account_request_validation();
        flex_account_report_rejected();
        flex_account_cancel();
        flex_account_forget_local();
        flex_account_step();
    }
    flex_account_set(&g_cfg, account_ops_netstub());
}

int main()
{
    printf("=== Flex Account · servicio en C contra el Arduino ===\n");
    gKey.generate();
    ref_acct_set_key(gKey.pub.data());
    flex_account_test_set_key(gKey.pub.data());
    static const flex_account_cfg_t cfg = {"1.0.0"};
    flex_account_set(&cfg, account_ops_netstub());
    g_ops_nl = *account_ops_netstub();
    g_ops_nl.lock = nullptr;
    g_ops_nl.unlock = nullptr;
    g_ops_nl.log = nullptr;
    kPortNL = kPort;
    kPortNL.name = "C sin cerrojo";
    t_prebegin();

    int named_ok = 0;
    std::vector<Script> sc = named();
    for (const Script &s : sc) {
        if (compare(s)) {
            named_ok++;
        }
    }
    printf("-- %d/%zu escenarios con nombre identicos --\n", named_ok, sc.size());
    {
        int nl_ok = 0;
        for (const Script &s2 : sc) {
            if (compare(s2, 1000, true)) nl_ok++;
        }
        printf("-- %d/%zu escenarios identicos con operaciones sin cerrojo --\n", nl_ok, sc.size());
    }

    // El mismo guion con el reloj a punto de dar la vuelta (los 32 bits de millis() se acaban en 49,7 dias): el C, que hace todas las cuentas de tiempo con diferencias sin
    // signo, tiene que contar lo mismo que con el reloj en 1000
    {
        int bad = 0;
        for (const Script &s : sc) {
            if (s.name == "arranque de fabrica") {
                continue;
            }
            Trace a, b;
            run(kPort, s, a, 1000);
            run(kPort, s, b, 0xFFFFFFFFul - 5000);
            // los relojes relativos coinciden; los absolutos no se comparan (solo las lineas)
            size_t n = std::min(a.lines.size(), b.lines.size());
            bool same = a.lines.size() == b.lines.size();
            for (size_t i = 0; same && i < n; i++) {
                same = a.lines[i] == b.lines[i];
            }
            if (!same) {
                bad++;
                printf("  FALLO: %s: con el reloj dando la vuelta se comporta distinto\n", s.name.c_str());
                for (size_t i = 0; i < n; i++) {
                    if (a.lines[i] != b.lines[i]) {
                        printf("    base : %s\n    vuelta: %s\n", a.lines[i].c_str(), b.lines[i].c_str());
                        break;
                    }
                }
            }
        }
        CHECK(bad == 0, "%d escenarios cambian al dar la vuelta millis()", bad);
        printf("-- vuelta de millis(): %zu escenarios --\n", sc.size());
    }

    int n = getenv("FUZZ_N") ? atoi(getenv("FUZZ_N")) : 400;
    int ok = 0;
    for (int i = 0; i < n; i++) {
        if (compare(random_script(i))) {
            ok++;
        } else if (gFails > 12) {
            break;
        }
    }
    printf("-- %d/%d guiones aleatorios identicos --\n", ok, n);
    printf("-- cobertura: %zu pares (estado, vinculo) distintos;", gPairs.size());
    for (auto &kv : gEvents) {
        printf(" %s=%d", kv.first.c_str(), kv.second);
    }
    printf(" --\n");
    gKey.free_();
    printf("=== %d comprobaciones, %d fallos ===\n", gChecks, gFails);
    if (!gFails) {
        printf("OK: Flex Account servicio\n");
    }
    return gFails ? 1 : 0;
}
