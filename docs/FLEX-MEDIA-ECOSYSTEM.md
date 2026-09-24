# Flex Media Ecosystem

Galería, Multimedia, Música y Flex Web Server leen y escriben **una sola
biblioteca de medios**. Este documento es el contrato de diseño: qué se
reutiliza, qué se construye, qué hace de verdad el ESP32-P4 y qué no.

Se escribió antes que el código. Si el código lo contradice, gana el código
solo después de corregir aquí.

---

## 0. Lo que había antes, medido en el repositorio

| Pieza | Estado real antes de este trabajo |
|---|---|
| Galería (`FlexOS_Ultra_AppGallery.h`) | rejilla 3×N sobre el índice por escaneo, caché LRU de 16 miniaturas **decodificadas en `loopTask`** (JPEG de hasta 768 KB), menú de 4 acciones del kit de archivos, selección múltiple **sin** "seleccionar todo", papelera |
| Multimedia (`FlexOS_Ultra_AppMultimedia.h`) | visor real: JPEG baseline con zoom, AVI MJPEG, WAV PCM; pestañas Todo/Vídeos/Fotos/Audio |
| Índice (`FlexOS_Media.*` + `FlexOS_Ultra_Media.h`) | recorrido incremental de `/Documentos` y `/Paint`. **Nada persistente**: ni id, ni fecha, ni dimensiones, ni bloqueo |
| Música | **no existía** (ni en esta rama ni en ninguna otra del remoto) |
| Servidor web | **no había ninguno activo**. `FlexOS_HttpShare` y `FlexOS_QR` estaban archivados en `futuras-versiones-BETA/Flex-Vector-Pro`, con sus pruebas |
| Seguridad | PIN/contraseña del sistema (`FlexOS_Passcode`: sal + PBKDF2) y la ruta única de verificación `lsuStartVerifyFor` → `lsuFinishAfter` |
| Codificador JPEG | **no había** (solo decodificador) |

## 1. El hardware, y solo estas cifras

| Pieza | Valor | Consecuencia |
|---|---|---|
| Almacenamiento | LittleFS en `spiffs` = **0xAE0000 B ≈ 10,9 MB** | presupuesto por tipo, reserva fija, miniaturas pequeñas |
| PSRAM | 32 MB, 6 MB de reserva del sistema, aviso por debajo de 6 MB libres | el editor elige su resolución de trabajo según la PSRAM libre |
| Radio | C6 por esp-hosted (SDIO) | la red **nunca** se toca desde `loopTask` |
| Vídeo | sin decodificador por hardware | solo **AVI MJPEG** se reproduce; MP4/H.264/HEVC se guarda pero **no se reproduce** |
| Audio | ES8311 por I2S, sin decodificador MP3/AAC en el core | solo **WAV PCM 8/16 bits** se reproduce |
| Imagen | decodificador propio | solo **JPEG baseline** (no progresivo, no PNG/WebP/HEIC) |

## 2. Decisiones

1. **Una biblioteca, no tres.** `FlexOS_MediaLib` (portable, con pruebas) es el
   catálogo persistente: `media_id`, nombre original, ruta interna, tipo,
   formato real detectado por firma, tamaño, fechas, dimensiones, duración,
   bloqueo, miniatura, estado de procesamiento y error. Vive en
   `/System/Media/library.fml` con CRC y escritura atómica. El índice por
   escaneo de siempre sigue siendo el **descubridor** de archivos; el catálogo
   se reconcilia con él.
2. **Destino por tipo, estricto.** Imagen y vídeo → Galería y Multimedia (un
   solo archivo físico). Audio → Música. Multimedia deja de listar audio.
3. **Música se crea** porque no existía. Id 18, al final del registro (no se
   mueve ningún id). No duplica el motor de audio: el reproductor WAV sale de
   Multimedia a un motor compartido.
4. **Flex Web Server reutiliza `FlexOS_HttpShare`**, que se promueve desde la
   BETA junto con `FlexOS_QR`. La lógica del servidor (`FlexOS_MediaWeb`) es
   portable y se prueba de extremo a extremo en el PC; la mitad de placa solo
   pone sockets, LittleFS y una tarea propia.
5. **La conversión pesada la hace el móvil.** El navegador ya decodifica lo
   que el P4 no puede (HEIC, PNG, WebP, JPEG progresivo, MP4, MP3, AAC). La web
   convierte a lo que el P4 **sí** reproduce: JPEG baseline, AVI MJPEG y WAV
   PCM, con perfiles. El P4 **valida** lo que recibe (firma, cabeceras,
   decodificación completa de la miniatura, CRC de extremo a extremo) y no
   transcodifica.
6. **Miniaturas persistentes** en JPEG (codificador nuevo `FlexOS_JPEGEnc`),
   cuadradas y recortadas al centro, generadas en una **tarea de fondo**, no en
   `loopTask`. La caché en RAM es compartida por las tres apps.
7. **Bloqueo = campo del catálogo + control de acceso del sistema.** Usa el
   método de Seguridad del usuario a través de `lsuStartVerifyFor`
   (`LSU_AFTER_MEDIA`). Sin PIN/contraseña no se puede bloquear. Un elemento
   bloqueado pierde su miniatura persistente y su entrada de la caché (se
   borran los píxeles, no solo la marca), no aparece en Archivos, ni en el
   selector de fondos, ni por su nombre en Almacenamiento, y la web no sirve
   ni su miniatura ni su contenido sin sesión de propietario.
8. **El editor vive en la Galería** (`FlexOS_Ultra_GalleryEdit.h`, misma app
   y mismo ciclo de vida). Trabaja en RGB888 para no introducir bandas: se
   añade una salida RGB888 al decodificador compartido sin tocar la RGB565.

## 3. Lo que NO se hace, con el motivo

| No | Por qué |
|---|---|
| Reproducir MP4/H.264/HEVC en el P4 | no hay decodificador; se guarda, se indexa, se ve su miniatura y se descarga, pero se marca "no reproducible en esta placa" |
| Transcodificar en el P4 | CPU, PSRAM y flash no dan; el navegador lo hace mejor y sin riesgo para el sistema |
| Reproducir MP3/AAC | no hay decodificador en el core; el MP3 se convierte a WAV en el móvil (con aviso de tamaño) o se guarda como original no reproducible |
| Cifrado de los archivos bloqueados | el bloqueo es control de acceso de Flex OS (interfaz, explorador, web), **no** protege frente a un volcado físico de la flash. Se dice así en la interfaz y aquí. La antigua Carpeta segura cifrada no se reintroduce |
| Borrar desde la web | no se pidió y reduce la superficie de ataque |
| Descargar sin autenticación contenido bloqueado | nunca |
| HEIC/WebP/PNG en el P4 | sin decodificador viable; se convierten en el móvil |

## 4. Rutas

| Qué | Dónde |
|---|---|
| Fotos recibidas | `/Imagenes` (la misma carpeta que ya lee el selector de fondos) |
| Vídeos | `/Videos` |
| Música | `/Musica` |
| Catálogo | `/System/Media/library.fml` |
| Miniaturas | `/System/Media/th/<id>.jpg` |
| Temporales de subida y de guardado | `/System/Media/tmp/` (se vacía al arrancar el servicio) |
| Raíces que se indexan | `/Imagenes`, `/Videos`, `/Musica`, `/Documentos`, `/Paint`, `/Camara`, `/Descargas` |

## 5. Límites por defecto (configurables en `FlexOS_MediaLib.h`)

| Límite | Valor |
|---|---|
| Foto | 6 MB (el mismo tope del visor) |
| Vídeo | 8 MB |
| Audio | 8 MB |
| Reserva libre que nunca se consume | 512 KB |
| Elementos del catálogo | 384 (el mismo tope que el índice) |

El resto del documento (API, pruebas y medidas) se completa en §6–§9.
