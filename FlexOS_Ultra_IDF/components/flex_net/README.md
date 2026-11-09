# flex_net

**Estado:** hora por red (N1a): modelo NTP, servicio, transporte UDP y trabajador. Lo demas (cliente HTTP/TLS comun, raices de confianza, trabajadores de
red en cola) llega con la nube y las actualizaciones.

Referencia funcional en la version Arduino (`FlexOS_Ultra/`): `FlexOS_Ultra_NTP.h` (hora), FlexOS_CloudTLS.*, FlexOS_HttpShare.*, FlexOS_HttpSink.h.

* `flex_ntp_model.c`: C puro (paquete SNTP, lectura de la respuesta, espera progresiva, textos de Ajustes); `tests/host` lo compara con las funciones de
  Arduino compiladas tal cual.
* `flex_ntp.c`: el servicio; todo lo que toca el sistema entra por `flex_ntp_env_t`.
* `flex_ntp_udp.c`: sockets BSD (lwIP en la placa, POSIX en las pruebas, con un servidor UDP real en `127.0.0.1`).
* `flex_ntp_port.c`: el reloj (`flex_time`), la NVS (`flexos_time`: `epoch`, `lastsync`), el modo avion, el modo seguro y el trabajador.
* Sin Wi-Fi (X3) no hay red: el estado dice "Esperando Wi-Fi". El servicio de Wi-Fi avisa con `flex_net_set_online()`.
