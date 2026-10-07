idf_component_register(
    SRCS "src/flex_core.c" "src/flex_metrics.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES esp_app_format esp_psram spi_flash esp_system hal log
)
