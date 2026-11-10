# Flex Account (N3): el nucleo (flex_account_model.c: clasificar, esperas, base64url, sobres firmados ES256, credencial) y el servicio (flex_account.c: enlace por codigo,
# validacion de la sesion, persistencia, desvincular) son C portable que prueba tests/host contra FlexOS_Account.cpp del Arduino. Lo de la placa (esp_http_client,
# NVS "flexacct", la tarea de fondo) es flex_account_port.c y NO esta probado en hardware.
idf_component_register(
    SRCS "src/flex_account_model.c" "src/flex_account.c" "src/flex_account_ui_model.c"
    INCLUDE_DIRS "include"
    REQUIRES flex_core
    PRIV_REQUIRES flex_net json mbedtls
)
