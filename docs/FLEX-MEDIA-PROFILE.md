# Perfil multimedia de Flex OS Ultra y Flex Cloud como extensión de Fotos, Vídeos y Música

> **Qué se pidió:** que lo que está en Flex Cloud (el almacenamiento del A55) se abra con **los mismos**
> visor de fotos, reproductor de vídeo y reproductor de música que lo local — un solo reproductor, dos
> fuentes de datos — sin descargar vídeos enteros al flash del P4, y que lo que el P4 no sabe decodificar
> se **convierta antes** en el teléfono, no en el reloj.
>
> **Qué se hizo:** el P4 ya leía vídeo AVI MJPEG por rangos desde el teléfono; faltaban la **música**, el
> **análisis real de los bytes**, la **conversión** y decir con claridad *por qué* algo no se abre. Todo
> lo de abajo sale de leer el código, no de suponer.

Documento hermano: [`FLEX-STORAGE.md`](FLEX-STORAGE.md) (emparejamiento, sesiones, la API de Flex Cloud).

---

## A. Qué decodifica REALMENTE el P4 (auditoría del firmware)

Cita = archivo:línea dentro de `FlexOS_Ultra/` salvo que se diga otra cosa.

| Cosa | Hecho | Dónde |
|---|---|---|
| **Vídeo** | Solo **AVI con fotogramas MJPEG**. No hay decodificador de H.264/HEVC y el código lo dice. MP4/MOV/MKV/WebM se clasifican "no soportado" con motivo | `FlexOS_Media.h:47-52`, `FlexOS_Media.cpp:50-82`, `FlexOS_MediaLib.cpp:131-134` |
| Códec del AVI | `fccHandler` de la pista `vids` ∈ {`mjpg`,`jpeg`,`dmb1`,`mjpa`}; otro → `ERR_CODEC` | `FlexOS_Media.cpp:264, 314-318, 348` |
| **Audio dentro de un vídeo** | **El P4 NO lo reproduce.** Los trozos que no son de vídeo se saltan; el visor no llama a ningún `flexAudio*` | `FlexOS_Media.cpp:432`, `FlexOS_Ultra_GalleryVideoEdit.h:216` |
| Lectura del AVI | Flujo, nunca el archivo entero; `idx1` no es obligatorio (índice disperso de ≤512 posiciones); seek = salto a la muestra + avance de cabeceras | `FlexOS_Ultra_Media.h:139-200`, `FlexOS_Media.cpp:186-251, 476-527` |
| OpenDML (AVIX) | **No** (solo el primer `movi`) | inferido; sin test |
| JPEG | Decodificador propio: **baseline/SOF1, 8 bits**, 1 o 3 componentes, luma 1×1/2×1/1×2/2×2 con croma 1×1; progresivo, aritmético y 12 bits → no. Techo **4096×4096** | `FlexOS_JPEG.h:36-45, 81`, `FlexOS_JPEG.cpp:498-531` |
| EXIF | **No se lee**: un JPEG girado por EXIF se ve girado mal | `FlexOS_JPEG.cpp:461-466` |
| Audio | **WAV PCM 8/16 bits e IMA ADPCM 4 bits** (1–2 canales; el parseo admite 4–192 kHz, el I2S 8–96 kHz). MP3/AAC/FLAC/OGG **no** | `FlexOS_Media.cpp:556-581`, `FlexOS_Audio.cpp:253-255` |
| Imagen | Solo JPEG (y dibujos `.fxp`). PNG/GIF/BMP/WebP/HEIC/AVIF = "solo guardar". No usa JPEG por hardware (evaluado, no usado) | `FlexOS_MediaLib.cpp:123-128`, `docs/FLEX-MEDIA-ECOSYSTEM.md:383-388` |
| Memoria | Nada proporcional al tamaño del archivo: solo al mayor fotograma (≤1 MiB) y a los píxeles mostrados | `docs/FLEX-MEDIA-ECOSYSTEM.md:379-381` |

### Cómo abre el P4 un archivo (local y nube): una sola abstracción

```
        visor de vídeo · visor de fotos · Música
                       │
              MediaStream + FlexMediaIO { read, seek, size }      (FlexOS_Ultra_Media.h:107-116, 139-214)
              ┌────────┴─────────┐
        MSTREAM_INT           MSTREAM_CLOUD
        (LittleFS)            (flexCloudStreamRead → caché de bloques 48 × 64 KB, PSRAM)
```

Ruta `cloud:<id>/<nombre>` → `mediaStreamOpen` bifurca por el prefijo. **Esto ya existía para el vídeo.** Lo que
se añadió: **Música** lee ahora de ese mismo `MediaStream` (antes solo de LittleFS).

### Lo que NO se pudo medir

No se ha ejecutado en la placa: los **fps reales**, los ms por fotograma, el ritmo de LittleFS, el audio por el altavoz
(`Audio.h:48-53` ya lo declara) y el uso real de PSRAM. Los 10–15 fps del perfil son el **objetivo de diseño** de la web
(`FX.PROFILES`), no una medida.

---

## B. El perfil oficial: «Flex OS Ultra Media Profile v1» (`flexos-ultra-v1`)

Dos conceptos, para no confundir *«el P4 lo decodifica»* con *«el P4 lo reproduce a buen ritmo»*:

* **Sobre de compatibilidad**: hasta dónde se reproduce **tal cual**, sin tocarlo. Dentro del sobre no se convierte nunca.
* **Objetivo de conversión**: a lo que se convierte lo demás (es el perfil `rec` de la web de Flex OS, ya vigilado por
  las pruebas contra el firmware).

Código: `android/FlexPhone/storage/.../cloud/media/MediaProfile.kt` (única fuente en Kotlin).

### VÍDEO

| | Sobre (se reproduce tal cual) | Objetivo (al convertir) |
|---|---|---|
| Contenedor | AVI RIFF, un solo `movi`, sin OpenDML | AVI con `idx1` |
| Códec | MJPEG (fourcc de la tabla de A) | MJPEG |
| Fotogramas | JPEG baseline, del tamaño declarado, ≤ **192 KB** | JPEG baseline 4:2:0, calidad 62 (baja a 20 si pasa de 192 KB) |
| Resolución | lado largo ≤ **800** px | lado largo **640**, múltiplo de 8, mín. 16 |
| FPS | ≤ **15** | **12** (o los del original si son menos) |
| Audio | indiferente (el P4 lo ignora) | **sin pista de audio** (el P4 no la reproduce) |

> **El sonido de los vídeos no existe en el P4.** No es algo que Flex Cloud pueda arreglar: requiere que el visor
> decodifique el audio del AVI y lo alimente al DMA con sincronía de A/V — trabajo de firmware que no se ha hecho ni se
> puede validar sin la placa. Es el hueco más visible que queda.

### AUDIO

| | Sobre | Objetivo |
|---|---|---|
| Contenedor | WAV | WAV |
| Códec | PCM 8/16 bits o IMA ADPCM 4 bits (`blockAlign` ≤ 8192, múltiplo de 4·canales) | **IMA ADPCM** |
| Canales | 1–2 | **1** (el altavoz es mono) |
| Frecuencia | 8 000–96 000 Hz | **22 050 Hz** (nunca < 8 000, nunca se sube de la del original) |
| Bloque | — | `256·canales·⌊frec/11025⌋` bytes (512 a 22,05 kHz = 1 017 muestras) |

### IMAGEN

| | Sobre | Objetivo |
|---|---|---|
| Formato | JPEG baseline/SOF1, 8 bits, sin girar por EXIF | JPEG baseline |
| Tamaño | ≤ 4096×4096 y ≤ 8 MB (lo que el P4 trae de la nube) | lado largo **1600**, calidad 85, EXIF aplicado |
| Miniatura | — | lado largo **256**, ≤ **40 KB** (la respuesta JSON del P4 admite 48 KB) |

---

## C. Qué se reproduce directamente (plan `NONE`)

AVI MJPEG baseline dentro del sobre · WAV PCM 8/16 e IMA mono/estéreo dentro del sobre · JPEG baseline dentro del
sobre. **Se sirve el original, byte a byte; no se convierte ni se duplica nada.** (Una foto compatible pero grande —más
de 1,5 MB o 2 400 px— además tiene una *vista previa* ligera para que el P4 la traiga en un momento.)

## D. Qué requiere REMUX (plan `REMUX`: los flujos valen, el contenedor no)

Se re-empaqueta **sin recodificar** (los bytes de cada fotograma son idénticos; hay prueba que lo comprueba):

| Entrada | Salida |
|---|---|
| MOV/MP4 cuyo vídeo es JPEG por fotograma (`jpeg`) y cabe en el sobre | AVI MJPEG |
| AVI MJPEG sin etiqueta de códec en la cabecera (`fccHandler` vacío; el firmware lo rechazaría) | AVI MJPEG con la cabecera corregida |
| MOV/MP4 de audio con PCM de 16 bits LE (`sowt`) | WAV PCM |

Si un solo fotograma no vale (progresivo, demasiado grande, cortado) el remux se niega y se **cae a recodificar**.
Renombrar la extensión **no** es nunca una conversión: el analizador mira los bytes.

## E. Qué requiere TRANSCODIFICACIÓN (plan `TRANSCODE`)

| Entrada | Salida | Quién lo decodifica |
|---|---|---|
| Vídeo H.264, HEVC, MPEG-4, VP8/9, AV1, H.263 en MP4/MOV/3GP/WebM/MKV | AVI MJPEG (sin audio) | `MediaExtractor` + `MediaCodec` (según el teléfono) |
| MJPEG de más de 800 px / 15 fps / fotogramas de más de 192 KB / progresivo / girado / OpenDML | AVI MJPEG del perfil | `BitmapFactory`, fotograma a fotograma (sin MediaCodec) |
| Foto PNG, GIF, BMP, WebP, HEIC/AVIF, JPEG progresivo, JPEG girado por EXIF, > 4096 px o > 8 MB | JPEG baseline derecho | `BitmapFactory` |
| Audio MP3, AAC/M4A, FLAC, Ogg Vorbis/Opus, AMR | WAV IMA mono 22 kHz | `MediaExtractor` + `MediaCodec` |
| WAV de 24/32 bits, float, µ-law, A-law, > 2 canales, fuera de 8–96 kHz | WAV IMA mono | Kotlin puro |

**No se puede convertir** (`UNSUPPORTED`, y se dice por qué): AVI con H.264/Xvid (Android no abre AVI), ProRes, WMV/WMA,
AIFF, OGG sin Vorbis/Opus, ALAC. **Roto** (`CORRUPT`): JPEG/PNG cortado, MP4 sin `moov` (grabación interrumpida), AVI o WAV
con cabecera rota o sin datos. Un `.jpg` que en realidad es un texto también se dice.

## F. Dónde ocurre la conversión: en el A55, con las APIs del sistema

```
 archivo llega a Flex Cloud ──► MediaAnalyzer (bytes, no extensión) ──► NONE ─► native (sin tocar nada)
        (cola de 1 hilo, baja prioridad)                         ├► REMUX/TRANSCODE ─► StandardConverter ─► verificar con el
        espera buen momento (cargador / >15 % / sin ahorro)      │                      MISMO analizador ─► ready (+ SHA-256)
        CPU despierta solo mientras trabaja, con tope            └► UNSUPPORTED/CORRUPT ─► el motivo
```

* **No se añade FFmpeg ni ninguna biblioteca**: `BitmapFactory`, `MediaExtractor`, `MediaCodec` ya están en Android. Ni tamaño de APK,
  ni ABI, ni licencia que evaluar. El escritor de AVI, el codificador IMA ADPCM, el remuestreador, los demux de MP4/MOV y Matroska
  y la conversión YUV→RGB están en Kotlin puro (módulo `:storage`) y se prueban en el PC.
* **El P4 no transcodifica nada**: solo hace streaming, buffering, decodificación y UI.
* **Nunca se publica algo que el propio analizador no daría por bueno** (un conversor que mintiera quedaría en `failed`).
* Parar el servicio a mitad devuelve el archivo a la cola (no es un fallo del archivo). Borrar el original cancela la conversión.

## G. Original + derivado: decisión

**Se conserva el original SIEMPRE y la versión del perfil es un objeto aparte, ligado a él.**

| Criterio | Decisión |
|---|---|
| Almacenamiento | MJPEG pesa más que H.264 a igual resolución: a 640×360 y 12 fps cada fotograma suele ser de 20–40 KB, es decir **~15–30 MB por minuto** (*estimación, no medida con vídeo real*). Es menos que el 1080p de un móvil (≈75–150 MB/min) pero **puede ser más que un original de bitrate bajo**. Cuenta en la cuota: es espacio real y se dice |
| Calidad | El original no se toca: se puede volver a convertir con otro perfil |
| Otros dispositivos | La web del A55 y cualquier otro cliente siguen viendo el original (los navegadores sí reproducen H.264) |
| Batería | Se convierte **una vez**, en el A55, y solo si hace falta |
| Duplicados | La versión **no sale en las listas**: es un campo `playable` del archivo; se borra con él |
| Metadatos | `playable.state/plan/profile/size/sha256/mime` + hechos leídos (`width,height,durationMs,fpsX100,sampleRate,channels,container,codec,audioCodec`) |

## H. Streaming, Range y seek

Servidor del teléfono (`CloudServer.kt`): `GET|HEAD /api/cloud/files/:id/playable` — **el mismo camino de bytes que
`/download/:id`**: `Range: bytes=a-b | a- | -n` → `206` + `Content-Range`, `Accept-Ranges: bytes`, `ETag` = SHA-256,
`If-Range`, `Content-Length` ajustado, `HEAD` sin cuerpo, y `416` **con `Content-Range: bytes */N`** (antes sin él).
Sirve la versión del perfil si existe, o el original si ya vale; `409 not_ready` si aún no.

En el P4 (`FlexOS_Cloud.cpp`): la **misma caché de bloques de siempre** (48 × 64 KB = 3 MB en PSRAM, ≈2,9 MB por delante), con una conexión
`Range: bytes=off-` por bloque. **Nunca se carga el archivo entero en RAM ni se descarga al flash para reproducirlo.** Una foto
se trae como vista previa ligera (con *su* tamaño y *su* SHA-256), no como el original de varios MB. «Descargar a Flex OS» sigue
trayendo el original.

Música: `FLEXIO_AGAIN`. `FlexMediaIO.read` puede contestar «aún no han llegado esos bytes»; `flexAsPump` **entrega lo que tiene,
queda «Cargando…» y sigue cuando llegan, sin perder ni repetir una muestra** (incluida la lectura parcial a mitad de un bloque).
La lectura de Música desde la nube **no bloquea** `loop()`.

### Reconexión
* Sin Wi-Fi o con el teléfono fuera: «Reconectando…» con espera acotada a **5 s** (antes crecía hasta 75 s) y **paciencia de 90 s**
  antes de declarar el fallo; al volver, **sigue donde estaba** (la posición y la caché se conservan). Probado: 60 s de corte →
  sigue esperando; vuelve → bytes correctos desde el mismo punto; 100 s → error claro.
* Música: si el flujo falla se dice con su motivo; 45 s sin recibir el principio → «Sin conexión con Flex Cloud».

## I. Lo que ve la persona

| Estado | Dónde | Texto |
|---|---|---|
| `native` | Detalles | «Compatible con Flex OS» |
| `ready` | Detalles | «Preparado para Flex OS» |
| `pending` | lista y al tocar | «En cola» · «El teléfono lo está preparando para Flex OS» |
| `preparing` | lista, Detalles, al tocar, notificación y pantalla del A55 | «Preparando 42 %» · «Preparando archivos para Flex OS · N en cola» |
| `failed` | lista y al tocar | «No se preparó» + el motivo real del teléfono |
| `unsupported` | lista y al tocar | «No se convierte» + el motivo (nunca se intenta abrir para que falle) |
| `corrupt` | lista y al tocar | «Dañado» + el motivo |

Tocar un elemento de Flex Cloud abre **el visor de fotos, el reproductor de vídeo o Música de siempre**; la lista, el
subtítulo y los Detalles muestran la **duración** que el teléfono leyó de los bytes (nunca inventada: si no se sabe, no se
muestra). La ruta que recibe el reproductor lleva la extensión de **lo que llega** (`.avi`, `.wav`, `.jpg`), porque por ella
decide el P4 quién lo abre.

## J. Pruebas (todas en el PC; ninguna sustituye a la placa)

| Qué | Cuántas | Qué demuestra |
|---|---|---|
| `gradle :storage:test` | 140 | analizador (28), escritores y remuxes (14), conversiones con fuentes sintéticas (19), servidor + cola con sockets reales (17), YUV (5), y las de siempre |
| `tests/host/media_profile_e2e.sh` | 65 | lo que **escribe** Kotlin (AVI MJPEG, WAV IMA, remuxes, conversiones) lo **abren y decodifican el demux AVI, el decodificador JPEG y el parser WAV REALES del firmware** (`mediacheck`); SNR de IMA ≥ 24 dB |
| `tests/host/phone_e2e.sh` | 88 + 6 | el **código del P4** contra el **servidor y la cola reales del teléfono** con 10 archivos (H.264, PNG, JPEG progresivo, foto grande, WAV 24 bits, AAC, nativos, rotos): el P4 los abre por su camino y lo recibido lo decodifica el firmware (24/24 fotogramas) |
| `test_cloudcore` | 148 | parseo de `playable`, decisión de apertura, textos |
| `test_cloud` | 508 | reconexión: 60 s esperando, retoma; 90 s, error |
| `test_media` | 2 456 | audio con datos que llegan a trozos (PCM16, PCM8, IMA) idéntico al de tener todo el archivo |
| `test_ino` | — | Música desde la nube con el código real (cargando, no es un error, siguiente/anterior, fallos); un MP4 preparado se abre en el visor; uno imposible o en preparación, no |
| `gradle -PflexTypecheck :typecheck:compileKotlin` | — | el pegamento de Android compila contra Android 15 (`android-all`) |

## K. Límites honestos

1. **No se ha ejecutado en la placa ni en el teléfono.** `MediaCodec` y `BitmapFactory` solo se han *compilado* contra Android 15;
   la lógica que los envuelve (cola, fps, giro, remuestreo, YUV→RGB, AVI/WAV) sí se prueba. Un códec que ese teléfono no tenga
   (AV1 antiguo, HEVC de 10 bits/HDR) dará «no se puede convertir» con el motivo.
2. **Sin sonido en los vídeos** (ver B). **Sin música con «siguiente» fuera de la lista abierta:** anterior/siguiente recorren
   la lista de Flex Cloud que se está viendo; no hay una pestaña «Nube» propia dentro de Música (se abre desde Archivos › Flex Cloud).
3. Una sola arena de streaming de 3 MB para todo el P4: si se abre un vídeo de la nube con música de la nube sonando, la
   música se corta y lo dice.
4. La web del navegador (Flex Storage) **no muestra** aún el estado de preparación; sube a Flex Cloud el original sin convertir
   (la conversión la hace ahora el teléfono, no el navegador).
5. HEIC/AVIF dependen de la versión de Android (`BitmapFactory` los abre desde la 9 y la 12 respectivamente).
6. Conversión de vídeos de varios GB: tarda (se hace con el teléfono despierto y con el aviso en la notificación); el original
   está disponible mientras tanto.
7. `THUMB_MAX` en el P4 es 96 KB pero la respuesta JSON cabe en 48 KB: las miniaturas del teléfono se mantienen ≤ 40 KB.

## L. Qué probar físicamente

**En el A55 + P4 (misma Wi-Fi):**
1. Instala la APK; activa Flex Cloud (ver `FLEX-STORAGE.md` §4.3). Sube por la web: un MP4 de móvil (H.264, vertical), un
   AVI MJPEG ya compatible, un MP3/AAC, un WAV compatible, un PNG y una foto de 12 MP.
2. En el A55: la pantalla Flex Cloud dice «Preparando archivos para Flex OS · N en cola»; la notificación igual. Con el
   móvil en ahorro de energía y sin cargador, espera.
3. En el P4 › Archivos › Flex Cloud (o la pestaña Nube de Galería/Multimedia): el subtítulo pasa de «Preparando 40 %» a la
   duración. **Vídeo:** tócalo → se abre el reproductor de vídeo de siempre; **pausa, barra, cambiar de posición, pantalla completa,
   horizontal**; el vídeo **vertical sale vertical**. **Foto:** tócala (la vista ligera llega en un instante); la de 12 MP debe
   abrirse sin esperar al original. **Música:** tócala → Música: play/pausa, **buscar**, **volumen**, siguiente/anterior.
4. Corta la Wi-Fi del A55 10–30 s **mientras suena/se ve**: debe poner «Cargando…» y **continuar desde la posición** al volver (sin
   volver al 0). A los 90 s sin Wi-Fi debe decir el error.
5. Un `.avi` con H.264/Xvid o un MP3 roto: debe decir *por qué* no se abre, sin intentarlo.
6. Mira en la consola serie del P4 `[CLOUD] streaming: … (… bytes, versión del perfil)` y la RAM libre (PSRAM) durante varios minutos de
   reproducción: debe ser estable; vigila los síntomas ya conocidos de Multimedia (cierre del visor con el dedo, parpadeo cian,
   watchdog, fotogramas fantasma).
7. Mide y apunta los **fps reales** a 640×360/12 fps y a 800 px/15 fps: es el único dato que falta para confirmar el sobre de B.
