# Flex Account (N3): el nucleo (flex_account_model.c: clasificar, esperas, base64url, sobres firmados ES256, credencial), el servicio (flex_account.c: enlace por codigo,
# validacion de la sesion, persistencia, desvincular) y las decisiones de la pantalla (flex_account_ui_model.c) son C portable que prueba tests/host contra
# FlexOS_Account.cpp y FlexOS_Account_Bridge.h del Arduino. Lo de la placa (esp_http_client con las raices de flex_net, NVS "flexacct", la tarea de fondo) es
# flex_account_port.c y NO esta probado en hardware.
idf_component_register(
    SRCS "src/flex_account_model.c" "src/flex_account.c" "src/flex_account_ui_model.c" "src/flex_account_port.c"
    INCLUDE_DIRS "include"
    REQUIRES flex_core
    PRIV_REQUIRES flex_net json mbedtls nvs_flash esp_http_client esp-tls esp_timer esp_hw_support esp_app_format lwip freertos log heap
)
# Las raices de confianza de Flex Cloud / Flex Account (11 autoridades, copia de FlexOS_CloudTLS.cpp): sin ellas el servicio no conecta
set(FLEX_CLOUD_ROOTS "${CMAKE_CURRENT_LIST_DIR}/../flex_net/certs/cloud_root_bundle.pem")
if(EXISTS "${FLEX_CLOUD_ROOTS}")
    target_add_binary_data(${COMPONENT_LIB} "${FLEX_CLOUD_ROOTS}" TEXT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FLEX_ACCOUNT_HAVE_ROOTS=1)
endif()
