// Referencia: el nucleo de FlexOS_Account.cpp (Arduino) tal cual, compilado contra tests/host/netstub del propio Arduino (red, NVS y reloj simulados con la semantica
// exacta de arduino-esp32). Se incluye el .cpp ENTERO: las funciones del espacio de nombres anonimo (base64url, firma, cargas, credencial) quedan al alcance de esta unidad
// y se exportan con nombres planos para que la prueba de C las compare con las del puerto.
#define FLEXOS_HOST_TEST 1
#define esp_fill_random ref_fill_random   // el aleatorio de este codigo lo fija la prueba (la credencial sale de el)
#include "../../../../FlexOS_Ultra/FlexOS_Account.cpp"
#undef esp_fill_random

#include <vector>

static std::vector<uint8_t> g_rnd;
static size_t g_rnd_pos = 0;
void ref_fill_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        p[i] = g_rnd.empty() ? (uint8_t)(i * 7 + 1) : g_rnd[g_rnd_pos++ % g_rnd.size()];
    }
}

extern "C" {
void ref_acct_set_random(const unsigned char *bytes, int n)
{
    g_rnd.assign(bytes, bytes + n);
    g_rnd_pos = 0;
}
void ref_acct_set_key(const unsigned char *pub65) { flexAccountTestSetKey(pub65); }
int ref_acct_classify(int status, const char *code) { return (int)flexAccountClassify(status, code); }
unsigned ref_acct_backoff(unsigned f) { return flexAccountBackoffMs((uint8_t)f); }
unsigned long ref_acct_nvslen(unsigned long ret, const char *buf, unsigned long cap) { return (unsigned long)flexNvsStrLen((size_t)ret, buf, (size_t)cap); }
int ref_acct_json_escape(const char *in, char *out, unsigned long cap) { return jsonEscape(in, out, cap) ? 1 : 0; }
int ref_acct_b64_value(char c) { return b64Value(c); }
int ref_acct_b64_decode(const char *text, unsigned char *out, unsigned long cap, unsigned long max_out, unsigned long *out_len)
{
    uint8_t *d = nullptr;
    size_t n = 0;
    if (!base64UrlDecode(text, &d, &n, max_out)) {
        return 0;
    }
    if (n <= cap) {
        memcpy(out, d, n);
    }
    *out_len = n;
    free(d);
    return n <= cap ? 1 : 0;
}
int ref_acct_valid_address(const char *a) { return validAddress(a) ? 1 : 0; }
int ref_acct_verify(const unsigned char *payload, unsigned long len, const unsigned char *sig64) { return verifySignature(payload, len, sig64) ? 1 : 0; }
int ref_acct_signed_payload(const unsigned char *env, unsigned long len, unsigned char *out, unsigned long cap, unsigned long *out_len)
{
    uint8_t *p = nullptr;
    size_t n = 0;
    if (!signedPayload(env, len, &p, &n)) {
        return 0;
    }
    if (n <= cap) {
        memcpy(out, p, n);
    }
    *out_len = n;
    free(p);
    return n <= cap ? 1 : 0;
}
int ref_acct_parse_code(const unsigned char *payload, unsigned long len, const char *hash, char *code, char *url)
{
    return parseCodePayload(payload, len, hash, code, url) ? 1 : 0;
}
int ref_acct_parse_status(const unsigned char *payload, unsigned long len, char *address, char *name) { return (int)parseStatusPayload(payload, len, address, name); }
int ref_acct_make_bearer(char *out, char *hash) { return makeBearer(out, hash) ? 1 : 0; }
void ref_acct_hex32(const unsigned char *d, char *out) { hex32(d, out); }
int ref_acct_sha256(const unsigned char *d, unsigned long n, unsigned char *out) { return sha256(d, n, out) ? 1 : 0; }
const char *ref_acct_link_label(int l) { return flexAccountLinkLabel((FlexAccountLink)l); }
const char *ref_acct_verdict_detail(int v) { return verdictDetail((FlexSessionVerdict)v); }
// FlexOS_CloudTLS.cpp (funciones publicas de Arduino)
extern size_t gNetInternalFree, gNetInternalBlock;   // netstub.cpp: lo que dice heap_caps_get_free_size / get_largest_free_block de la SRAM interna
int ref_tls_room(unsigned long internal_free, unsigned long block)
{
    gNetInternalFree = internal_free;
    gNetInternalBlock = block;
    return flexTlsRoom(nullptr, nullptr) ? 1 : 0;
}
const char *ref_tls_reason(int e, char *out, unsigned long cap) { return flexTlsReason(e, out, cap); }
const char *ref_tls_phase(int dns, unsigned el, unsigned hs, unsigned cn, char *out, unsigned long cap) { return flexTlsPhase(dns != 0, el, hs, cn, out, cap); }
int ref_url_host(const char *url, char *out, unsigned long cap) { return flexUrlHost(url, out, cap) ? 1 : 0; }
}
