#include "flex_boot_model.h"

// valores de esp_reset_reason_t (esp_system.h); flex_safeboot.c ya comprueba que coinciden
enum { R_POWERON = 1, R_SW = 3, R_PANIC = 4, R_INT_WDT = 5, R_TASK_WDT = 6, R_WDT = 7, R_DEEPSLEEP = 8, R_BROWNOUT = 9 };

const char *flex_boot_reason_str(int reason)
{
    switch (reason) {
    case R_POWERON: return "POWERON";
    case R_SW: return "SW";
    case R_PANIC: return "PANIC (crash)";
    case R_INT_WDT: return "INT_WDT";
    case R_TASK_WDT: return "TASK_WDT";
    case R_WDT: return "WDT";
    case R_BROWNOUT: return "BROWNOUT (voltaje)";
    case R_DEEPSLEEP: return "DEEPSLEEP";
    default: return "OTRO";
    }
}

bool flex_boot_banner_wanted(int reason)
{
    return reason != R_POWERON && reason != R_SW && reason != R_DEEPSLEEP;
}

int flex_boot_alpha(uint32_t e)
{
    if (e < 600) {
        return (int)(e * 255 / 600);
    }
    if (e < 2000) {
        return 255;
    }
    if (e < FLEX_BOOT_SPLASH_MS) {
        return (int)(255 - (e - 2000) * 255 / 600);
    }
    return -1;
}

int flex_boot_dot(uint32_t ms)
{
    return (int)((ms / FLEX_BOOT_DOT_MS) % 3);
}
