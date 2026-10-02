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
| `FlexOS_CloudTLS.{h,cpp}` | Raices de confianza para TLS (la misma CA que usa Flex Account), `flexTlsRoom()` (hay SRAM **interna** para abrir TLS) y `flexTlsReason()` (por que fallo una conexion). | `FlexOS_Ultra/` |
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
  en vez de insistir con la misma credencial. Que pasa despues, en
  "Cuenta desvinculada" (mas abajo).

### Primer arranque: Iniciar sesion y que hacer si falla

La pantalla de Flex Account del primer arranque (y la de Ajustes) vincula el aparato
con un codigo: el P4 genera una credencial aleatoria, publica **solo su SHA-256** y
muestra un codigo que se escribe en la web de Flex Account. Estados que se ven:

| Pantalla | Que pasa | Que hacer |
|---|---|---|
| `Creando enlace seguro` | pide el codigo al servicio | esperar; `Cancelar` lo detiene |
| el codigo y `El codigo vence en 10 minutos` | espera la aprobacion en el celular | escribirlo en la web |
| el codigo y una linea ambar (`Sin Wi-Fi...`, `Poca memoria interna (N KB). Reintentando`, `Sin conexion (motivo). Reintentando`) | **la consulta de aprobacion no pudo hacerse**; antes se quedaba muda hasta caducar el codigo | nada: se reintenta sola cada 3 s; si es memoria, cerrar una app |
| `No se pudo vincular` + motivo + `Reintentar` | fallo al pedir el codigo | `Reintentar` (no hace falta reiniciar) |
| `Poca memoria interna (N KB libres). Cierra una app y reintenta` | no hay SRAM interna para abrir TLS (suelo en `FlexOS_CloudTLS.h`) | cerrar una app y `Reintentar` |
| `Sin memoria interna para Flex Account...` | no pudo crearse la tarea del modulo (pila de 12 KB seguidos) | cerrar una app y `Reintentar`: se recrea sin reiniciar |
| `El codigo expiro` + `Reintentar` | pasaron 10 minutos | `Reintentar` |

* Con la cuenta ya vinculada, un fallo al volver a vincular **conserva** la cuenta
  guardada y dice por que.
* El emparejado usa `setInsecure()` a proposito (solo viaja una huella y la respuesta
  se acepta unicamente con firma ES256 valida del servicio, clave anclada en el
  firmware); la **credencial** solo sale por TLS verificado (validacion y Flex Cloud).
* **El servicio que atiende `POST/GET /api/devices/code` y `/activate` no esta en
  ningun repositorio** (vive en el sitio publicado de Flex Developer Studio). Este
  repositorio solo prueba que el P4 cumple su contrato (`tests/host/test_account.cpp`).

### Cuenta desvinculada: que detecta el P4, cuando y que deja de hacer

El usuario puede quitar este aparato desde la web de Flex Account. Flex Account es la
**autoridad de identidad**: cuando el servidor deja de reconocer la credencial
(`device_revoked`, `auth_required`, `token_expired`) el P4 pasa a
`FLEX_LINK_AUTH_REQUIRED` / `FLEX_LINK_TOKEN_EXPIRED` ("hay que volver a vincular").
**No** es "sin conexion" ni "servicio no disponible" (`LINKED_OFFLINE` /
`NETWORK_UNAVAILABLE`): esos son problemas de red, la cuenta sigue vinculada y se
reintenta.

* **Cuando se entera**: en la primera peticion a Flex Cloud con la nube a la vista (y en
  cada transferencia en marcha), al abrir la pantalla de Flex Account con Wi-Fi, al volver
  el Wi-Fi y, en reposo, cada 6 h. **Mientras nadie pregunta, el P4 no lo sabe**: no hay
  ningun canal por el que el servidor avise al aparato.
* **Una sola respuesta no basta**: un `401` sin codigo del servicio (la pagina de error de un
  proxy), un `403` suelto o un fallo de red NO desvinculan; Flex Account lo comprueba con su
  propia peticion antes de dar la cuenta por rechazada.
* **La credencial no se borra**: se conserva para poder decir "Vuelve a vincular" con la
  direccion a la vista, y se sustituye al volver a vincular (o con "olvidar" / restablecer
  de fabrica). `flexAccountUsable()` pasa a `false`: es lo que corta el uso de la nube.
* **Que deja de hacer el P4 con una cuenta que no sirve**
  * `FlexOS_Cloud`: no entra nada nuevo (subir, descargar, crear/renombrar/eliminar, abrir una
    foto en el visor, abrir un video, reintentar): devuelve `0`/`false` en vez de encolar algo
    que esperaria "conexion" para siempre. Las transferencias a medias **esperan sin
    perderse** (su fila dice `Vuelve a vincular tu Flex Account`) y siguen solas al revincular.
  * La lista dice lo mismo; un video ya abierto lo dice (en vez de `Sin conexion con Flex
    Cloud` a los 15 s).
  * La tarjeta de estado pasa a **`Vuelve a vincular tu cuenta`** en rojo, con el motivo y
    **sin la cuota ni la direccion** de la sesion anterior (eran de otra cuenta), y un boton
    `Abrir Flex Account`.
  * En Galeria, Multimedia y Archivos, "Subir a Flex Cloud" dice `Vuelve a vincular en
    Ajustes > General` (antes solo se comprobaba que HUBIERA credencial, y la subida se
    encolaba). El menu `...` de la nube ofrece solo `Transferencias`.
  * Un aviso **unico** por la isla (`Flex Account: sesion perdida` / `... caduco`), con o sin
    la nube a la vista, para que una subida larga no se quede "esperando" sin que nadie diga
    por que.
* **Desvincular a proposito** (boton `Desvincular cuenta` de la pantalla de Flex
  Account, tambien con la cuenta rechazada). Pregunta antes (`Cancelar` y el toque
  fuera no borran nada); al confirmar, `flexAccountForgetLocal()` borra la
  credencial (NVS y RAM, **una** escritura y solo si habia algo que borrar), deja
  `FLEX_ACCOUNT_UNLINKED` / `FLEX_LINK_UNLINKED` (sigue asi tras reiniciar; no es
  "sin conexion" ni "servicio no disponible") y `flexCloudAccountUnlinked()` cancela
  lo que estaba en cola o en marcha y suelta la cuota, la direccion y la lista de
  esa cuenta: nada de la cuenta anterior puede seguir hacia otra que se vincule
  despues. Una validacion que estuviera en vuelo no escribe nada despues (epoch).
  **No habla con el servidor**: el aparato sigue en la lista de la web de Flex
  Account hasta que se quita alli, y los archivos de la nube no se borran.
* **Salir**: `Volver a vincular` (pantalla de Flex Account, o `Abrir Flex Account` en la
  tarjeta) con una credencial nueva; la nube vuelve a `Conectado`, relee la cuota de la
  cuenta nueva y las transferencias esperando continuan.
* **Limites**
  * El P4 **no tiene boton "Desvincular"**: no existe una API de revocacion para el
    dispositivo; se revoca desde la web de Flex Account.
  * Si no se puede abrir una conexion TLS verificada (`Sin respuesta segura del servidor
    (-1: ...)`), el P4 **no puede enterarse** de que lo quitaron: seguira "Vinculada ·
    servicio no disponible" con el motivo a la vista. Es lo correcto: aceptar una orden de
    desvincular por un canal sin verificar seria peor que no enterarse.

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
* **El menu (...) se cierra sin restos.** Se ancla arriba a la derecha y SOBRESALE de
  la zona de la nube (tapa las pestanas de la Galeria o el selector de Archivos).
  Al cerrarlo, si sobresalia (`ckMenuSpills`), se repinta la app por su anfitrion
  (como los menus locales); si cabe dentro, solo la zona de la nube. Antes solo
  se repintaba esta, y la parte de arriba del menu se quedaba pegada sobre las
  pestanas ("la barra azul de Transferencias que no se va").

## 7. Memoria del P4

| Que | Cuanto | Cuando |
|---|---|---|
| Respuestas JSON | 48 KB PSRAM | siempre (una vez) |
| E/S de archivos | 16 KB PSRAM | siempre (una vez) |
| Lista visible | 200 elementos (~95 KB PSRAM) | siempre (una vez) |
| Huellas de partes | 32 B x partes (<= 16 KB) | durante una subida |
| Miniaturas | <= 24 x 34 KB PSRAM (LRU) | con la nube a la vista; `flexCloudShed()` las suelta |
| Streaming | 3 MB PSRAM | desde el primer video; se suelta al cerrar Galeria/Multimedia |

### Las tareas y la SRAM interna

Las pilas de tareas, los buffers de mbedTLS y el parseo de las raices salen de la
**SRAM interna** (el P4 tiene 32 MB de PSRAM pero la interna es la escasa), y Flex
Store reserva 24 KB seguidos para su propia tarea cada vez que se usa. Por eso:

* **Las tareas se crean al hacer falta.** `flex-cloud` (16 KB) nace con la primera
  peticion a la nube (abrirla, subir, bajar...) o al arrancar solo si el diario
  trae trabajo pendiente; `flex-cloud-st` (10 KB) con el primer video. Sin
  cuenta, o con la nube sin usar, no ocupan nada. (Antes se creaban siempre en
  `setup()`: 26 KB de pila fija mas los 12 KB de Flex Account.)
* **Van ancladas al core 1**, como el resto de tareas de red: el core 0 es del
  presentador grafico (ver `FlexOS_OTA.cpp`). Con `xTaskCreate` "a secas" podian
  migrar a el.
* **La creacion se comprueba.** Si no hay un bloque contiguo para la pila se
  escribe en el puerto serie (`[CLOUD] no se pudo crear la tarea ...`) y la
  siguiente peticion lo reintenta, sin reiniciar. Antes el fallo era mudo y el
  modulo se quedaba sin hilo para siempre.
* **No se abre TLS a ciegas.** `flexTlsRoom()` mide la SRAM interna libre (suelo
  40 KB, el del modo de proteccion del sistema) y su mayor bloque (16 KB). La guarda
  anterior usaba `esp_get_free_heap_size()`, que suma la PSRAM y nunca saltaba. Sin
  sitio se dice "sin memoria" y se reintenta con la espera de siempre.
* **Al arrancar solo valida Flex Account.** La cuota (`GET /me`) se pide con la nube
  a la vista, no cada vez que vuelve la red: Flex Account ya valida la misma
  credencial contra el mismo servidor y dos handshakes a la vez agotaban la
  interna justo cuando entra el Wi-Fi.
* **La tarjeta no afirma nada antes de tiempo.** Como la tarea nace con la primera
  peticion, la primera pintada llega ANTES de su primera vuelta: hasta entonces
  el estado es "Conectando..." (antes era "Sin Flex Account", y una cuenta
  vinculada ensenaba un instante "Vincular cuenta"). Si la tarea no puede nacer,
  la tarjeta y la lista dicen "No hay memoria libre ahora".

La cifra "~12 KB de pila" que figuraba aqui era una **estimacion con codigo
compilado para PC** (sin mbedTLS real): no esta medida en el P4. Si en la placa
apareciera un desbordamiento, esa pila es lo primero que hay que revisar.

## 8. Pruebas

| Bateria | Que demuestra |
|---|---|
| `make -C tests/host` -> `test_cloudcore` | nucleo portable: JSON hostil, SHA, diario con CRC, cache, textos de la interfaz |
| `make -C tests/host` -> `test_cloud` | el gestor REAL contra un servidor simulado con el contrato de `cloud/`: estados, listas, Unicode, subidas y descargas reanudables (apagado, Wi-Fi, cortes), errores, cuota, cancelacion, liberar espacio, miniaturas, visor, streaming de 160 MB con 3 MB, 160 MB de subida con < 256 KB, y auditoria TLS/credencial/id de cuenta |
| `make -C tests/host` -> `test_ino` (`testFlexCloudUi`) | la interfaz REAL (Galeria, Multimedia, Archivos, visor) contra un doble programable: que pide, que abre, streaming con red lenta, liberar espacio y descargas. `INO_SHOTS=1` deja capturas `build/shot_nube_*.ppm` |
| `tests/host/cloud_e2e.sh` | el gestor del P4 contra el servidor **real** de Flex-Developer-Studio (Node): contrato completo de la API, reanudacion, Range, miniaturas, reserva, aislamiento entre cuentas |

## 9. Limites honestos

* **TLS verificado contra el dominio real nunca se ha demostrado en la placa.**
  Antes de la validacion de sesion todo lo que hablaba con ese host (el
  emparejado, Flex Store) usaba `setInsecure()`. Si la conexion falla, la
  pantalla de Flex Account y la pildora de la nube dicen POR QUE (lo que
  devuelve `WiFiClientSecure::lastError()`): `certificado no reconocido` (la
  cadena del servidor no llega a ninguna de las 11 raices de `FlexOS_CloudTLS`:
  fija la correcta con `-DFLEX_CLOUD_ROOT_CA=...` o usa el bundle completo de
  ESP-IDF), `sin memoria interna`, `tiempo agotado en TLS`... y por el puerto
  serie salen la SRAM interna y el mayor bloque libres en ese momento
  (`[ACCOUNT]`, `[CLOUD]`). **El codigo `-1`** no dice que fallo: `WiFiClientSecure`
  lo usa para "no resuelve el nombre (DNS)", "no abre el socket TCP" y "el saludo
  TLS no termino a tiempo". Tras ese fallo (y solo entonces) se comprueba el DNS y
  se mira lo que tardo el intento, y la pantalla dice `no se encuentra el servidor
  (DNS)`, `el servidor rechaza la conexion TCP` (< 3 s), `el saludo TLS no termino
  en 12 s`, `TCP sin respuesta tras 15 s` o `sin respuesta en N s (TCP o TLS)`.
  Es una pista (por eso lleva los segundos), no una prueba.

### "Servicio no disponible": que es, que no es y que hace falta

* **Es** `FLEX_LINK_NETWORK_UNAVAILABLE`: hay Wi-Fi, pero la validacion de la sesion
  (`GET /api/cloud/me`, TLS verificado) no se completo. La cuenta SIGUE vinculada y
  sirve; se reintenta a los 30 s, 1, 2, 4, 8 y 15 min. **No es** "desvinculada"
  (`UNLINKED`: sin credencial), ni "rechazada" (`AUTH_REQUIRED` / `TOKEN_EXPIRED`: el
  servidor contesto que no), ni "sin conexion" (`LINKED_OFFLINE`: sin Wi-Fi). La
  prueba `testStateMatrix` recorre las combinaciones (primer arranque, vinculada,
  desvinculada, Wi-Fi apagado/encendido, servicio disponible/caido, reinicio).
* **No es una firma cambiada.** Auditoria de `FlexOS_Account.h` contra el ultimo
  commit anterior a la validacion (`467feff`): no se elimino ni cambio ninguna
  funcion publica; solo se **anadieron** (`flexAccountUsable`, `flexAccountLinkState`,
  `flexAccountLinkLabel`, `flexAccountRequestValidation`, `flexAccountReportRejected`,
  `flexAccountClassify`, `flexAccountBackoffMs`, `flexNvsStrLen`) y tres campos al final
  de `FlexAccountSnapshot`. Todos los *call sites* (Bridge, Ajustes, Flex Store,
  CloudKit, Cloud, Recovery) compilan contra esa cabecera y el contrato con el servidor
  lo ejecuta `cloud_e2e.sh` contra el servidor Node real.
* **Lo que si cambio es lo que hace el P4.** Hasta `20b32d5` el P4 **nunca validaba** la
  credencial: la guardaba y ya (todo lo que hablaba con el host usaba `setInsecure()`),
  asi que "servicio no disponible" no existia. Desde entonces depende de que
  `FLEX_ACCOUNT_SESSION_URL` conteste por TLS verificado. Para eso hacen falta DOS cosas
  que **no estan en estos repositorios**: el servicio Flex Cloud desplegado en
  `/api/cloud/*` del mismo dominio, y que Flex Account exponga la introspeccion
  (`POST /api/internal/introspect`, ver `cloud/docs/FLEX_ACCOUNT_INTEGRATION.md`). Si falta
  una, o el dominio no es alcanzable desde la red del P4, aparece "servicio no disponible".
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
