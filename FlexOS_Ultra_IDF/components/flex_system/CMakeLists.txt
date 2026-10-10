idf_component_register(
    SRCS "src/flex_system.c" "src/flex_safeboot_model.c" "src/flex_safeboot.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES flex_core flex_storage esp_driver_tsens espcoredump esp_psram esp_timer esp_system heap
)
