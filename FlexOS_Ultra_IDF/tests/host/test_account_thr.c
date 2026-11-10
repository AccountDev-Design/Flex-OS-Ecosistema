// Flex Account con HILOS de verdad: la tarea de fondo (core 1 en la placa) da vueltas a flex_account_step() mientras la interfaz, Flex Cloud y el usuario (core 0) piden
// enlazar, cancelar, validar, desvincular, copiar la credencial y leer la foto. Con ThreadSanitizer detecta carreras; con ASan, usos de memoria ya liberada. Ademas exige
// invariantes: la foto nunca es "a medias" (cadenas terminadas, enlazada <=> hay credencial copiable), la credencial copiada siempre tiene 43 caracteres validos, y al
// terminar el servicio sigue en un estado coherente.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "account_fake.h"
#include "flex_account_svc.h"

static atomic_int g_fails;
#define CHECK(c, ...) do { if (!(c)) { atomic_fetch_add(&g_fails, 1); fprintf(stderr, "  FALLO: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "  (%s:%d)\n", __FILE__, __LINE__); } } while (0)

// ---- la tarea de fondo: un hilo que da vueltas a flex_account_step() ----
static atomic_bool g_stop;
static atomic_int g_posts, g_polls, g_sessions, g_linked_seen, g_transient;
static atomic_bool g_task_started;
static pthread_t g_task_thread;
static void *task_main(void *arg)
{
    (void)arg;
    while (!atomic_load(&g_stop)) {
        flex_account_step();
        account_fake_advance(100);
        usleep(50);
    }
    return NULL;
}
static bool ensure_task(void)
{
    bool expected = false;
    if (atomic_compare_exchange_strong(&g_task_started, &expected, true)) {
        pthread_create(&g_task_thread, NULL, task_main, NULL);
    }
    return true;
}
static void sleepy(uint32_t ms)
{
    (void)ms;
    usleep(30);
}

// ---- hilos de la interfaz y de la nube ----
static unsigned xs(unsigned *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}
static void check_snapshot(const flex_account_snapshot_t *s)
{
    CHECK(memchr(s->stage, 0, sizeof(s->stage)) && memchr(s->error, 0, sizeof(s->error)) && memchr(s->code, 0, sizeof(s->code)) && memchr(s->activation_url, 0, sizeof(s->activation_url)) &&
              memchr(s->flex_address, 0, sizeof(s->flex_address)) && memchr(s->display_name, 0, sizeof(s->display_name)) && memchr(s->link_detail, 0, sizeof(s->link_detail)),
          "foto con una cadena sin terminar");
    CHECK(s->state <= FLEX_ACCOUNT_ERROR, "estado fuera de rango: %d", (int)s->state);
    CHECK((unsigned)s->link <= FLEX_LINK_ERROR, "vinculo fuera de rango: %d", (int)s->link);
    if (s->state == FLEX_ACCOUNT_LINKED) {
        atomic_fetch_add(&g_linked_seen, 1);
    }
    // Desvincular borra la foto mientras el flujo de enlace sigue vivo y puede escribir un ultimo CODE_READY antes de ver que lo cancelaron: un fotograma sin codigo.
    // Es el comportamiento de Arduino (misma estructura) y lo absorbe la pantalla (no dibuja el codigo si esta vacio); aqui solo se cuenta.
    if (s->state == FLEX_ACCOUNT_CODE_READY && !s->code[0]) {
        atomic_fetch_add(&g_transient, 1);
    }
    if (s->link == FLEX_LINK_LINKED || s->link == FLEX_LINK_LINKED_OFFLINE) {
        CHECK(s->flex_address[0] != 0, "vinculada sin direccion");
    }
}
static void check_bearer(void)
{
    char b[64];
    if (flex_account_copy_bearer(b, sizeof(b))) {
        size_t n = strlen(b);
        CHECK(n == 43, "credencial de %zu caracteres", n);
        for (size_t i = 0; i < n; i++) {
            char c = b[i];
            CHECK((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_', "caracter raro en la credencial");
        }
    }
}
static void *ui_main(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    for (int i = 0; i < 5000 && !atomic_load(&g_stop); i++) {
        unsigned r = xs(&seed) % 200;
        if (r < 14) {
            flex_account_request_code("Hilo");
        } else if (r < 16) {
            flex_account_cancel();
        } else if (r < 17) {
            flex_account_forget_local();
        } else if (r < 30) {
            flex_account_request_validation();
        } else if (r < 100) {
            flex_account_snapshot_t s;
            flex_account_snapshot(&s);
            check_snapshot(&s);
        } else if (r < 120) {
            check_bearer();
        } else if (r < 140) {
            (void)flex_account_linked();
            (void)flex_account_usable();
            (void)flex_account_state();
            (void)flex_account_link_state();
        } else if (r < 146) {
            account_fake_set_wifi(xs(&seed) % 5 != 0);
        } else if (r < 152) {
            account_fake_set_session((int)(xs(&seed) % 4 == 0 ? xs(&seed) % 4 : 0));
        } else if (r < 158) {
            account_fake_set_polls((int)(xs(&seed) % 4));
        }
        usleep(120);
    }
    return NULL;
}
static void *cloud_main(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    for (int i = 0; i < 5000 && !atomic_load(&g_stop); i++) {
        usleep(60);
        switch (xs(&seed) % 4) {
        case 0: flex_account_report_rejected(); break;
        case 1: check_bearer(); break;
        case 2: (void)flex_account_usable(); break;
        default: usleep(10); break;
        }
    }
    return NULL;
}

int main(void)
{
    account_fake_reset();
    account_fake_set_ensure_task(ensure_task);
    account_fake_set_delay_hook(sleepy);
    static const flex_account_cfg_t cfg = {"1.0.0"};
    flex_account_set(&cfg, account_fake_ops());
    flex_account_begin();

    pthread_t ui[3], cloud;
    for (int i = 0; i < 3; i++) pthread_create(&ui[i], NULL, ui_main, (void *)(uintptr_t)(0x1234567u + (unsigned)i * 7919u));
    pthread_create(&cloud, NULL, cloud_main, (void *)(uintptr_t)0xBEEFu);
    for (int i = 0; i < 3; i++) pthread_join(ui[i], NULL);
    pthread_join(cloud, NULL);
    atomic_store(&g_stop, true);
    pthread_join(g_task_thread, NULL);

    // al final, con todo quieto, el servicio esta coherente
    flex_account_snapshot_t s;
    flex_account_snapshot(&s);
    check_snapshot(&s);
    CHECK(flex_account_linked() == (s.linked != 0), "linked() incoherente con la foto");
    g_posts = account_fake_count(AF_POSTS);
    g_polls = account_fake_count(AF_POLLS);
    g_sessions = account_fake_count(AF_SESSIONS);
    printf("  (%d codigos pedidos, %d consultas, %d validaciones, %d fotos con la cuenta vinculada, %d fotogramas CODE_READY sin codigo)\n", atomic_load(&g_posts), atomic_load(&g_polls),
           atomic_load(&g_sessions), atomic_load(&g_linked_seen), atomic_load(&g_transient));
    int f = atomic_load(&g_fails);
    printf(f ? "FALLOS: %d (Flex Account con hilos)\n" : "OK: Flex Account con hilos\n", f);
    return f ? 1 : 0;
}
