// Flex OS Ultra · hora por red: el servicio (ver flex_ntp.h). Portable: todo lo que toca el sistema entra por flex_ntp_env_t.
#include <stdatomic.h>
#include <string.h>
#include "flex_ntp.h"

static flex_ntp_t G = {.state = FLEX_NTPS_NEVER, .time_nvs_ok = true};   // arranca asi: ninguna inicializacion perezosa que dos tareas puedan pisarse
static atomic_bool s_online;        // lo escribe el servicio de Wi-Fi y lo leen la interfaz y el trabajador
static atomic_bool s_pending;       // hay una consulta lanzada que el trabajador aun no ha empezado
static uint32_t s_last_save;        // clkPersistTick: millis del ultimo guardado (0 = ninguno)
static flex_ntp_env_t s_env;
static bool s_env_set;

__attribute__((weak)) const flex_ntp_env_t *flex_ntp_port_env(void)   // la placa (flex_ntp_port.c)
{
    return NULL;
}

void flex_ntp_set_env(const flex_ntp_env_t *env)
{
    if (env) {
        s_env = *env;
        s_env_set = true;
    } else {
        s_env_set = false;
    }
}

const flex_ntp_env_t *flex_ntp_env(void)
{
    if (s_env_set) {
        return &s_env;
    }
    return flex_ntp_port_env();
}

void flex_ntp_service_reset(void)
{
    flex_ntp_init(&G);
    atomic_store(&s_online, false);
    atomic_store(&s_pending, false);
    s_last_save = 0;
}

static void lk(const flex_ntp_env_t *e)
{
    if (e && e->lock) {
        e->lock();
    }
}
static void ulk(const flex_ntp_env_t *e)
{
    if (e && e->unlock) {
        e->unlock();
    }
}

static void inputs(const flex_ntp_env_t *e, flex_ntp_in_t *in)
{
    in->now_ms = e->millis ? e->millis() : 0;
    in->airplane = e->airplane ? e->airplane() : false;
    in->online = atomic_load(&s_online);
    in->system_ready = e->system_ready ? e->system_ready() : true;
}

// ---- estado de la red --------------------------------------------------------------------------------------------------------------------
void flex_net_set_online(bool online)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        atomic_store(&s_online, online);
        return;
    }
    lk(e);
    bool was = atomic_exchange(&s_online, online);
    if (online && !was) {
        flex_ntp_on_wifi_up(&G, e->millis ? e->millis() : 0);   // el enlace acaba de confirmarse: adelanta la proxima sincronizacion
    }
    ulk(e);
}

bool flex_net_online(void)
{
    return atomic_load(&s_online);
}

// ---- persistencia de la hora -------------------------------------------------------------------------------------------------------------
// Solo se guarda una hora que merece guardarse: la de la red, la de un ajuste manual o la aproximada que se recupero de la NVS. La semilla de
// fabrica NO (Arduino la guardaba en la primera vuelta y el siguiente arranque la presentaba como "Hora aproximada").
static bool worth_saving(const flex_ntp_env_t *e)
{
    lk(e);
    bool known = G.last_sync_utc != 0 || G.from_nvs;
    ulk(e);
    return known || (e->synced && e->synced());
}

static void save_time(const flex_ntp_env_t *e)
{
    if (!worth_saving(e)) {
        return;
    }
    uint32_t now = e->utc ? e->utc() : 0;
    if (now < FLEX_NTP_MIN_EPOCH) {
        return;   // nunca persistir una hora que ya sabemos mala
    }
    lk(e);
    uint32_t last = G.last_sync_utc;
    ulk(e);
    bool ok = e->save ? e->save((uint64_t)now, (uint64_t)last) : false;
    lk(e);
    G.time_nvs_ok = ok;
    ulk(e);
}

void flex_ntp_save_time(void)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        return;
    }
    save_time(e);
}

void flex_ntp_boot_load(void)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        return;
    }
    uint64_t epoch = 0, last = 0;
    bool ok = e->load ? e->load(&epoch, &last) : false;
    lk(e);
    G.time_nvs_ok = ok;
    ulk(e);
    if (!ok) {
        return;   // hora de fabrica
    }
    if (epoch >= FLEX_NTP_MIN_EPOCH && epoch <= 0xFFFFFFFFull) {
        if (e->set_utc) {
            e->set_utc((uint32_t)epoch, false);   // aproximada: el equipo estuvo apagado un rato sin contar
        }
        lk(e);
        G.last_sync_utc = (uint32_t)last;
        G.from_nvs = true;
        ulk(e);
    }
}

// ---- planificacion y consulta ------------------------------------------------------------------------------------------------------------
void flex_ntp_request_sync(bool user_asked)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        return;
    }
    flex_ntp_in_t in;
    lk(e);
    inputs(e, &in);
    bool launch = flex_ntp_request(&G, user_asked, &in);
    if (launch) {
        atomic_store(&s_pending, true);
    }
    ulk(e);
    if (launch && e->wake) {
        e->wake();
    }
}

void flex_ntp_tick_service(void)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        return;
    }
    bool safe = e->safe_mode && e->safe_mode();
    if (!safe) {
        flex_ntp_in_t in;
        lk(e);
        inputs(e, &in);
        bool launch = flex_ntp_tick(&G, &in);
        if (launch) {
            atomic_store(&s_pending, true);
        }
        ulk(e);
    }
    // guardado horario (clkPersistTick): un corte de corriente pierde como mucho una hora de reloj
    uint32_t now = e->millis ? e->millis() : 0;
    if (!s_last_save || now - s_last_save >= 3600000UL) {
        if (worth_saving(e)) {
            s_last_save = now ? now : 1;
            save_time(e);
        }
    }
}

bool flex_ntp_work_pending(void)
{
    return atomic_load(&s_pending);
}

void flex_ntp_work(void)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    if (!e) {
        return;
    }
    atomic_store(&s_pending, false);
    uint32_t utc = 0;
    for (int i = 0; i < FLEX_NTP_NSERV && utc == 0; i++) {
        if (!atomic_load(&s_online)) {
            break;   // el Wi-Fi cayo a mitad: no insistir
        }
        utc = e->query ? e->query(flex_ntp_servers[i]) : 0;
    }
    if (utc && e->set_utc) {
        e->set_utc(utc, true);
    }
    lk(e);
    flex_ntp_finish(&G, utc, e->millis ? e->millis() : 0);
    ulk(e);
    if (utc) {
        save_time(e);
    }
}

// ---- lo que lee la interfaz --------------------------------------------------------------------------------------------------------------
bool flex_ntp_get(flex_ntp_t *out)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    lk(e);
    *out = G;
    ulk(e);
    return true;
}

void flex_ntp_texts(char *state, size_t ns, char *last, size_t nl, bool *busy)
{
    const flex_ntp_env_t *e = flex_ntp_env();
    flex_ntp_t c;
    flex_ntp_get(&c);
    bool air = e && e->airplane ? e->airplane() : false;
    if (state) {
        flex_ntp_state_text(state, ns, &c, air, atomic_load(&s_online));
    }
    if (last) {
        flex_ntp_last_text(last, nl, c.last_sync_utc, e && e->utc ? e->utc() : 0);
    }
    if (busy) {
        *busy = c.busy;
    }
}

bool flex_ntp_time_nvs_ok(void)
{
    flex_ntp_t c;
    flex_ntp_get(&c);
    return c.time_nvs_ok;
}
