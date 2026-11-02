// Arranque (docs/spec/01a §2.3-2.4): motivo del reinicio, cuando sale la banda forense y la curva del splash.
#include <string.h>
#include "flex_boot_model.h"
#include "flex_test.h"

const char *ref_boot_reason(int r);
int ref_boot_alpha(unsigned long e);
int ref_boot_dot(unsigned long ms);

void test_boot(void)
{
    int bad = 0;
    for (int r = -2; r <= 20; r++) {
        bad += strcmp(ref_boot_reason(r), flex_boot_reason_str(r)) != 0;
        // la banda sale con cualquier motivo que no sea POWERON, SW ni DEEPSLEEP
        bool want = !(r == 1 || r == 3 || r == 8);
        bad += flex_boot_banner_wanted(r) != want;
    }
    CHECK_EQ_I(bad, 0);
    CHECK(strcmp(flex_boot_reason_str(4), "PANIC (crash)") == 0 && strcmp(flex_boot_reason_str(9), "BROWNOUT (voltaje)") == 0);
    int ad = 0;
    for (unsigned long e = 0; e <= 3200; e++) {
        ad += ref_boot_alpha(e) != flex_boot_alpha((uint32_t)e);
        ad += ref_boot_dot(e * 7) != flex_boot_dot((uint32_t)(e * 7));
    }
    CHECK_EQ_I(ad, 0);
    CHECK_EQ_I(flex_boot_alpha(0), 0);
    CHECK_EQ_I(flex_boot_alpha(1000), 255);
    CHECK_EQ_I(flex_boot_alpha(2599) >= 0, 1);
    CHECK_EQ_I(flex_boot_alpha(2600), -1);
}
