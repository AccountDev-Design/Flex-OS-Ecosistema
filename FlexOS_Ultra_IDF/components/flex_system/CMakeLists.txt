idf_component_register(
    SRCS "src/flex_system.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_core flex_storage esp_driver_tsens espcoredump esp_psram esp_timer esp_system heap
)
