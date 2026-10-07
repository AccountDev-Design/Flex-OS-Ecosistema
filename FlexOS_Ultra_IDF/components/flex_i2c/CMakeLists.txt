idf_component_register(
    SRCS "src/flex_i2c.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_board esp_driver_i2c esp_driver_gpio esp_timer
)
