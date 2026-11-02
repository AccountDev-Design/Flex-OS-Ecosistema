// Flex OS Ultra · memoria de las apps: medir, soltar, admitir, desalojar (docs/spec/01c §17).
//
// Tres capas separadas a proposito (Core.h:31-63): MEDIR (aqui: solo heap_caps_* y el estado del
// almacenamiento, nunca dentro del dibujo), DECIDIR (FlexOS_Mem.cpp de Arduino en flex_portable y
// flex_mem_model.c, probados contra el codigo de Arduino) y SOLTAR (el registro de caches
// reconstruibles de abajo y el gancho shed() de cada app). Nunca se toca un dato del usuario.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef FLEX_SIM
#include "esp_heap_caps.h"
#endif
#include "flex_app.h"
#include "flex_i18n.h"
#include "flex_mem_model.h"
#include "flex_reset.h"
#include "flex_safeboot.h"
#include "flex_shell.h"
#include "flex_storage.h"
#include "flex_theme.h"
#include "flex_touch_lvgl.h"
#include "flex_wallmgr.h"

#define MEM_TICK_MS      1000
#define BLOCK_TIGHT_MS   2000     // mayor bloque contiguo: cada 2 s con la memoria apretada...
#define BLOCK_RELAXED_MS 10000    // ...y cada 10 s con holgura y sin interaccion (la llamada es cara)
#define MB(x) ((uint32_t)((x) * 1024u * 1024u))

static flex_mem_t M;
static FlexMemSnap s_snap;
static bool s_snap_ok;
static uint32_t s_block_ms;
static lv_timer_t *s_timer;

// ---- registro de caches reconstruibles (memShedSystem) ---------------------------------------
#define CACHES_MAX 12
static struct {
    const char *name;
    size_t (*shed)(void);
    bool (*guard)(void);     // false = no se puede soltar ahora (se esta viendo)
} s_cache[CACHES_MAX];
static int s_ncache;

void flex_mem_register_cache(const char *name, size_t (*shed)(void), bool (*guard)(void))
{
    if (s_ncache < CACHES_MAX && shed) {
        s_cache[s_ncache].name = name;
        s_cache[s_ncache].shed = shed;
        s_cache[s_ncache].guard = guard;
        s_ncache++;
    }
}

// ---- medir ---------------------------------------------------------------------------------
#ifdef FLEX_SIM
// En el simulador la memoria es la que fija la escena (flex_mem_sim_set)
static FlexMemSnap s_sim;
void flex_mem_sim_set(const FlexMemSnap *s) { s_sim = *s; }
static uint32_t free_psram(void) { return s_sim.psFree; }
static void measure(FlexMemSnap *s, bool block) { (void)block; *s = s_sim; }
#else
static uint32_t free_psram(void) { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static void measure(FlexMemSnap *s, bool block)
{
    uint32_t used_peak = s->psPeakUsed;
    s->psTotal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s->psFree = free_psram();
    if (block || !s_snap_ok) {
        s->psLargest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
    uint32_t used = s->psTotal > s->psFree ? s->psTotal - s->psFree : 0;
    s->psPeakUsed = used > used_peak ? used : used_peak;
    s->inTotal = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s->inFree = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s->inMin = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    flex_storage_status_t st;
    flex_storage_get_status(&st);
    s->fsTotal = (uint32_t)st.fs_total;
    s->fsUsed = (uint32_t)st.fs_used;
    s->fsValid = st.fs_mounted && st.fs_total > 0;
}
#endif

static void sample_now(void *ctx)
{
    (void)ctx;
    measure(&s_snap, true);   // para las DECISIONES siempre con bloque (memSampleNow)
    s_snap_ok = true;
    s_block_ms = lv_tick_get();
}
static const FlexMemSnap *snap(void *ctx) { (void)ctx; return &s_snap; }
static uint32_t free_cb(void *ctx) { (void)ctx; return free_psram(); }

// ---- soltar --------------------------------------------------------------------------------
static uint32_t shed_system(void *ctx)
{
    (void)ctx;
    uint32_t before = free_psram();
    for (int i = 0; i < s_ncache; i++) {
        if (!s_cache[i].guard || s_cache[i].guard()) {
            s_cache[i].shed();
        }
    }
    uint32_t after = free_psram();
    return after > before ? after - before : 0u;   // bytes de PSRAM MEDIDOS, no la cuenta de cada cache
}

static bool bg_busy(void *ctx, int app)
{
    (void)ctx;
    const flex_app_def_t *d = flex_app_def(app);
    return d && (d->flags & FLEX_APP_BG_KEEP) && d->ops && d->ops->bg_work && d->ops->bg_work();
}
static bool has_shed(void *ctx, int app)
{
    (void)ctx;
    const flex_app_def_t *d = flex_app_def(app);
    return d && d->ops && d->ops->shed;
}
static size_t app_shed(void *ctx, int app)
{
    (void)ctx;
    return flex_app_def(app)->ops->shed();
}
__attribute__((weak)) bool flex_net_radio_busy(void) { return false; }   // el enlace con el C6 se levanta (Fase 6)
static bool radio_busy(void *ctx) { (void)ctx; return flex_net_radio_busy(); }
static bool terminate_cb(void *ctx, int app) { (void)ctx; return flex_app_terminate_ex(app, false); }
static void drop_card(void *ctx, int app) { (void)ctx; (void)app; }   // flex_app_terminate ya quita su tarjeta
static void notify_cb(void *ctx, const char *t, const char *s) { (void)ctx; flex_notify(t, s); }

static const flex_mem_ops_t OPS = {free_cb, sample_now, snap, shed_system, bg_busy, has_shed, app_shed,
                                   radio_busy, terminate_cb, drop_card, notify_cb, NULL};

// El estado de cada app vive en el marco (flex_shell.c): se copia antes de decidir
static void sync_apps(void)
{
    for (int i = 0; i < FLEX_APP_N; i++) {
        int life = flex_app_life(i);   // 0 cerrada, 1 en marcha, 2 suspendida
        M.state[i] = (uint8_t)(life == 2 ? FLEX_ALIFE_SUSPENDED : life == 1 ? FLEX_ALIFE_RUNNING : FLEX_ALIFE_CLOSED);
        M.seen_ms[i] = flex_app_last_used(i);
    }
}

uint32_t flex_mem_shed_all_now(void)   // memShedAll(0): Optimizar y el alivio
{
    sync_apps();
    return flex_mem_shed_all(&M, &OPS, 0);
}

uint32_t flex_mem_shed_system_now(void)
{
    return shed_system(NULL);
}

uint32_t flex_mem_shed_app_now(int id)
{
    sync_apps();
    return flex_mem_shed_app(&M, &OPS, id);
}

void flex_mem_sample_now(void)
{
    sample_now(NULL);
}

const FlexMemSnap *flex_mem_snap(void)
{
    if (!s_snap_ok) {
        sample_now(NULL);
    }
    return &s_snap;
}

// ---- admision al abrir (memAdmitApp / appDenyMemory) --------------------------------------------
bool flex_mem_admit_app(int id)
{
    const flex_app_def_t *d = flex_app_def(id);
    if (!d) {
        return true;
    }
    sync_apps();
    int v = flex_mem_admit(&M, &OPS, d->weight);
    if (v == FLEXMEM_OK) {
        return true;
    }
    char t[72];
    snprintf(t, sizeof(t), "%s no se abre ahora", flex_app_name(id));
    flex_notify(t, flex_mem_deny_text(v));
    printf("[MEM] apertura denegada: %s (veredicto %d, libre %u KB, bloque %u KB)\n", flex_app_name(id), v,
           (unsigned)(s_snap.psFree / 1024u), (unsigned)(s_snap.psLargest / 1024u));
    return false;
}

// ---- huella medida por app -------------------------------------------------------------------
uint32_t flex_mem_free_psram(void)
{
    return free_psram();
}
void flex_mem_app_commit(int id, uint32_t free_before)
{
    flex_mem_commit(&M, &OPS, id, free_before);
}
void flex_mem_app_forget(int id)
{
    flex_mem_forget(&M, id);
}
bool flex_mem_app_known(int id)
{
    return flex_mem_has(&M, id);
}
uint32_t flex_mem_app_bytes(int id)
{
    return flex_mem_bytes(&M, id);
}
bool flex_mem_app_shed(int id)
{
    return id >= 0 && id < FLEX_MEM_APPS && M.shed[id];
}
void flex_mem_app_resumed(int id)
{
    if (id >= 0 && id < FLEX_MEM_APPS) {
        M.shed[id] = false;   // al reanudarla ya tiene lo suyo otra vez
    }
}

// ---- desalojo por presupuesto (se llama en cada suspension) ------------------------------------------
void flex_mem_enforce(void)
{
    sync_apps();
    flex_mem_enforce_budget(&M, &OPS);
}

// ---- tick: medir y avisar (memTick + memAlertTick) --------------------------------------------------
static bool act_ok(void)
{
    flex_shell_state_t st = flex_shell_state();
    return (st == FLEX_SH_HOME || st == FLEX_SH_APP) && !flex_shell_transition_active() && !flex_optimize_active() &&
           !flex_net_radio_busy();
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    // el mayor bloque contiguo cuesta: cada 2 s apretado, cada 10 s con holgura y sin interaccion
    bool tight = !s_snap_ok || s_snap.psFree < MB(11) || M.alerts.level >= FLEXMEM_LV_NOTICE;
    bool busy_ui = flex_touch_arb()->t.down || flex_shell_transition_active();
    bool block = now - s_block_ms >= (tight ? BLOCK_TIGHT_MS : BLOCK_RELAXED_MS) && (tight || !busy_ui);
    measure(&s_snap, block);
    s_snap_ok = true;
    if (block) {
        s_block_ms = now;
    }
    const flex_fr_marker_t *mk = flex_reset_marker();
    bool blocked = flex_safe_mode() || (mk && mk->pending);
    sync_apps();
    flex_mem_alert_tick(&M, &OPS, now, blocked, act_ok());
    // el modo visual eficiente (temporal) se apaga solo cuando el nivel SOSTENIDO vuelve a OK
    if (flex_look()->eff_mode && M.alerts.level == FLEXMEM_LV_OK) {
        flex_theme_set_eff_mode(false);
    }
}

static size_t shed_wall(void) { return flex_wallmgr_shed(); }
static bool guard_wall(void)
{
    flex_shell_state_t st = flex_shell_state();
    return st == FLEX_SH_HOME || st == FLEX_SH_APP;   // no en el bloqueo, la clave, Recientes, la caja ni el apagado
}
size_t flex_recents_shed(void);

void flex_mem_start(void)
{
    flex_mem_init(&M, FLEX_APP_N);
    flex_mem_register_cache("fondos", shed_wall, guard_wall);
    flex_mem_register_cache("recientes", flex_recents_shed, NULL);
    if (!s_timer) {
        s_timer = lv_timer_create(tick_cb, MEM_TICK_MS, NULL);
    }
}

int flex_mem_level(void)
{
    return M.alerts.level;
}
