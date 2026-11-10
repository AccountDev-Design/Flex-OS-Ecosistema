# Red: por ahora la hora por red (NTP). El modelo (flex_ntp_model.c) es C puro y lo prueban tests/host contra Arduino; el servicio (flex_ntp.c) es
# portable y entra por flex_ntp_env_t; el transporte UDP (flex_ntp_udp.c) usa sockets BSD; flex_ntp_port.c es lo de la placa.
# El cliente HTTP/TLS comun (esp_http_client + esp-tls) llega con la nube (N4) y las actualizaciones (N5).
idf_component_register(
    SRCS "src/flex_ntp_model.c" "src/flex_ntp.c" "src/flex_ntp_udp.c" "src/flex_ntp_port.c" "src/flex_tls.c"
    INCLUDE_DIRS "include"
    REQUIRES flex_core
    PRIV_REQUIRES flex_storage flex_system freertos esp_timer lwip log
)
