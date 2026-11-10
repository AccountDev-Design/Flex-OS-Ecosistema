// Flex OS Ultra · Flex Account: el servicio (ver flex_account_svc.h). Repite el flujo de FlexOS_Account.cpp sobre flex_account_ops_t: enlace por codigo, validacion de la sesion
// con flanco de Wi-Fi y espera progresiva, guardado a prueba de cortes, recuperacion tras reiniciar y desvincular. Las pruebas de host lo comparan, paso a paso, con el .cpp
// de Arduino sobre el mismo mundo simulado (red, NVS y reloj de tests/host/netstub).
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "flex_account_svc.h"
#include "flex_tls.h"

enum { AUTHST_OK = 0, AUTHST_REQUIRED = 1, AUTHST_EXPIRED = 2 };   // valores de la clave "authst" (solo se escribe en las TRANSICIONES)
#define HTTP_CREATED 201
#define HTTP_OK 200
#define HTTP_CONFLICT 409
#define HTTP_NOT_FOUND 404

static flex_account_cfg_t s_cfg = {"1.0.0"};
static flex_account_ops_t s_ops;
static bool s_set;

void flex_account_set(const flex_account_cfg_t *cfg, const flex_account_ops_t *ops)
{
    if (cfg && ops) {
        s_cfg = *cfg;
        s_ops = *ops;
        s_set = true;
    } else {
        s_set = false;
    }
}
const flex_account_ops_t *flex_account_ops(void) { return s_set ? &s_ops : NULL; }   // sin operaciones el servicio no hace nada (la placa las pone con flex_account_start)
static const flex_account_cfg_t *cfg(void) { return &s_cfg; }

// ---- estado ----
static bool s_begun;   // el cerrojo existe (gMutex): antes de flex_account_begin las consultas dicen "sin cuenta" y las peticiones se rechazan
static flex_account_snapshot_t G;
static char s_label[49] = "FlexOS Ultra";
static char s_bearer[48] = "";   // 32 bytes en base64url = 43 caracteres
static atomic_bool s_have_credential;   // hay credencial guardada y cargada (independiente de la red); se escribe con el cerrojo, se lee tambien sin el
static char s_request_error[128] = "";
static atomic_uint s_installation_id;   // distingue reinstalaciones del mismo P4
static int s_last_http_status;
static atomic_uchar s_auth_st = AUTHST_OK;   // lo escribe la tarea de fondo y tambien desvincular (otra tarea)
static atomic_bool s_start_requested, s_cancel_requested, s_validate_wanted;
// Cambia cada vez que el USUARIO desvincula: la validacion corre en la tarea de fondo y tarda segundos; si mientras tanto se desvinculo, su resultado era de una credencial
// que ya no existe y no puede escribir nada en la NVS
static atomic_uint s_epoch;
static bool s_was_online;
static atomic_uint s_wait_from_ms, s_wait_ms;   // espera hasta el proximo intento como "desde cuando" + "cuanto": el tiempo TRANSCURRIDO sin signo sigue siendo correcto al dar la vuelta el reloj
static uint32_t s_last_attempt_ms;
static bool s_attempted;
static atomic_uchar s_failures;
static atomic_uint s_last_verified_ms;
static atomic_bool s_verified_this_boot;
static uint32_t s_last_reject_ms;   // con el cerrojo: lo llaman Flex Cloud y la interfaz
static bool s_reject_seen;

#define OPS(o) const flex_account_ops_t *o = flex_account_ops()
static void lk(const flex_account_ops_t *o)
{
    if (o && o->lock) {
        o->lock();
    }
}
static void ulk(const flex_account_ops_t *o)
{
    if (o && o->unlock) {
        o->unlock();
    }
}
static void say(const flex_account_ops_t *o, const char *msg)
{
    if (o && o->log) {
        o->log(msg);
    }
}

static void set_status(const flex_account_ops_t *o, flex_account_state_t state, uint8_t progress, const char *stage, const char *error)
{
    lk(o);
    G.state = state;
    G.progress = progress;
    G.linked = s_have_credential;
    snprintf(G.stage, sizeof(G.stage), "%s", stage ? stage : "");
    if (error) {
        snprintf(G.error, sizeof(G.error), "%s", error);
    } else if (state != FLEX_ACCOUNT_ERROR) {
        G.error[0] = 0;
    }
    ulk(o);
}

static void set_link(const flex_account_ops_t *o, flex_account_link_t link, const char *detail)
{
    lk(o);
    G.link = link;
    snprintf(G.link_detail, sizeof(G.link_detail), "%s", detail ? detail : "");
    ulk(o);
}

static void hardware_id(const flex_account_ops_t *o, char out[40])
{
    uint64_t mac = o->efuse_mac();
    snprintf(out, 40, "P4-%04lX%08lX-%08lX", (unsigned long)((mac >> 32) & 0xFFFFu), (unsigned long)(mac & 0xFFFFFFFFu), (unsigned long)s_installation_id);
}

static bool rotate_installation_id(const flex_account_ops_t *o)
{
    uint32_t next = 0;
    do {
        o->fill_random(&next, sizeof(next));
    } while (next == 0 || next == s_installation_id);
    if (!o->nvs_begin()) {
        return false;
    }
    bool ok = o->nvs_put_u32("installId", next) == sizeof(next);
    o->nvs_end();
    if (ok) {
        s_installation_id = next;
    }
    return ok;
}

// ---- HTTP ----
// `honor_cancel`: el flujo de enlace se puede cancelar; la validacion de la sesion no depende de esa bandera
static bool read_http_body(const flex_account_ops_t *o, uint8_t **out, size_t *out_len, bool honor_cancel)
{
    *out = NULL;
    *out_len = 0;
    int declared = o->http_size();
    if (declared > 0 && (size_t)declared > FLEX_ACCOUNT_ENVELOPE_MAX) {
        return false;
    }
    if (!o->http_has_stream()) {
        return false;
    }
    uint8_t *data = (uint8_t *)malloc(FLEX_ACCOUNT_ENVELOPE_MAX + 1);
    if (!data) {
        return false;
    }
    size_t used = 0;
    uint32_t last = o->millis();
    while (o->http_connected() || o->http_available() > 0) {
        if (honor_cancel && atomic_load(&s_cancel_requested)) {
            free(data);
            return false;
        }
        int available = o->http_available();
        if (available > 0) {
            size_t wanted = (size_t)available;
            if (wanted > FLEX_ACCOUNT_ENVELOPE_MAX - used) {
                wanted = FLEX_ACCOUNT_ENVELOPE_MAX - used;
            }
            if (!wanted) {
                free(data);
                return false;
            }
            int got = o->http_read(data + used, wanted);
            if (got <= 0) {
                free(data);
                return false;
            }
            used += (size_t)got;
            last = o->millis();
            if (declared > 0 && used >= (size_t)declared) {
                break;
            }
        } else {
            if (o->millis() - last > FLEX_ACCOUNT_HTTP_TIMEOUT_MS) {
                free(data);
                return false;
            }
            o->delay_ms(10);
        }
    }
    if (declared > 0 && used != (size_t)declared) {
        free(data);
        return false;
    }
    data[used] = 0;
    *out = data;
    *out_len = used;
    return used > 0;
}

static bool copy_json_string(cJSON *object, const char *key, char *out, size_t cap, bool required)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNull(value) && !required) {
        out[0] = 0;
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= cap) {
        if (!required) {
            out[0] = 0;
        }
        return !required;
    }
    memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}

// ---- persistencia ----
// Escribe la cuenta entera. La marca "linked" va la ULTIMA: si el corte de energia llega a mitad, el arranque siguiente ve un registro incompleto (y lo dice como ERROR, no
// como "sin cuenta") en vez de uno que parece valido
static bool persist_linked(const flex_account_ops_t *o, const char *bearer, const char *address, const char *display_name)
{
    if (!o->nvs_begin()) {
        return false;
    }
    size_t a = o->nvs_put_str("token", bearer);
    size_t b = o->nvs_put_str("address", address);
    o->nvs_put_str("display", display_name ? display_name : "");
    // Un enlace nuevo deja la sesion limpia (solo se escribe si hacia falta)
    if (o->nvs_get_u8("authst", AUTHST_OK) != AUTHST_OK) {
        o->nvs_put_u8("authst", AUTHST_OK);
    }
    bool c = o->nvs_put_bool("linked", true);
    o->nvs_end();
    return a == strlen(bearer) && b == strlen(address) && c;
}

static void persist_auth_state(const flex_account_ops_t *o, uint8_t st)
{
    if (st == s_auth_st) {
        return;   // sin cambios: ni una escritura de flash
    }
    if (!o->nvs_begin()) {
        return;
    }
    o->nvs_put_u8("authst", st);
    o->nvs_end();
    s_auth_st = st;
}

static void persist_profile(const flex_account_ops_t *o, const char *address, const char *display_name)
{
    if (!o->nvs_begin()) {
        return;
    }
    o->nvs_put_str("address", address);
    o->nvs_put_str("display", display_name ? display_name : "");
    o->nvs_end();
}

// ---- enlace: peticion del codigo y sondeo ----
static bool request_device_code(const flex_account_ops_t *o, const char *label, const char *token_hash, char code[9], char activation_url[192])
{
    s_request_error[0] = 0;
    s_last_http_status = 0;
    if (!o->wifi_connected()) {
        snprintf(s_request_error, sizeof(s_request_error), "Wi-Fi se desconecto durante el enlace");
        return false;
    }
    char escaped[104];
    if (!flex_account_json_escape(label, escaped, sizeof(escaped))) {
        snprintf(s_request_error, sizeof(s_request_error), "Nombre del dispositivo no valido");
        return false;
    }
    char id[40];
    hardware_id(o, id);
    char body[420];
    int written = snprintf(body, sizeof(body), "{\"hardwareId\":\"%s\",\"label\":\"%s\",\"model\":\"ESP32-P4\",\"flexVersion\":\"%s\",\"tokenHash\":\"%s\"}", id, escaped, cfg()->fw_version, token_hash);
    if (written <= 0 || written >= (int)sizeof(body)) {
        snprintf(s_request_error, sizeof(s_request_error), "Solicitud de cuenta demasiado grande");
        return false;
    }
    // Sin SRAM interna para el handshake no se intenta: se dice QUE hacer (cerrar una app) en vez de un "Fallo HTTPS -1" sin explicacion
    size_t in_free = 0, in_block = 0;
    if (!o->tls_room(&in_free, &in_block)) {
        snprintf(s_request_error, sizeof(s_request_error), "Poca memoria interna (%u KB libres). Cierra una app y reintenta", (unsigned)(in_free / 1024u));
        return false;
    }
    flex_account_req_t req = {.method = "POST", .url = FLEX_ACCOUNT_CODE_URL, .auth = NULL, .body = (const uint8_t *)body, .body_len = (size_t)written, .insecure = true,
                              .timeout_ms = FLEX_ACCOUNT_HTTP_TIMEOUT_MS, .set_connect_timeout = false};
    if (!o->http_begin(&req)) {
        snprintf(s_request_error, sizeof(s_request_error), "No se pudo abrir el servicio de Flex Account");
        return false;
    }
    int status_code = o->http_send();
    s_last_http_status = status_code;
    int tls_err = 0;
    if (status_code < 0) {
        tls_err = o->http_tls_error();
    }
    if (status_code != HTTP_CREATED && status_code != HTTP_OK) {
        if (status_code == HTTP_CONFLICT) {
            snprintf(s_request_error, sizeof(s_request_error), "Este registro del dispositivo ya estaba vinculado");
        } else if (status_code == 429) {
            snprintf(s_request_error, sizeof(s_request_error), "Demasiados intentos; espera un minuto");
        } else if (status_code > 0) {
            snprintf(s_request_error, sizeof(s_request_error), "Flex Account respondio HTTP %d", status_code);
        } else {
            char why[48];
            flex_tls_reason(tls_err, why, sizeof(why));
            snprintf(s_request_error, sizeof(s_request_error), "Fallo HTTPS %d (%s)", status_code, why);
        }
        o->http_end();
        return false;
    }
    uint8_t *envelope = NULL;
    size_t envelope_len = 0;
    bool ok = read_http_body(o, &envelope, &envelope_len, true);
    o->http_end();
    if (!ok) {
        snprintf(s_request_error, sizeof(s_request_error), "Respuesta de Flex Account incompleta");
    }
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (ok && !flex_account_signed_payload(envelope, envelope_len, &payload, &payload_len)) {
        ok = false;
        snprintf(s_request_error, sizeof(s_request_error), "Firma ES256 de Flex Account no valida");
    }
    if (ok && !flex_account_parse_code(payload, payload_len, token_hash, code, activation_url)) {
        ok = false;
        snprintf(s_request_error, sizeof(s_request_error), "Datos de vinculacion incompatibles");
    }
    free(payload);
    free(envelope);
    return ok;
}

// `problem` recibe, si la consulta NO llego a contestarse, un motivo corto que la pantalla ensena mientras se reintenta
static flex_poll_t poll_device_code(const flex_account_ops_t *o, const char *code, const char *token_hash, char address[48], char display_name[64], char *problem, size_t problem_cap)
{
    if (problem && problem_cap) {
        problem[0] = 0;
    }
    if (!o->wifi_connected()) {
        return FLEX_POLL_PENDING;
    }
    char url[320];
    int written = snprintf(url, sizeof(url), "%s?code=%s&tokenHash=%s", FLEX_ACCOUNT_CODE_URL, code, token_hash);
    if (written <= 0 || written >= (int)sizeof(url)) {
        return FLEX_POLL_INVALID;
    }
    size_t in_free = 0;
    if (!o->tls_room(&in_free, NULL)) {
        if (problem) {
            snprintf(problem, problem_cap, "Poca memoria interna (%u KB). Reintentando", (unsigned)(in_free / 1024u));
        }
        return FLEX_POLL_PENDING;
    }
    flex_account_req_t req = {.method = "GET", .url = url, .auth = NULL, .body = NULL, .body_len = 0, .insecure = true, .timeout_ms = FLEX_ACCOUNT_HTTP_TIMEOUT_MS,
                              .set_connect_timeout = false};
    if (!o->http_begin(&req)) {
        if (problem) {
            snprintf(problem, problem_cap, "No se pudo abrir la conexion. Reintentando");
        }
        return FLEX_POLL_PENDING;
    }
    int status_code = o->http_send();
    if (problem && status_code < 0) {
        char why[48];
        flex_tls_reason(o->http_tls_error(), why, sizeof(why));
        snprintf(problem, problem_cap, "Sin conexion (%s). Reintentando", why);
    } else if (problem && status_code != HTTP_OK && status_code != HTTP_NOT_FOUND) {
        snprintf(problem, problem_cap, "Flex Account respondio HTTP %d. Reintentando", status_code);
    }
    if (status_code != HTTP_OK) {
        o->http_end();
        return status_code == HTTP_NOT_FOUND ? FLEX_POLL_INVALID : FLEX_POLL_PENDING;
    }
    uint8_t *envelope = NULL;
    size_t envelope_len = 0;
    bool ok = read_http_body(o, &envelope, &envelope_len, true);
    o->http_end();
    if (!ok) {
        free(envelope);
        return FLEX_POLL_PENDING;
    }
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    ok = flex_account_signed_payload(envelope, envelope_len, &payload, &payload_len);
    flex_poll_t result = ok ? flex_account_parse_status(payload, payload_len, address, display_name) : FLEX_POLL_INVALID;
    free(payload);
    free(envelope);
    return result;
}

// ---- validacion de la sesion ----
// GET autenticado. Rellena el perfil si el servidor lo devuelve. Nunca imprime la credencial. `detail` recibe un motivo legible en caso de fallo
static flex_session_verdict_t validate_session(const flex_account_ops_t *o, const char *bearer, char address[48], char display_name[64], char *detail, size_t detail_cap)
{
    address[0] = 0;
    display_name[0] = 0;
    detail[0] = 0;
    if (!o->wifi_connected()) {
        snprintf(detail, detail_cap, "Sin Wi-Fi");
        return FLEX_SESSION_UNAVAILABLE;
    }
    // SRAM INTERNA, no la memoria total (que suma la PSRAM): sin sitio para el handshake no se intenta
    size_t in_free = 0, in_block = 0;
    if (!o->tls_room(&in_free, &in_block)) {
        snprintf(detail, detail_cap, "Poca memoria interna (%u KB libres): se reintentara", (unsigned)(in_free / 1024u));
        return FLEX_SESSION_UNAVAILABLE;
    }
    char auth[64];
    snprintf(auth, sizeof(auth), "Bearer %s", bearer);
    flex_account_req_t req = {.method = "GET", .url = FLEX_ACCOUNT_SESSION_URL, .auth = auth, .body = NULL, .body_len = 0, .insecure = false,
                              .timeout_ms = FLEX_ACCOUNT_HTTP_TIMEOUT_MS, .set_connect_timeout = true};
    if (!o->http_begin(&req)) {
        memset(auth, 0, sizeof(auth));
        snprintf(detail, detail_cap, "No se pudo abrir la conexion segura");
        return FLEX_SESSION_UNAVAILABLE;
    }
    memset(auth, 0, sizeof(auth));
    const uint32_t t0 = o->millis();
    int status = o->http_send();
    const uint32_t took_ms = o->millis() - t0;
    // Por que no hubo conexion: el codigo de esta misma conexion
    int tls_err = 0;
    if (status < 0) {
        tls_err = o->http_tls_error();
    }
    char code[40] = "";
    uint8_t *body = NULL;
    size_t body_len = 0;
    if (status > 0) {
        read_http_body(o, &body, &body_len, false);
    }
    o->http_end();
    cJSON *root = body ? cJSON_ParseWithLength((const char *)body, body_len) : NULL;
    if (root && status != HTTP_OK) {
        cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
        if (cJSON_IsObject(err)) {
            copy_json_string(err, "code", code, sizeof(code), false);
        }
    }
    flex_session_verdict_t v = flex_account_classify(status, code);
    if (v == FLEX_SESSION_OK) {
        // Cuerpo esperado: {"account":{"flexAddress":"...","displayName":"..."}}. Un 200 sin cuerpo valido no es una validacion: servicio no disponible (nunca "desvinculado")
        cJSON *acc = root ? cJSON_GetObjectItemCaseSensitive(root, "account") : NULL;
        if (!cJSON_IsObject(acc) || !copy_json_string(acc, "flexAddress", address, 48, true) || !flex_account_valid_address(address)) {
            address[0] = 0;
            v = FLEX_SESSION_UNAVAILABLE;
            snprintf(detail, detail_cap, "Respuesta de Flex Account incompleta");
        } else {
            copy_json_string(acc, "displayName", display_name, 64, false);
        }
    } else if (v == FLEX_SESSION_UNAVAILABLE) {
        if (status < 0) {
            char why[56];
            if (tls_err == -1) {
                // -1 = DNS, TCP o saludo TLS: se distingue por el DNS y por lo que tardo
                char host[80];
                bool dns = true;
                if (flex_url_host(FLEX_ACCOUNT_SESSION_URL, host, sizeof(host))) {
                    dns = o->tls_dns_ok(host);
                }
                flex_tls_phase(dns, took_ms, 12000u, FLEX_ACCOUNT_HTTP_TIMEOUT_MS, why, sizeof(why));
            } else {
                flex_tls_reason(tls_err, why, sizeof(why));
            }
            snprintf(detail, detail_cap, "Sin respuesta segura (%d: %s)", status, why);
        } else {
            snprintf(detail, detail_cap, "Flex Account respondio HTTP %d; se reintentara", status);
        }
    }
    if (root) {
        cJSON_Delete(root);
    }
    free(body);
    if (!detail[0]) {
        snprintf(detail, detail_cap, "%s", flex_account_verdict_detail(v));
    }
    return v;
}

static void session_tick(const flex_account_ops_t *o)
{
    lk(o);
    bool have = s_have_credential && s_bearer[0];
    bool busy_flow = G.state == FLEX_ACCOUNT_REQUESTING || G.state == FLEX_ACCOUNT_CODE_READY;
    flex_account_link_t link = G.link;
    ulk(o);
    if (!have || busy_flow) {
        return;
    }
    const uint32_t now = o->millis();
    if (!o->wifi_connected()) {
        s_was_online = false;
        // Sin red la cuenta SIGUE vinculada: solo se dice que no se puede comprobar. Los estados que vienen del servidor (credencial rechazada o caducada) se conservan
        if (link == FLEX_LINK_LINKED || link == FLEX_LINK_NETWORK_UNAVAILABLE) {
            set_link(o, FLEX_LINK_LINKED_OFFLINE, "Sin Wi-Fi: se comprobara al volver la conexion");
        }
        return;
    }
    if (!s_was_online) {
        // Flanco: acaba de volver el Wi-Fi (o es el primero de este arranque)
        s_was_online = true;
        atomic_store(&s_validate_wanted, true);
        // Lo que quede de la espera en curso (backoff tras un fallo de red) no se acorta por volver a conectar; si no hay ninguna, se deja asentar la red
        uint32_t left = 0;
        if (s_failures) {
            uint32_t el = now - atomic_load(&s_wait_from_ms);
            left = el < atomic_load(&s_wait_ms) ? atomic_load(&s_wait_ms) - el : 0;
        }
        if (left < FLEX_ACCOUNT_SETTLE_MS) {
            left = FLEX_ACCOUNT_SETTLE_MS;
        }
        if (s_attempted) {
            uint32_t since = now - s_last_attempt_ms;
            if (since < FLEX_ACCOUNT_MIN_ATTEMPT_GAP_MS && FLEX_ACCOUNT_MIN_ATTEMPT_GAP_MS - since > left) {
                left = FLEX_ACCOUNT_MIN_ATTEMPT_GAP_MS - since;
            }
        }
        atomic_store(&s_wait_from_ms, now);
        atomic_store(&s_wait_ms, left);
    }
    if (link == FLEX_LINK_LINKED && s_verified_this_boot && !atomic_load(&s_validate_wanted) && (uint32_t)(now - s_last_verified_ms) >= FLEX_ACCOUNT_REVALIDATE_MS) {
        atomic_store(&s_validate_wanted, true);
        atomic_store(&s_wait_from_ms, now);
        atomic_store(&s_wait_ms, 0);
    }
    if (!atomic_load(&s_validate_wanted) || (uint32_t)(now - atomic_load(&s_wait_from_ms)) < atomic_load(&s_wait_ms)) {
        return;
    }
    char bearer[48];
    const uint32_t epoch = atomic_load(&s_epoch);   // ANTES de copiar la credencial: si se desvincula despues, el resultado se descarta
    lk(o);
    snprintf(bearer, sizeof(bearer), "%s", s_bearer);
    ulk(o);
    s_attempted = true;
    s_last_attempt_ms = now;
    char address[48], display_name[64], detail[96];
    flex_session_verdict_t v = validate_session(o, bearer, address, display_name, detail, sizeof(detail));
    memset(bearer, 0, sizeof(bearer));
    if (epoch != atomic_load(&s_epoch)) {
        return;   // se desvinculo mientras esperaba al servidor: el resultado ya no vale
    }
    const uint32_t done = o->millis();
    if (v == FLEX_SESSION_OK) {
        atomic_store(&s_validate_wanted, false);
        s_failures = 0;
        s_last_verified_ms = done;
        s_verified_this_boot = true;
        persist_auth_state(o, AUTHST_OK);
        bool changed = false;
        lk(o);
        changed = strcmp(address, G.flex_address) || strcmp(display_name, G.display_name);
        if (changed) {
            snprintf(G.flex_address, sizeof(G.flex_address), "%s", address);
            snprintf(G.display_name, sizeof(G.display_name), "%s", display_name);
        }
        ulk(o);
        if (changed) {
            persist_profile(o, address, display_name);   // solo si el servidor cambio el perfil
        }
        set_link(o, FLEX_LINK_LINKED, "Conectada con Flex Account");
        say(o, "[ACCOUNT] sesion validada");
    } else if (v == FLEX_SESSION_AUTH_REQUIRED || v == FLEX_SESSION_TOKEN_EXPIRED) {
        atomic_store(&s_validate_wanted, false);
        s_failures = 0;
        persist_auth_state(o, v == FLEX_SESSION_TOKEN_EXPIRED ? AUTHST_EXPIRED : AUTHST_REQUIRED);
        set_link(o, v == FLEX_SESSION_TOKEN_EXPIRED ? FLEX_LINK_TOKEN_EXPIRED : FLEX_LINK_AUTH_REQUIRED, detail);
        say(o, "[ACCOUNT] el servidor rechazo la credencial");
    } else {
        if (s_failures < 250) {
            s_failures++;
        }
        atomic_store(&s_wait_from_ms, done);
        atomic_store(&s_wait_ms, flex_account_backoff_ms(s_failures));
        // Si el servidor ya habia rechazado la credencial, un fallo de red no lo "arregla": se conserva ese estado
        if (link != FLEX_LINK_AUTH_REQUIRED && link != FLEX_LINK_TOKEN_EXPIRED) {
            set_link(o, FLEX_LINK_NETWORK_UNAVAILABLE, detail);
        }
        say(o, "[ACCOUNT] validacion aplazada");
    }
}

// ---- flujo de enlace ----
// Un enlace nuevo que no termina bien NO toca la cuenta que ya estaba guardada: se vuelve a ella con el motivo del fallo a la vista
static void finish_failed_flow(const flex_account_ops_t *o, flex_account_state_t state, const char *stage, const char *error)
{
    if (s_have_credential) {
        set_status(o, FLEX_ACCOUNT_LINKED, 100, "Cuenta vinculada", NULL);
        if (error) {
            lk(o);
            snprintf(G.error, sizeof(G.error), "%s", error);
            ulk(o);
        }
        return;
    }
    set_status(o, state, 0, stage, error);
}

static void link_flow(const flex_account_ops_t *o, const char *label)
{
    set_status(o, FLEX_ACCOUNT_REQUESTING, 8, "Creando enlace seguro", NULL);
    char bearer[48] = "", token_hash[65] = "", code[9] = "", activation_url[192] = "";
    uint8_t rnd[32];
    o->fill_random(rnd, sizeof(rnd));
    bool made = flex_account_make_bearer(rnd, bearer, token_hash);
    memset(rnd, 0, sizeof(rnd));
    if (!made) {
        finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "No se pudo iniciar", "No se pudo crear la credencial segura");
        return;
    }
    if (atomic_load(&s_cancel_requested)) {
        finish_failed_flow(o, FLEX_ACCOUNT_CANCELLED, "Cancelado", NULL);
        return;
    }
    if (!o->wifi_connected()) {
        finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "Sin Wi-Fi", "Conecta el dispositivo a Wi-Fi y vuelve a intentar");
        return;
    }
    set_status(o, FLEX_ACCOUNT_REQUESTING, 28, "Contactando Flex Account", NULL);
    bool requested = request_device_code(o, label, token_hash, code, activation_url);
    if (!requested && s_last_http_status == HTTP_CONFLICT) {
        // La placa puede haber sido vinculada antes de que la credencial alcanzara a guardarse (reinicio, reflasheo o corte de energia). Una nueva identidad de instalacion permite
        // recuperar el MISMO hardware sin reutilizar el token perdido ni debilitar la autenticacion del servidor
        set_status(o, FLEX_ACCOUNT_REQUESTING, 34, "Recuperando vinculacion anterior", NULL);
        if (rotate_installation_id(o)) {
            requested = request_device_code(o, label, token_hash, code, activation_url);
        } else {
            snprintf(s_request_error, sizeof(s_request_error), "No se pudo guardar la nueva identidad del dispositivo");
        }
    }
    if (!requested) {
        if (atomic_load(&s_cancel_requested)) {
            finish_failed_flow(o, FLEX_ACCOUNT_CANCELLED, "Cancelado", NULL);
        } else {
            finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "Enlace no disponible", s_request_error[0] ? s_request_error : "No se recibio una respuesta autentica de Flex Account");
        }
        memset(bearer, 0, sizeof(bearer));
        return;
    }
    lk(o);
    snprintf(G.code, sizeof(G.code), "%s", code);
    snprintf(G.activation_url, sizeof(G.activation_url), "%s", activation_url);
    ulk(o);
    set_status(o, FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular", NULL);
    uint32_t started = o->millis(), last_poll = 0;
    while (!atomic_load(&s_cancel_requested) && o->millis() - started < FLEX_ACCOUNT_LINK_TIMEOUT_MS) {
        if (last_poll && o->millis() - last_poll < 3000) {
            o->delay_ms(100);
            continue;
        }
        last_poll = o->millis();
        if (!o->wifi_connected()) {
            set_status(o, FLEX_ACCOUNT_CODE_READY, 55, "Esperando que vuelva el Wi-Fi", "Sin Wi-Fi. Se reintenta al volver la conexion");
            continue;
        }
        char address[48] = "", display_name[64] = "", problem[96] = "";
        flex_poll_t result = poll_device_code(o, code, token_hash, address, display_name, problem, sizeof(problem));
        if (result == FLEX_POLL_PENDING) {
            // Si la consulta no se pudo contestar, el motivo va en `error` (la pantalla lo ensena junto al codigo)
            set_status(o, FLEX_ACCOUNT_CODE_READY, 55, "Esperando aprobacion en tu celular", problem[0] ? problem : NULL);
        } else if (result == FLEX_POLL_EXPIRED) {
            finish_failed_flow(o, FLEX_ACCOUNT_EXPIRED, "El codigo expiro", s_have_credential ? "El codigo de vinculacion expiro" : NULL);
            memset(bearer, 0, sizeof(bearer));
            return;
        } else if (result == FLEX_POLL_INVALID) {
            finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "Respuesta rechazada", "FlexOS rechazo un estado sin firma valida");
            memset(bearer, 0, sizeof(bearer));
            return;
        } else {
            set_status(o, FLEX_ACCOUNT_REQUESTING, 88, "Guardando tu Flex Account", NULL);
            if (!persist_linked(o, bearer, address, display_name)) {
                finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "No se pudo guardar", "El almacenamiento seguro no respondio");
                memset(bearer, 0, sizeof(bearer));
                return;
            }
            lk(o);
            snprintf(s_bearer, sizeof(s_bearer), "%s", bearer);
            snprintf(G.flex_address, sizeof(G.flex_address), "%s", address);
            snprintf(G.display_name, sizeof(G.display_name), "%s", display_name);
            s_have_credential = true;
            ulk(o);
            s_auth_st = AUTHST_OK;
            // El servidor ACABA de aprobar esta credencial: cuenta como validada
            s_last_verified_ms = o->millis();
            s_verified_this_boot = true;
            atomic_store(&s_validate_wanted, false);
            s_failures = 0;
            s_was_online = true;
            set_link(o, FLEX_LINK_LINKED, "Conectada con Flex Account");
            set_status(o, FLEX_ACCOUNT_LINKED, 100, "Cuenta vinculada", NULL);
            say(o, "[ACCOUNT] cuenta vinculada y guardada");
            memset(bearer, 0, sizeof(bearer));
            return;
        }
    }
    memset(bearer, 0, sizeof(bearer));
    if (atomic_load(&s_cancel_requested)) {
        finish_failed_flow(o, FLEX_ACCOUNT_CANCELLED, "Cancelado", NULL);
    } else {
        finish_failed_flow(o, FLEX_ACCOUNT_EXPIRED, "El codigo expiro", s_have_credential ? "El codigo de vinculacion expiro" : NULL);
    }
}

void flex_account_step(void)
{
    OPS(o);
    if (!o) {
        return;
    }
    if (atomic_load(&s_start_requested)) {
        char label[49];
        lk(o);
        snprintf(label, sizeof(label), "%s", s_label);
        atomic_store(&s_start_requested, false);
        ulk(o);
        link_flow(o, label);
        return;
    }
    session_tick(o);
}

// ---- memoria y recuperacion ----
static void clear_memory(const flex_account_ops_t *o)
{
    lk(o);
    memset(s_bearer, 0, sizeof(s_bearer));
    memset(&G, 0, sizeof(G));
    s_have_credential = false;
    G.state = FLEX_ACCOUNT_UNLINKED;
    G.link = FLEX_LINK_UNLINKED;
    snprintf(G.stage, sizeof(G.stage), "Sin cuenta vinculada");
    snprintf(G.link_detail, sizeof(G.link_detail), "Sin cuenta vinculada");
    ulk(o);
    s_auth_st = AUTHST_OK;
    atomic_store(&s_validate_wanted, false);
    s_failures = 0;
    s_verified_this_boot = false;
}

// Carga la cuenta guardada. Va SIEMPRE antes de mirar la red: el estado de la vinculacion es el de la NVS, no el de la ultima respuesta del servidor
static void load_persisted(const flex_account_ops_t *o)
{
    memset(&G, 0, sizeof(G));
    memset(s_bearer, 0, sizeof(s_bearer));
    s_have_credential = false;
    G.state = FLEX_ACCOUNT_UNLINKED;
    G.link = FLEX_LINK_UNLINKED;
    snprintf(G.stage, sizeof(G.stage), "Sin cuenta vinculada");
    snprintf(G.link_detail, sizeof(G.link_detail), "Sin cuenta vinculada");
    if (!o->nvs_begin()) {
        G.link = FLEX_LINK_ERROR;
        snprintf(G.link_detail, sizeof(G.link_detail), "El almacenamiento seguro no respondio");
        say(o, "[ACCOUNT] NVS no disponible: no se pudo leer la cuenta");
        return;
    }
    s_installation_id = o->nvs_get_u32("installId", 0);
    if (atomic_load(&s_installation_id) == 0) {
        uint32_t fresh;
        do {
            o->fill_random(&fresh, sizeof(fresh));
        } while (fresh == 0);
        s_installation_id = fresh;
        o->nvs_put_u32("installId", fresh);
    }
    bool linked = o->nvs_get_bool("linked", false);
    // EL FALLO QUE DESVINCULABA EN CADA ARRANQUE: la lectura de una cadena devuelve la longitud CON el terminador (44 para un token de 43)
    size_t token_len = flex_nvs_str_len(o->nvs_get_str("token", s_bearer, sizeof(s_bearer)), s_bearer, sizeof(s_bearer));
    size_t address_len = flex_nvs_str_len(o->nvs_get_str("address", G.flex_address, sizeof(G.flex_address)), G.flex_address, sizeof(G.flex_address));
    if (!flex_nvs_str_len(o->nvs_get_str("display", G.display_name, sizeof(G.display_name)), G.display_name, sizeof(G.display_name))) {
        G.display_name[0] = 0;
    }
    s_auth_st = o->nvs_get_u8("authst", AUTHST_OK);
    bool any_key = linked || o->nvs_is_key("token") || o->nvs_is_key("address");
    o->nvs_end();

    bool valid = linked && token_len == 43 && address_len >= 6 && flex_account_valid_address(G.flex_address);
    if (valid) {
        s_have_credential = true;
        G.state = FLEX_ACCOUNT_LINKED;
        G.linked = true;
        G.progress = 100;
        snprintf(G.stage, sizeof(G.stage), "Cuenta vinculada");
        if (s_auth_st == AUTHST_EXPIRED) {
            G.link = FLEX_LINK_TOKEN_EXPIRED;
            snprintf(G.link_detail, sizeof(G.link_detail), "%s", flex_account_verdict_detail(FLEX_SESSION_TOKEN_EXPIRED));
        } else if (s_auth_st == AUTHST_REQUIRED) {
            G.link = FLEX_LINK_AUTH_REQUIRED;
            snprintf(G.link_detail, sizeof(G.link_detail), "%s", flex_account_verdict_detail(FLEX_SESSION_AUTH_REQUIRED));
        } else {
            G.link = FLEX_LINK_LINKED_OFFLINE;
            snprintf(G.link_detail, sizeof(G.link_detail), "Vinculada; se comprobara al conectar el Wi-Fi");
        }
        say(o, "[ACCOUNT] cuenta guardada recuperada");
    } else {
        memset(s_bearer, 0, sizeof(s_bearer));
        G.flex_address[0] = 0;
        G.display_name[0] = 0;
        if (any_key) {
            // Habia algo, pero incompleto (corte de energia a mitad de guardar, o una version anterior danada): se dice, no se finge "sin cuenta"
            G.link = FLEX_LINK_ERROR;
            snprintf(G.link_detail, sizeof(G.link_detail), "La cuenta guardada esta incompleta: vuelve a vincular");
            say(o, "[ACCOUNT] registro de cuenta incompleto en NVS");
        }
    }
}

// ---- API ----
void flex_account_begin(void)
{
    OPS(o);
    if (!o) {
        return;
    }
    s_begun = true;
    lk(o);
    load_persisted(o);
    ulk(o);
    s_was_online = false;
    atomic_store(&s_validate_wanted, false);
    s_failures = 0;
    s_verified_this_boot = false;
    s_last_verified_ms = 0;
    atomic_store(&s_wait_from_ms, 0);
    atomic_store(&s_wait_ms, 0);
    s_attempted = false;
    o->ensure_task();
}

bool flex_account_request_code(const char *device_label)
{
    OPS(o);
    if (!o || !s_begun) {
        return false;
    }
    // Sin tarea no hay quien haga el enlace: se reintenta crearla ahora y, si tampoco hay memoria, se dice en pantalla (la cuenta guardada, si la hay, se conserva)
    if (!o->ensure_task()) {
        finish_failed_flow(o, FLEX_ACCOUNT_ERROR, "No se pudo iniciar", "Sin memoria interna para Flex Account. Cierra una app y reintenta");
        return false;
    }
    lk(o);
    bool busy = atomic_load(&s_start_requested) || G.state == FLEX_ACCOUNT_REQUESTING || G.state == FLEX_ACCOUNT_CODE_READY;
    if (!busy) {
        snprintf(s_label, sizeof(s_label), "%s", (device_label && device_label[0]) ? device_label : "FlexOS Ultra");
        G.code[0] = 0;
        G.activation_url[0] = 0;
        G.error[0] = 0;
        G.state = FLEX_ACCOUNT_REQUESTING;
        G.progress = 1;
        G.linked = s_have_credential;   // la cuenta guardada sigue ahi mientras tanto
        snprintf(G.stage, sizeof(G.stage), "Preparando enlace");
        atomic_store(&s_cancel_requested, false);
        atomic_store(&s_start_requested, true);
    }
    ulk(o);
    return !busy;
}

void flex_account_cancel(void) { atomic_store(&s_cancel_requested, true); }

bool flex_account_linked(void)
{
    OPS(o);
    if (!s_begun) {
        return false;
    }
    lk(o);
    bool result = s_have_credential;
    ulk(o);
    return result;
}

bool flex_account_usable(void)
{
    OPS(o);
    if (!s_begun) {
        return false;
    }
    lk(o);
    bool result = s_have_credential && G.link != FLEX_LINK_AUTH_REQUIRED && G.link != FLEX_LINK_TOKEN_EXPIRED;
    ulk(o);
    return result;
}

flex_account_state_t flex_account_state(void)
{
    OPS(o);
    if (!s_begun) {
        return FLEX_ACCOUNT_UNLINKED;
    }
    lk(o);
    flex_account_state_t result = G.state;
    ulk(o);
    return result;
}

flex_account_link_t flex_account_link_state(void)
{
    OPS(o);
    if (!s_begun) {
        return FLEX_LINK_UNLINKED;
    }
    lk(o);
    flex_account_link_t result = G.link;
    ulk(o);
    return result;
}

void flex_account_snapshot(flex_account_snapshot_t *out)
{
    OPS(o);
    if (!out) {
        return;
    }
    if (!s_begun || !o) {
        memset(out, 0, sizeof(*out));
        out->state = FLEX_ACCOUNT_UNLINKED;
        out->link = FLEX_LINK_UNLINKED;
        return;
    }
    lk(o);
    memcpy(out, &G, sizeof(*out));
    out->linked = s_have_credential;
    out->verified_age_s = s_verified_this_boot ? (uint32_t)(o->millis() - s_last_verified_ms) / 1000u : 0xFFFFFFFFu;
    ulk(o);
}

void flex_account_request_validation(void)
{
    OPS(o);
    if (!o) {
        return;
    }
    atomic_store(&s_wait_ms, 0);
    atomic_store(&s_wait_from_ms, o->millis());
    atomic_store(&s_validate_wanted, true);
    if (s_begun) {
        o->ensure_task();   // sin tarea nadie validaria: si no se pudo crear al arrancar, se reintenta aqui
    }
}

void flex_account_report_rejected(void)
{
    OPS(o);
    if (!o) {
        return;
    }
    uint32_t now = o->millis();
    lk(o);
    bool too_soon = s_reject_seen && (uint32_t)(now - s_last_reject_ms) < FLEX_ACCOUNT_REJECT_RATE_MS;
    if (!too_soon) {
        s_reject_seen = true;
        s_last_reject_ms = now;
    }
    ulk(o);
    if (!too_soon) {
        flex_account_request_validation();
    }
}

bool flex_account_copy_bearer(char *out, size_t capacity)
{
    OPS(o);
    if (!out || capacity == 0 || !s_begun) {
        return false;
    }
    lk(o);
    bool ok = s_have_credential && s_bearer[0] && strlen(s_bearer) + 1 <= capacity;
    if (ok) {
        memcpy(out, s_bearer, strlen(s_bearer) + 1);
    } else {
        out[0] = 0;
    }
    ulk(o);
    return ok;
}

// DESVINCULAR ESTE APARATO (accion explicita del usuario, o restablecer de fabrica): sin credencial en la NVS ni en RAM y con un id de instalacion nuevo, para que el servidor
// no reconozca a este aparato como el anterior. Se escribe en la NVS UNA vez y solo si hay algo que borrar
void flex_account_forget_local(void)
{
    OPS(o);
    if (!o) {
        return;
    }
    atomic_store(&s_cancel_requested, true);   // un enlace en curso se cancela
    atomic_fetch_add(&s_epoch, 1);             // y una validacion en vuelo no escribira nada despues
    lk(o);
    bool had = s_have_credential;
    ulk(o);
    if (o->nvs_begin()) {
        bool any = had || o->nvs_is_key("linked") || o->nvs_is_key("token") || o->nvs_is_key("address") || o->nvs_is_key("display") || o->nvs_is_key("authst");
        if (any) {
            o->nvs_clear();
            o->nvs_end();
            rotate_installation_id(o);
            say(o, "[ACCOUNT] cuenta desvinculada de este aparato");
        } else {
            o->nvs_end();
        }
    }
    clear_memory(o);
}

#ifdef FLEX_HOST_TEST
void flex_account_test_power_cycle(void)
{
    OPS(o);
    // Se pierde TODO lo de RAM (la NVS es de la prueba y sobrevive)
    lk(o);
    memset(&G, 0, sizeof(G));
    memset(s_bearer, 0, sizeof(s_bearer));
    s_have_credential = false;
    ulk(o);
    atomic_store(&s_start_requested, false);
    atomic_store(&s_cancel_requested, false);
    s_was_online = false;
    atomic_store(&s_validate_wanted, false);
    atomic_store(&s_wait_from_ms, 0);
    atomic_store(&s_wait_ms, 0);
    s_failures = 0;
    s_last_verified_ms = 0;
    s_verified_this_boot = false;
    s_reject_seen = false;
    s_auth_st = AUTHST_OK;
    s_installation_id = 0;
    s_attempted = false;
    s_last_attempt_ms = 0;
    flex_account_begin();
}
#endif
