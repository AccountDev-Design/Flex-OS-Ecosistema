// Flex OS Ultra · TLS: textos de fallo (ver flex_tls.h). Repite FlexOS_CloudTLS.cpp: las pruebas de host comparan cada codigo con el de Arduino.
#include <stdio.h>
#include <string.h>
#include "flex_tls.h"

bool flex_tls_room_ok(size_t internal_free, size_t largest_block)
{
    return internal_free >= FLEX_TLS_MIN_INTERNAL && largest_block >= FLEX_TLS_MIN_BLOCK;
}

const char *flex_tls_reason(int err, char *out, size_t cap)
{
    if (!out || !cap) {
        return "";
    }
    const char *t = NULL;
    // 0 = sin error y > 0 = el descriptor del socket (lastError() guarda lo que devolvio el cliente SSL al abrir, que tras una conexion que SI se abrio es el socket):
    // solo los negativos son un error de la conexion
    if (err >= 0) {
        t = "sin detalle";
    } else if (err == -1) {
        t = "sin DNS ni TCP o tiempo agotado";
    } else {
        // mbedTLS suma un codigo de MODULO (multiplo de 0x80) y, a veces, uno de CAUSA (< 0x80): primero el del modulo, despues la causa
        unsigned u = (unsigned)(-err), hi = u & 0xFF80u, lo = u & 0x007Fu;
        switch (hi) {
        case 0x2700:
        case 0x7A00:
            t = "certificado no reconocido";
            break;
        case 0x7F00:
        case 0x2880:
        case 0x3F80:
        case 0x4D80:
        case 0x5280:
            t = "sin memoria interna";
            break;
        case 0x6800:
            t = "tiempo agotado en TLS";
            break;
        case 0x7280:
            t = "el servidor cerro la conexion";
            break;
        case 0x7780:
        case 0x7680:
            t = "el servidor rechazo TLS";
            break;
        default:
            break;
        }
        if (!t) {
            switch (lo) {
            case 0x10:
                t = "sin memoria interna";
                break;
            case 0x50:
                t = "el servidor cerro la conexion";
                break;
            case 0x52:
                t = "no se encontro el servidor";
                break;
            case 0x42:
            case 0x44:
                t = "no se pudo abrir la conexion";
                break;
            default:
                break;
            }
        }
    }
    if (t) {
        snprintf(out, cap, "%s", t);
    } else {
        snprintf(out, cap, "error TLS -0x%04X", (unsigned)(-err) & 0xFFFFu);
    }
    return out;
}

const char *flex_tls_phase(bool dns_ok, uint32_t elapsed_ms, uint32_t handshake_ms, uint32_t connect_ms, char *out, size_t cap)
{
    if (!out || !cap) {
        return "";
    }
    unsigned s = (unsigned)((elapsed_ms + 500u) / 1000u);
    if (!dns_ok) {
        snprintf(out, cap, "no se encuentra el servidor (DNS)");
    } else if (elapsed_ms < 3000u) {
        snprintf(out, cap, "el servidor rechaza la conexion TCP");
    } else if (connect_ms > 1000u && elapsed_ms + 1000u >= connect_ms) {
        snprintf(out, cap, "TCP sin respuesta tras %u s", s);
    } else if (handshake_ms && elapsed_ms + 1500u >= handshake_ms) {
        snprintf(out, cap, "el saludo TLS no termino en %u s", s);
    } else {
        snprintf(out, cap, "sin respuesta en %u s (TCP o TLS)", s);
    }
    return out;
}

bool flex_url_host(const char *url, char *out, size_t cap)
{
    if (!out || !cap) {
        return false;
    }
    out[0] = 0;
    if (!url) {
        return false;
    }
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t n = 0;
    while (p[n] && p[n] != '/' && p[n] != ':' && p[n] != '?') {
        n++;
    }
    if (!n || n >= cap) {
        return false;
    }
    memcpy(out, p, n);
    out[n] = 0;
    return true;
}
