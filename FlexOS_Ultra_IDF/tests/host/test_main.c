#include "flex_test.h"

int g_fails, g_checks;

int main(void)
{
    test_flex_kv();
    test_flex_kv_threads();
    test_fs_path();
    test_flip_rows();
    test_wallpaper();
    test_glass();
    test_home();
    test_home_load();
    test_touch_arb();
    test_drawer();
    test_kb();
    printf("%s: %d comprobaciones, %d fallos\n", g_fails ? "FALLO" : "OK", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
