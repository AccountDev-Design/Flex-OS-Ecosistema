// Flex OS Ultra · Modo seguro, modelo (C puro). Ver flex_safeboot.h.
#include "flex_safeboot.h"

bool flex_safe_abnormal(int reason)
{
    // Arduino: PANIC, INT_WDT, TASK_WDT, WDT y BROWNOUT. El P4 informa ademas del
    // bloqueo de CPU y del fallo de alimentacion: tambien son anormales.
    switch (reason) {
    case FLEX_RST_PANIC:
    case FLEX_RST_INT_WDT:
    case FLEX_RST_TASK_WDT:
    case FLEX_RST_WDT:
    case FLEX_RST_BROWNOUT:
    case FLEX_RST_PWR_GLITCH:
    case FLEX_RST_CPU_LOCKUP:
        return true;
    default:
        return false;
    }
}

flex_safe_eval_t flex_safe_eval(int reason, int saved_fails, int saved_cause, bool have_cause)
{
    flex_safe_eval_t r = {0};
    int f = saved_fails < 0 ? 0 : saved_fails > FLEX_SAFE_FAILS_CAP ? FLEX_SAFE_FAILS_CAP : saved_fails;
    r.abnormal = flex_safe_abnormal(reason);
    if (r.abnormal) {
        r.fails = f < FLEX_SAFE_FAILS_CAP ? f + 1 : f;
        r.cause = reason;
        r.write = true;
    } else {
        r.fails = f;   // un arranque normal NO limpia: lo hace el arranque estable
        r.cause = have_cause ? saved_cause : reason;   // la causa que disparo la cadena
    }
    r.safe = r.fails >= FLEX_SAFE_FAIL_MAX;
    return r;
}

bool flex_safe_stable_clear(bool safe, int fails, uint32_t uptime_ms)
{
    return !safe && fails > 0 && uptime_ms >= FLEX_SAFE_STABLE_MS;
}

const char *flex_safe_cause_text(int cause)
{
    switch (cause) {
    case FLEX_RST_PANIC: return "Fallo del sistema (crash)";
    case FLEX_RST_TASK_WDT: return "Watchdog de tarea (TASK_WDT)";
    case FLEX_RST_INT_WDT: return "Watchdog de interrupci\xC3\xB3n (INT_WDT)";
    case FLEX_RST_WDT: return "Watchdog del chip";
    case FLEX_RST_BROWNOUT: return "Ca\xC3\xAD" "da de tensi\xC3\xB3n (brownout)";
    case FLEX_RST_CPU_LOCKUP: return "Bloqueo de la CPU (lockup)";
    case FLEX_RST_PWR_GLITCH: return "Fallo de alimentaci\xC3\xB3n";
    default: return "Reinicio inesperado";
    }
}
