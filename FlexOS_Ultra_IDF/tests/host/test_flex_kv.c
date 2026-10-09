#include <pthread.h>
#include <stdio.h>
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
    while (flex_kv_next_dirty(&kv, &s) > 0) {
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

// El escritor graba en el orden de los cambios, no en el de la lista: la clave
// del sistema pone "sin clave" antes de borrar el hash, y un corte a mitad de
// la pasada no puede dejar "PIN" sin hash en la flash.
static void t_write_order(void)
{
    flex_kv_t kv;
    flex_kv_init(&kv, NULL, NULL, NULL);
    // creadas en orden a, b, c (la lista queda c, b, a)
    flex_kv_load_num(&kv, "flexos", "a", FLEX_KV_I32, 0);
    flex_kv_load_num(&kv, "flexos", "b", FLEX_KV_I32, 0);
    flex_kv_load_num(&kv, "flexos", "c", FLEX_KV_I32, 0);
    CHECK(flex_kv_set_num(&kv, "flexos", "b", FLEX_KV_I32, 1));
    CHECK(flex_kv_erase(&kv, "flexos", "c"));
    CHECK(flex_kv_set_num(&kv, "flexos", "a", FLEX_KV_I32, 1));
    CHECK(flex_kv_set_num(&kv, "flexos", "n", FLEX_KV_I32, 1));   // nueva: cabeza de la lista
    CHECK(flex_kv_set_num(&kv, "flexos", "b", FLEX_KV_I32, 2));   // vuelve a cambiar: pasa al final
    const char *want[] = {"c", "a", "n", "b"};
    flex_kv_snapshot_t s;
    for (int i = 0; i < 4; i++) {
        CHECK(flex_kv_next_dirty(&kv, &s) == 1);
        CHECK(strcmp(s.key, want[i]) == 0);
        flex_kv_mark_written(&kv, &s);
        flex_kv_snapshot_free(&s);
    }
    CHECK(flex_kv_next_dirty(&kv, &s) == 0);
    // Una copia que cambia mientras se graba vuelve a la cola detras de lo posterior
    CHECK(flex_kv_set_num(&kv, "flexos", "a", FLEX_KV_I32, 5));
    CHECK(flex_kv_next_dirty(&kv, &s) == 1);
    CHECK(flex_kv_set_num(&kv, "flexos", "b", FLEX_KV_I32, 5));
    CHECK(flex_kv_set_num(&kv, "flexos", "a", FLEX_KV_I32, 6));
    flex_kv_mark_written(&kv, &s);   // version vieja: "a" sigue pendiente
    flex_kv_snapshot_free(&s);
    CHECK(flex_kv_next_dirty(&kv, &s) == 1 && strcmp(s.key, "b") == 0);
    flex_kv_mark_written(&kv, &s);
    flex_kv_snapshot_free(&s);
    CHECK(flex_kv_next_dirty(&kv, &s) == 1 && strcmp(s.key, "a") == 0 && s.num == 6);
    flex_kv_mark_written(&kv, &s);
    flex_kv_snapshot_free(&s);
    // Contador dando la vuelta: la comparacion sigue el orden
    kv.seq = 0xFFFFFFFEu;
    CHECK(flex_kv_set_num(&kv, "flexos", "a", FLEX_KV_I32, 7));   // seq 0xFFFFFFFF
    CHECK(flex_kv_set_num(&kv, "flexos", "b", FLEX_KV_I32, 7));   // seq 0
    CHECK(flex_kv_next_dirty(&kv, &s) == 1 && strcmp(s.key, "a") == 0);
    flex_kv_snapshot_free(&s);
    flex_kv_clear(&kv);
}

// Pasada del escritor: lo que falla se salta y el resto sigue; el grupo
// ordenado (clave del sistema) no deja adelantarse a nada suyo.
static char s_wr[16][16];
static int s_wr_n;
static const char *s_fail[4];
static bool s_fail_f;   // fallan todas las que empiezan por 'f'
static int pass_write(const flex_kv_snapshot_t *sn, void *ctx)
{
    (void)ctx;
    if (s_fail_f && sn->key[0] == 'f') {
        return 0x105;
    }
    for (int i = 0; i < 4 && s_fail[i]; i++) {
        if (strcmp(sn->key, s_fail[i]) == 0) {
            return 0x105;
        }
    }
    if (s_wr_n < 16) {
        strcpy(s_wr[s_wr_n++], sn->key);
    }
    return 0;
}
static bool pass_ordered(const char *ns, const char *key)
{
    (void)ns;
    return strncmp(key, "lock", 4) == 0;
}
static void t_flush_pass(void)
{
    flex_kv_t kv;
    flex_kv_init(&kv, NULL, NULL, NULL);
    flex_kv_set_num(&kv, "flexos", "lockx", FLEX_KV_I32, 1);
    flex_kv_set_num(&kv, "flexos", "a", FLEX_KV_I32, 1);
    flex_kv_set_num(&kv, "flexos", "b", FLEX_KV_I32, 1);
    flex_kv_set_num(&kv, "flexos", "locky", FLEX_KV_I32, 1);
    flex_kv_set_num(&kv, "flexos", "c", FLEX_KV_I32, 1);
    s_fail[0] = "lockx";
    s_fail[1] = "a";
    s_fail[2] = NULL;
    s_wr_n = 0;
    int first = 0;
    CHECK(flex_kv_flush_pass(&kv, pass_write, pass_ordered, NULL, &first) == FLEX_KV_PASS_ERR);
    CHECK(first == 0x105);
    CHECK(s_wr_n == 2 && strcmp(s_wr[0], "b") == 0 && strcmp(s_wr[1], "c") == 0);   // locky espera
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 3);
    s_fail[0] = NULL;
    s_wr_n = 0;
    CHECK(flex_kv_flush_pass(&kv, pass_write, pass_ordered, NULL, &first) == FLEX_KV_PASS_OK);
    CHECK(s_wr_n == 3 && strcmp(s_wr[0], "lockx") == 0 && strcmp(s_wr[1], "a") == 0 && strcmp(s_wr[2], "locky") == 0);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);
    // muchas que fallan: la pasada termina (no se queda dando vueltas)
    static const char *const FK[12] = {"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb"};
    for (int i = 0; i < 12; i++) {
        flex_kv_set_num(&kv, "flexos", FK[i], FLEX_KV_I32, 1);
    }
    flex_kv_set_num(&kv, "flexos", "z", FLEX_KV_I32, 1);
    s_fail_f = true;
    s_wr_n = 0;
    CHECK(flex_kv_flush_pass(&kv, pass_write, pass_ordered, NULL, &first) == FLEX_KV_PASS_ERR);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 13);   // 8 saltadas y la pasada para: "z" a la siguiente
    s_fail_f = false;
    CHECK(flex_kv_flush_pass(&kv, pass_write, pass_ordered, NULL, &first) == FLEX_KV_PASS_OK);
    CHECK_EQ_I(flex_kv_dirty_count(&kv), 0);
    flex_kv_clear(&kv);
}

void test_flex_kv(void)
{
    t_types_like_preferences();
    t_dirty_and_versions();
    t_buffers_and_limits();
    t_write_order();
    t_flush_pass();
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
        while (flex_kv_next_dirty(&s_kv, &s) > 0) {
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
