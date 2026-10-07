idf_component_register(
    SRCS "src/flex_display.c" "src/flex_display_lvgl.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES flex_board flex_core esp_lcd esp_driver_ledc esp_driver_gpio esp_hw_support esp_timer
)
