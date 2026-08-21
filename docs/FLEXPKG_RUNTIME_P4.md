# Flex Package Runtime para FlexOS Ultra P4

Esta implementación conecta el formato binario `FLXP v1` del Flex SDK con el firmware real de FlexOS Ultra para ESP32-P4.

## Archivos

- `FlexOS_Package.h/.cpp`: lector FLXP, SHA-256, ECDSA P-256, extracción segura, registro y actualización transaccional.
- `FlexOS_Runtime.h/.cpp`: runtime declarativo `flex-ui-1` para pantallas, textos, botones, navegación y notificaciones autorizadas.
- `FlexOS_Store.h/.cpp`: catálogo oficial firmado, descarga en tarea FreeRTOS, comprobación del SHA publicado e instalación.
- `FlexOS_Store_Bridge.h`: interfaz táctil de Flex Store y adaptación a las primitivas gráficas de `FlexOS_Ultra.ino`.

## Instalación y actualización

1. Flex Store descarga el archivo a `/FlexApps/.download.flexpkg` sin bloquear el hilo gráfico.
2. Comprueba que el catálogo fue firmado por la clave oficial incrustada en el firmware.
3. Comprueba el SHA-256 del paquete indicado por ese catálogo.
4. El instalador valida encabezado, tamaños, JSON canónico, rutas, hashes de todos los archivos, huella del desarrollador y firma ECDSA P-256.
5. Extrae a `/FlexApps/<packageId>/.stage`.
6. Comprueba que el entrypoint `flex-ui-1` puede abrirse.
7. Mueve la versión activa a `.old` y activa `.stage` mediante `rename`.
8. Si el cambio falla, restaura `.old`. Si funciona, elimina `.old`; queda únicamente la versión reciente.

Al arrancar, `flexPkgBegin()` elimina stages incompletos y recupera `.old` si un corte de energía ocurrió en medio del cambio.

## Identidad del desarrollador

La primera instalación guarda `developerKeySha256`. Una actualización del mismo `packageId` sólo es aceptada cuando usa la misma clave y un `version.code` superior. Esto evita que otra persona suplante una app existente.

## Runtime v1

`flex-ui-1` admite en esta primera versión:

- varias pantallas declarativas;
- componentes `text` y `button`;
- estilos `title`, `body` y `caption`;
- acciones `navigate` y `notify`;
- temas con `background`, `surface`, `text` y `accent`;
- control del permiso `notifications` antes de aceptar la acción.

No ejecuta código nativo incluido por terceros. Las apps se interpretan dentro del runtime y sólo reciben las capacidades declaradas, una base más segura para publicar apps de cualquier desarrollador.

## Compilación objetivo

- Arduino IDE 2.3.10.
- `esp32 by Espressif Systems` 3.2.0.
- `ESP32P4 Dev Module`.
- Flash de 16 MB y PSRAM habilitada.
- Tabla `flexos_ultra_usb.csv`.
- Primera grabación por USB.
