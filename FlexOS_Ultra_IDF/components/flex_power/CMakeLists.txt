# Apagado completo (deep sleep) y filtro de encendido. El modelo
# (flex_poweroff_model.c) es C puro y lo prueban tests/host.
idf_component_register(
    SRCS "src/flex_poweroff_model.c" "src/flex_poweroff.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_board flex_display flex_i2c flex_storage flex_touch esp_driver_gpio esp_timer esp_system
)
