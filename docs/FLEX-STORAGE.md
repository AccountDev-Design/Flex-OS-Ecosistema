# Flex Storage

Flex Storage une en una sola experiencia lo que hasta ahora eran tres cosas
separadas en Flex OS Ultra (ESP32-P4):

| Antes | Ahora, dentro de Flex Storage |
|---|---|
| **Flex Web Server** ("Conectar con el móvil": QR + código de 6 cifras, subir y bajar fotos, vídeos y música del P4 desde el navegador) | **Transferencias** y el almacenamiento **Local** del P4. Mismo servidor, mismo QR, mismo protocolo |
| **Flex Cloud** (cliente nativo del P4 + servicio y web en Flex Developer Studio, en Internet con Flex Account) | **Flex Cloud** puede vivir ahora **en el teléfono** (Galaxy A55 de referencia): 5 GB lógicos del almacenamiento del móvil, en la red local |
| La web de Flex Cloud (Flex Developer Studio, `cloud/web`) | La web que sirve el P4 adopta su diseño (tokens, vidrio, tarjetas, barra de cuota, menús, panel de transferencias) |

El usuario ve **"Mi almacenamiento de Flex OS"**: elige dónde guardar
(📟 Flex OS o ☁️ Flex Cloud), mueve o copia entre los dos, y reproduce desde
los dos. Nada de "esto es Transferencia" o "esto es Flex Cloud".

> Este documento empieza con la **auditoría** que se hizo antes de tocar nada
> (sección 1), sigue con las **decisiones** (2), la **arquitectura** (3–8) y el
> **plan de cambios archivo por archivo** con su motivo, dependencias e impacto
> (9). Lo que es **LOCAL** y lo que es **CLOUD/A55** se marca en cada sección.

---

## 1. Auditoría del código existente

### 1.1 Servidor HTTP y QR (P4) — se REUTILIZA

| Pieza | Archivo | Qué hace hoy |
|---|---|---|
| Protocolo HTTP portable | `FlexOS_Ultra/FlexOS_HttpShare.{h,cpp}` | Analiza HTTP/1.1 (GET/HEAD/POST/DELETE/OPTIONS, consulta, cookie `fxs`, `Range`, `X-Flex`, `Host`), compone cabeceras, `Content-Disposition` UTF-8, trozos. Probado con sanitizers (`test_httpshare`) |
| La aplicación del servidor | `FlexOS_Ultra/FlexOS_MediaWeb.{h,cpp}` | Rutas, sesiones (6 plazas, cookie HttpOnly+SameSite=Strict), código de 6 cifras con limitador, nivel de propietario (PIN del sistema), subida con CRC de extremo a extremo y validación del formato real, descargas con `Range`, ZIP por flujo. Portable: se ejecuta entero en el PC (`test_mediaweb`, `flexweb_host` + Chromium) |
| Mitad de placa | `FlexOS_Ultra/FlexOS_Ultra_WebServer.h` | Tarea `flexWeb` (núcleo 1, 14 KB de pila) que atiende **una conexión cada vez** en el puerto **8080**; la hoja "Conectar con el móvil" con el **QR** (`http://<ip>:8080/?k=<código>`), interruptor, código nuevo, desconectar móviles y tarjetas de progreso con repintado localizado |
| QR | `FlexOS_Ultra/FlexOS_QR.{h,cpp}` | Codificador QR probado (la prueba vuelve a leer el código) |
| La web del móvil | `FlexOS_Ultra/webui/{index.html,app.css,app.js}` → `FlexOS_WebUI.h` (`gen_header.py`) | "Flex OS · Biblioteca": emparejar, rejilla, **conversión en el móvil** (HEIC/PNG→JPEG, MP4/WebM→AVI MJPEG, audio→WAV IMA ADPCM) con perfiles y límite de tamaño, visor (incluido un reproductor MJPEG/IMA en JS), selección y ZIP, contenido protegido. CSP estricta (`script-src 'self'`, sin `innerHTML`) |

Rutas que existen hoy (todas se conservan sin cambios):
`GET /`, `/app.js`, `/app.css`, `GET /api/hello`, `POST /api/pair`,
`GET /api/library`, `/api/status`, `/api/zip`, `/api/thumb/:id`,
`/api/file/:id` (Range), `POST /api/upload`, `/api/thumb/:id`, `/api/login`,
`/api/logout`, `/api/unpair`.

**Puntos de riesgo detectados**
* El servidor atiende **una conexión cada vez**. Una transferencia larga a
  través de él bloquea al resto de peticiones del navegador mientras dura.
  Por eso Flex Storage **no** hace pasar ficheros grandes enteros por el P4
  (ver 5.3): las subidas a la nube van por partes cortas y las descargas y la
  reproducción en el navegador van directas al teléfono con enlaces firmados.
* El servidor **solo arranca si el usuario lo enciende** (hoja del QR). Nada
  de Flex Storage depende de que esté encendido después del emparejamiento:
  el P4 es el **cliente** del teléfono.
* La web se sirve por `http://<ip privada>`: **no es un contexto seguro**, así
  que `crypto.subtle` (SHA-256), Service Workers y OPFS **no existen** en esa
  página. La web portada calcula el SHA-256 en JavaScript puro.

### 1.2 Almacenamiento local (P4) — se REUTILIZA

| Pieza | Archivo | Datos |
|---|---|---|
| LittleFS | `FlexOS_FS.{h,cpp}` | Partición `spiffs` de **0xAE0000 (≈ 11 MB)** en `flexos_ultra_usb.csv`. Rutas ≤ 96 bytes, nombres ≤ 48. Carpetas de usuario: `/Imagenes`, `/Videos`, `/Musica`, `/Documentos`, `/Notas`, `/Paint`, `/Papelera`. `/System` es del sistema |
| Biblioteca de medios | `FlexOS_MediaLib.*`, `FlexOS_MediaStore.*` | Catálogo de hasta 384 elementos, miniaturas persistentes, límites por archivo (foto 6 MB, vídeo 8 MB, audio 8 MB) y **reserva de 512 KB** que nunca se consume |
| Pantalla Almacenamiento | `FlexOS_Ultra_AppStorage.h` | Cifras REALES (`flexFsTotalBytes/UsedBytes`, PSRAM, categorías, archivos grandes) |

La tabla de particiones **no se toca** (cambiarla borra los archivos del
usuario). La microSD no interviene: Flex Storage funciona sin ella.

### 1.3 Flex Cloud — se REUTILIZA y se AMPLÍA

| Pieza | Dónde | Qué hace |
|---|---|---|
| Servicio | Flex-Developer-Studio `cloud/src` (Node ≥ 22, SQLite) | API `/api/cloud`: carpetas, papelera, subidas reanudables por partes con SHA-256 por parte, descargas con `Range`/`If-Range`/ETag = SHA-256, miniaturas aparte, cuota por plan (5 GB) |
| Web | Flex-Developer-Studio `cloud/web` | La web de Flex Cloud (módulos ES sin framework) |
| Núcleo portable del P4 | `FlexOS_CloudCore.{h,cpp}` | Lee las respuestas de esa API sin fiarse, SHA-256 por partes, diario de trabajos con CRC, caché de bloques del streaming, textos de la interfaz |
| Gestor del P4 | `FlexOS_Cloud.{h,cpp}` | Dos tareas (API y streaming), subidas por partes de 256 KB con huellas por tramos de 1 MB, descargas reanudables verificadas, "subir y liberar espacio" (borra el original **solo** tras confirmar el SHA-256 y releerlo), streaming AVI con una arena fija de 48 × 64 KB (3 MB de PSRAM) |
| Interfaz del P4 | `FlexOS_Ultra_CloudKit.h` + Archivos, Galería, Multimedia, visor | Pestaña Nube, tarjeta de estado y cuota, transferencias, visor por rangos (`cloud:<id>/<nombre>`) |

Hoy ese gestor solo sabe hablar con **un** destino: el servicio en Internet,
con la credencial de Flex Account y TLS verificado (`setCACert`). El contrato
de la API es justo el que necesita un "Flex Cloud en el teléfono".

### 1.4 Reproductores (P4) — se REUTILIZAN

* **Vídeo**: `FlexOS_Ultra_MediaViewer.h` + `FlexOS_Media.cpp`. **AVI MJPEG**
  (el P4 no tiene decodificador de H.264/HEVC). Local por flujo; de la nube por
  rangos con la caché de bloques (el visor nunca espera más de 25 ms dentro del
  bucle; si falta el tramo, para el reloj y enseña "Cargando").
* **Fotos**: JPEG baseline (≤ 6 MB en local, ≤ 8 MB desde la nube, original
  verificado). PNG/HEIC/WebP se guardan y se descargan, no se abren en el P4.
* **Audio**: `FlexOS_Ultra_AppMusic.h` + `FlexAudioStream`: WAV PCM 8/16 bits y
  WAV IMA ADPCM. MP3/AAC/FLAC/OGG no se reproducen en el P4 (no hay
  decodificador). El DMA guarda ~0,4 s.

### 1.5 Android — se REUTILIZA

`android/FlexPhone` (Kotlin, Compose): enlace Wi-Fi con el P4 (Flex Link v2,
TCP 47820 / UDP 47821), emparejamiento con código, claves en el **Android
Keystore** (`BondStore`), servidor del Browser Relay, servicio en primer plano
`connectedDevice`. Módulo `:protocol` Kotlin/JVM puro con vectores dorados
compartidos con el firmware.

### 1.6 Pruebas existentes (línea base, antes de cambiar nada)

| Batería | Resultado |
|---|---|
| `make -C tests/host` (todas las de host, perfil P4) | pasa |
| `make -C tests/host web` (`webui.test.js` 255 + `e2e.test.js` 94 en Chromium) | pasa |
| `tests/host/cloud_e2e.sh` (gestor del P4 contra el servidor Node real) | 38 / 38 |
| `gradle :protocol:test` (Android) | pasa |
| `tests/link/run.sh` (servidor del enlace real, sockets) | 50 / 50 |
| Flex-Developer-Studio `npm test` | 57 / 57 |
| Flex-Developer-Studio `npm run test:e2e` | **inestable ya antes de este trabajo**: en dos ejecuciones seguidas falló un paso distinto cada vez ("Mover a la papelera" por tiempo, "tras recargar conserva 6 de 6 partes") |

---

## 2. Decisiones

| Pregunta | Decisión | Por qué / alternativas descartadas |
|---|---|---|
| ¿Cómo comparte el A55 su almacenamiento? | Una **app/servicio Android** (dentro de Flex Phone, módulo aislado) sirve una carpeta **administrada** con la **misma API de Flex Cloud** en la red local | Un navegador no puede servir archivos (sin servidor, OPFS no existe en `http://`). La API de Flex Cloud ya la entiende el P4: se reutiliza su gestor entero (subidas, descargas verificadas, streaming, interfaz) en vez de escribir otro |
| ¿Qué carpeta? | **Almacenamiento específico de la app** (`getExternalFilesDir("FlexCloud")`), sin ningún permiso de almacenamiento | Es justo "solo el espacio autorizado": el P4 no puede ver nada fuera de ahí. Sin `MANAGE_EXTERNAL_STORAGE`, sin `READ_MEDIA_*`. El usuario mete archivos desde la web de Flex Storage o desde **Compartir** de Android |
| ¿5 GB reales? | **Cuota lógica de 5 GB** recortada por el espacio **real** libre del teléfono (`StorageManager.getAllocatableBytes`, menos un margen de 512 MB) | Android no permite reservar una partición para una app. Se enseña "1,73 GB de 5 GB" y, si el teléfono tiene menos libre, se dice ("En el teléfono solo quedan 800 MB") |
| ¿Cómo se conecta? | El **QR de siempre** abre la web del P4 en el navegador del móvil; desde ella, "Activar Flex Cloud en este teléfono" entrega a la app una **oferta de un solo uso** (`flexstorage://attach?...`). La app hace un **ECDH P-256**, ambos muestran un **código de verificación de 6 cifras** y el **P4 pide permiso en su pantalla** | El QR no se reescribe ni se convierte en credencial. La autorización es **por dispositivo** y la da quien tiene el P4 delante |
| ¿Sesiones? | El P4 abre sesión con reto-respuesta HMAC-SHA256 (clave del emparejamiento, que **nunca** viaja); el teléfono devuelve un **token temporal** ligado a la IP del P4 (30 min sin uso, 12 h como mucho) y una prueba de que él también tiene la clave | Autenticación **mutua**: ni un equipo de la red se hace pasar por el P4, ni otro se hace pasar por el teléfono |
| ¿Quién es cliente de quién? | El **P4 es cliente** del teléfono (`http://<teléfono>:47830/api/cloud`) | El servidor web del P4 solo está encendido mientras el usuario lo quiere; el teléfono tiene un servicio en primer plano |
| ¿Flex Cloud en Internet? | **Se conserva intacto.** El gestor del P4 tiene ahora un **destino**: Internet (Flex Account, TLS) o Teléfono. Un destino a la vez; cada uno con **su diario** | No se elimina nada que exista. Si no hay teléfono emparejado todo es exactamente como antes |
| ¿La web? | La web del P4 **evoluciona** con el diseño de Flex Cloud (tokens, vidrio, tarjetas, cuota, menús, panel de transferencias). El motor de conversión (FX) y el flujo de subida a Flex OS **no cambian** | Una sola experiencia; las pruebas existentes de la web siguen valiendo |

---

## 3. Arquitectura

```
                     📱 TELÉFONO ANDROID (Galaxy A55 de referencia)
          ┌─────────────────────────────────────────────────────────┐
          │ Navegador ──► web de Flex Storage (servida por el P4)    │
          │ Flex Phone ─► FlexStorageService (primer plano)          │
          │               └ FlexCloudServer :47830  /api/cloud/*     │
          │                 └ carpeta administrada "FlexCloud"       │
          │                   cuota lógica 5 GB ∩ espacio real       │
          └───────────────▲──────────────────────────▲──────────────┘
          Wi-Fi (LAN)     │ HTTP + token de sesión   │ enlaces firmados (15 min)
                          │ (P4 cliente)             │ (navegador → teléfono)
          ┌───────────────┴──────────────────────────┴──────────────┐
          │                 ESP32-P4 · FLEX OS ULTRA                 │
          │  Flex Web Server :8080 (el de siempre, ampliado)         │
          │    /api/*        biblioteca local, subida, ZIP (igual)   │
          │    /api/fs/*     resumen, emparejamiento, local, colas   │
          │    /api/cloud/*  pasarela a Flex Cloud del teléfono      │
          │  FlexOS_StorageLink  teléfono emparejado + sesión        │
          │  FlexOS_Cloud        destino Internet | Teléfono         │
          │  Almacenamiento · Archivos · Galería · Multimedia · Música│
          └───────────────┬──────────────────────────┬──────────────┘
                          ▼                          ▼
                 📟 LOCAL (LittleFS ≈ 11 MB)   ☁️ FLEX CLOUD (A55, 5 GB)
```

### LOCAL (P4)
* LittleFS real. Lo que cabe y lo que el P4 reproduce: fotos JPEG, vídeos AVI
  MJPEG, audio WAV (la web convierte en el móvil como siempre).
* Se gestiona desde la web (subir con conversión, descargar, ZIP, borrar,
  renombrar) y desde Archivos/Galería/Multimedia/Música en el P4.

### CLOUD/A55
* Archivos **originales** (sin recomprimir) en la carpeta administrada del
  teléfono. El P4 los ve en Archivos › Flex Cloud y en la pestaña Nube de
  Galería y Multimedia, **igual que antes** (mismo gestor y misma interfaz).
* El P4 **reproduce por streaming** los AVI MJPEG del teléfono (rangos, 3 MB de
  arena fija), abre fotos JPEG trayendo el original verificado y no copia nada
  entero a su flash salvo que el usuario lo pida ("Descargar a Flex OS").

---

## 4. Emparejamiento (CLOUD/A55)

```
 Navegador del A55            P4 (Flex Web Server)                 App Flex Phone
 ────────────────             ────────────────────                 ──────────────
 escanea el QR ─────────────► /?k=<código>  (sesión web, igual que siempre)
 "Activar Flex Cloud" ──────► POST /api/fs/phone/offer  → oferta (1 uso, 3 min)
 intent flexstorage://attach?h=<ip:8080>&o=<oferta> ─────────────────────────►
                                                                 "¿Compartir 5 GB con
                                                                  Flex OS en 192.168.1.50?"
                              POST /api/fs/phone/pair ◄───────── oferta, id, nombre, puerto,
                              (ECDH P-256: clave pública)          clave pública
                              ───────────────────────────────────► clave pública del P4
                    código de verificación 482 913       código de verificación 482 913
                    "Galaxy A55 quiere usar Flex Cloud"
                    [Rechazar]  [Permitir]
                              POST /api/fs/phone/pair/<id> ◄──── prueba HMAC (sondeo 1,5 s)
                              aprobado + prueba del P4 ──────────► guarda la clave (Keystore)
                    guarda la clave (NVS)
                    "Galaxy A55 conectado"
```

* `K = HMAC-SHA256(Z, "flexstorage-v1-key" ‖ oferta ‖ idP4 ‖ idTeléfono)`,
  donde `Z` es el secreto ECDH. El código de verificación son 6 cifras de
  `HMAC(K, "flexstorage-v1-sas")`. Las pruebas usan etiquetas distintas por
  rol (no sirve reenviar la del otro).
* Un teléfono que **ya estaba emparejado** demuestra que conserva la clave
  anterior y no hace falta volver a aprobarlo (sirve para "cambió la IP del
  teléfono: vuelve a escanear el QR").
* La oferta caduca a los 3 minutos, se gasta al usarla y hay como mucho dos
  vivas. Cinco fallos seguidos activan la espera creciente del limitador.

## 5. Uso diario

### 5.1 Sesión P4 → teléfono
`GET /api/fs/challenge` → reto (60 s) · `POST /api/fs/session` con
`HMAC(K, "flexstorage-v1-sess" ‖ reto ‖ idP4)` → token temporal +
`HMAC(K, "flexstorage-v1-sess-ok" ‖ reto ‖ token)` que el P4 comprueba. El
token va en `Authorization: Bearer` y solo vale desde la IP del P4.

### 5.2 Gestor del P4
Con el destino **Teléfono**, `FlexOS_Cloud` usa `http://<teléfono>:<puerto>/api/cloud`,
un `WiFiClient` normal (sin TLS: no gasta SRAM interna de mbedTLS) y el token
de sesión. Todo lo demás es el gestor de siempre: diario (en
`/System/Cloud/phone.bin`, separado del de Internet), subidas por partes con
SHA-256, descargas verificadas, streaming, miniaturas.

### 5.3 La web
* **Metadatos** (listas, renombrar, mover, papelera, miniaturas, cuota):
  `/api/cloud/*` del P4, que lo reenvía al teléfono con su token. Una sola
  sesión y una sola política de seguridad en el navegador.
* **Subir a Flex Cloud**: por partes de 1 MB con SHA-256 de cada parte (el P4
  atiende otras peticiones entre parte y parte: la interfaz no se congela).
* **Descargar o ver** un archivo del teléfono: el P4 pide un **enlace firmado**
  de 15 minutos y el navegador lo usa **directamente** contra el teléfono
  (rangos de verdad para `<video>`; el P4 no se bloquea).
* **Copiar / Mover entre Flex OS y Flex Cloud**: son trabajos del gestor del
  P4 (verificados con SHA-256). **Mover = copiar, verificar y solo entonces
  borrar el original** (para subir: la regla de "liberar espacio" de siempre).

## 6. Seguridad

* Solo red local: el teléfono escucha en la dirección de su Wi-Fi y rechaza
  orígenes que no sean de rangos privados. No hay reenvío de puertos ni nada
  abierto a Internet.
* Lo que viaja **va en claro** por la red local (HTTP), igual que la web del P4
  y Flex Phone. Se dice en las dos pantallas. La clave del emparejamiento no
  viaja nunca (ECDH); los tokens caducan y están ligados a una IP.
* Límites: cabeceras ≤ 16 KB, JSON ≤ 64 KB, partes ≤ 8 MB, miniaturas ≤ 512 KB,
  4 conexiones simultáneas en el teléfono, sesiones y retos acotados,
  limitadores de intentos en P4 y teléfono.
* Cada petición del navegador que cambia algo exige la cabecera `X-Flex` y la
  sesión del P4 (CSRF); `Host` debe ser la IP del P4 (DNS rebinding).

## 7. Desconexión y recuperación

* Teléfono sin responder: Flex Cloud dice **"El teléfono no responde"** y
  reintenta con espera creciente (2 s … 60 s). Lo local sigue funcionando y
  nada se bloquea. Las transferencias esperan y siguen solas.
* "Desconectar" en el P4 deja de usar el teléfono sin olvidar el
  emparejamiento; "Olvidar este teléfono" borra la clave (NVS). En el teléfono,
  "Dejar de compartir" para el servidor y "Olvidar Flex OS" borra su clave.
* Reproducción: cambiar de vídeo o cerrar el visor cierra el flujo anterior
  (una sola arena, un solo flujo).

## 8. Memoria del P4 (lo nuevo)

| Qué | Cuánto | Cuándo |
|---|---|---|
| Registro del teléfono | ~200 B RAM + blob en NVS | siempre |
| Emparejamiento en curso | ~400 B + ECDH de mbedTLS (KB de montón, segundos) | solo mientras se empareja |
| Pasarela `/api/cloud` | reutiliza el buffer de E/S del servidor (16 KB) | por petición |
| Gestor con destino Teléfono | lo mismo que con Internet, **sin** los buffers TLS | igual que antes |

## 9. Plan de cambios archivo por archivo

| Archivo | Motivo | Depende de | Impacto |
|---|---|---|---|
| `android/FlexPhone/storage/` (módulo nuevo `:storage`, JVM puro) | Servidor de Flex Cloud del teléfono con la API de `cloud/`, cuota, partes, rangos, emparejamiento y sesiones | JDK (sin dependencias) | Nuevo, aislado; pruebas en el PC |
| `android/FlexPhone/app/.../storage/*` | Servicio, actividad del enlace `flexstorage://`, volumen, almacén de claves | `:storage`, Keystore | Nuevo; manifiesto: un servicio y una actividad, **sin permisos nuevos** |
| `FlexOS_Ultra/FlexOS_StorageCore.{h,cpp}` | Núcleo portable: registro, ECDH, derivaciones, ofertas, emparejamiento, estado | FlexOS_FlexAuth (HMAC), cJSON, OpenSSL/mbedTLS | Nuevo, probado en el PC |
| `FlexOS_Ultra/FlexOS_StorageLink.{h,cpp}` | Mitad de placa: NVS, cerrojo, sesión con el teléfono | StorageCore, HTTPClient | Nuevo |
| `FlexOS_Ultra/FlexOS_Cloud.{h,cpp}` | Destino Teléfono (URL, cliente sin TLS, token, diario propio, textos) | StorageLink | Con destino Internet el comportamiento es idéntico (las pruebas existentes lo vigilan) |
| `FlexOS_Ultra/FlexOS_CloudCore.{h,cpp}` | Textos nuevos (teléfono) | — | Solo añade |
| `FlexOS_Ultra/FlexOS_MediaWeb.{h,cpp}` | Rutas `/api/fs/*`, pasarela `/api/cloud/*`, IP del cliente | StorageLink (por funciones del anfitrión) | Las rutas existentes no cambian |
| `FlexOS_Ultra/FlexOS_Ultra_WebServer.h` | Anfitrión de las rutas nuevas; la hoja del QR enseña el teléfono y la aprobación | MediaWeb, StorageLink | Mismo servidor y mismo QR |
| `FlexOS_Ultra/webui/*` | La web Flex Storage con el diseño de Flex Cloud | MediaWeb | FX y el flujo de subida a Flex OS no cambian |
| `FlexOS_Ultra/FlexOS_Ultra_AppStorage.h` | Almacenamiento pasa a ser el centro de Flex Storage (Local + Flex Cloud + Transferir) | StorageLink, FlexOS_Cloud | Las cifras de siempre se conservan |
| `FlexOS_Ultra/FlexOS_Ultra.ino` | `flexStorageBegin()` en `setup()` | StorageLink | Una línea |
| `tests/host/*` | Pruebas del núcleo, del gestor con destino Teléfono, de las rutas y e2e contra el servidor Kotlin real | — | Nuevas baterías |

## 10. Pruebas

Ver la sección final ("Resultados") cuando se complete la implementación.

## 11. Límites honestos

* **No se ha ejecutado en la placa ni en el teléfono.** Todo lo de este
  documento se prueba en el PC: el servidor del teléfono en la JVM, el
  firmware compilado con los dobles de siempre y los dos hablando entre sí por
  sockets reales. Falta medir en el P4: ECDH con mbedTLS, ritmo real del
  streaming desde el A55 por el Wi-Fi del C6, y memoria con dos conexiones a la
  vez (gestor + pasarela).
* La app Android no se puede compilar en este entorno (el repositorio Maven de
  Google no es accesible): el código de plataforma se comprueba contra las
  clases de Android 15 de Robolectric y la lógica entera vive en `:storage`,
  que sí se compila y se prueba.
* El P4 solo reproduce **AVI MJPEG, JPEG baseline y WAV PCM/IMA ADPCM**. Un MP4
  del teléfono se guarda en Flex Cloud y se ve en el navegador, no en el P4.
