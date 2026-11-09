# OTA (N5a): el modelo (flex_ota_model.c, C puro) y el motor (flex_ota.c, portable sobre flex_ota_ops_t) los prueba tests/host contra Arduino; flex_ota_port.c
# es lo de la placa (esp_http_client, esp_ota, mbedtls, rollback del bootloader).
idf_component_register(
    SRCS "src/flex_ota_model.c" "src/flex_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES flex_core
)
