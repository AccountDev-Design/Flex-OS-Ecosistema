idf_component_register(
    SRCS "src/flex_touch.c" "src/gt911.c" "src/flex_touch_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
    PRIV_REQUIRES flex_board flex_core flex_i2c esp_driver_gpio esp_timer esp_system
)
