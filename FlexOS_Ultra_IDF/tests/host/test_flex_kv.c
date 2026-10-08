#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include "flex_kv.h"
#include "flex_test.h"

static void t_types_like_preferences(void)
{
    flex_kv_t kv;
    flex_kv_init(&kv, NULL, NULL, NULL);
    // Datos "de Arduino": putInt("bright"), putBool("dark") (U8), putString("name").
    CHECK(flex_kv_load_num(&kv, "flexos", "bright", FLEX_KV_I32, 42));
    CHECK(flex_kv_load_num(&kv, "flexos", "dark", FLEX_KV_U8, 1));
    CHECK(flex_kv_load_buf(&kv, "flexos", "name", FLEX_KV_STR, "Mi Flex", 8));
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);   // cargar no deja nada pendiente

    int64_t v = -1;
    CHECK(flex_kv_get_num(&kv, "flexos", "bright", FLEX_KV_I32, &v));
    CHECK_EQ_I(v, 42);
    // Otro tipo => como Preferences: no se encuentra (el llamante usa su defecto).
    v = -1;
    CHECK(!flex_kv_get_num(&kv, "flexos", "bright", FLEX_KV_U8, &v));
    CHECK_EQ_I(v, -1);
    CHECK(!flex_kv_get_num(&kv, "flexos", "name", FLEX_KV_STR, &v));
    // Mismo nombre en otro espacio: no se confunde.
    CHECK(!flex_kv_get_num(&kv, "flexcare", "bright", FLEX_KV_I32, &v));

    char buf[16];
    size_t len = 0;
    CHECK(flex_kv_get_buf(&kv, "flexos", "name", FLEX_KV_STR, buf, sizeof(buf), &len));
    CHECK_EQ_I(len, 8);
    CHECK(strcmp(buf, "Mi Flex") == 0);
    // Buffer pequeno: copia lo que cabe y dice el tamano real.
    char small[3] = {0};
    CHECK(flex_kv_get_buf(&kv, "flexos", "name", FLEX_KV_STR, small, sizeof(small), &len));
    CHECK_EQ_I(len, 8);
    CHECK(memcmp(small, "Mi ", 3) == 0);
    CHECK(!flex_kv_get_buf(&kv, "flexos", "name", FLEX_KV_BLOB, buf, sizeof(buf), &len));
    flex_kv_clear(&kv);
}

static void t_dirty_and_versions(void)
{
    flex_kv_t kv;
    flex_kv_init(&kv, NULL, NULL, NULL);
    flex_kv_load_num(&kv, "flexos", "bright", FLEX_KV_I32, 80);
    // Mismo valor: no cambia, no se graba (no se gasta flash).
    CHECK(!flex_kv_set_num(&kv, "flexos", "bright", FLEX_KV_I32, 80));
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);
    CHECK(flex_kv_set_num(&kv, "flexos", "bright", FLEX_KV_I32, 55));
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 1);

    flex_kv_snapshot_t s;
    CHECK(flex_kv_next_dirty(&kv, &s));
    CHECK(strcmp(s.ns, "flexos") == 0 && strcmp(s.key, "bright") == 0);
    CHECK_EQ_I(s.num, 55);
    CHECK_EQ_I(s.type, FLEX_KV_I32);
    // Cambia mientras el escritor graba la copia: no se pierde.
    CHECK(flex_kv_set_num(&kv, "flexos", "bright", FLEX_KV_I32, 60));
    flex_kv_mark_written(&kv, &s);
    flex_kv_snapshot_free(&s);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 1);
    CHECK(flex_kv_next_dirty(&kv, &s));
    CHECK_EQ_I(s.num, 60);
    flex_kv_mark_written(&kv, &s);
    flex_kv_snapshot_free(&s);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);
    CHECK(!flex_kv_next_dirty(&kv, &s));

    // Cambio de tipo: cuenta como cambio aunque el numero sea igual.
    CHECK(flex_kv_set_num(&kv, "flexos", "bright", FLEX_KV_U8, 60));

    // Borrar: queda pendiente como borrado y deja de existir para los lectores.
    CHECK(flex_kv_erase(&kv, "flexos", "bright"));
    CHECK(!flex_kv_exists(&kv, "flexos", "bright"));
    CHECK(!flex_kv_erase(&kv, "flexos", "bright"));
    CHECK(flex_kv_next_dirty(&kv, &s));
    CHECK(s.erase);
    flex_kv_mark_written(&kv, &s);
    flex_kv_snapshot_free(&s);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);
    // Volver a escribir algo borrado.
    CHECK(flex_kv_set_num(&kv, "flexos", "bright", FLEX_KV_I32, 70));
    CHECK(flex_kv_exists(&kv, "flexos", "bright"));

    // Tras borrar el almacen, todo lo vivo se vuelve a grabar (lo borrado no).
    flex_kv_load_num(&kv, "flexos", "dark", FLEX_KV_U8, 1);
    flex_kv_erase(&kv, "flexos", "dark");
    while (flex_kv_next_dirty(&kv, &s)) {
        flex_kv_mark_written(&kv, &s);
        flex_kv_snapshot_free(&s);
    }
    flex_kv_mark_all_dirty(&kv);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 1);
    flex_kv_clear(&kv);
}

static void t_buffers_and_limits(void)
{
    flex_kv_t kv;
    flex_kv_init(&kv, NULL, NULL, NULL);
    uint8_t blob[FLEX_KV_BUF_MAX + 1];
    memset(blob, 0xA5, sizeof(blob));
    CHECK(flex_kv_set_buf(&kv, "flexos", "lockhsh", FLEX_KV_BLOB, blob, 32));
    CHECK(!flex_kv_set_buf(&kv, "flexos", "lockhsh", FLEX_KV_BLOB, blob, 32));   // igual
    blob[5] = 0;
    CHECK(flex_kv_set_buf(&kv, "flexos", "lockhsh", FLEX_KV_BLOB, blob, 32));
    CHECK(flex_kv_set_buf(&kv, "flexos", "lockhsh", FLEX_KV_BLOB, blob, 31));    // otro tamano
    CHECK(!flex_kv_set_buf(&kv, "flexos", "big", FLEX_KV_BLOB, blob, FLEX_KV_BUF_MAX + 1));
    CHECK(flex_kv_set_buf(&kv, "flexos", "max", FLEX_KV_BLOB, blob, FLEX_KV_BUF_MAX));
    CHECK(!flex_kv_set_buf(&kv, "flexos", "nul", FLEX_KV_BLOB, NULL, 4));
    CHECK(flex_kv_set_buf(&kv, "flexos", "empty", FLEX_KV_BLOB, NULL, 0));
    size_t len = 99;
    CHECK(flex_kv_get_buf(&kv, "flexos", "empty", FLEX_KV_BLOB, NULL, 0, &len));
    CHECK_EQ_I(len, 0);

    // Nombres: NVS admite 15 caracteres.
    CHECK(flex_kv_set_num(&kv, "flexos", "abcdefghijklmno", FLEX_KV_U8, 1));
    CHECK(!flex_kv_set_num(&kv, "flexos", "abcdefghijklmnop", FLEX_KV_U8, 1));
    CHECK(!flex_kv_set_num(&kv, "abcdefghijklmnop", "k", FLEX_KV_U8, 1));
    CHECK(!flex_kv_set_num(&kv, "", "k", FLEX_KV_U8, 1));
    CHECK(!flex_kv_set_num(&kv, "flexos", "", FLEX_KV_U8, 1));
    CHECK(!flex_kv_set_num(&kv, "flexos", "k", FLEX_KV_STR, 1));
    CHECK(!flex_kv_set_buf(&kv, "flexos", "k", FLEX_KV_I32, "x", 1));
    CHECK(!flex_kv_load_num(&kv, "flexos", "k", FLEX_KV_NONE, 1));

    // Limites de los enteros: se guardan sin perder bits.
    CHECK(flex_kv_set_num(&kv, "flexos", "u64", FLEX_KV_U64, (int64_t)UINT64_MAX));
    int64_t v;
    CHECK(flex_kv_get_num(&kv, "flexos", "u64", FLEX_KV_U64, &v));
    CHECK((uint64_t)v == UINT64_MAX);
    flex_kv_clear(&kv);
    CHECK_EQ_I(kv.count, 0);
}

void test_flex_kv(void)
{
    t_types_like_preferences();
    t_dirty_and_versions();
    t_buffers_and_limits();
}

// ---- concurrencia: la UI cambia ajustes mientras el escritor graba ----------
static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static void lk(void *c) { (void)c; pthread_mutex_lock(&s_mtx); }
static void ulk(void *c) { (void)c; pthread_mutex_unlock(&s_mtx); }

static flex_kv_t s_kv;
static int64_t s_store[8];      // "flash": ultimo valor grabado de cada clave
static atomic_int s_done;

static void *setter(void *arg)
{
    (void)arg;
    char key[8];
    for (int i = 1; i <= 20000; i++) {
        snprintf(key, sizeof(key), "k%d", i % 8);
        flex_kv_set_num(&s_kv, "flexos", key, FLEX_KV_I32, i);
    }
    atomic_store(&s_done, 1);
    return NULL;
}

static void *writer(void *arg)
{
    (void)arg;
    flex_kv_snapshot_t s;
    for (;;) {
        int finished = atomic_load(&s_done);
        bool any = false;
        while (flex_kv_next_dirty(&s_kv, &s)) {
            any = true;
            s_store[s.key[1] - '0'] = s.num;
            flex_kv_mark_written(&s_kv, &s);
            flex_kv_snapshot_free(&s);
        }
        if (finished && !any) {
            break;
        }
    }
    return NULL;
}

void test_flex_kv_threads(void)
{
    flex_kv_init(&s_kv, lk, ulk, NULL);
    pthread_t a, b;
    pthread_create(&a, NULL, setter, NULL);
    pthread_create(&b, NULL, writer, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    CHECK_EQ_I(flex_kv_dirty_count(&s_kv), 0);
    // Lo grabado es el ultimo valor de cada clave: ninguna actualizacion perdida.
    for (int k = 0; k < 8; k++) {
        int64_t last = 0;
        for (int i = 1; i <= 20000; i++) {
            if (i % 8 == k) {
                last = i;
            }
        }
        CHECK_EQ_I(s_store[k], last);
    }
    flex_kv_clear(&s_kv);
}
