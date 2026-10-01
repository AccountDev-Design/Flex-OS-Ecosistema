# Flex Cloud en Flex OS Ultra (ESP32-P4)

Flex Cloud es el almacenamiento en la nube del ecosistema Flex. El **servicio y la
web** viven en el repositorio **Flex-Developer-Studio** (`cloud/`); aqui esta el
**cliente nativo del P4** y su integracion en Archivos, Galeria, Multimedia y el
visor. Este repositorio no contiene codigo del servidor ni de la web, y aquel no
contiene codigo del firmware.

## 1. Piezas

| Pieza | Que es | Donde |
|---|---|---|
| `FlexOS_CloudCore.{h,cpp}` | Nucleo **portable**: lee las respuestas de la API (sin confiar en ellas), SHA-256 por partes, diario de transferencias con CRC, cache de bloques del streaming, nombres para LittleFS y los **textos y decisiones de la interfaz** (cuota, "que se abre en el P4", lineas de transferencia). | `FlexOS_Ultra/` |
| `FlexOS_Cloud.{h,cpp}` | **Flex Cloud Manager**: el unico modulo que habla con la nube. Dos tareas FreeRTOS propias, API sin bloqueos para la interfaz. | `FlexOS_Ultra/` |
| `FlexOS_CloudTLS.{h,cpp}` | Raices de confianza para TLS (la misma CA que usa Flex Account). | `FlexOS_Ultra/` |
| `FlexOS_Ultra_CloudKit.h` | Interfaz de la nube **compartida** por Archivos, Galeria y Multimedia + el procesado de avisos en `loop()`. | cadena del sketch |
| `FlexOS_Ultra_MediaViewer.h` | El visor comun reproduce un AVI de la nube **por rangos** (`cloud:<id>/<nombre>`). | cadena del sketch |

## 2. Identidad y seguridad

* **Una sola identidad**: la credencial de dispositivo de **Flex Account**
  (`FlexOS_Account`). Flex Cloud no tiene login propio. Sin cuenta vinculada no
  se toca la red.
* **TLS verificado siempre** (`setCACert(flexCloudRootCA())`), nunca
  `setInsecure()`. Las pruebas lo comprueban en CADA peticion que lleva la
  credencial (`test_cloud`, `cloud_e2e`).
* La credencial solo viaja en `Authorization: Bearer`, nunca en una URL ni en un
  cuerpo, y se borra de la pila al usarla.
* **El P4 nunca envia un id de cuenta**: el propietario lo decide el servidor a
  partir de la credencial. El P4 no conoce ninguna credencial de la
  infraestructura (almacenamiento de objetos, base de datos).
* Un `401` del servicio (`auth_required`, `device_revoked`, `token_expired`) no
  desvincula: se avisa a Flex Account (`flexAccountReportRejected`) y la nube
  **espera** a que la cuenta lo compruebe (como minimo 30 s, como mucho 5 min)
  en vez de insistir con la misma credencial.

## 3. Transferencias

* **Diario** en `/System/Cloud/jobs.bin` (escritura atomica, CRC por registro).
  Se escribe solo en cambios de estado: encolar, sesion creada, terminada,
  fallida, cancelada, aviso entregado. Nunca por parte.
* **Subida por partes** (256 KB; mas grandes si el archivo pasa de 512 partes).
  Huellas SHA-256 del archivo y de cada parte en UNA pasada, **por tramos de
  1 MB por vuelta** (la tarea sigue atendiendo a la interfaz). Cada `PUT` lleva
  `X-Part-SHA256`; el servidor rechaza una parte alterada (`422`) y se reenvia
  solo esa.
* **Reanudar**: la clave del cliente es el contenido (`p4:<sha256>:<tamano>`);
  tras un apagado se pregunta a la nube que partes tiene (`GET /uploads/:id`) y
  solo se envian las que faltan. Cortes de Wi-Fi, conexiones cortadas a mitad,
  servidor caido y apagados estan cubiertos por pruebas.
* **Errores con criterio**: cuota llena, nombre invalido, archivo demasiado
  grande -> fallo inmediato, sin reintentos. Red y `5xx` -> espera creciente
  (2 s ... 60 s). El servidor que no se recupera -> "fallida" tras 12 intentos
  seguidos; **Reintentar** la reanuda donde iba.
* **Cancelar** suelta la reserva de cuota en el servidor (`DELETE /uploads/:id`),
  tambien si se cancelo sin red o se apago despues (queda pendiente en el diario).
* **Descargas** a `/System/Cloud/dl/` con `Range` + `If-Range` (la huella del
  archivo): un corte reanuda desde el byte exacto y nunca mezcla dos versiones.
  Se comprueba espacio en LittleFS **antes** de bajar nada (se respeta la reserva
  de 512 KB de la biblioteca) y el SHA-256 **antes** de avisar.

## 4. "Subir y liberar espacio": la regla

El original se borra del P4 **solo** cuando:

1. la nube completo la subida y devolvio el archivo con **el mismo SHA-256** que
   se calculo sobre el original; y
2. el gestor **vuelve a leer el original** despues de esa confirmacion y su
   SHA-256 sigue siendo ese (si alguien lo edito entre medias, se conserva); y
3. en la interfaz (`ckFreeLocal`) el archivo sigue existiendo, mide lo mismo,
   es el mismo elemento de la biblioteca y no esta protegido.

Lo protegido no se ofrece para subir. Encolar no borra nada. Un aviso repetido
(tras un reinicio) no hace nada.

## 5. Ver y reproducir sin perder calidad

* **Foto** (JPEG <= 8 MB): se trae el **original** verificado a un unico hueco
  (`/System/Cloud/view/<nombre>`) y se abre en el visor de la app. No hay copias
  reducidas ni recompresion. La miniatura de la rejilla es un **objeto aparte**
  de la nube (132x132), nunca sustituye al original.
* **Video AVI MJPEG**: streaming por rangos con una **arena fija de 48 bloques de
  64 KB (3 MB de PSRAM)**, reservada una vez y reutilizada. Un video de 160 MB se
  reproduce con esos 3 MB (prueba `test_cloud`).
  * El visor abre en fases: cabecera (64 KB) -> indice `idx1` del final (se
    **fija** en la cache para poder buscar) -> abierto.
  * Antes de leer cada fotograma comprueba que el tramo esta en la cache; si no,
    **para el reloj** y ensena "Cargando" (no salta fotogramas ni da el video por
    roto). Nunca espera dentro del bucle de la interfaz mas de 25 ms.
* **MP4, HEIC, PNG...**: no se intentan; se dice por que y se ofrece descargar o
  abrir en la web. El P4 no transcodifica nada.

## 6. Interfaz

* **Archivos**: selector `Este dispositivo | Flex Cloud`. En la nube: carpetas,
  ruta, papelera de la nube, nueva carpeta, renombrar, eliminar/restaurar/borrar
  para siempre, detalles, descargar. En local: "Subir a Flex Cloud" en el menu de
  un archivo.
* **Galeria**: pestana **Nube** (fotos y videos de la nube, con su marca de nube)
  y "Subir a Flex Cloud" en el menu de cada elemento (conservar o liberar
  espacio).
* **Multimedia**: pestana **Nube** con los videos de la nube, que se reproducen sin
  descargarlos.
* Tarjeta de estado comun: conexion (Conectado / Sin Wi-Fi / Vuelve a vincular tu
  cuenta / Flex Cloud no responde), cuota real del servidor (5 GB de inicio, el
  plan lo decide el servidor) con aviso de espacio bajo y lleno, y transferencias
  en curso. Pantalla de **Transferencias** con progreso, velocidad, cancelar,
  reintentar y quitar terminadas.
* **Liquid Glass**: tarjetas planas resueltas por filas sobre el fondo que se
  acaba de pintar (sin desenfoque por cuadro, sin apilar); los menus usan la banda
  pre-desenfocada de siempre. El progreso se repinta como mucho 4 veces por
  segundo y nunca con el dedo apoyado.
* La cuota se refresca (cada 60 s) **solo** con la nube a la vista.

## 7. Memoria del P4

| Que | Cuanto | Cuando |
|---|---|---|
| Respuestas JSON | 48 KB PSRAM | siempre (una vez) |
| E/S de archivos | 16 KB PSRAM | siempre (una vez) |
| Lista visible | 200 elementos (~95 KB PSRAM) | siempre (una vez) |
| Huellas de partes | 32 B x partes (<= 16 KB) | durante una subida |
| Miniaturas | <= 24 x 34 KB PSRAM (LRU) | con la nube a la vista; `flexCloudShed()` las suelta |
| Streaming | 3 MB PSRAM | desde el primer video; se suelta al cerrar Galeria/Multimedia |

Pilas: `flex-cloud` 16 KB, `flex-cloud-st` 10 KB (cadena mas profunda medida con
`-fstack-usage`: ~12 KB contando HTTP/TLS).

## 8. Pruebas

| Bateria | Que demuestra |
|---|---|
| `make -C tests/host` -> `test_cloudcore` | nucleo portable: JSON hostil, SHA, diario con CRC, cache, textos de la interfaz |
| `make -C tests/host` -> `test_cloud` | el gestor REAL contra un servidor simulado con el contrato de `cloud/`: estados, listas, Unicode, subidas y descargas reanudables (apagado, Wi-Fi, cortes), errores, cuota, cancelacion, liberar espacio, miniaturas, visor, streaming de 160 MB con 3 MB, 160 MB de subida con < 256 KB, y auditoria TLS/credencial/id de cuenta |
| `make -C tests/host` -> `test_ino` (`testFlexCloudUi`) | la interfaz REAL (Galeria, Multimedia, Archivos, visor) contra un doble programable: que pide, que abre, streaming con red lenta, liberar espacio y descargas. `INO_SHOTS=1` deja capturas `build/shot_nube_*.ppm` |
| `tests/host/cloud_e2e.sh` | el gestor del P4 contra el servidor **real** de Flex-Developer-Studio (Node): contrato completo de la API, reanudacion, Range, miniaturas, reserva, aislamiento entre cuentas |

## 9. Limites honestos

* Nada de esto se ha ejecutado aun **en la placa**: las pruebas usan dobles de
  red, FS y FreeRTOS fieles a arduino-esp32 3.2.1, y el servidor real en el PC.
  Falta medir en el P4: memoria de mbedTLS con dos o tres conexiones a la vez
  (API, descarga, streaming), ritmo real del streaming por Wi-Fi del C6 y
  tiempos de las huellas SHA-256 en flash.
* LittleFS del P4 tiene ~11 MB: una descarga que no cabe se rechaza antes de
  empezar. Los archivos grandes se ven por streaming (AVI) o en la web.
* Solo AVI MJPEG y JPEG baseline se abren en el P4 (no hay decodificador de
  video por hardware).
* El servicio de Flex Account (la introspeccion de la credencial que usa Flex
  Cloud) debe estar desplegado en el dominio de Flex Developer Studio; su
  contrato esta en `cloud/docs/FLEX_ACCOUNT_INTEGRATION.md` del otro repositorio.
