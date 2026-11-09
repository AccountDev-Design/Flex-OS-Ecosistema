// Flex OS Ultra · hora por red (NTP). Referencia: FlexOS_Ultra_NTP.h (cliente SNTP propio de Arduino) y docs/spec/03b §12.
//
// Dos capas:
//  * MODELO (flex_ntp_model.c, C portable, sin ESP-IDF): el paquete SNTP de 48 bytes, la lectura de la respuesta, la planificacion (primera consulta,
//    cada 6 h, espera progresiva de 30 s a 30 min tras un fallo) y los textos de Ajustes. Comparado con las funciones de Arduino compiladas tal cual.
//  * SERVICIO (flex_ntp.c): une el modelo con el reloj del sistema (flex_time), la NVS (espacio "flexos_time", claves "epoch" y "lastsync"), el
//    estado de la red y un transporte UDP (flex_ntp_udp.c, sockets BSD de lwIP). Nunca corre en la tarea de la interfaz y nunca la bloquea.
//
// Sin Wi-Fi (X3) el servicio no tiene red que usar: dice "Esperando Wi-Fi" y no consulta nada. Cuando exista el servicio de Wi-Fi, avisa con
// flex_net_set_online(true) y la primera consulta sale 1,5 s despues, como en Arduino.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLEX_NTP_PERIOD_MS    (6UL * 3600UL * 1000UL)   // resincronizacion: 6 h
#define FLEX_NTP_FIRST_DELAY  1500UL                    // margen tras conectar antes del primer intento
#define FLEX_NTP_TIMEOUT_MS   4000UL                    // espera maxima por respuesta de UN servidor
#define FLEX_NTP_PORT         123
#define FLEX_NTP_PKT_SIZE     48
#define FLEX_NTP_EPOCH_DELTA  2208988800UL              // segundos entre 1900-01-01 y 1970-01-01
#define FLEX_NTP_BACKOFF_MIN  30000UL                   // 30 s tras el primer fallo
#define FLEX_NTP_BACKOFF_MAX  1800000UL                 // techo: 30 min
#define FLEX_NTP_MIN_EPOCH    1767225600u               // 2026-01-01 00:00 UTC: ninguna epoca menor se acepta (de la red ni de la NVS)
#define FLEX_NTP_TZ_SEC       (-5 * 3600)               // Lima, UTC-5 sin horario de verano
#define FLEX_NTP_NSERV        3
extern const char *const flex_ntp_servers[FLEX_NTP_NSERV];   // pool.ntp.org, time.google.com, time.cloudflare.com

enum { FLEX_NTPS_NEVER = 0, FLEX_NTPS_OK, FLEX_NTPS_FAIL, FLEX_NTPS_BUSY };

// ---- modelo ----------------------------------------------------------------------------------------------------------------
typedef struct {
    uint8_t state;            // FLEX_NTPS_*
    bool busy;
    uint32_t done_ms;         // millis del ultimo intento terminado (0 = ya tratado)
    uint32_t last_sync_utc;   // epoca UTC de la ultima sincronizacion correcta
    bool from_nvs;            // la hora actual viene de la NVS, no de la red
    uint8_t fails;            // fallos seguidos (espera progresiva)
    uint32_t next_ms;         // millis del proximo intento permitido (0 = nunca programado)
    bool user_asked;          // el intento en curso lo pidio el usuario
    bool time_nvs_ok;         // el espacio de la hora de la NVS responde
} flex_ntp_t;

typedef struct {
    uint32_t now_ms;
    bool airplane;
    bool online;
    bool system_ready;        // el sistema esta en el escritorio o en el bloqueo (no a medio arranque)
} flex_ntp_in_t;

void flex_ntp_build_packet(uint8_t pkt[FLEX_NTP_PKT_SIZE]);                  // lo que se manda a un servidor
uint32_t flex_ntp_parse_reply(const uint8_t *pkt, int n);                    // epoca UTC de la respuesta, o 0 si no vale
bool flex_ntp_reply_sane(const uint8_t *pkt, int n);                         // comprobaciones extra (NO estan en Arduino): modo servidor, aviso de reloj, estrato
uint32_t flex_ntp_backoff_ms(uint8_t fails);                                 // espera tras `fails` fallos seguidos
void flex_ntp_init(flex_ntp_t *s);
void flex_ntp_on_wifi_up(flex_ntp_t *s, uint32_t now_ms);                    // ntpOnWifiUp
bool flex_ntp_request(flex_ntp_t *s, bool user_asked, const flex_ntp_in_t *in);   // ntpRequestSync: true = hay que lanzar la consulta
bool flex_ntp_tick(flex_ntp_t *s, const flex_ntp_in_t *in);                  // ntpTick: true = hay que lanzar la consulta
void flex_ntp_launch_failed(flex_ntp_t *s);                                  // la tarea no se pudo crear
void flex_ntp_finish(flex_ntp_t *s, uint32_t utc, uint32_t now_ms);          // resultado de la consulta (utc = 0: ningun servidor contesto)
void flex_ntp_state_text(char *out, size_t n, const flex_ntp_t *s, bool airplane, bool online);   // ntpStateText
void flex_ntp_last_text(char *out, size_t n, uint32_t last_sync_utc, uint32_t now_utc);           // ntpLastSyncText
void flex_ntp_civil_from_days(long z, int *y, int *m, int *d);               // clkCivilFromDays (Hinnant)

// ---- servicio ----------------------------------------------------------------------------------------------------------------
// Todo lo que toca el sistema entra por aqui; el de la placa lo pone flex_ntp_port.c y las pruebas ponen el suyo.
typedef struct {
    uint32_t (*millis)(void);
    uint32_t (*utc)(void);
    void (*set_utc)(uint32_t utc, bool from_network);
    bool (*synced)(void);                                 // la hora del sistema ya es de la red o de un ajuste manual (no la semilla de fabrica)
    bool (*airplane)(void);
    bool (*system_ready)(void);
    bool (*safe_mode)(void);                              // Modo seguro: ni primera sincronizacion ni reintentos (el guardado horario sigue)
    bool (*nvs_ok)(void);
    bool (*load)(uint64_t *epoch, uint64_t *last);        // false = el espacio no se pudo abrir
    bool (*save)(uint64_t epoch, uint64_t last);          // false = no se pudo escribir
    uint32_t (*query)(const char *host);                  // consulta UN servidor: epoca UTC o 0
    void (*wake)(void);                                   // despierta al trabajador (en las pruebas: lo ejecuta en el acto)
    void (*lock)(void);                                   // NULL = sin exclusion (una sola tarea)
    void (*unlock)(void);
} flex_ntp_env_t;

void flex_ntp_set_env(const flex_ntp_env_t *env);        // NULL: el de la placa
const flex_ntp_env_t *flex_ntp_env(void);
void flex_ntp_service_reset(void);                       // solo pruebas: estado de un arranque en frio

void flex_ntp_boot_load(void);                           // clkLoadNvs: la hora de antes del apagado (aproximada) antes de la interfaz
void flex_ntp_tick_service(void);                        // ntpTick + guardado horario: la llama el trabajador cada segundo
bool flex_ntp_work_pending(void);                        // hay una consulta por hacer (el trabajador la hace con flex_ntp_work)
void flex_ntp_work(void);                                // ntpTask: la consulta, en el trabajador (puede tardar hasta 12 s)
void flex_ntp_save_time(void);                           // clkSaveNvs: guardar la hora ya (apagado limpio)
void flex_ntp_start(void);                               // la placa: arranca el trabajador (tarea de baja prioridad)
const flex_ntp_env_t *flex_ntp_port_env(void);           // la placa (flex_ntp_port.c); en las pruebas no existe

// El transporte UDP (flex_ntp_udp.c): consulta UN servidor y devuelve la epoca UTC o 0
uint32_t flex_ntp_udp_query(const char *host, int port, uint32_t timeout_ms);

// Para los demas servicios y la interfaz (Ajustes lee los textos; el panel rapido pide la sincronizacion)
void flex_ntp_request_sync(bool user_asked);
void flex_ntp_texts(char *state, size_t ns, char *last, size_t nl, bool *busy);
bool flex_ntp_time_nvs_ok(void);
void flex_net_set_online(bool online);                   // lo llama el servicio de Wi-Fi al confirmar / perder el enlace
bool flex_net_online(void);
bool flex_ntp_get(flex_ntp_t *out);                      // copia del estado (para las pruebas)

#ifdef __cplusplus
}
#endif
