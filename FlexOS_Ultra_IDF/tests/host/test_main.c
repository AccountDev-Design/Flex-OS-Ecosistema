#include "flex_test.h"

int g_fails, g_checks;

int main(void)
{
    test_flex_kv();
    test_flex_kv_threads();
    test_fs_path();
    test_fs_wipe();
    test_flip_rows();
    test_wallpaper();
    test_glass();
    test_home();
    test_home_load();
    test_touch_arb();
    test_drawer();
    test_kb();
    test_recents();
    test_qs();
    test_notif();
    test_poweroff();
    test_safeboot();
    test_reset();
    test_oobe();
    test_kiosk();
    test_home_edit();
    test_homecfg();
    test_session();
    test_mem_model();
    test_boot();
    test_calc();
    test_layout();
    test_fsnames();
    test_fsops();
    test_fk();
    test_ned();
    test_clip();
    test_notes();
    test_paint();
    test_raster();
    test_set();
    test_kbs();
    test_chrono();
    test_fsscan();
    test_stor();
    test_files();
    printf("%s: %d comprobaciones, %d fallos\n", g_fails ? "FALLO" : "OK", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
