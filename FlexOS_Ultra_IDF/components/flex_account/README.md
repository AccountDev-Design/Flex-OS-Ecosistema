# flex_account

Flex Account de Flex OS Ultra: enlazar este P4 con una cuenta por código de dispositivo, validar la sesión y desvincular (referencia: `FlexOS_Account.h/.cpp`,
`FlexOS_Account_Bridge.h` y `FlexOS_CloudTLS.h/.cpp`; spec `docs/spec/01a` §3.3 y `docs/spec/03b` §16).

| Capa | Archivo | Qué es | Dónde se prueba |
|---|---|---|---|
| Núcleo | `src/flex_account_model.c` (+ `flex_net/src/flex_tls.c`) | C puro: clasificar la respuesta de la sesión, espera progresiva, longitud de cadenas de la NVS, base64url, sobre firmado ES256 con la clave anclada, cargas de código y de estado, credencial con su SHA-256, textos de fallo TLS | host, contra las funciones de `FlexOS_Account.cpp` / `FlexOS_CloudTLS.cpp` (103 553 comprobaciones; con **OpenSSL y con mbedTLS 3.6 de ESP-IDF compilado para el PC**, que es el backend que corre en el P4) |
| Servicio | `src/flex_account.c` | El flujo entero sobre `flex_account_ops_t` (tiempo, Wi-Fi, NVS, HTTP, tarea, cerrojo inyectados): enlace con sondeo, cancelar y caducar, guardado a prueba de cortes, validación con flanco de Wi-Fi, espera progresiva, recuperar tras reiniciar, desvincular | host: **lockstep** contra `FlexOS_Account.cpp` entero sobre el mismo mundo simulado (red, NVS, reloj, memoria, tareas), tras cada acción el registro completo —foto, API, credencial, reloj, peticiones con cabeceras, NVS entera, tareas— tiene que ser idéntico; escenarios con nombre y miles de guiones aleatorios; invariancia ante la vuelta de `millis()`; prueba con hilos reales (TSan + ASan) |
| Decisiones de la pantalla | `src/flex_account_ui_model.c` | etiquetas, colores del vínculo y **qué hace cada toque** (`flex_account_hit`) | host, contra `FlexOS_Account_Bridge.h` compilado tal cual: 29,7 millones de comprobaciones sobre una rejilla de toques por cada combinación de estado / confirmación / destino / Wi-Fi / foto |
| Pantalla | `flex_ui/src/shell/flex_account_ui.c` | la pantalla LVGL (sin vincular, solicitando, código, vinculada, volver a iniciar sesión, error, desvincular con confirmación) | simulador, escena `cuenta`: el dibujo coincide **primitiva a primitiva** con lo que pinta `accountRender` de Arduino en 3 360 combinaciones |
| Transporte | `src/flex_account_port.c` | `esp_http_client` con las raíces de Flex Cloud, NVS `flexacct`, MAC de eFuse, tarea en el core 1 | host contra dobles de ESP-IDF (`test_account_port.c`); **sin hardware** |

## Qué conserva de Arduino
* Credencial de 32 bytes aleatorios del P4 (base64url, 43 caracteres); al servidor solo llega su SHA-256. Las respuestas del servicio van firmadas con ES256 y se
  verifican con una clave pública anclada (`FLEX_ACCOUNT_KEY_ID`).
* Estados: sin red **no** es sin cuenta (`LINKED_OFFLINE`); un 401 sin código de error de la API (la página de un proxy) no desvincula; servicio caído = `NETWORK_UNAVAILABLE`;
  `AUTH_REQUIRED` / `TOKEN_EXPIRED` solo si el servidor lo dice con su código.
* NVS `flexacct` con las mismas claves y tipos que `Preferences` de Arduino (`token`, `address`, `display`: cadenas; `linked`, `authst`: u8; `installId`: u32): una placa que
  cambia de firmware conserva su cuenta. `linked` se escribe LA ÚLTIMA (un corte a mitad se lee como "cuenta incompleta", nunca como cuenta válida).
* Espera tras un fallo de red 30 s, 60 s, 2 min … tope de 15 min; revalida cada 6 h; el Wi-Fi que entra y sale no provoca una validación por flanco (asentamiento de 3 s,
  intervalo mínimo de 30 s); un 401 de Flex Cloud revalida como mucho una vez por minuto.
* `forget_local` borra la NVS (una vez, y solo si había algo), pone un id de instalación nuevo y descarta lo que una validación en vuelo escriba después.

## Diferencias deliberadas con Arduino
1. **TLS siempre validado** (raíces de `flex_net/certs/cloud_root_bundle.pem`, idénticas a las de `FlexOS_CloudTLS.cpp`: lo comprueba `test_account`). Arduino pedía el código
   de enlace con `setInsecure()` y confiaba en la firma ES256; aquí el TLS sin verificar no existe (como en la OTA). Consecuencia: **enlazar necesita la hora de red ajustada**
   (con el reloj en 1970 el certificado "aún no es válido": sale como fallo de certificado y "Reintentar" funciona en cuanto hay hora).
2. El cuerpo de la respuesta se lee entero (≤ 8 KB + 1) dentro de `http_send`; lo observable es lo mismo.
3. Un carrera conocida, igual que en Arduino: desvincular con un enlace vivo puede dejar un último fotograma `CODE_READY` sin código; la pantalla no dibuja un código vacío.

## Limitaciones conocidas
* Sin hardware: el transporte solo se ha probado contra dobles. Faltan el Wi-Fi (X3), un servidor real y la hora de red (ver `ESP_IDF_HARDWARE_REQUIREMENTS.md`).
* Flex Cloud (N4) todavía no existe: el gancho `flex_cloud_account_unlinked` (weak) no hace nada; el configurador de Wi-Fi (`flex_wifi_oobe_enter`, weak) tampoco: los
  botones "Conectar Wi-Fi" cuentan la petición y no abren nada hasta que X3 exista.
* El paso de Flex Account de la primera configuración (S2b) usa `flex_account_ui_open(FLEX_ACC_RET_OOBE, done)`; falta engancharlo en el OOBE (`flex_oobe_account_step`).
