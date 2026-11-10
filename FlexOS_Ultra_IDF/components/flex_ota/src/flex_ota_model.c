// Flex OS Ultra · OTA: el modelo (ver flex_ota.h). Cada funcion repite la de FlexOS_OTA.cpp; las pruebas de host las comparan con las de Arduino compiladas tal cual.
#include <string.h>
#include "flex_ota.h"

static bool jspace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Busca la clave "key" y devuelve el puntero al PRIMER caracter de su valor. Exige comilla-clave-comilla-dos puntos, asi que no confunde una clave con un
// valor que resulte tener el mismo texto.
const char *flex_ota_json_find(const char *p, const char *end, const char *key)
{
    size_t kl = strlen(key);
    while (p < end) {
        const char *q = (const char *)memchr(p, '"', (size_t)(end - p));
        if (!q) {
            return NULL;
        }
        if ((size_t)(end - q) > kl + 2 && memcmp(q + 1, key, kl) == 0 && q[1 + kl] == '"') {
            const char *r = q + 2 + kl;
            while (r < end && jspace(*r)) {
                r++;
            }
            if (r < end && *r == ':') {
                r++;
                while (r < end && jspace(*r)) {
                    r++;
                }
                return r;
            }
        }
        p = q + 1;
    }
    return NULL;
}

// Acota el objeto { ... } de una clave, contando llaves y respetando las que aparezcan dentro de una cadena. La clave se busca con sus comillas incluidas:
// "flex_os_ultra" NO casa con "flex_os_ultra_s3".
bool flex_ota_json_block(const char *buf, const char *key, const char **bs, const char **be)
{
    const char *end = buf + strlen(buf);
    const char *p = flex_ota_json_find(buf, end, key);
    if (!p || p >= end || *p != '{') {
        return false;
    }
    int depth = 0;
    bool instr = false;
    for (const char *q = p; q < end; q++) {
        if (instr) {
            if (*q == '\\' && q + 1 < end) {
                q++;
            } else if (*q == '"') {
                instr = false;
            }
        } else if (*q == '"') {
            instr = true;
        } else if (*q == '{') {
            depth++;
        } else if (*q == '}') {
            depth--;
            if (depth == 0) {
                *bs = p + 1;
                *be = q;
                return true;
            }
        }
    }
    return false;   // llaves desbalanceadas: JSON corrupto
}

// Extrae una cadena JSON (con escapes) a un buffer acotado
bool flex_ota_json_str(const char *p, const char *end, char *out, size_t cap)
{
    if (!p || p >= end || *p != '"' || cap == 0) {
        return false;
    }
    p++;
    size_t n = 0;
    while (p < end && *p != '"') {
        char c = *p++;
        if (c == '\\' && p < end) {
            char e = *p++;
            switch (e) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                continue;   // se ignora: \n ya salta linea
            case 't':
                c = ' ';
                break;
            case 'u':
                for (int i = 0; i < 4 && p < end; i++) {
                    p++;
                }
                c = '?';   // sin tabla Unicode
                break;
            default:
                c = e;   // \" \\ \/ ...
                break;
            }
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = 0;
    return p < end && *p == '"';   // cadena sin cerrar: corrupto
}

uint32_t flex_ota_json_num(const char *p, const char *end)
{
    uint32_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    return v;
}

// Versiones MAJOR.MINOR.PATCH. Estricto a proposito: cualquier cosa que no sean exactamente tres numeros separados por puntos se rechaza
bool flex_ota_ver_parse(const char *s, uint16_t v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (!s || !*s) {
        return false;
    }
    int i = 0;
    while (*s) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        uint32_t n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10u + (uint32_t)(*s - '0');
            if (n > 65535u) {
                return false;
            }
            s++;
        }
        if (i > 2) {
            return false;
        }
        v[i++] = (uint16_t)n;
        if (*s == '.') {
            s++;
            if (!*s) {
                return false;
            }
        } else if (*s) {
            return false;
        }
    }
    return i == 3;
}

int flex_ota_ver_cmp(const uint16_t a[3], const uint16_t b[3])
{
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

// Un MD5 utilizable: 32 digitos hexadecimales Y que no sean todo ceros (el relleno de los manifiestos de ejemplo mide 32 caracteres y condenaria la
// actualizacion: el MD5 real de la imagen nunca sera 32 ceros)
bool flex_ota_md5_usable(const char *s)
{
    if (!s || strlen(s) != 32) {
        return false;
    }
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
        if (c != '0') {
            all_zero = false;
        }
    }
    return !all_zero;
}

const char *flex_ota_err_text(flex_ota_err_t e)
{
    switch (e) {
    case FLEX_OTA_ERR_NO_WIFI: return "Sin conexion a internet";
    case FLEX_OTA_ERR_LOWMEM: return "Memoria insuficiente";
    case FLEX_OTA_ERR_HTTPS: return "No se pudo conectar (HTTPS)";
    case FLEX_OTA_ERR_TLS: return "Certificado no valido";
    case FLEX_OTA_ERR_HTTP_STATUS: return "Archivo no encontrado";
    case FLEX_OTA_ERR_TIMEOUT: return "Se agoto el tiempo de espera";
    case FLEX_OTA_ERR_JSON: return "Manifiesto corrupto";
    case FLEX_OTA_ERR_NO_BLOCK: return "Sin version para este equipo";
    case FLEX_OTA_ERR_BAD_VERSION: return "Version no valida";
    case FLEX_OTA_ERR_BAD_URL: return "Direccion de descarga no valida";
    case FLEX_OTA_ERR_BAD_SIZE: return "Tamano de firmware no valido";
    case FLEX_OTA_ERR_NO_PARTITION: return "Particion sin soporte OTA";
    case FLEX_OTA_ERR_BEGIN: return "No hay espacio para la imagen";
    case FLEX_OTA_ERR_WRITE: return "Error de escritura";
    case FLEX_OTA_ERR_TRUNCATED: return "Descarga incompleta";
    case FLEX_OTA_ERR_CORRUPT: return "Firmware corrupto";
    case FLEX_OTA_ERR_NO_CA: return "Falta el certificado del servidor";
    case FLEX_OTA_ERR_NO_SIG: return "La actualizacion no viene firmada";
    case FLEX_OTA_ERR_BAD_SIG: return "La firma de la actualizacion no es valida";
    case FLEX_OTA_ERR_ROLLBACK: return "Esa version es anterior a la instalada";
    case FLEX_OTA_ERR_CANCELLED: return "Actualizacion cancelada";
    default: return "Error desconocido";
    }
}

const char *flex_ota_phase_text(flex_ota_state_t s)
{
    switch (s) {
    case FLEX_OTA_CONNECTING: return "Conectando...";
    case FLEX_OTA_DOWNLOADING: return "Descargando...";
    case FLEX_OTA_VERIFYING: return "Verificando...";
    case FLEX_OTA_INSTALLING: return "Instalando...";
    case FLEX_OTA_FINALIZING: return "Finalizando...";
    case FLEX_OTA_REBOOTING: return "Reiniciando...";
    default: return "Preparando...";
    }
}

flex_ota_err_t flex_ota_manifest_read(const char *buf, const flex_ota_cfg_t *cfg, flex_ota_manifest_t *out, bool *newer)
{
    memset(out, 0, sizeof(*out));
    *newer = false;
    // El bloque de ESTA placa: cada plataforma lee unicamente lo suyo
    const char *bs = NULL, *be = NULL;
    if (!flex_ota_json_block(buf, cfg->key, &bs, &be)) {
        return FLEX_OTA_ERR_NO_BLOCK;
    }
    char ver[FLEX_OTA_VER_MAX] = "";
    const char *pv = flex_ota_json_find(bs, be, "version");
    if (!pv || !flex_ota_json_str(pv, be, ver, sizeof(ver))) {
        return FLEX_OTA_ERR_JSON;
    }
    uint16_t rv[3], lv[3];
    if (!flex_ota_ver_parse(ver, rv)) {
        return FLEX_OTA_ERR_BAD_VERSION;
    }
    if (!flex_ota_ver_parse(cfg->local_version, lv)) {
        return FLEX_OTA_ERR_BAD_VERSION;
    }
    if (flex_ota_ver_cmp(rv, lv) <= 0) {   // igual o mas antigua: nada que hacer
        if (cfg->antirollback && flex_ota_ver_cmp(rv, lv) < 0) {
            return FLEX_OTA_ERR_ROLLBACK;
        }
        return FLEX_OTA_ERR_NONE;   // al dia
    }
    char url[FLEX_OTA_URL_MAX] = "";
    const char *pu = flex_ota_json_find(bs, be, "url");
    if (!pu || !flex_ota_json_str(pu, be, url, sizeof(url))) {
        return FLEX_OTA_ERR_JSON;
    }
    if (strncmp(url, "https://", 8) != 0) {
        return FLEX_OTA_ERR_BAD_URL;
    }
    char log[FLEX_OTA_CHANGELOG_MAX] = "";
    const char *pc = flex_ota_json_find(bs, be, "changelog");
    if (pc) {
        flex_ota_json_str(pc, be, log, sizeof(log));   // opcional
    }
    char md5[FLEX_OTA_MD5_MAX] = "";
    const char *pm = flex_ota_json_find(bs, be, "md5");
    if (pm) {
        flex_ota_json_str(pm, be, md5, sizeof(md5));   // opcional pero muy recomendable
    }
    uint32_t size = 0;
    const char *ps = flex_ota_json_find(bs, be, "size");
    if (ps) {
        size = flex_ota_json_num(ps, be);   // opcional (informativo)
    }
    // Firma digital (arquitectura preparada): se lee y se guarda SIEMPRE; con la firma obligatoria, su ausencia es un error
    char sig[FLEX_OTA_SIG_MAX] = "";
    const char *pg = flex_ota_json_find(bs, be, "sig");
    if (pg) {
        flex_ota_json_str(pg, be, sig, sizeof(sig));
    }
    if (cfg->require_signature && !sig[0]) {
        return FLEX_OTA_ERR_NO_SIG;
    }
    // Cada cadena acaba en 0 aunque llene el buffer (como los strncpy + terminador de Arduino): quien lea `out` nunca ve basura tras el ultimo caracter
    memset(out, 0, sizeof(*out));
    strncpy(out->ver, ver, sizeof(out->ver) - 1);
    out->ver[sizeof(out->ver) - 1] = 0;
    strncpy(out->url, url, sizeof(out->url) - 1);
    out->url[sizeof(out->url) - 1] = 0;
    strncpy(out->md5, md5, sizeof(out->md5) - 1);
    out->md5[sizeof(out->md5) - 1] = 0;
    strncpy(out->changelog, log, sizeof(out->changelog) - 1);
    out->changelog[sizeof(out->changelog) - 1] = 0;
    strncpy(out->sig, sig, sizeof(out->sig) - 1);
    out->sig[sizeof(out->sig) - 1] = 0;
    out->size = size;
    *newer = true;
    return FLEX_OTA_ERR_NONE;
}

// Que hacer con la confirmacion de arranque: nada (la imagen ya esta validada), esperar, confirmarla o mantenerla sin confirmar
flex_ota_boot_t flex_ota_boot_decide(bool pending, uint32_t uptime_ms, bool safe_mode, bool ui_ready)
{
    if (!pending) {
        return FLEX_OTA_BOOT_NONE;
    }
    if (safe_mode) {
        return FLEX_OTA_BOOT_HOLD;
    }
    if (!ui_ready || uptime_ms < FLEX_OTA_CONFIRM_MS) {
        return FLEX_OTA_BOOT_WAIT;
    }
    return FLEX_OTA_BOOT_CONFIRM;
}
