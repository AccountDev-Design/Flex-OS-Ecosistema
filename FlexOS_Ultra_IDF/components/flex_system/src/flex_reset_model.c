// Flex OS Ultra · restablecimiento de fabrica, modelo (C puro). Ver flex_reset.h.
#include "flex_reset.h"

static const char *const NAMES[FLEX_FR_STEPS] = {
    "Preparando el dispositivo",
    "Borrando credenciales y tokens",
    "Cerrando la sesi\xC3\xB3n remota",
    "Borrando datos de aplicaciones",
    "Borrando archivos del usuario",
    "Borrando ajustes del sistema",
    "Restaurando valores de f\xC3\xA1" "brica",
};

flex_fr_boot_t flex_fr_boot_decision(const flex_fr_marker_t *m)
{
    if (!m || !m->pending) {
        return FLEX_FR_BOOT_NORMAL;
    }
    if (m->ver != FLEX_FR_VER) {
        return FLEX_FR_BOOT_DISCARD;
    }
    if (m->stage == FLEX_FR_DONE) {
        return FLEX_FR_BOOT_DONE;
    }
    if (m->stage >= FLEX_FR_ARMED && m->stage <= FLEX_FR_FAIL) {
        return FLEX_FR_BOOT_RESUME;
    }
    return FLEX_FR_BOOT_DISCARD;   // etapa imposible: NVS corrupta
}

bool flex_fr_arm(const flex_fr_ops_t *ops, flex_fr_marker_t *m)
{
    flex_fr_marker_t n = {.pending = true, .ver = FLEX_FR_VER, .stage = FLEX_FR_ARMED, .err = 0};
    if (!ops->save(ops->ctx, &n)) {
        // Nada borrado: el fallo lo dice la pantalla; "Reintentar" vuelve a armar.
        *m = (flex_fr_marker_t){.pending = false, .ver = FLEX_FR_VER, .stage = FLEX_FR_FAIL, .err = 0};
        return false;
    }
    *m = n;
    return true;
}

int flex_fr_step(const flex_fr_ops_t *ops, flex_fr_marker_t *m)
{
    if (!m->pending || m->stage < FLEX_FR_ARMED || m->stage > FLEX_FR_DEFAULTS) {
        return m->stage;
    }
    int st = m->stage;
    if (ops->run(ops->ctx, st)) {
        flex_fr_marker_t n = *m;
        n.stage = st + 1;
        if (ops->save(ops->ctx, &n)) {
            *m = n;
            return m->stage;
        }
        // La etapa se hizo pero no se pudo anotar: nunca confiar solo en la RAM
    }
    m->stage = FLEX_FR_FAIL;
    m->err = st;
    ops->save(ops->ctx, m);   // si tampoco se puede, el arranque retomara la etapa anotada
    return m->stage;
}

bool flex_fr_retry(const flex_fr_ops_t *ops, flex_fr_marker_t *m)
{
    if (m->stage != FLEX_FR_FAIL) {
        return false;
    }
    if (!m->pending || m->err < FLEX_FR_ARMED || m->err > FLEX_FR_DEFAULTS) {
        return flex_fr_arm(ops, m);   // no se llego a armar: nada borrado, se empieza de cero
    }
    flex_fr_marker_t n = *m;
    n.stage = m->err;
    n.err = 0;
    if (!ops->save(ops->ctx, &n)) {
        return false;
    }
    *m = n;
    return true;
}

int flex_fr_step_index(int stage)
{
    if (stage < FLEX_FR_ARMED) {
        return 1;
    }
    return stage > FLEX_FR_DEFAULTS ? FLEX_FR_STEPS : stage;
}

const char *flex_fr_stage_name(int stage)
{
    return stage >= FLEX_FR_ARMED && stage <= FLEX_FR_DEFAULTS ? NAMES[stage - 1] : "";
}
