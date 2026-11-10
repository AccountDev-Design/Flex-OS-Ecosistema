// Flex OS Ultra · OTA: el motor (ver flex_ota.h). Repite otaCheck / otaInstall de FlexOS_OTA.cpp sobre flex_ota_ops_t.
//
// Reglas de oro de Arduino que se conservan: nada de red en la tarea de la interfaz; la descarga es POR STREAMING (el firmware completo nunca esta en RAM:
// bloques de FLEX_OTA_CHUNK); los buffers grandes son estaticos; solo se reinicia si el cierre confirma que la imagen esta entera y es valida; ante CUALQUIER
// fallo se aborta y la particion activa queda intacta.
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "flex_ota.h"

enum { CMD_NONE = 0, CMD_CHECK, CMD_INSTALL };

typedef struct {
    flex_ota_state_t state;
    flex_ota_err_t error;
    uint8_t percent;
    uint32_t done, total, bps;
    bool has_upd, just_updated;
    char remote[FLEX_OTA_VER_MAX];
    char url[FLEX_OTA_URL_MAX];
    char md5[FLEX_OTA_MD5_MAX];
    char changelog[FLEX_OTA_CHANGELOG_MAX];
    char sig[FLEX_OTA_SIG_MAX];
} shared_t;

static shared_t G;
static atomic_bool s_cancel;
static atomic_uchar s_cmd;
static bool s_auto_done;
static flex_ota_cfg_t s_cfg;
static flex_ota_ops_t s_ops;
static bool s_set;
static uint8_t s_chunk[FLEX_OTA_CHUNK];
static char s_manifest[FLEX_OTA_MANIFEST_MAX];

// La interfaz (N5b) se entera de que se pidio una instalacion; sin ella, no hace nada
__attribute__((weak)) void flex_ota_ui_install_started(void) {}
__attribute__((weak)) const flex_ota_cfg_t *flex_ota_port_cfg(void) { return NULL; }
__attribute__((weak)) const flex_ota_ops_t *flex_ota_port_ops(void) { return NULL; }

void flex_ota_set(const flex_ota_cfg_t *cfg, const flex_ota_ops_t *ops)
{
    if (cfg && ops) {
        s_cfg = *cfg;
        s_ops = *ops;
        s_set = true;
    } else {
        s_set = false;
    }
}

static const flex_ota_ops_t *ops(void)
{
    if (s_set) {
        return &s_ops;
    }
    return flex_ota_port_ops();
}
const flex_ota_cfg_t *flex_ota_cfg(void)
{
    return s_set ? &s_cfg : flex_ota_port_cfg();
}

void flex_ota_service_reset(void)
{
    memset(&G, 0, sizeof(G));
    G.state = FLEX_OTA_IDLE;
    atomic_store(&s_cancel, false);
    atomic_store(&s_cmd, CMD_NONE);
    s_auto_done = false;
}

static void lk(const flex_ota_ops_t *o)
{
    if (o && o->lock) {
        o->lock();
    }
}
static void ulk(const flex_ota_ops_t *o)
{
    if (o && o->unlock) {
        o->unlock();
    }
}

static void set_state(const flex_ota_ops_t *o, flex_ota_state_t s)
{
    lk(o);
    G.state = s;
    ulk(o);
}

static bool fail(const flex_ota_ops_t *o, flex_ota_err_t e)
{
    lk(o);
    G.error = e;
    G.state = FLEX_OTA_ERROR;
    ulk(o);
    return false;
}

// Abre una peticion HTTPS y deja la respuesta lista para leer. Traduce cada modo de fallo a SU codigo.
static bool open_url(const flex_ota_ops_t *o, const flex_ota_cfg_t *c, const char *url)
{
    if (!o->net_ready()) {
        return fail(o, FLEX_OTA_ERR_NO_WIFI);
    }
    if (strncmp(url, "https://", 8) != 0) {
        return fail(o, FLEX_OTA_ERR_BAD_URL);
    }
    // Filtro de memoria ANTES de tocar TLS: mbedTLS reserva sus buffers de registro al conectar
    if (o->free_heap() < c->min_heap) {
        return fail(o, FLEX_OTA_ERR_LOWMEM);
    }
    // TLS. Sin CA definido NO se conecta, salvo que se haya pedido a proposito para una placa de pruebas: aceptar cualquier certificado convierte la
    // actualizacion en la via de entrada mas facil del dispositivo (quien controle la red sirve el manifiesto Y el firmware)
    const char *ca = c->root_ca;
    bool pinned = ca && ca[0];
    bool insecure = false;
    if (!pinned) {
        if (c->allow_insecure) {
            insecure = true;
        } else {
            return fail(o, FLEX_OTA_ERR_NO_CA);
        }
    }
    if (!o->http_begin(url, ca, insecure, FLEX_OTA_HTTP_TIMEOUT)) {
        return fail(o, FLEX_OTA_ERR_HTTPS);
    }
    int code = o->http_get();
    if (code < 0) {
        // Codigos negativos = fallo de transporte. Con un CA fijado, lo mas probable es que el certificado no valide
        if (code == FLEX_OTA_HTTPE_TIMEOUT || code == FLEX_OTA_HTTPE_LOST) {
            return fail(o, FLEX_OTA_ERR_TIMEOUT);
        }
        return fail(o, pinned ? FLEX_OTA_ERR_TLS : FLEX_OTA_ERR_HTTPS);
    }
    if (code != 200) {
        return fail(o, FLEX_OTA_ERR_HTTP_STATUS);   // 404 incluido
    }
    return true;
}

// ---- Paso 1: el manifiesto ----
static bool do_check(const flex_ota_ops_t *o, const flex_ota_cfg_t *c)
{
    set_state(o, FLEX_OTA_CHECKING);
    lk(o);
    G.error = FLEX_OTA_ERR_NONE;
    ulk(o);
    if (!open_url(o, c, c->manifest_url)) {
        o->http_end();
        return false;
    }
    if (!o->http_stream()) {
        o->http_end();
        return fail(o, FLEX_OTA_ERR_HTTPS);   // sin flujo no hay nada que leer
    }
    size_t n = 0;
    uint32_t last = o->millis();
    while (n < sizeof(s_manifest) - 1) {
        if (o->http_available() > 0) {
            int r = o->http_read((uint8_t *)s_manifest + n, sizeof(s_manifest) - 1 - n);
            if (r > 0) {
                n += (size_t)r;
                last = o->millis();
                continue;
            }
        }
        if (!o->http_connected() && !(o->http_available() > 0)) {
            break;   // el servidor cerro: fin normal
        }
        if (o->millis() - last > FLEX_OTA_STALL_TIMEOUT) {
            o->http_end();
            return fail(o, FLEX_OTA_ERR_TIMEOUT);
        }
        o->delay_ms(5);
    }
    s_manifest[n] = 0;
    o->http_end();
    if (n == 0) {
        return fail(o, FLEX_OTA_ERR_JSON);
    }
    flex_ota_manifest_t m;
    bool newer = false;
    flex_ota_err_t e = flex_ota_manifest_read(s_manifest, c, &m, &newer);
    if (e != FLEX_OTA_ERR_NONE) {
        return fail(o, e);
    }
    if (!newer) {
        lk(o);
        G.has_upd = false;
        G.remote[0] = 0;
        G.state = FLEX_OTA_UP_TO_DATE;
        ulk(o);
        return true;
    }
    // Publicacion: primero los datos, el estado DESPUES (la interfaz solo lee las cadenas cuando ve ese estado)
    lk(o);
    strncpy(G.remote, m.ver, sizeof(G.remote) - 1);
    G.remote[sizeof(G.remote) - 1] = 0;
    strncpy(G.url, m.url, sizeof(G.url) - 1);
    G.url[sizeof(G.url) - 1] = 0;
    strncpy(G.md5, m.md5, sizeof(G.md5) - 1);
    G.md5[sizeof(G.md5) - 1] = 0;
    strncpy(G.changelog, m.changelog, sizeof(G.changelog) - 1);
    G.changelog[sizeof(G.changelog) - 1] = 0;
    strncpy(G.sig, m.sig, sizeof(G.sig) - 1);
    G.sig[sizeof(G.sig) - 1] = 0;
    G.total = m.size;
    G.done = 0;
    G.percent = 0;
    G.has_upd = true;
    G.state = FLEX_OTA_AVAILABLE;
    ulk(o);
    return true;
}

// ---- Paso 2: descarga por streaming + instalacion ----
static bool do_install(const flex_ota_ops_t *o, const flex_ota_cfg_t *c)
{
    lk(o);
    bool has = G.has_upd && G.url[0];
    ulk(o);
    if (!has) {
        return fail(o, FLEX_OTA_ERR_BAD_URL);
    }
    atomic_store(&s_cancel, false);
    lk(o);
    G.done = 0;
    G.percent = 0;
    G.bps = 0;
    G.error = FLEX_OTA_ERR_NONE;
    G.state = FLEX_OTA_CONNECTING;
    char url[FLEX_OTA_URL_MAX], md5[FLEX_OTA_MD5_MAX], remote[FLEX_OTA_VER_MAX];
    strcpy(url, G.url);
    strcpy(md5, G.md5);
    strcpy(remote, G.remote);
    ulk(o);
    // Sin segunda particion de app no hay OTA posible: se comprueba ANTES de descargar un solo byte
    if (!o->next_partition()) {
        return fail(o, FLEX_OTA_ERR_NO_PARTITION);
    }
    if (!open_url(o, c, url)) {
        o->http_end();
        return false;
    }
    int len = o->http_size();
    if (len <= 0) {
        o->http_end();
        return fail(o, FLEX_OTA_ERR_BAD_SIZE);   // sin Content-Length
    }
    uint32_t total = (uint32_t)len;
    if (total < FLEX_OTA_MIN_BIN || total > FLEX_OTA_MAX_BIN) {
        o->http_end();
        return fail(o, FLEX_OTA_ERR_BAD_SIZE);
    }
    lk(o);
    G.total = total;
    ulk(o);
    if (!o->upd_begin(total)) {   // no cabe en la particion
        o->http_end();
        return fail(o, FLEX_OTA_ERR_BEGIN);
    }
    // Integridad REAL: con un MD5 de verdad, el cierre lo verifica y rechaza la imagen si no cuadra. Un relleno (32 ceros, no hexadecimal...) se ignora en vez de
    // condenar la actualizacion.
    if (flex_ota_md5_usable(md5)) {
        o->upd_set_md5(md5);
    }
    set_state(o, FLEX_OTA_DOWNLOADING);
    if (!o->http_stream()) {
        o->upd_abort();
        o->http_end();
        return fail(o, FLEX_OTA_ERR_HTTPS);   // deja la particion intacta
    }
    uint32_t written = 0, last = o->millis();
    uint32_t spd_bytes = 0, spd_t0 = o->millis();
    while (written < total) {
        if (atomic_load(&s_cancel)) {   // se atiende en cada vuelta
            o->upd_abort();
            o->http_end();
            return fail(o, FLEX_OTA_ERR_CANCELLED);
        }
        size_t avail = (size_t)(o->http_available() > 0 ? o->http_available() : 0);
        if (avail) {
            size_t want = avail > sizeof(s_chunk) ? sizeof(s_chunk) : avail;
            if (want > total - written) {
                want = total - written;
            }
            int r = o->http_read(s_chunk, want);
            if (r <= 0) {
                // available() dijo que habia datos y la lectura no devolvio ninguno: tambien mira el reloj de atasco
                if (o->millis() - last > FLEX_OTA_STALL_TIMEOUT) {
                    o->upd_abort();
                    o->http_end();
                    return fail(o, FLEX_OTA_ERR_TIMEOUT);
                }
                o->delay_ms(2);
                continue;
            }
            // Escritura en flash SERIALIZADA con la subida al panel: escribir apaga la cache de los dos nucleos
            if (o->panel_lock) {
                o->panel_lock();
            }
            size_t w = o->upd_write(s_chunk, (size_t)r);
            if (o->panel_unlock) {
                o->panel_unlock();
            }
            if (w != (size_t)r) {   // fallo de escritura en flash
                o->upd_abort();
                o->http_end();
                return fail(o, FLEX_OTA_ERR_WRITE);
            }
            written += (uint32_t)r;
            spd_bytes += (uint32_t)r;
            last = o->millis();
            // El progreso se publica junto: la interfaz nunca lee un porcentaje nuevo con un contador de bytes viejo
            uint8_t pct = (uint8_t)((uint64_t)written * 100u / total);
            lk(o);
            G.done = written;
            G.percent = pct;
            ulk(o);
            uint32_t dt = o->millis() - spd_t0;
            if (dt >= 500) {   // velocidad suavizada cada 0,5 s
                uint32_t inst = (uint32_t)((uint64_t)spd_bytes * 1000u / dt);
                lk(o);
                G.bps = G.bps ? (uint32_t)(((uint64_t)G.bps * 3u + inst) / 4u) : inst;
                ulk(o);
                spd_bytes = 0;
                spd_t0 = o->millis();
            }
            o->delay_ms(1);   // cede la CPU entre bloques
        } else {
            if (!o->http_connected() && !(o->http_available() > 0)) {
                break;   // conexion cerrada antes de tiempo
            }
            if (o->millis() - last > FLEX_OTA_STALL_TIMEOUT) {   // ni un byte en 15 s
                o->upd_abort();
                o->http_end();
                return fail(o, FLEX_OTA_ERR_TIMEOUT);
            }
            o->delay_ms(4);
        }
    }
    o->http_end();
    if (written != total) {
        o->upd_abort();
        return fail(o, FLEX_OTA_ERR_TRUNCATED);
    }
    // Verificacion + cierre
    set_state(o, FLEX_OTA_VERIFYING);
    o->delay_ms(120);   // deja ver el estado en pantalla
    set_state(o, FLEX_OTA_INSTALLING);
    if (o->panel_lock) {
        o->panel_lock();
    }
    bool end_ok = o->upd_end();   // valida MD5 + imagen y fija la particion de arranque
    if (o->panel_unlock) {
        o->panel_unlock();
    }
    if (!end_ok) {
        o->upd_abort();
        return fail(o, FLEX_OTA_ERR_CORRUPT);
    }
    if (!o->upd_finished()) {
        o->upd_abort();
        return fail(o, FLEX_OTA_ERR_CORRUPT);
    }
    // Finalizacion
    set_state(o, FLEX_OTA_FINALIZING);
    o->save_pending(remote);   // para saludar tras el reinicio
    o->delay_ms(400);
    set_state(o, FLEX_OTA_REBOOTING);
    for (int i = 0; i < 16; i++) {
        o->delay_ms(100);   // 1,6 s de cortesia visual
    }
    o->restart();
    return true;   // en la placa no se llega
}

// ---- API ----
bool flex_ota_busy(void)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    flex_ota_state_t s = G.state;
    ulk(o);
    return s == FLEX_OTA_CHECKING || s == FLEX_OTA_CONNECTING || s == FLEX_OTA_DOWNLOADING || s == FLEX_OTA_VERIFYING || s == FLEX_OTA_INSTALLING ||
           s == FLEX_OTA_FINALIZING || s == FLEX_OTA_REBOOTING;
}

void flex_ota_begin(void)
{
    const flex_ota_ops_t *o = ops();
    const flex_ota_cfg_t *c = flex_ota_cfg();
    if (!o || !c) {
        return;
    }
    // Saludo posterior a una actualizacion correcta: si la version apuntada coincide con la que ahora corre, la OTA funciono
    char pend[FLEX_OTA_VER_MAX] = "";
    if (o->load_pending && o->load_pending(pend, sizeof(pend)) && pend[0]) {
        if (!strcmp(pend, c->local_version)) {
            lk(o);
            G.just_updated = true;
            ulk(o);
        }
        if (o->clear_pending) {
            o->clear_pending();
        }
    }
}

void flex_ota_request_check(void)
{
    const flex_ota_ops_t *o = ops();
    if (!o) {
        return;
    }
    if (flex_ota_busy()) {
        return;
    }
    if (!o->net_ready()) {
        lk(o);
        G.error = FLEX_OTA_ERR_NO_WIFI;
        G.state = FLEX_OTA_ERROR;
        ulk(o);
        return;
    }
    lk(o);
    G.error = FLEX_OTA_ERR_NONE;
    G.state = FLEX_OTA_CHECKING;
    ulk(o);
    atomic_store(&s_cmd, CMD_CHECK);
}

void flex_ota_request_install(void)
{
    const flex_ota_ops_t *o = ops();
    if (!o) {
        return;
    }
    lk(o);
    bool has = G.has_upd;
    ulk(o);
    if (flex_ota_busy() || !has) {
        return;
    }
    if (!o->net_ready()) {
        lk(o);
        G.error = FLEX_OTA_ERR_NO_WIFI;
        G.state = FLEX_OTA_ERROR;
        ulk(o);
        return;
    }
    atomic_store(&s_cancel, false);
    set_state(o, FLEX_OTA_CONNECTING);
    atomic_store(&s_cmd, CMD_INSTALL);
    flex_ota_ui_install_started();
}

void flex_ota_cancel(void)
{
    atomic_store(&s_cancel, true);   // el motor lo ve en su siguiente vuelta
}

bool flex_ota_run_check(void)
{
    const flex_ota_ops_t *o = ops();
    const flex_ota_cfg_t *c = flex_ota_cfg();
    return o && c && do_check(o, c);
}

bool flex_ota_run_install(void)
{
    const flex_ota_ops_t *o = ops();
    const flex_ota_cfg_t *c = flex_ota_cfg();
    return o && c && do_install(o, c);
}

void flex_ota_prime(const char *remote, const char *url, const char *md5, uint32_t total)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    strncpy(G.remote, remote, sizeof(G.remote) - 1);
    strncpy(G.url, url, sizeof(G.url) - 1);
    strncpy(G.md5, md5, sizeof(G.md5) - 1);
    G.total = total;
    G.has_upd = true;
    G.state = FLEX_OTA_AVAILABLE;
    ulk(o);
}

void flex_ota_test_force(flex_ota_state_t st, flex_ota_err_t err, const char *remote)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    G.state = st;
    G.error = err;
    if (remote) {
        strncpy(G.remote, remote, sizeof(G.remote) - 1);
        G.remote[sizeof(G.remote) - 1] = 0;
        strcpy(G.url, "https://x/fw.bin");
        G.has_upd = true;
    }
    ulk(o);
}

void flex_ota_test_progress(uint8_t percent, uint32_t done, uint32_t total, uint32_t bps)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    G.percent = percent;
    G.done = done;
    G.total = total;
    G.bps = bps;
    ulk(o);
}

bool flex_ota_work_pending(void)
{
    return atomic_load(&s_cmd) != CMD_NONE;
}

void flex_ota_work(void)
{
    const flex_ota_ops_t *o = ops();
    const flex_ota_cfg_t *c = flex_ota_cfg();
    if (!o || !c) {
        return;
    }
    uint8_t cmd = atomic_exchange(&s_cmd, CMD_NONE);   // una sola palabra, sin cola: la ultima orden gana
    if (cmd == CMD_CHECK) {
        do_check(o, c);
    } else if (cmd == CMD_INSTALL) {
        do_install(o, c);
    }
}

// Comprobacion automatica: una sola vez, ~20 s despues del arranque y solo si hay red. No se reintenta sola: si falla, el usuario decide.
void flex_ota_auto_check_tick(void)
{
    const flex_ota_ops_t *o = ops();
    if (!o || s_auto_done) {
        return;
    }
    if (o->millis() < 20000) {
        return;
    }
    s_auto_done = true;
    lk(o);
    flex_ota_state_t s = G.state;
    ulk(o);
    if (o->net_ready() && s == FLEX_OTA_IDLE) {
        atomic_store(&s_cmd, CMD_CHECK);
    }
}

void flex_ota_get(flex_ota_info_t *out)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    out->state = G.state;
    out->error = G.error;
    out->percent = G.percent;
    out->done = G.done;
    out->total = G.total;
    out->bps = G.bps;
    out->has_update = G.has_upd;
    out->just_updated = G.just_updated;
    memcpy(out->remote, G.remote, sizeof(out->remote));
    memcpy(out->changelog, G.changelog, sizeof(out->changelog));
    ulk(o);
}

bool flex_ota_manifest_copy(flex_ota_manifest_t *out)
{
    const flex_ota_ops_t *o = ops();
    lk(o);
    bool has = G.has_upd;
    memset(out, 0, sizeof(*out));
    memcpy(out->ver, G.remote, sizeof(out->ver));
    memcpy(out->url, G.url, sizeof(out->url));
    memcpy(out->md5, G.md5, sizeof(out->md5));
    memcpy(out->changelog, G.changelog, sizeof(out->changelog));
    memcpy(out->sig, G.sig, sizeof(out->sig));
    out->size = G.total;
    ulk(o);
    return has;
}

const char *flex_ota_local_version(void)
{
    const flex_ota_cfg_t *c = flex_ota_cfg();
    return c ? c->local_version : "";
}

const char *flex_ota_status_text(void)
{
    static char buf[48];
    flex_ota_info_t i;
    flex_ota_get(&i);
    switch (i.state) {
    case FLEX_OTA_CHECKING: return "Buscando actualizaciones...";
    case FLEX_OTA_UP_TO_DATE: return "El sistema esta al dia";
    case FLEX_OTA_AVAILABLE:
        snprintf(buf, sizeof(buf), "%s disponible", i.remote);
        return buf;
    case FLEX_OTA_ERROR: return flex_ota_err_text(i.error);
    case FLEX_OTA_IDLE: return "Buscar actualizacion";
    default: return flex_ota_phase_text(i.state);
    }
}
