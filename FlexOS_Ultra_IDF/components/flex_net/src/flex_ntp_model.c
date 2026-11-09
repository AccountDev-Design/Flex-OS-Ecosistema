// Flex OS Ultra · hora por red: el modelo (ver flex_ntp.h). Cada funcion repite la de FlexOS_Ultra_NTP.h; las pruebas de host las comparan con las
// de Arduino compiladas tal cual.
#include <stdio.h>
#include <string.h>
#include "flex_ntp.h"

const char *const flex_ntp_servers[FLEX_NTP_NSERV] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};

void flex_ntp_build_packet(uint8_t pkt[FLEX_NTP_PKT_SIZE])
{
    memset(pkt, 0, FLEX_NTP_PKT_SIZE);
    pkt[0] = 0xE3;   // LI = 3 (sin aviso), VN = 4, Mode = 3 (cliente)
    pkt[1] = 0;      // estrato
    pkt[2] = 6;      // intervalo de sondeo
    pkt[3] = 0xEC;   // precision
    pkt[12] = 0x31;  // identificador de referencia
    pkt[13] = 0x4E;
    pkt[14] = 0x31;
    pkt[15] = 0x34;
}

// Bytes 40..43: marca de tiempo de transmision, segundos desde 1900. Precision +-1 s (sin fraccion ni compensacion de ida y vuelta, como en Arduino).
uint32_t flex_ntp_parse_reply(const uint8_t *pkt, int n)
{
    if (!pkt || n < FLEX_NTP_PKT_SIZE) {
        return 0;
    }
    uint32_t secs1900 = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) | ((uint32_t)pkt[42] << 8) | (uint32_t)pkt[43];
    if (secs1900 > FLEX_NTP_EPOCH_DELTA) {
        uint32_t utc = secs1900 - (uint32_t)FLEX_NTP_EPOCH_DELTA;
        if (utc >= FLEX_NTP_MIN_EPOCH) {
            return utc;   // filtro de cordura
        }
    }
    return 0;
}

// Lo que Arduino no comprueba y un servidor de verdad siempre cumple: modo 4 (servidor), el reloj del servidor no esta en alarma (LI != 3) y estrato
// de 1 a 15 (0 es un "kiss-o'-death" o una respuesta sin sentido; 16 es "no sincronizado").
bool flex_ntp_reply_sane(const uint8_t *pkt, int n)
{
    if (!pkt || n < FLEX_NTP_PKT_SIZE) {
        return false;
    }
    int mode = pkt[0] & 7, li = pkt[0] >> 6, stratum = pkt[1];
    return mode == 4 && li != 3 && stratum >= 1 && stratum <= 15;
}

uint32_t flex_ntp_backoff_ms(uint8_t fails)
{
    if (fails > 16) {
        return (uint32_t)FLEX_NTP_BACKOFF_MAX;   // el desplazamiento de mas de 31 bits no esta definido; el techo se alcanza con 7
    }
    uint32_t wait = (uint32_t)FLEX_NTP_BACKOFF_MIN << (fails > 0 ? (fails - 1) : 0);
    if (wait > FLEX_NTP_BACKOFF_MAX || wait < FLEX_NTP_BACKOFF_MIN) {
        wait = (uint32_t)FLEX_NTP_BACKOFF_MAX;
    }
    return wait;
}

void flex_ntp_init(flex_ntp_t *s)
{
    memset(s, 0, sizeof(*s));
    s->state = FLEX_NTPS_NEVER;
    s->time_nvs_ok = true;
}

void flex_ntp_on_wifi_up(flex_ntp_t *s, uint32_t now_ms)
{
    s->fails = 0;
    s->next_ms = now_ms + (uint32_t)FLEX_NTP_FIRST_DELAY;
    if (!s->next_ms) {
        s->next_ms = 1;   // 0 esta reservado para "nunca programada"
    }
}

bool flex_ntp_request(flex_ntp_t *s, bool user_asked, const flex_ntp_in_t *in)
{
    if (s->busy) {
        return false;
    }
    if (in->airplane) {
        return false;
    }
    if (!in->online) {
        return false;
    }
    if (!user_asked && s->next_ms && (int32_t)(in->now_ms - s->next_ms) < 0) {
        return false;
    }
    s->busy = true;
    s->state = FLEX_NTPS_BUSY;
    s->user_asked = user_asked;
    return true;
}

void flex_ntp_launch_failed(flex_ntp_t *s)
{
    s->busy = false;
    s->state = FLEX_NTPS_FAIL;
}

bool flex_ntp_tick(flex_ntp_t *s, const flex_ntp_in_t *in)
{
    uint32_t now = in->now_ms;
    if (s->busy) {
        return false;
    }
    // Un intento acaba de terminar: programa el siguiente. Correcto -> 6 h. Fallido -> espera progresiva.
    if (s->done_ms) {
        uint32_t done = s->done_ms;
        s->done_ms = 0;
        if (s->state == FLEX_NTPS_OK) {
            s->next_ms = done + (uint32_t)FLEX_NTP_PERIOD_MS;
        } else {
            s->next_ms = done + flex_ntp_backoff_ms(s->fails);
        }
        return false;
    }
    if (in->airplane || !in->online) {
        return false;
    }
    // Primera sincronizacion de la sesion: en cuanto haya red y el sistema este operativo (no a mitad del arranque)
    if (s->next_ms == 0) {
        if (!in->system_ready) {
            return false;
        }
        if (now < FLEX_NTP_FIRST_DELAY) {
            return false;
        }
        bool launch = flex_ntp_request(s, false, in);
        if (s->busy) {
            s->next_ms = now + (uint32_t)FLEX_NTP_PERIOD_MS;   // se reajusta al terminar
        }
        return launch;
    }
    if ((int32_t)(now - s->next_ms) >= 0) {
        return flex_ntp_request(s, false, in);
    }
    return false;
}

void flex_ntp_finish(flex_ntp_t *s, uint32_t utc, uint32_t now_ms)
{
    if (utc) {
        s->last_sync_utc = utc;
        s->from_nvs = false;
        s->fails = 0;
        s->state = FLEX_NTPS_OK;
    } else {
        if (s->fails < 8) {
            s->fails++;
        }
        s->state = FLEX_NTPS_FAIL;
    }
    s->done_ms = now_ms;
    s->busy = false;
}

void flex_ntp_state_text(char *out, size_t n, const flex_ntp_t *s, bool airplane, bool online)
{
    if (!s->time_nvs_ok) {
        snprintf(out, n, "Error: almacenamiento NVS no disponible");
        return;
    }
    if (s->busy) {
        snprintf(out, n, "Sincronizando...");
        return;
    }
    switch (s->state) {
    case FLEX_NTPS_OK:
        snprintf(out, n, "Sincronizado - UTC-5 (Lima)");
        break;
    case FLEX_NTPS_FAIL:
        snprintf(out, n, "%s", airplane ? "Modo avi\xC3\xB3n activo" : (online ? "Sin respuesta del servidor" : "Sin conexi\xC3\xB3n Wi-Fi"));
        break;
    default:
        if (s->from_nvs) {
            snprintf(out, n, "Hora aproximada (sin sincronizar)");
        } else if (airplane) {
            snprintf(out, n, "Modo avi\xC3\xB3n activo");
        } else if (!online) {
            snprintf(out, n, "Esperando Wi-Fi");
        } else {
            snprintf(out, n, "Pendiente");
        }
        break;
    }
}

// Howard Hinnant: dias desde 1970-01-01 -> fecha civil (clkCivilFromDays)
void flex_ntp_civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

void flex_ntp_last_text(char *out, size_t n, uint32_t last_sync_utc, uint32_t now_utc)
{
    if (last_sync_utc < FLEX_NTP_MIN_EPOCH) {
        snprintf(out, n, "Nunca");
        return;
    }
    long local = (long)last_sync_utc + FLEX_NTP_TZ_SEC;
    long days = local / 86400L, rem = local % 86400L;
    if (rem < 0) {
        rem += 86400L;
        days--;
    }
    int y, mo, d;
    flex_ntp_civil_from_days(days, &y, &mo, &d);
    long now_local = (long)now_utc + FLEX_NTP_TZ_SEC;
    long now_days = now_local / 86400L;
    if (now_local % 86400L < 0) {
        now_days--;
    }
    int hh = (int)(rem / 3600L), mm = (int)((rem % 3600L) / 60L);
    if (days == now_days) {
        snprintf(out, n, "Hoy %02d:%02d", hh, mm);
    } else if (days == now_days - 1) {
        snprintf(out, n, "Ayer %02d:%02d", hh, mm);
    } else {
        snprintf(out, n, "%02d/%02d/%04d %02d:%02d", d, mo, y, hh, mm);
    }
}
