idf_component_register(
    SRCS "src/flex_core.c" "src/flex_metrics.c" "src/flex_bus.c" "src/flex_inbox.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_event
    PRIV_REQUIRES esp_app_format esp_psram spi_flash esp_system hal log
)
