#include "flex_passcode.h"

#include <stdlib.h>
#include <string.h>
#include "flex_storage.h"

#define SALT_LEN 16
#define KEY_LEN  32

#ifdef FLEX_HOST_TEST
#include <openssl/hmac.h>
#include <openssl/rand.h>
static void hmac256(const uint8_t *key, size_t klen, const uint8_t *in, size_t ilen, uint8_t out[32])
{
    unsigned int n = 32;
    HMAC(EVP_sha256(), key, (int)klen, in, ilen, out, &n);
}
static void random_bytes(void *out, size_t n)
{
    RAND_bytes(out, (int)n);
}
#else
#include "esp_random.h"
#include "mbedtls/md.h"
static void hmac256(const uint8_t *key, size_t klen, const uint8_t *in, size_t ilen, uint8_t out[32])
{
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, klen, in, ilen, out);
}
static void random_bytes(void *out, size_t n)
{
    esp_fill_random(out, n);   // generador de hardware
}
#endif

void flex_lock_wipe(void *p, size_t n)
{
    if (!p) {
        return;
    }
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

bool flex_lock_equal_ct(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= x[i] ^ y[i];
    }
    return d == 0;
}

// PBKDF2-HMAC-SHA256, un solo bloque (out_len <= 32): U1 = HMAC(clave, sal||00000001),
// Ui = HMAC(clave, Ui-1), resultado = XOR de todas.
void flex_lock_kdf(const char *secret, const uint8_t *salt, size_t salt_len, uint32_t iters, uint8_t *out,
                   size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    out_len = out_len > 32 ? 32 : out_len;
    iters = iters ? iters : 1;
    size_t sl = secret ? strlen(secret) : 0;
    salt_len = salt ? salt_len : 0;
    uint8_t blk[SALT_LEN + 4 + 32];
    size_t bl = salt_len > sizeof(blk) - 4 ? sizeof(blk) - 4 : salt_len;
    if (bl) {
        memcpy(blk, salt, bl);
    }
    blk[bl] = 0;
    blk[bl + 1] = 0;
    blk[bl + 2] = 0;
    blk[bl + 3] = 1;
    uint8_t u[32], acc[32];
    hmac256((const uint8_t *)secret, sl, blk, bl + 4, u);
    memcpy(acc, u, 32);
    for (uint32_t i = 1; i < iters; i++) {
        hmac256((const uint8_t *)secret, sl, u, 32, u);
        for (int k = 0; k < 32; k++) {
            acc[k] ^= u[k];
        }
    }
    memcpy(out, acc, out_len);
    flex_lock_wipe(u, sizeof(u));
    flex_lock_wipe(acc, sizeof(acc));
    flex_lock_wipe(blk, sizeof(blk));
}

int flex_lock_type(void)
{
    int t = (int)flex_cfg_get_i32("locktype", 0);
    return (t == FLEX_LOCK_PIN || t == FLEX_LOCK_PASS) ? t : FLEX_LOCK_NONE;
}

int flex_lock_len(void)
{
    return flex_lock_type() == FLEX_LOCK_PIN ? (int)flex_cfg_get_i32("locklen", 0) : 0;
}

static bool store(const char *secret, int type, bool drop_plain)
{
    uint8_t salt[SALT_LEN], hash[KEY_LEN];
    random_bytes(salt, sizeof(salt));
    flex_lock_kdf(secret, salt, sizeof(salt), FLEX_LOCK_ITERS, hash, sizeof(hash));
    bool ok = flex_cfg_set_blob("lockslt", salt, sizeof(salt)) == ESP_OK &&
              flex_cfg_set_blob("lockhsh", hash, sizeof(hash)) == ESP_OK &&
              flex_cfg_set_u32("lockitr", FLEX_LOCK_ITERS) == ESP_OK &&
              flex_cfg_set_i32("locklen", (int32_t)strlen(secret)) == ESP_OK &&
              flex_cfg_set_i32("locktype", type) == ESP_OK;
    // La clave nueva tiene que estar EN LA FLASH antes de borrar la antigua:
    // un corte de corriente entre medias no puede dejar el aparato sin
    // ninguna de las dos.
    if (ok) {
        ok = flex_cfg_flush(3000) == ESP_OK;
    }
    if (ok && drop_plain) {
        flex_cfg_erase("lockpin");
        flex_cfg_erase("lockpass");
        flex_cfg_flush(3000);
    }
    flex_lock_wipe(hash, sizeof(hash));
    flex_lock_wipe(salt, sizeof(salt));
    return ok;
}

bool flex_lock_set(const char *secret, int type)
{
    if (!secret || !secret[0] || strlen(secret) >= FLEX_LOCK_SECRET_MAX) {
        return false;
    }
    if (type != FLEX_LOCK_PIN && type != FLEX_LOCK_PASS) {
        return false;
    }
    flex_lock_verify_cancel();
    return store(secret, type, true);
}

// ---- a plazos ------------------------------------------------------------------
static bool s_on;
static char s_secret[FLEX_LOCK_SECRET_MAX];
static uint8_t s_u[32], s_acc[32], s_want[KEY_LEN];
static uint32_t s_iters, s_done;

void flex_lock_verify_cancel(void)
{
    s_on = false;
    s_iters = s_done = 0;
    flex_lock_wipe(s_secret, sizeof(s_secret));
    flex_lock_wipe(s_u, sizeof(s_u));
    flex_lock_wipe(s_acc, sizeof(s_acc));
    flex_lock_wipe(s_want, sizeof(s_want));
}

bool flex_lock_verify_active(void)
{
    return s_on;
}

static bool read_params(uint8_t salt[SALT_LEN], uint8_t want[KEY_LEN], uint32_t *iters)
{
    size_t gs = flex_cfg_get_blob("lockslt", salt, SALT_LEN);
    size_t gh = flex_cfg_get_blob("lockhsh", want, KEY_LEN);
    *iters = flex_cfg_get_u32("lockitr", FLEX_LOCK_ITERS);
    return gs == SALT_LEN && gh == KEY_LEN && *iters != 0 && *iters <= 1000000u;
}

// Red de seguridad que la version Arduino no tiene: si la migracion del
// arranque no pudo completarse (flash llena, NVS en solo lectura), la clave en
// texto claro de una version antigua sigue siendo la valida. Sin esto, ese
// usuario se quedaria fuera de su propio aparato. Comparacion en tiempo
// constante sobre el buffer entero (no revela la longitud por el tiempo).
static bool legacy_match(const char *secret)
{
    int type = (int)flex_cfg_get_i32("locktype", 0);
    if (type != FLEX_LOCK_PIN && type != FLEX_LOCK_PASS) {
        return false;
    }
    char old[FLEX_LOCK_SECRET_MAX], in[FLEX_LOCK_SECRET_MAX];
    memset(old, 0, sizeof(old));
    memset(in, 0, sizeof(in));
    flex_cfg_get_str(type == FLEX_LOCK_PIN ? "lockpin" : "lockpass", old, sizeof(old), "");
    size_t n = strlen(secret);
    memcpy(in, secret, n < sizeof(in) ? n : sizeof(in) - 1);
    bool ok = old[0] != 0 && flex_lock_equal_ct(old, in, sizeof(old));
    flex_lock_wipe(old, sizeof(old));
    flex_lock_wipe(in, sizeof(in));
    return ok;
}

bool flex_lock_verify_begin(const char *secret)
{
    flex_lock_verify_cancel();
    if (!secret || strlen(secret) >= sizeof(s_secret)) {
        return false;
    }
    uint8_t salt[SALT_LEN];
    uint32_t iters;
    if (!read_params(salt, s_want, &iters)) {
        flex_lock_verify_cancel();
        return false;
    }
    memcpy(s_secret, secret, strlen(secret) + 1);
    uint8_t blk[SALT_LEN + 4];
    memcpy(blk, salt, SALT_LEN);
    blk[SALT_LEN] = 0;
    blk[SALT_LEN + 1] = 0;
    blk[SALT_LEN + 2] = 0;
    blk[SALT_LEN + 3] = 1;
    hmac256((const uint8_t *)s_secret, strlen(s_secret), blk, sizeof(blk), s_u);
    memcpy(s_acc, s_u, 32);
    flex_lock_wipe(blk, sizeof(blk));
    flex_lock_wipe(salt, sizeof(salt));
    s_iters = iters;
    s_done = 1;
    s_on = true;
    return true;
}

int flex_lock_verify_step(uint32_t budget)
{
    if (!s_on) {
        return FLEX_LOCK_FAIL;
    }
    budget = budget ? budget : 1;
    size_t sl = strlen(s_secret);
    uint32_t n = 0;
    while (s_done < s_iters && n < budget) {
        hmac256((const uint8_t *)s_secret, sl, s_u, 32, s_u);
        for (int k = 0; k < 32; k++) {
            s_acc[k] ^= s_u[k];
        }
        s_done++;
        n++;
    }
    if (s_done < s_iters) {
        return FLEX_LOCK_BUSY;
    }
    bool ok = flex_lock_equal_ct(s_acc, s_want, sizeof(s_want));
    flex_lock_verify_cancel();
    return ok ? FLEX_LOCK_OK : FLEX_LOCK_FAIL;
}

bool flex_lock_verify(const char *secret)
{
    if (!flex_lock_verify_begin(secret)) {
        return false;
    }
    return flex_lock_verify_step(0xFFFFFFFFu) == FLEX_LOCK_OK;
}

// Variables locales: no lee ni toca el estado de la verificacion a plazos, asi
// que se puede llamar desde otra tarea mientras la pantalla tiene una a medias.
bool flex_lock_verify_alone(const char *secret)
{
    if (!secret || strlen(secret) >= FLEX_LOCK_SECRET_MAX) {
        return false;
    }
    uint8_t salt[SALT_LEN], want[KEY_LEN], got[KEY_LEN];
    uint32_t iters;
    bool ok = false;
    if (read_params(salt, want, &iters)) {
        flex_lock_kdf(secret, salt, sizeof(salt), iters, got, sizeof(got));
        ok = flex_lock_equal_ct(got, want, sizeof(want));
    } else {
        ok = legacy_match(secret);
    }
    flex_lock_wipe(salt, sizeof(salt));
    flex_lock_wipe(want, sizeof(want));
    flex_lock_wipe(got, sizeof(got));
    return ok;
}

bool flex_lock_clear(void)
{
    flex_lock_verify_cancel();
    flex_cfg_erase("lockslt");
    flex_cfg_erase("lockhsh");
    flex_cfg_erase("lockitr");
    flex_cfg_erase("locklen");
    flex_cfg_erase("lockpin");
    flex_cfg_erase("lockpass");
    flex_cfg_set_i32("locktype", 0);
    return true;
}

int flex_lock_migrate(void)
{
    int type = (int)flex_cfg_get_i32("locktype", 0);
    uint8_t probe[KEY_LEN];
    bool have_hash = flex_cfg_get_blob("lockhsh", probe, sizeof(probe)) == sizeof(probe);
    flex_lock_wipe(probe, sizeof(probe));
    char old[FLEX_LOCK_SECRET_MAX];
    flex_cfg_get_str(type == FLEX_LOCK_PIN ? "lockpin" : "lockpass", old, sizeof(old), "");
    int ret;
    if (have_hash) {
        ret = 0;
        if (old[0]) {
            flex_cfg_erase("lockpin");
            flex_cfg_erase("lockpass");
            ret = 1;
        }
    } else if (type == 0 || !old[0]) {
        ret = 0;
    } else if (store(old, type, false) && flex_lock_verify(old)) {
        // escrito, comprobado y en la flash: ahora si, fuera el texto claro
        flex_cfg_erase("lockpin");
        flex_cfg_erase("lockpass");
        flex_cfg_flush(3000);
        ret = 1;
    } else {
        flex_cfg_erase("lockslt");
        flex_cfg_erase("lockhsh");
        flex_cfg_erase("lockitr");
        ret = -1;
    }
    flex_lock_wipe(old, sizeof(old));
    return ret;
}

int flex_lock_fails(void)
{
    int32_t n = flex_cfg_get_i32("lockfails", 0);
    return n < 0 ? 0 : n > 9999 ? 9999 : (int)n;
}

void flex_lock_set_fails(int n)
{
    flex_cfg_set_i32("lockfails", n < 0 ? 0 : n > 9999 ? 9999 : n);
}

uint32_t flex_lock_penalty_ms(int fails)
{
    if (fails >= 6) {
        return 300000u;
    }
    if (fails >= 4) {
        return 30000u;
    }
    return 0;
}
