idf_component_register(
    SRCS "src/flex_ui.c" "src/diag/flex_ui_diag.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES lvgl__lvgl flex_core
    PRIV_REQUIRES flex_display flex_touch flex_i2c flex_storage esp_timer heap esp_system
)
