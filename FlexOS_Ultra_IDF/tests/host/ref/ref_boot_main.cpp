// Referencia: resetReasonStr y la curva de opacidad del splash de la version Arduino (Home.h:246-320).
#include "ref_prelude.h"
enum { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT,
       ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
static int g_reason;
static int esp_reset_reason() { return g_reason; }
#include "ref_boot.inc"
extern "C" const char *ref_boot_reason(int r) { g_reason = r; return resetReasonStr(); }
// splashTick (Home.h:303-306), transcrito: e = millis() - splashStart; -1 = termina
extern "C" int ref_boot_alpha(unsigned long e)
{
    uint8_t a;
    if (e < 600) a = (uint8_t)(e * 255 / 600);
    else if (e < 2000) a = 255;
    else if (e < 2600) a = (uint8_t)(255 - (e - 2000) * 255 / 600);
    else return -1;
    return a;
}
// splashFrame: phase = (millis() / 320) % 3
extern "C" int ref_boot_dot(unsigned long ms) { return (int)((ms / 320) % 3); }
