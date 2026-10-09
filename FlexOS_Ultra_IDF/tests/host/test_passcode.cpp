// Pruebas de host de la clave del sistema (components/flex_security).
//
// La clave guardada por una placa con el firmware Arduino tiene que abrir la
// misma placa con el firmware ESP-IDF, y al reves. Para comprobarlo de verdad
// se compilan LAS DOS implementaciones (FlexOS_Ultra/FlexOS_Passcode.cpp, solo
// lectura, y flex_passcode.c) sobre la MISMA NVS en memoria: el doble de
// Preferences del arnes Arduino (tests/host/inostub). Los ajustes de ESP-IDF
// (flex_cfg_*) se implementan aqui encima de ese mismo almacen, con los mismos
// tipos que usa la NVS real (i32, u32, str, blob).
//
// Limite honesto: el HMAC de las dos es el de OpenSSL; en la placa es el de
// mbedTLS. Lo que se prueba es el formato, la derivacion y la logica.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "Preferences.h"
#include "FlexOS_Passcode.h"

extern "C" {
#include "flex_passcode.h"
#include "flex_storage.h"
}

static int g_checks, g_fails;
#define CHECK(c)                                                                       \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(c)) {                                                                    \
            g_fails++;                                                                 \
            fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #c);              \
        }                                                                              \
    } while (0)

// ---- flex_cfg_* sobre el almacen de Preferences ("flexos") ----------------------
static bool g_fail_blob;   // inyecta un fallo de escritura (flash llena)
// Corte de corriente: a partir de la escritura numero g_cut ya no llega nada a
// la flash. El almacen real graba en el orden de los cambios (flex_kv), que es
// el orden de las llamadas: este doble escribe en ese mismo orden.
static int g_cut = -1, g_cut_n;
static bool cut_now(void)
{
    return g_cut >= 0 && g_cut_n++ >= g_cut;
}

extern "C" {
int32_t flex_cfg_get_i32(const char *k, int32_t d)
{
    Preferences p;
    p.begin(FLEX_NVS_NS, true);
    return p.isKey(k) ? p.getInt(k, d) : d;
}
uint32_t flex_cfg_get_u32(const char *k, uint32_t d)
{
    Preferences p;
    p.begin(FLEX_NVS_NS, true);
    return p.isKey(k) ? p.getUInt(k, d) : d;
}
void flex_cfg_get_str(const char *k, char *out, size_t cap, const char *d)
{
    Preferences p;
    p.begin(FLEX_NVS_NS, true);
    String s = p.getString(k, d ? d : "");
    snprintf(out, cap, "%s", s.c_str());
}
size_t flex_cfg_get_blob(const char *k, void *out, size_t cap)
{
    Preferences p;
    p.begin(FLEX_NVS_NS, true);
    uint8_t tmp[FLEXPREF_VAL_MAX];
    size_t n = p.getBytes(k, tmp, sizeof(tmp));
    if (n && out) {
        memcpy(out, tmp, n < cap ? n : cap);
    }
    return n;
}
esp_err_t flex_cfg_set_i32(const char *k, int32_t v)
{
    if (cut_now()) {
        return ESP_OK;
    }
    Preferences p;
    p.begin(FLEX_NVS_NS, false);
    p.putInt(k, v);
    return ESP_OK;
}
esp_err_t flex_cfg_set_u32(const char *k, uint32_t v)
{
    if (cut_now()) {
        return ESP_OK;
    }
    Preferences p;
    p.begin(FLEX_NVS_NS, false);
    p.putUInt(k, v);
    return ESP_OK;
}
esp_err_t flex_cfg_set_str(const char *k, const char *v)
{
    if (cut_now()) {
        return ESP_OK;
    }
    Preferences p;
    p.begin(FLEX_NVS_NS, false);
    p.putString(k, v);
    return ESP_OK;
}
esp_err_t flex_cfg_set_blob(const char *k, const void *d, size_t l)
{
    if (g_fail_blob) {
        return ESP_FAIL;
    }
    if (cut_now()) {
        return ESP_OK;
    }
    Preferences p;
    p.begin(FLEX_NVS_NS, false);
    p.putBytes(k, d, l);
    return ESP_OK;
}
esp_err_t flex_cfg_erase(const char *k)
{
    if (cut_now()) {
        return ESP_OK;
    }
    Preferences p;
    p.begin(FLEX_NVS_NS, false);
    p.remove(k);
    return ESP_OK;
}
esp_err_t flex_cfg_flush(uint32_t t)
{
    (void)t;
    return ESP_OK;
}
}

static bool has(const char *k)
{
    Preferences p;
    p.begin(FLEX_NVS_NS, true);
    return p.isKey(k);
}

static void hex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        sprintf(out + i * 2, "%02x", p[i]);
    }
}

static void test_kdf(void)
{
    uint8_t dk[32];
    char h[80];
    flex_lock_kdf("password", (const uint8_t *)"salt", 4, 1, dk, 32);
    hex(dk, 32, h);
    CHECK(!strcmp(h, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    flex_lock_kdf("password", (const uint8_t *)"salt", 4, 2, dk, 32);
    hex(dk, 32, h);
    CHECK(!strcmp(h, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
    flex_lock_kdf("password", (const uint8_t *)"salt", 4, 4096, dk, 32);
    hex(dk, 32, h);
    CHECK(!strcmp(h, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));

    // Bit a bit igual que la de Arduino en casos raros (0 iteraciones = 1,
    // salida > 32 recortada, sal de 16 como la real)
    uint8_t a[32], b[32];
    const uint8_t salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const uint32_t its[] = {0, 1, 7, 1500, 12000};
    for (uint32_t it : its) {
        memset(a, 0, sizeof(a));
        memset(b, 0, sizeof(b));
        flex_lock_kdf("1234", salt, 16, it, a, 32);
        flexLockKdf("1234", salt, 16, it, b, 32);
        CHECK(!memcmp(a, b, 32));
    }
    uint8_t big[40], bigr[40];
    memset(big, 0x5A, sizeof(big));
    memset(bigr, 0x5A, sizeof(bigr));
    flex_lock_kdf("x", salt, 16, 3, big, sizeof(big));
    flexLockKdf("x", salt, 16, 3, bigr, sizeof(bigr));
    CHECK(!memcmp(big, bigr, sizeof(big)));
    CHECK(big[32] == 0x5A);   // no escribe mas alla de 32
    flex_lock_kdf("x", salt, 16, 3, NULL, 32);   // no revienta
    CHECK(flex_lock_equal_ct(a, a, 32));
    CHECK(!flex_lock_equal_ct(a, salt, 16));
    uint8_t z[16];
    memset(z, 0xAA, sizeof(z));
    flex_lock_wipe(z, sizeof(z));
    bool zero = true;
    for (uint8_t v : z) {
        zero = zero && v == 0;
    }
    CHECK(zero);
}

static void test_cross(void)
{
    // Arduino guarda -> ESP-IDF abre
    flexPrefsWipe();
    CHECK(flexLockSet("2580", FLEXLOCK_PIN));
    CHECK(flex_lock_type() == FLEX_LOCK_PIN);
    CHECK(flex_lock_len() == 4);
    CHECK(flex_lock_verify("2580"));
    CHECK(flex_lock_verify_alone("2580"));
    CHECK(!flex_lock_verify_alone("2581"));
    CHECK(!flex_lock_verify_alone("258"));
    CHECK(!flex_lock_verify_alone("25800"));
    CHECK(!flex_lock_verify_alone(""));
    CHECK(!flex_lock_verify(NULL));

    // ESP-IDF guarda -> Arduino abre (contrasena UTF-8)
    flexPrefsWipe();
    const char *pw = "Contrase\xC3\xB1" "a 2026!";
    CHECK(flex_lock_set(pw, FLEX_LOCK_PASS));
    CHECK(flexLockType() == FLEXLOCK_PASS);
    CHECK(flexLockLen() == 0);
    CHECK(flex_lock_len() == 0);
    CHECK(flexLockVerify(pw));
    CHECK(flexLockVerifyAlone(pw));
    CHECK(!flexLockVerify("Contrase\xC3\xB1" "a 2026"));
    CHECK(!has("lockpin") && !has("lockpass"));
    {
        Preferences p;
        p.begin("flexos", true);
        CHECK(p.getUInt("lockitr", 0) == FLEX_LOCK_ITERS);
        uint8_t s[32];
        CHECK(p.getBytes("lockslt", s, sizeof(s)) == 16);
        CHECK(p.getBytes("lockhsh", s, sizeof(s)) == 32);
    }

    // Sal nueva en cada set: misma clave, hash distinto
    uint8_t h1[32], h2[32];
    CHECK(flex_lock_set("1234", FLEX_LOCK_PIN));
    flex_cfg_get_blob("lockhsh", h1, 32);
    CHECK(flex_lock_set("1234", FLEX_LOCK_PIN));
    flex_cfg_get_blob("lockhsh", h2, 32);
    CHECK(memcmp(h1, h2, 32) != 0);
    CHECK(flexLockVerify("1234"));

    // Reglas de entrada
    CHECK(!flex_lock_set("", FLEX_LOCK_PIN));
    CHECK(!flex_lock_set(NULL, FLEX_LOCK_PIN));
    CHECK(!flex_lock_set("1234", 3));
    char longs[FLEX_LOCK_SECRET_MAX + 1];
    memset(longs, 'a', sizeof(longs));
    longs[FLEX_LOCK_SECRET_MAX] = 0;
    CHECK(!flex_lock_set(longs, FLEX_LOCK_PASS));
    CHECK(!flex_lock_verify_alone(longs));
    longs[FLEX_LOCK_SECRET_MAX - 1] = 0;   // 63 bytes: el maximo
    CHECK(flex_lock_set(longs, FLEX_LOCK_PASS));
    CHECK(flexLockVerify(longs));
    CHECK(flex_lock_verify_alone(longs));
}

static void test_steps(void)
{
    flexPrefsWipe();
    CHECK(flex_lock_set("13579", FLEX_LOCK_PIN));
    const uint32_t budgets[] = {1, 7, 1500, 11999, 12000, 50000};
    for (uint32_t b : budgets) {
        for (int good = 0; good < 2; good++) {
            CHECK(flex_lock_verify_begin(good ? "13579" : "13578"));
            CHECK(flex_lock_verify_active());
            int r, steps = 0;
            while ((r = flex_lock_verify_step(b)) == FLEX_LOCK_BUSY) {
                steps++;
            }
            CHECK(r == (good ? FLEX_LOCK_OK : FLEX_LOCK_FAIL));
            CHECK(!flex_lock_verify_active());
            CHECK(steps == (int)((FLEX_LOCK_ITERS - 1 + b - 1) / b) - 1 || b >= FLEX_LOCK_ITERS);
        }
    }
    // Cancelar a medias deja el modulo limpio
    CHECK(flex_lock_verify_begin("13579"));
    CHECK(flex_lock_verify_step(100) == FLEX_LOCK_BUSY);
    flex_lock_verify_cancel();
    CHECK(!flex_lock_verify_active());
    CHECK(flex_lock_verify_step(100) == FLEX_LOCK_FAIL);
    // La aislada no toca una a plazos en curso
    CHECK(flex_lock_verify_begin("13579"));
    CHECK(flex_lock_verify_step(100) == FLEX_LOCK_BUSY);
    CHECK(!flex_lock_verify_alone("00000"));
    CHECK(flex_lock_verify_active());
    int r;
    while ((r = flex_lock_verify_step(FLEX_LOCK_STEP_ITERS)) == FLEX_LOCK_BUSY) {
    }
    CHECK(r == FLEX_LOCK_OK);
    // Sin clave guardada: fallo inmediato
    flexPrefsWipe();
    CHECK(!flex_lock_verify_begin("1234"));
    CHECK(!flex_lock_verify("1234"));
}

static void legacy(const char *key, const char *val, int type)
{
    flexPrefsWipe();
    Preferences p;
    p.begin("flexos", false);
    p.putInt("locktype", type);
    p.putString(key, val);
}

static void test_migrate(void)
{
    // PIN en texto claro de una version antigua -> hash; Arduino lo abre
    legacy("lockpin", "2468", FLEX_LOCK_PIN);
    CHECK(flex_lock_migrate() == 1);
    CHECK(!has("lockpin") && !has("lockpass"));
    CHECK(flex_lock_type() == FLEX_LOCK_PIN && flex_lock_len() == 4);
    CHECK(flexLockVerify("2468") && flex_lock_verify_alone("2468"));
    CHECK(flex_lock_migrate() == 0);   // segunda vez: nada que hacer

    // Contrasena
    legacy("lockpass", "hola mundo", FLEX_LOCK_PASS);
    CHECK(flex_lock_migrate() == 1);
    CHECK(!has("lockpass"));
    CHECK(flexLockVerify("hola mundo"));

    // Ya migrado pero con restos en claro: se limpian
    flexPrefsWipe();
    CHECK(flex_lock_set("1111", FLEX_LOCK_PIN));
    {
        Preferences p;
        p.begin("flexos", false);
        p.putString("lockpin", "1111");
    }
    CHECK(flex_lock_migrate() == 1);
    CHECK(!has("lockpin"));
    CHECK(flex_lock_verify_alone("1111"));

    // Sin clave: no escribe nada
    flexPrefsWipe();
    unsigned w0 = flexPrefsWrites();
    CHECK(flex_lock_migrate() == 0);
    CHECK(flexPrefsWrites() == w0);

    // Misma disposicion final que la migracion de Arduino
    legacy("lockpin", "975310", FLEX_LOCK_PIN);
    CHECK(flexLockMigrate() == 1);
    bool ard[6];
    const char *keys[6] = {"lockslt", "lockhsh", "lockitr", "locklen", "locktype", "lockpin"};
    for (int i = 0; i < 6; i++) {
        ard[i] = has(keys[i]);
    }
    legacy("lockpin", "975310", FLEX_LOCK_PIN);
    CHECK(flex_lock_migrate() == 1);
    for (int i = 0; i < 6; i++) {
        CHECK(has(keys[i]) == ard[i]);
    }
    CHECK(flex_lock_len() == 6);

    // Fallo al escribir (flash llena): todo queda como estaba y la clave
    // antigua sigue abriendo (red de seguridad de ESP-IDF)
    legacy("lockpin", "8642", FLEX_LOCK_PIN);
    g_fail_blob = true;
    CHECK(flex_lock_migrate() == -1);
    g_fail_blob = false;
    CHECK(has("lockpin"));
    CHECK(!has("lockhsh") && !has("lockslt") && !has("lockitr"));
    CHECK(flex_lock_verify_alone("8642"));
    CHECK(!flex_lock_verify_alone("8643"));
    CHECK(!flex_lock_verify_alone("864"));
    CHECK(!flex_lock_verify_alone("86420"));
    CHECK(!flex_lock_verify_alone(""));
    // y el siguiente arranque si migra
    CHECK(flex_lock_migrate() == 1);
    CHECK(!has("lockpin"));
    CHECK(flexLockVerify("8642"));

    // locktype sin clave alguna: nunca se abre (no se trata como "sin clave")
    flexPrefsWipe();
    flex_cfg_set_i32("locktype", FLEX_LOCK_PIN);
    CHECK(!flex_lock_verify_alone(""));
    CHECK(!flex_lock_verify_alone("0000"));
    CHECK(flex_lock_type() == FLEX_LOCK_PIN);
}

static void test_corrupt(void)
{
    flexPrefsWipe();
    CHECK(flex_lock_set("4321", FLEX_LOCK_PIN));
    flex_cfg_set_u32("lockitr", 0);
    CHECK(!flex_lock_verify_alone("4321"));
    CHECK(!flex_lock_verify_begin("4321"));
    flex_cfg_set_u32("lockitr", 1000001);
    CHECK(!flex_lock_verify_alone("4321"));
    flex_cfg_set_u32("lockitr", FLEX_LOCK_ITERS);
    CHECK(flex_lock_verify_alone("4321"));
    uint8_t shortb[8] = {0};
    flex_cfg_set_blob("lockhsh", shortb, sizeof(shortb));
    CHECK(!flex_lock_verify_alone("4321"));
    flex_cfg_set_i32("locktype", 7);
    CHECK(flex_lock_type() == FLEX_LOCK_NONE);
    CHECK(flex_lock_len() == 0);
}

static void test_clear_fails(void)
{
    flexPrefsWipe();
    CHECK(flex_lock_set("1234", FLEX_LOCK_PIN));
    CHECK(flex_lock_clear());
    CHECK(flex_lock_type() == FLEX_LOCK_NONE && flexLockType() == FLEXLOCK_NONE);
    CHECK(!has("lockslt") && !has("lockhsh") && !has("lockitr") && !has("locklen"));
    CHECK(!flex_lock_verify_alone("1234"));

    CHECK(flex_lock_penalty_ms(0) == 0 && flex_lock_penalty_ms(3) == 0);
    CHECK(flex_lock_penalty_ms(4) == 30000 && flex_lock_penalty_ms(5) == 30000);
    CHECK(flex_lock_penalty_ms(6) == 300000 && flex_lock_penalty_ms(9999) == 300000);
    flex_lock_set_fails(-5);
    CHECK(flex_lock_fails() == 0);
    flex_lock_set_fails(12345);
    CHECK(flex_lock_fails() == 9999);
    flex_lock_set_fails(4);
    Preferences p;
    p.begin("flexos", true);
    CHECK(p.getInt("lockfails", -1) == 4);   // mismo tipo y clave que Arduino
    {
        Preferences q;
        q.begin("flexos", false);
        q.putInt("lockfails", -3);   // NVS corrupta
    }
    CHECK(flex_lock_fails() == 0);
}

// Un corte de corriente en CUALQUIER punto de cambiar o quitar la clave: tras
// el arranque (flex_lock_migrate) abre la de antes o la nueva, nunca ninguna, y
// Arduino y ESP-IDF ven lo mismo.
enum { OLD_NONE, OLD_PIN, OLD_PASS, OLD_LEGACY };
static void prep_old(int old)
{
    flexPrefsWipe();
    if (old == OLD_PIN) {
        flex_lock_set("1111", FLEX_LOCK_PIN);
    } else if (old == OLD_PASS) {
        flex_lock_set("vieja clave", FLEX_LOCK_PASS);
    } else if (old == OLD_LEGACY) {
        legacy("lockpin", "2468", FLEX_LOCK_PIN);
    }
}

static void run_op(int op)
{
    if (op == 0) {
        flex_lock_set("2580", FLEX_LOCK_PIN);
    } else if (op == 1) {
        flex_lock_set("nueva clave", FLEX_LOCK_PASS);
    } else {
        flex_lock_clear();
    }
}

static void test_power_cut(void)
{
    static const char *olds[] = {NULL, "1111", "vieja clave", "2468"};
    static const char *news[] = {"2580", "nueva clave", NULL};
    int cases = 0;
    for (int old = OLD_NONE; old <= OLD_LEGACY; old++) {
        for (int op = 0; op < 3; op++) {
            prep_old(old);
            g_cut = 1 << 30;
            g_cut_n = 0;
            run_op(op);
            int total = g_cut_n;
            g_cut = -1;
            for (int k = 0; k <= total; k++) {
                prep_old(old);
                g_cut = k;
                g_cut_n = 0;
                run_op(op);
                g_cut = -1;           // arranque siguiente
                flex_lock_migrate();
                cases++;
                const char *o = olds[old], *n = news[op];
                bool vo = o && flex_lock_verify_alone(o);
                bool vn = n && flex_lock_verify_alone(n);
                int t = flex_lock_type();
                // sin bloqueo, o se abre con la de antes o con la nueva
                CHECK(t == FLEX_LOCK_NONE || vo || vn);
                // la de antes deja de valer solo si la nueva ya vale (o se quito)
                if (o && !vo && t != FLEX_LOCK_NONE) {
                    CHECK(vn);
                }
                // los dos firmwares, de acuerdo
                CHECK(flexLockType() == t);
                if (o) {
                    CHECK(flexLockVerify(o) == vo);
                }
                if (n) {
                    CHECK(flexLockVerify(n) == vn);
                }
                CHECK(!has("lockjrn"));   // el arranque lo completa y lo borra
                // con el corte al final, el cambio esta hecho
                if (k == total) {
                    CHECK(op == 2 ? t == FLEX_LOCK_NONE : (vn && !(o && vo && strcmp(o, n) != 0)));
                }
            }
        }
    }
    CHECK(cases > 40);

    // Diario ilegible (otro formato, corrupto): no se aplica y se borra
    prep_old(OLD_PIN);
    uint8_t junk[60] = {'F', 'J', 9};
    flex_cfg_set_blob("lockjrn", junk, sizeof(junk));
    CHECK(flex_lock_migrate() == 0);
    CHECK(!has("lockjrn"));
    CHECK(flex_lock_verify_alone("1111"));
}

int main(void)
{
    test_kdf();
    test_cross();
    test_steps();
    test_migrate();
    test_corrupt();
    test_clear_fails();
    test_power_cut();
    printf("%s: clave del sistema, %d comprobaciones, %d fallos\n", g_fails ? "FALLO" : "OK", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
