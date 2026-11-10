# OTA (N5): el modelo (flex_ota_model.c, C puro) y el motor (flex_ota.c, portable sobre flex_ota_ops_t) los prueba tests/host contra Arduino;
# flex_ota_port.c es lo de la placa (esp_http_client, esp_ota, mbedtls, rollback del bootloader) y NO esta probado en hardware.
#
# El certificado raiz del servidor de actualizaciones se FIJA: si existe certs/ota_root_ca.pem se embebe en el firmware; si no, el motor no conecta
# ("Falta el certificado del servidor", igual que Arduino con FLEXOS_OTA_ROOT_CA vacio). Aceptar cualquier certificado convertiria la actualizacion
# en la entrada mas facil del dispositivo.
idf_component_register(
    SRCS "src/flex_ota_model.c" "src/flex_ota.c" "src/flex_ota_port.c"
    INCLUDE_DIRS "include"
    REQUIRES flex_core
    PRIV_REQUIRES flex_net flex_storage flex_system app_update esp_http_client esp_app_format esp_timer mbedtls freertos log heap
)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/certs/ota_root_ca.pem")
    target_add_binary_data(${COMPONENT_LIB} "certs/ota_root_ca.pem" TEXT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FLEX_OTA_HAVE_CA=1)
endif()
