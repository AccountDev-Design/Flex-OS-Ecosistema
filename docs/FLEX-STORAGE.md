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

> Actualización: Música también lee de Flex Cloud (por `MediaStream`, el mismo lector que el visor) y
> todo lo multimedia pasa por el perfil de [`FLEX-MEDIA-PROFILE.md`](FLEX-MEDIA-PROFILE.md).

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
                                                                 "¿Usar este teléfono
                                                                  como Flex Cloud?"
                              POST /api/fs/phone/pair ◄───────── oferta, id, nombre, puerto,
                              (ECDH P-256: clave pública)          clave pública
                              ───────────────────────────────────► clave pública del P4
                    código de verificación 482 913       código de verificación 482 913
                    "¿Emparejar este teléfono?"
                    [Emparejar]  [Rechazar]
                              POST /api/fs/phone/pair/<id> ◄──── prueba HMAC (sondeo 1,5 s)
                              aprobado + prueba del P4 ──────────► guarda la clave (Keystore)
                    guarda la clave (NVS)
                    aviso "Activado en Galaxy A55"
```

* `K = HMAC-SHA256(Z, "flexstorage-v1-key" ‖ oferta ‖ idP4 ‖ idTeléfono)`,
  donde `Z` es el secreto ECDH. El código de verificación son 6 cifras de
  `HMAC(K, "flexstorage-v1-sas")`. Las pruebas usan etiquetas distintas por
  rol (no sirve reenviar la del otro).
* Un teléfono que **ya estaba emparejado** demuestra que conserva la clave
  anterior y no hace falta volver a aprobarlo (sirve para "cambió la IP del
  teléfono: vuelve a escanear el QR").
* La oferta caduca a los 3 minutos, se gasta al usarla y hay como mucho dos
  vivas. Cinco **ofertas falsas** seguidas activan la espera creciente del
  limitador (30 s, 1 min, 2 min… hasta 16 min); **una oferta buena pasa
  siempre** (solo la da una sesión web autenticada) y limpia el limitador, así
  que nadie en la Wi‑Fi puede bloquear el emparejamiento del teléfono de verdad
  mandando basura.
* Hay **2 minutos** para decidir en la pantalla del P4; tres pruebas
  incorrectas del teléfono cancelan ese emparejamiento.
* La aprobación sale como una ventana propia del sistema, **nunca sobre la
  pantalla de bloqueo** ni en modo antirrobo, DeX u OTA: si el P4 está
  bloqueado, avisa "Desbloquea para aprobar el teléfono" y la pregunta aparece
  al desbloquear (si aún no ha caducado).

### 4.1 El emparejamiento no depende de ninguna pantalla (Android)

El protocolo, su estado y la cancelación viven en `StorageAttach` (un objeto de la app, un solo
hilo), **no** en `StorageAttachActivity`. La actividad y la pantalla *Flex Cloud* solo lo observan
(`StateFlow`): se pueden destruir y recrear (giro, tamaño de letra, Atrás, irse al navegador a mirar
la web) sin cancelar nada, y solo el botón **Cancelar** corta un emparejamiento en curso. Antes
`onDestroy` ponía `cancelled = true` y deshacía lo hecho, y de ahí que "se perdía la conexión un
segundo, la pantalla del código desaparecía y volvía *Esperando a Flex OS*".

### 4.2 Si «Abrir Flex Phone» no abre nada

Un navegador no avisa de si abrió o no una app por un enlace `flexstorage://`. La web lo detecta por
la visibilidad de la página: si 1,8 s después de pulsar sigue a la vista y el emparejamiento no
avanzó, dice **«Flex Phone no se abrió»** y ofrece (1) comprobar que Flex Phone está instalada y
actualizada con Flex Cloud, (2) abrir la página en Chrome o Samsung Internet (el navegador de la
cámara y los WebView no abren enlaces de apps), (3) **Copiar enlace** y pegarlo en *Flex Phone → Flex
Cloud → Usar este enlace*, y (4) un enlace `intent://` con el paquete exacto
(`com.flexos.flexphone`). Causas habituales: la APK instalada es anterior a Flex Storage (no tiene
la actividad `flexstorage://`), o la página se abrió desde la cámara.

### 4.3 «No se activó · No se pudo hablar con Flex OS» con el enlace abierto

**Quién llama a quién.** Hay DOS canales distintos y que uno funcione no prueba el otro:

| Canal | Quién inicia | A dónde | Qué prueba |
|---|---|---|---|
| Enlace Flex Phone (la pantalla *Conexión* del P4: «conectado», latencia) | P4 | teléfono `192.168.1.2:47820`, TCP propio | que el P4 llega al teléfono |
| Emparejar Flex Cloud (`flexstorage://attach?h=<ip:puerto>&o=<oferta>`) | **teléfono** | web del P4 `http://192.168.1.4:<puerto>/api/fs/phone/pair` | que el teléfono llega al P4 y habla HTTP |
| Sesión Flex Cloud (ya emparejado) | P4 | servidor de Flex Cloud del teléfono (puerto que el teléfono declara en el emparejamiento) | almacenamiento |

Que el P4 tenga `192.168.1.4` y el teléfono `192.168.1.2` es lo normal: misma LAN, IP distinta. Ningún
código compara IPs para decidir nada (`Lan.sameSubnet` usa la máscara REAL que da Android, y solo
para dar una pista cuando algo falla).

**La causa.** `AttachClient` hablaba con el P4 con `HttpURLConnection`. Android 9+ **prohíbe el HTTP
en claro** en esa pila (la app apunta a la API 35 y no declara nada), y lo hace lanzando una
`IOException` («Cleartext HTTP traffic to 192.168.1.4 not permitted») *antes* de abrir ningún socket.
Cualquier `IOException` se contaba como «no se pudo hablar con Flex OS… misma Wi‑Fi», así que con la
Wi‑Fi perfecta y el enlace abierto fallaba siempre. En la JVM del PC esa regla no existe: ninguna
prueba lo veía.

**La corrección.** `LocalHttpPoster` (módulo `:storage`) habla HTTP sobre un `Socket` propio, solo
con IPv4 literales de la red local (nada de nombres ni de Internet), y la app abre ese socket por la
red **Wi‑Fi** (`NetAddress.wifiSocket`) aunque Android haya elegido los datos móviles (una Wi‑Fi sin
Internet). No se activa `usesCleartextTraffic` para toda la app: Android no deja acotarlo por rango
de IP y abriría la puerta a cualquier conexión futura.

**Qué paso falla** (el mensaje y el «Detalle técnico» lo dicen, y el motivo sale también en logcat
`FlexPhone/FlexCloud`):

| Paso | Mensaje | Se reintenta |
|---|---|---|
| TCP, sin ruta o sin respuesta | «El teléfono no llega a Flex OS (ip:puerto)» + pista de subred si la hay, si no «aislamiento de clientes / red de invitados» | sí, 3 veces (el pedido no llegó) |
| TCP, puerto rechazado | «llegó a la dirección… pero su servidor web no aceptó la conexión» | sí |
| HTTP, abierto sin respuesta | «aceptó la conexión pero no contestó a tiempo» | **no** (la oferta de un solo uso pudo gastarse) |
| HTTP, respuesta que no es HTTP | «no contestó como Flex OS» | no |
| Aprobación (403 / 410 / 404 / prueba) | el motivo que da Flex OS | no |
| Corte de red mientras se espera la aprobación | «Se perdió la comunicación con Flex OS mientras esperaba su aprobación» | el sondeo sigue cada 1,5 s |

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
* Límites del teléfono: cabeceras ≤ 16 KB (y ≤ 64), JSON ≤ 64 KB, partes de
  64 KB a 16 MB (la web usa 1 MB; el gestor del P4, 256 KB, o lo justo para no
  pasar de 512 partes), miniaturas ≤ 512 KB, **6 conexiones a la vez** (la
  séptima recibe `503 server_busy` al instante, nunca se queda colgada), 4
  sesiones, 16 retos vivos de 60 s; 10 fallos de autenticación en un minuto
  bloquean esa IP un minuto.
* Límites del P4: la pasarela acepta como mucho una parte (17 MB) por
  petición y la reenvía a trozos de 12 KB con el buffer de E/S de siempre; un
  JSON de ≤ 4 KB se guarda para repetir la petición si el teléfono pide una
  sesión nueva. El emparejamiento tiene su propio limitador (sección 4).
* Cada petición del navegador que cambia algo exige la cabecera `X-Flex` y la
  sesión del P4 (CSRF); `Host` debe ser la IP del P4 (DNS rebinding).

## 7. Desconexión y recuperación

* Teléfono sin responder: Flex Cloud dice **"El teléfono no responde"** y
  reintenta con espera creciente (2 s … 60 s). Lo local sigue funcionando y
  nada se bloquea. Las transferencias esperan y siguen solas.
* En el P4 (Almacenamiento › Flex Cloud en tu teléfono): **«Poner en pausa»**
  deja de usar el teléfono sin olvidar el emparejamiento y **«Volver a
  conectar»** lo reanuda en el acto (sin esperas ni rechazos anteriores);
  **«Olvidar este teléfono»** (pide un segundo toque) borra la clave de la NVS.
  En el teléfono (Flex Phone › Flex Cloud): el interruptor **«Compartir con
  Flex OS»** o **«Detener»** en la notificación paran el servidor; **«Olvidar
  este Flex OS»** borra su clave del Keystore y **«Borrar todo lo de Flex
  Cloud»** borra los archivos (también la papelera).
* **Si cambia la IP (o el puerto) del teléfono**, el P4 deja de llegar a él y
  dice "El teléfono no responde". Se arregla sin reiniciar nada: en el
  teléfono, abrir otra vez la web de Flex OS (QR) y pulsar «Activar Flex Cloud
  en este teléfono». Como el teléfono demuestra que conserva la clave, el P4
  lo aprueba solo (sin pregunta en pantalla) y guarda la dirección nueva.
* Sin Wi‑Fi, el teléfono deja de escuchar; cuando vuelve (o cambia la red),
  vuelve a escuchar él solo, por el aviso de red de Android, sin bucles.
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
| `android/FlexPhone/app/.../flexcloud/*` y `ui/screens/FlexCloud.kt` | `FlexStorageService` (primer plano `connectedDevice`), `StorageAttachActivity` (enlace `flexstorage://`), `FlexCloudPhone` (carpeta privada, cuota, espacio real), `KeystorePairingRepo` (clave envuelta por el Keystore) y la pantalla Flex Cloud | `:storage`, Keystore | Nuevo; manifiesto: un servicio y una actividad, **sin permisos nuevos** (el tipo `connectedDevice` se apoya en `CHANGE_WIFI_MULTICAST_STATE`, que ya estaba) |
| `FlexOS_Ultra/FlexOS_StorageCore.{h,cpp}` | Núcleo portable: registro, ECDH, derivaciones, ofertas, emparejamiento, estado | FlexOS_FlexAuth (HMAC), cJSON, OpenSSL/mbedTLS | Nuevo, probado en el PC |
| `FlexOS_Ultra/FlexOS_StorageLink.{h,cpp}` | Mitad de placa: NVS, cerrojo, sesión con el teléfono | StorageCore, HTTPClient | Nuevo |
| `FlexOS_Ultra/FlexOS_Cloud.{h,cpp}` | Destino Teléfono (URL, cliente sin TLS, token, diario propio, textos) | StorageLink | Con destino Internet el comportamiento es idéntico (las pruebas existentes lo vigilan) |
| `FlexOS_Ultra/FlexOS_CloudCore.{h,cpp}` | Textos nuevos (teléfono) | — | Solo añade |
| `FlexOS_Ultra/FlexOS_MediaWeb.{h,cpp}` | Rutas `/api/fs/*`, pasarela `/api/cloud/*`, IP del cliente | StorageLink (por funciones del anfitrión) | Las rutas existentes no cambian |
| `FlexOS_Ultra/FlexOS_Ultra_WebServer.h` | Anfitrión de las rutas nuevas; la hoja del QR enseña el teléfono y la aprobación | MediaWeb, StorageLink | Mismo servidor y mismo QR |
| `FlexOS_Ultra/webui/*` | La web Flex Storage con el diseño de Flex Cloud | MediaWeb | FX y el flujo de subida a Flex OS no cambian |
| `FlexOS_Ultra/FlexOS_Ultra_AppStorage.h` | Almacenamiento pasa a ser el centro de Flex Storage (Local + Flex Cloud + Transferir) | StorageLink, FlexOS_Cloud | Las cifras de siempre se conservan |
| `FlexOS_Ultra/FlexOS_Ultra_StoragePair.h` (nuevo, en la cadena tras FallAlert) | La pregunta "¿Emparejar este teléfono?" con el código de 6 cifras, como ventana del sistema (nunca con la pantalla bloqueada) | StorageLink | Nuevo; mismo patrón que la alerta de caídas (banda en PSRAM, un solo `present()`) |
| `FlexOS_Ultra/FlexOS_Ultra_CloudKit.h` | Con destino Teléfono, la nube no exige Flex Account: dice "Empareja tu teléfono", "Vuelve a emparejar" o "Reactiva el teléfono" y lleva a Almacenamiento | StorageLink | Con destino Internet, igual que antes |
| `FlexOS_Ultra/FlexOS_Ultra.ino` | `flexStorageBegin()` en `setup()`; `spaWatch()`/`spaTick()` en `loop()` como la alerta de caídas | StorageLink, StoragePair | Unas líneas |
| `FlexOS_Ultra/FlexOS_HttpShare.{h,cpp}`, `FlexOS_HttpSink.h` | El analizador HTTP acepta PUT y PATCH (DELETE ya estaba), `If-Range` y `X-Part-SHA256`, y analiza las respuestas del teléfono (solo para la pasarela). `FlexOS_HttpSink.h`: el buffer acotado de respuestas que comparten Flex Cloud y StorageLink | — | Lo que ya aceptaba se acepta igual (pruebas de siempre + nuevas) |
| Flex-Developer-Studio `cloud/` | `web/js/sha256.js` (huellas sin `crypto.subtle` por `http://`), perfil teléfono en `docs/API.md` | — | El servicio no cambia |
| `tests/host/*` | Pruebas del núcleo, del gestor con destino Teléfono, de las rutas y e2e contra el servidor Kotlin real | — | Nuevas baterías |

## 10. Pruebas

Todo se ejecuta en el PC, sin placa ni teléfono (ver la sección 11). Las
baterías nativas se compilan con ASan + UBSan.

| Batería | Qué demuestra | Resultado |
|---|---|---|
| `make -C tests/host run` (todas las de siempre + las nuevas) | El sketch completo compila (cadena, ganchos, prototipos, pila, `FlexOS_WebUI.h` al día) y **todas** las pruebas de antes siguen pasando | ✅ |
| `test_ino` → «Flex Storage» | La pregunta de emparejar en la pantalla real simulada (vertical y horizontal), nunca con bloqueo, antirrobo ni DeX; decidir, caducar; Flex Cloud con destino Teléfono sin Flex Account; Almacenamiento › Flex Cloud en tu teléfono (pausa, volver a conectar, olvidar con doble toque) | ✅ |
| `test_storagecore` | Núcleo del P4: vectores dorados compartidos con Kotlin, ECDH P‑256, ofertas y limitador, peticiones hostiles, sesión, registro NVS | ✅ 16 621 (OpenSSL) y 16 621 con **mbedTLS 3.6.2** |
| `test_cloud` → bloques «Flex Storage» | El gestor de Flex Cloud con destino Teléfono: sesión mutua, sesión que caduca, teléfono que no responde, rechaza o miente, cambio de destino sin perder nada, olvidar, apagado a mitad, streaming acotado, emparejar desde la web | ✅ 504 (toda la batería) |
| `test_mediaweb` / `test_httpshare` | El Flex Web Server con y sin Flex Storage (sin sus funciones, el servidor es el de siempre), emparejamiento sin sesión web, pasarela `/api/cloud` (el token del teléfono nunca llega al navegador), PUT/PATCH y respuestas del teléfono | ✅ 184 / 154 |
| `test_qr` | El QR de siempre | ✅ 171 |
| `tests/web` (`make web`): `webui.test.js`, `e2e.test.js`, `storage_e2e.test.js` | La lógica de la web (SHA‑256 contra Node, tamaños, cuota, destinos); el flujo **clásico** completo en Chromium (subir con conversión, biblioteca, ZIP, bloqueo...); y Flex Storage en Chromium contra el servidor web del P4 **y** el servidor real del teléfono: emparejar por QR, activar, subir por partes, carpetas, mover, renombrar, miniaturas, ver directo desde el teléfono, papelera, reanudar tras cerrar la pestaña, transferencias Flex OS ↔ Flex Cloud, teléfono desconectado | ✅ 284 / 94 / 85 |
| `tests/host/phone_e2e.sh` | El gestor y StorageLink del firmware contra `CloudServer` **real** (Kotlin en la JVM; 68 peticiones reales), y la app emparejándose con el servidor web del P4 | ✅ 41 + 16 |
| `tests/host/cloud_e2e.sh` | El destino **Internet** de siempre contra el servidor real de Flex Developer Studio: no ha cambiado | ✅ 38 |
| `gradle :storage:test` / `:protocol:test` | Servidor, almacén, sesiones y emparejamiento del teléfono; protocolo de Flex Phone intacto | ✅ 40 / 69 |
| `gradle -PflexTypecheck :typecheck:compileKotlin` | El pegamento Android y la pantalla compilan contra Android 15 (android‑all de Robolectric) y Compose Multiplatform 1.7 | ✅ (solo tipos) |
| `make all-boards` | Los tres perfiles de placa compilan y pasan | ✅ |
| Flex Developer Studio `cloud/`: `npm test`, `npm run test:e2e`, `npm run check` | El servicio de siempre; y la subida por `http://` sin `crypto.subtle` (antes se quedaba parada) | ✅ 62 / 28 / 0 problemas |
| Bucles de estabilidad | 10 vueltas de las baterías nativas de Flex Storage y 3 de cada e2e (web y teléfono), sin un fallo | ✅ |

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
* El P4 solo reproduce **AVI MJPEG, JPEG baseline y WAV PCM/IMA ADPCM**, pero lo que no sabe
  abrir (MP4/H.264, PNG, MP3...) **lo prepara el teléfono**: lo analiza por sus bytes, deja
  una versión del perfil y el P4 abre esa, con el mismo visor/reproductor que lo local. Ver
  [`FLEX-MEDIA-PROFILE.md`](FLEX-MEDIA-PROFILE.md) (perfil, conversión, streaming y límites:
  el P4 sigue sin reproducir el audio de un vídeo).
* **Si cambia la IP (o el puerto) del teléfono**, el P4 no lo encuentra solo:
  hay que volver a pulsar «Activar Flex Cloud en este teléfono» en la web de
  Flex OS (el P4 lo aprueba sin preguntar, porque el teléfono conserva la
  clave). Una reserva de IP en el router lo evita.
* **Pantalla apagada**: con la optimización de batería de Android activa, el
  sistema puede dormir la Wi‑Fi de Flex Phone y el P4 dejaría de llegar al
  teléfono hasta encenderlo. La pantalla Flex Cloud del teléfono lo avisa y
  lleva a los ajustes; la app no pide la exención por su cuenta.
* La web del P4 se sirve por `http://`, donde el navegador no ofrece
  `crypto.subtle`: las huellas SHA‑256 de las subidas se calculan en
  JavaScript (por partes de 1 MB, nunca el archivo entero en memoria). Es
  más lento que la huella nativa; en archivos grandes, desde el móvil, se
  puede notar.
* La cuota de Flex Cloud en el teléfono es **lógica** (Android no permite
  particiones por app): lo que de verdad cabe es lo que el teléfono tiene
  libre, y así se dice siempre (`limitedByDevice`).
* Lo que viaja por la Wi‑Fi local va en claro (HTTP). La clave del
  emparejamiento no viaja nunca y los tokens caducan, pero alguien en la misma
  red podría ver los archivos que pasan. Es la misma situación que la web del
  P4 de siempre, y se dice en las pantallas.
