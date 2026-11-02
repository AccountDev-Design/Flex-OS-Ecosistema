#include "flex_mem_model.h"
#include <string.h>

void flex_mem_init(flex_mem_t *m, int napps)
{
    memset(m, 0, sizeof(*m));
    m->napps = napps > FLEX_MEM_APPS ? FLEX_MEM_APPS : napps;
    flexMemAlertsReset(&m->alerts);
}

void flex_mem_commit(flex_mem_t *m, const flex_mem_ops_t *o, int app, uint32_t free_before)
{
    if (app < 0 || app >= m->napps || free_before == 0) {
        return;
    }
    uint32_t now = o->free_psram(o->ctx);
    if (free_before > now) {
        m->mem[app] = free_before - now;
        m->known[app] = true;
    } else if (!m->known[app]) {
        m->mem[app] = 0;
        m->known[app] = true;   // no reservo: dato real
    }
}

void flex_mem_released(flex_mem_t *m, int app, uint32_t bytes)
{
    if (app < 0 || app >= m->napps || !m->known[app]) {
        return;
    }
    m->mem[app] = (m->mem[app] > bytes) ? (m->mem[app] - bytes) : 0;
}

void flex_mem_forget(flex_mem_t *m, int app)
{
    if (app < 0 || app >= m->napps) {
        return;
    }
    m->mem[app] = 0;
    m->known[app] = false;
    m->shed[app] = false;
}

bool flex_mem_has(const flex_mem_t *m, int app)
{
    return app >= 0 && app < m->napps && m->known[app];
}

uint32_t flex_mem_bytes(const flex_mem_t *m, int app)
{
    return flex_mem_has(m, app) ? m->mem[app] : 0u;
}

uint32_t flex_mem_shed_app(flex_mem_t *m, const flex_mem_ops_t *o, int app)
{
    if (app < 0 || app >= m->napps) {
        return 0;
    }
    if (m->state[app] != FLEX_ALIFE_SUSPENDED) {
        return 0;   // la activa, jamas
    }
    if (o->bg_busy(o->ctx, app)) {
        return 0;   // trabajo real: intocable
    }
    if (!o->has_shed(o->ctx, app)) {
        return 0;
    }
    uint32_t before = o->free_psram(o->ctx);
    size_t hint = o->app_shed(o->ctx, app);      // la app dice SI solto algo...
    uint32_t after = o->free_psram(o->ctx);
    uint32_t got = after > before ? (after - before) : 0u;   // ...y aqui se MIDE cuanto
    flex_mem_released(m, app, got);
    if (hint || got) {
        m->shed[app] = true;
    }
    return got;
}

uint32_t flex_mem_shed_all(flex_mem_t *m, const flex_mem_ops_t *o, uint32_t stop_at)
{
    uint32_t got = o->shed_system(o->ctx);
    if (stop_at && got >= stop_at) {
        return got;
    }
    uint8_t done[FLEX_MEM_APPS];
    memset(done, 0, sizeof(done));
    for (int pass = 0; pass < m->napps; pass++) {
        int id = -1;
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < m->napps; i++) {
            if (done[i] || m->state[i] != FLEX_ALIFE_SUSPENDED || o->bg_busy(o->ctx, i)) {
                continue;
            }
            if (m->seen_ms[i] <= oldest) {
                oldest = m->seen_ms[i];
                id = i;
            }
        }
        if (id < 0) {
            break;
        }
        done[id] = 1;
        got += flex_mem_shed_app(m, o, id);
        if (stop_at && got >= stop_at) {
            break;
        }
    }
    return got;
}

int flex_mem_admit(flex_mem_t *m, const flex_mem_ops_t *o, int weight)
{
    o->sample_now(o->ctx);
    int v = flexMemCanOpen(o->snap(o->ctx), weight);
    if (v != FLEXMEM_NEED_SHED) {
        return v;
    }
    flex_mem_shed_all(m, o, 0);
    o->sample_now(o->ctx);
    v = flexMemCanOpen(o->snap(o->ctx), weight);
    return (v == FLEXMEM_NEED_SHED) ? FLEXMEM_DENY_PSRAM : v;
}

const char *flex_mem_deny_text(int verdict)
{
    switch (verdict) {
    case FLEXMEM_DENY_BLOCK: return "Memoria libre en trozos peque\xC3\xB1os";
    case FLEXMEM_DENY_SRAM: return "Memoria interna baja: se protege el sistema";
    default: return "Cierra una app o pulsa Optimizar Flex OS";
    }
}

void flex_mem_alert_tick(flex_mem_t *m, const flex_mem_ops_t *o, uint32_t now, bool blocked, bool act_ok)
{
    if (blocked) {
        return;
    }
    // nivel sostenido: SIEMPRE, en cualquier pantalla (si se congelara, el modo visual eficiente se quedaria atascado)
    int lv = flexMemLevelStep(o->snap(o->ctx), &m->alerts);
    bool rose = m->alerts.rose != 0;
    if (!act_ok) {
        return;
    }
    if (lv >= FLEXMEM_LV_WARN) {
        if (rose || flexMemReliefDue(&m->alerts, now)) {
            flexMemReliefDone(&m->alerts, now);
            flex_mem_shed_all(m, o, 0);
            o->sample_now(o->ctx);
            lv = flexMemLevelStep(o->snap(o->ctx), &m->alerts);
            if (lv < FLEXMEM_LV_WARN) {
                rose = false;
            } else if (rose) {
                if (lv >= FLEXMEM_LV_CRITICAL) {
                    o->notify(o->ctx, "Memoria cr\xC3\xADtica", "Flex OS est\xC3\xA1 protegiendo el sistema. Cierra una app.");
                } else {
                    o->notify(o->ctx, "Memoria casi llena", "Flex OS liber\xC3\xB3 recursos en segundo plano.");
                }
            }
        }
    }
    int k = flexMemAlertPick(o->snap(o->ctx), now, &m->alerts);
    switch (k) {
    case FLEXMEM_AL_FRAG:
        o->notify(o->ctx, "Memoria libre repartida en trozos peque\xC3\xB1os", "Las im\xC3\xA1genes o apps pesadas pueden tardar m\xC3\xA1s");
        break;
    case FLEXMEM_AL_SRAM:
        o->notify(o->ctx, "Memoria interna del sistema baja", "Se limitan cargas pesadas para proteger Wi-Fi y t\xC3\xA1" "ctil");
        break;
    case FLEXMEM_AL_FLASH80:
        o->notify(o->ctx, "Almacenamiento interno en uso elevado", "Limpiar la cach\xC3\xA9 temporal puede ayudar");
        break;
    case FLEXMEM_AL_FLASH90:
        o->notify(o->ctx, "Almacenamiento interno casi lleno", "Las actualizaciones y los datos nuevos podr\xC3\xAD" "an fallar");
        break;
    default:
        break;
    }
}

int flex_mem_enforce_budget(flex_mem_t *m, const flex_mem_ops_t *o)
{
    int closed = 0;
    if (o->radio_busy(o->ctx)) {
        return 0;   // la radio primero
    }
    o->sample_now(o->ctx);
    if (flexMemLevel(o->snap(o->ctx)) != FLEXMEM_LV_CRITICAL) {
        return 0;
    }
    flex_mem_shed_all(m, o, 0);   // 1) adelgazar, sin cerrar nada
    o->sample_now(o->ctx);
    if (flexMemLevel(o->snap(o->ctx)) != FLEXMEM_LV_CRITICAL) {
        return 0;
    }
    for (int guard = 0; guard < m->napps; guard++) {   // 2) ultimo recurso, acotado
        int victim = -1;
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < m->napps; i++) {
            if (m->state[i] != FLEX_ALIFE_SUSPENDED || o->bg_busy(o->ctx, i)) {
                continue;
            }
            if (m->seen_ms[i] <= oldest) {
                oldest = m->seen_ms[i];
                victim = i;
            }
        }
        if (victim < 0) {
            break;   // todas protegidas: no se fuerza nada
        }
        if (!o->terminate(o->ctx, victim)) {
            break;   // no se pudo guardar: mejor conservar
        }
        flex_mem_forget(m, victim);
        o->drop_card(o->ctx, victim);
        closed++;
        o->sample_now(o->ctx);
        if (flexMemLevel(o->snap(o->ctx)) != FLEXMEM_LV_CRITICAL) {
            break;
        }
    }
    return closed;
}
