#include "flex_session_model.h"
#include <string.h>

uint32_t flex_crc32u(const uint8_t *d, size_t n, uint32_t crc)
{
    crc = ~crc;
    while (n--) {
        crc ^= *d++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

size_t flex_sess_pack(uint8_t *out, size_t cap, uint16_t ver, uint16_t app, const void *data, size_t len)
{
    if (!out || !data || len + FLEX_SESS_HDR_SIZE > FLEX_SESS_IO_MAX || cap < len + FLEX_SESS_HDR_SIZE) {
        return 0;
    }
    flex_sess_hdr_t h;
    h.magic = FLEX_SESS_MAGIC;
    h.ver = ver;
    h.app = app;
    h.len = (uint32_t)len;
    h.crc = flex_crc32u((const uint8_t *)data, len, 0);
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), data, len);
    return sizeof(h) + len;
}

size_t flex_sess_unpack(const uint8_t *buf, size_t sz, uint16_t ver, uint16_t app, void *out, size_t max)
{
    if (!buf || !out || sz < FLEX_SESS_HDR_SIZE || sz > FLEX_SESS_IO_MAX) {
        return 0;
    }
    flex_sess_hdr_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != FLEX_SESS_MAGIC || h.ver != ver || h.app != app || h.len > max || FLEX_SESS_HDR_SIZE + h.len != sz) {
        return 0;
    }
    memcpy(out, buf + FLEX_SESS_HDR_SIZE, h.len);
    return flex_crc32u((const uint8_t *)out, h.len, 0) == h.crc ? h.len : 0;
}

void flex_sess_init(flex_sess_t *s)
{
    memset(s, 0, sizeof(*s));
    s->dirty_app = -1;
}

bool flex_sess_save_app(flex_sess_t *s, int app, flex_sess_save_fn save, void *ctx)
{
    if (app < 0 || app >= FLEX_SESS_APPS) {
        return true;
    }
    if (!s->need[app]) {
        return true;               // nada ha cambiado desde el ultimo volcado
    }
    int r = save(ctx, app);
    if (r == FLEX_SESS_NOHOOK) {
        return true;               // la app no tiene nada que guardar: exito trivial
    }
    if (r == FLEX_SESS_OK) {
        s->need[app] = false;
        return true;
    }
    return false;
}

void flex_sess_mark_dirty(flex_sess_t *s, int app, uint32_t now, flex_sess_save_fn save, void *ctx)
{
    if (app < 0 || app >= FLEX_SESS_APPS) {
        return;
    }
    if (s->dirty_app != app && s->dirty_app >= 0) {
        flex_sess_save_app(s, s->dirty_app, save, ctx);   // otra app pendiente: se cierra ya
    }
    if (s->dirty_app != app) {
        s->dirty_app = (int8_t)app;
        s->first_ms = now;
    }
    s->need[app] = true;
    s->dirty_ms = now;
}

void flex_sess_flush_now(flex_sess_t *s, flex_sess_save_fn save, void *ctx)
{
    if (s->dirty_app < 0) {
        return;
    }
    int app = s->dirty_app;
    s->dirty_app = -1;
    flex_sess_save_app(s, app, save, ctx);
}

void flex_sess_tick(flex_sess_t *s, uint32_t now, bool blocked, flex_sess_save_fn save, void *ctx)
{
    if (s->dirty_app < 0 || blocked) {
        return;
    }
    if (now - s->dirty_ms < FLEX_SESS_IDLE_MS && now - s->first_ms < FLEX_SESS_MAXWAIT_MS) {
        return;
    }
    flex_sess_flush_now(s, save, ctx);
}

bool flex_sess_terminate_begin(flex_sess_t *s, int app, bool force, flex_sess_save_fn save, void *ctx)
{
    if (app < 0 || app >= FLEX_SESS_APPS) {
        return true;
    }
    if (s->dirty_app == app) {
        s->dirty_app = -1;         // se guarda aqui, no por el camino diferido
    }
    bool saved = flex_sess_save_app(s, app, save, ctx);
    return saved || force;
}
