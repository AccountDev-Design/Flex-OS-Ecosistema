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
| Audio | ES8311 por I2S, sin decodificador MP3/AAC en el core | se reproduce **WAV PCM 8/16 bits** y **WAV IMA ADPCM** de 4 bits (lo produce la web al convertir un MP3); MP3/AAC/FLAC/OGG no |
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
   (`LSU_AFTER_MEDIA`). Sin PIN/contraseña no se puede bloquear (se explica y
   se ofrece ir a Seguridad). Bloquear **mueve** el archivo y su miniatura
   persistente a `/System/Media/Protegido/` con nombres neutros (`<id>.jpg`,
   `<id>.t.jpg`) y borra su entrada de la caché en RAM (los píxeles, no solo
   la marca). Mientras está bloqueado, ninguna interfaz enseña su miniatura
   ni su nombre (Galería, Multimedia y Música solo un candado), no aparece en
   Archivos, ni en el selector de fondos, ni por su ruta en Almacenamiento, y
   la web no sirve ni su miniatura ni su contenido sin sesión de propietario.
8. **El editor vive en la Galería** (`FlexOS_Ultra_GalleryEdit.h`, misma app
   y mismo ciclo de vida). Trabaja en RGB888 para no introducir bandas: se
   añade una salida RGB888 al decodificador compartido sin tocar la RGB565.
   Ver §7.

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
| Protegidos (archivo y miniatura) | `/System/Media/Protegido/<id>.<ext>` y `<id>.t.jpg` |
| Original apartado mientras se reemplaza | `<ruta>.fxorig`, junto al original (lo resuelve el recorrido si se va la luz) |
| Raíces que se indexan | `/Imagenes`, `/Videos`, `/Musica`, `/Documentos`, `/Paint`, `/Camara`, `/Descargas` |

## 5. Límites por defecto (configurables en `FlexOS_MediaLib.h`)

| Límite | Valor |
|---|---|
| Foto | 6 MB (el mismo tope del visor) |
| Vídeo | 8 MB |
| Audio | 8 MB |
| Reserva libre que nunca se consume | 512 KB |
| Elementos del catálogo | 384 (el mismo tope que el índice) |

## 6. Piezas y API

### Núcleo portable (compila igual en el P4 y en el PC; pruebas con ASan/UBSan)

| Módulo | Qué decide |
|---|---|
| `FlexOS_MediaLib` | catálogo: registro POD, firmas (formato real), nombres seguros, vistas filtradas/ordenadas, serialización con CRC |
| `FlexOS_MediaStore` | lo que la biblioteca hace con el disco: reconciliar, miniaturas, bloquear/desbloquear (mover), borrar, papelera, renombrar, **reemplazar** (`flexMsReplace`), **añadir copia** (`flexMsAddFile(..., parent, ...)`), publicar subidas. Regla: el cambio de disco y el del registro van bajo el MISMO cerrojo (no recursivo) |
| `FlexOS_MediaThumb` | miniatura cuadrada 132 px desde JPEG, AVI MJPEG o dibujo, a JPEG |
| `FlexOS_JPEGEnc` | codificador JPEG baseline 4:2:0/4:4:4 por filas de MCU (nada de imagen entera en RAM) |
| `FlexOS_MediaWeb` + `FlexOS_HttpShare` + `FlexOS_QR` | Flex Web Server: sesión de propietario, subida por trozos con CRC, descarga, ZIP, bloqueo con la clave del sistema |
| `FlexOS_Media` (`FlexAudioStream`) | lectura por bloques de WAV PCM e IMA ADPCM a PCM16 |
| `FlexOS_ImgEdit` | el editor: estado no destructivo, historial, geometría, color, filtros, trazos/formas/textos, render por filas y guardado por bandas |

### Placa (partes del sketch único)

| Módulo | Qué hace |
|---|---|
| `FlexOS_Ultra_MediaLib.h` | el almacén sobre LittleFS, tarea de fondo `flexMedia` (reconciliar, miniaturas, guardar), caché de miniaturas, menú/diálogo y ruta de la clave compartidos |
| `FlexOS_Ultra_FileKit.h` | menú, nombre, confirmación y Papelera (hasta 256 elementos; la lista vive en PSRAM y se suelta al cerrarla) |
| `FlexOS_Ultra_MediaKit.h` | kit de listas: selección, "Seleccionar todo"/"Deseleccionar todo" (sin protegidos), menú por elemento, acciones y la hoja del servidor. Menú, diálogos y Papelera con los colores y el material del tema |
| `FlexOS_Ultra_MediaViewer.h` | **el** visor de fotos, dibujos y vídeos del sistema (§10); lo usan la Galería y Multimedia |
| `FlexOS_Ultra_AppGallery.h` | Galería (fotos, vídeos, dibujos); un toque abre el elemento en su propio visor, sin salir de la app |
| `FlexOS_Ultra_GalleryEdit.h` | editor de la Galería (§7) |
| `FlexOS_Ultra_AppMultimedia.h` | lista de vídeos y fotos; abre cada elemento en el visor común (§10); el audio lo manda a Música |
| `FlexOS_Ultra_AppMusic.h` | Música: lista, "Reproduciendo", anterior/siguiente sin protegidos, DMA alimentado desde `loop()` |
| `FlexOS_Ultra_WebServer.h` | tarea `flexWeb`, cola de eventos hacia `loopTask`, hoja "Conectar con el móvil" con QR (repinta solo la zona que cambia, §11) |

Funciones de la biblioteca que usan las apps (todas en `loopTask`):
`mlGet`, `mlRev`, `mlDelete`, `mlTrash`, `mlSetLock`, `mlRename`,
`mlAddFile(tmp, kind, nombre, origen, parent, why, cap)`,
`mlReplaceFile(id, tmp, why, cap)`, `mlRequestScan`, `mlThumbGetLocked`,
`mediaAuthRequest` (clave del sistema).

## 7. El editor de la Galería

**Entrada:** pulsación larga sobre una foto → menú → **Editar**, o el botón
**Editar** de la barra del visor (§10). Solo se ofrece para fotos JPEG no
protegidas que el P4 sabe decodificar (`gedEditable`). Lo protegido no se
edita.

**Aspecto:** los colores y las superficies son los del tema del sistema
(Claro/Oscuro, Liquid Glass o plano: `uiSurfaceFlat`/`uiSurface` y los
tokens `TH_*`), no una paleta propia. Solo lo que va **encima de la foto**
(marco de recorte, tercios, marco del texto) es blanco/negro fijo.

**Herramientas:** Recortar (esquinas, lados y mover; proporciones Libre, 1:1,
4:3, 3:4, 16:9 y Original; girar 90° a izquierda y derecha; voltear en
horizontal y vertical), Ajustes (brillo, contraste, saturación, exposición,
temperatura, sombras, luces y nitidez, de −100 a +100), Filtros (Original,
B/N, Sepia, Vívido, Frío, Cálido y Vintage, con intensidad y miniaturas
reales), Dibujo (pincel, línea, flecha, rectángulo y elipse, con relleno,
8 colores y 3 grosores), Texto (fuente del sistema, 8 colores, 3 tamaños,
se coloca arrastrando) y Tamaño (Original, 75 %, 50 %, 25 %, y
"Restablecer todo", que también se puede deshacer). Deshacer/rehacer de 40
pasos; arrastrar un regulador es **un** paso.

**Cómo funciona:**

- Edición **no destructiva**: la base (RGB888 en PSRAM) no se toca; el estado
  (geometría, ajustes, filtro, figuras normalizadas a la foto) y su historial
  son pequeños. La imagen editada se calcula por filas a cualquier tamaño.
- Vista previa desde una **copia reducida** (720 px de lado largo) salvo que el
  recorte pida más detalle; se calcula una vez por cambio y se guarda en
  RGB565, así que arrastrar un recorte, dibujar o colocar un texto solo
  recompone encima. En vivo, como mucho un repintado cada 40 ms.
- **Abrir** lee la foto **por trozos**: el decodificador en flujo guarda una
  ventana de 8 KB y, al leer la cabecera, `gedPickCb` elige el divisor (1, 2,
  4 u 8) con la PSRAM libre real y reserva la base en un bloque. Antes se
  leía el archivo entero (hasta 6 MB) a PSRAM y luego se decodificaba.
- **Abrir y guardar** corren en un trabajador de un solo uso (`flexEdit`,
  núcleo 1, prioridad 1, 8 KB de pila), con progreso real y Cancelar. Mientras
  trabaja, la interfaz no toca el estado; el resultado se publica con barrera
  `__atomic` y lo recoge `loopTask`. Cede la CPU cada ~20 ms.
- **Guardar**: comprobación previa de espacio (≈0,5 B/px + 128 KB + la reserva
  de 512 KB) y de memoria; JPEG calidad 92 por bandas a
  `/System/Media/tmp/ed-<id>.jpg`; comprobación del temporal (tamaño en disco,
  cabecera con las medidas esperadas y marca de fin `FFD9`); y solo entonces
  se publica en `loopTask`: **Guardar como copia** (id nueva, nombre libre
  "X (editada).jpg", `parent` = la original, miniatura nueva) o
  **Reemplazar original** (con confirmación; el original se aparta junto a sí
  mismo y solo se borra cuando el nuevo ya está en su sitio). Si algo falla,
  el original no cambia y no quedan temporales.
- **Protegidos**: si la foto se bloquea o se borra con el editor abierto
  (desde Multimedia, por ejemplo), el editor se cierra y suelta los píxeles en
  cuanto cambia el catálogo; si ocurre a mitad de guardar, no se publica nada
  (ni copia sin proteger).
- **Ciclo de vida**: la Galería declara `APP_BG_KEEP` + `bgWork` mientras el
  editor guarda (no se desaloja a mitad); `loop()` publica el resultado aunque
  la Galería no esté delante; `shed` suelta la foto (las ediciones se quedan y
  se relee al volver); `dirty` avisa de cambios sin guardar; ATRÁS pregunta
  antes de descartar.

## 8. Pruebas

Todo se ejecuta con `make run` en `tests/host` (con AddressSanitizer y
UndefinedBehaviorSanitizer) y las pruebas web con Node y Chromium:

| Prueba | Qué comprueba |
|---|---|
| `test_medialib` | catálogo, firmas, nombres, vistas, serialización |
| `test_mediastore` | disco con fallos provocados e hilos a la vez: bloquear/mover, borrar, renombrar, reemplazar con corte de luz a mitad, copia con `parent`, catálogo lleno |
| `test_mediathumb`, `test_jpegenc`, `test_jpeg` | miniaturas y JPEG (lo codificado se vuelve a leer con el decodificador del firmware) |
| `test_mediaweb`, `test_httpshare`, `test_qr` | servidor, protocolo HTTP y QR |
| `test_media` | clasificación, AVI/MJPEG, WAV PCM e IMA ADPCM por bloques |
| `test_imgedit` | el núcleo del editor: identidad exacta, giros/volteos/recorte, historial (incluido restablecer y deshacer), cada ajuste en su sentido, filtros, trazos/formas/textos pegados a la foto, copia reducida, guardado por bandas que el firmware abre y cancelación sin memoria viva |
| `test_ino` | el sketch entero enlazado: kit de listas, Música y el **editor de punta a punta** sobre un disco en memoria (abrir, toques reales, copia, reemplazo, protegidos, cancelar, disco lleno, escritura que falla, soltar memoria y releer, guardado en segundo plano, trabajador que tarda); el **visor** mirando el framebuffer (ajuste y centrado en las dos orientaciones, vidrio idéntico tras repintar y tras Play/Pausa ×10, auto-ocultado, pellizco, arrastre, deslizar, Papelera, protegidos, sin captura en Recientes, vídeo, PSRAM devuelta); la hoja web que solo repinta su zona viva; el menú contextual sin vidrio apilado; la Papelera con más de 16 elementos |
| `check_wiring.py` | ganchos obligatorios y llamadas prohibidas (p. ej. el trabajador del editor no pinta, no avisa por la isla y no cambia el catálogo) |
| `tests/web` | interfaz del móvil (conversión, subida, biblioteca, bloqueo) contra el servidor real compilado para el PC |

Las pruebas del núcleo del editor y del pegamento de la placa se validaron
además con mutaciones (romper el giro, deshacer, el brillo, la copia
reducida, la liberación de memoria, la comprobación del candado al publicar,
la del temporal...): cada mutación hace fallar al menos una comprobación.

## 9. Límites reales y lo que falta por medir en la placa

| Límite | Valor / motivo |
|---|---|
| Resolución de trabajo del editor | hasta ~3,1 MP (`FLEXIE_BASE_MAX_PX` = 2048×1536): una foto de 12 MP se abre a la mitad de lado (1/2, 1/4 u 1/8 lo hace el decodificador). La hoja de guardar lo dice y "Reemplazar" lo advierte |
| Tamaño de archivo que abre el editor | 6 MB (`FML_LIMIT_PHOTO`) |
| Formatos editables | JPEG baseline (ni progresivo, ni PNG/WebP/HEIC: esos los convierte el móvil al subirlos) |
| Figuras por edición | 48 trazos+formas+textos y 4096 puntos de trazo (restablecer no los recupera: se pueden deshacer) |
| Texto | la fuente del sistema: caracteres latinos y acentos; lo que no tiene glifo sale como "?" |
| Vídeo | solo AVI MJPEG |
| Audio | WAV PCM 8/16 bits y WAV IMA ADPCM |

**Compilado de verdad para `esp32p4`** con el core 3.1.3 (tamaños y RAM en
`INSTALACION-USB-P4.md` §0): el firmware entero compila y enlaza, y las
tablas grandes de los medios (selección, vistas, cola de eventos del
servidor) se reservan en PSRAM para no gastar la RAM interna, que es la
justa.

**Core 3.2.1 (auditoría del 26-09-2026):** el sketch se volvió a compilar
(sin enlazar) con el toolchain y las librerías reales de arduino-esp32 3.2.1
para `esp32p4`, con los mismos flags de su `platform.txt`: compila, sin
avisos nuevos en los módulos tocados.

**No ejecutado aún en el ESP32-P4 real** (no hay placa en este entorno): los
tiempos de apertura y guardado del editor, la fluidez de la vista previa en
vivo, los tiempos del visor (abrir, pellizco, vídeo), el efecto real de la
corrección del destello cian (§11) y el consumo real de PSRAM en cada paso. Todo lo anterior está
comprobado en el PC con el mismo código; las cifras de rendimiento hay que
tomarlas en la placa (el editor escribe en Serie el tamaño y los KB de cada
guardado).

## 10. El visor común (Galería y Multimedia)

`FlexOS_Ultra_MediaViewer.h` es el **único** visor de fotos, dibujos y vídeos
del sistema. Cada app lo usa con su `VwHost` (qué app es, su sesión para
volver al mismo elemento, el vecino para deslizar y, si la app lo ofrece,
Editar). El visor anterior de Multimedia se retiró.

- **Abrir:** en la Galería, un toque abre el elemento **dentro de la
  Galería**; desde la rejilla, con una expansión desde la miniatura
  (240 ms). "Abrir en Multimedia" sigue en la pulsación larga para los
  vídeos que se reproducen.
- **Ajuste:** la foto se decodifica en la tarea de medios **por flujo** a una
  resolución suficiente (tope 2 MP) y se escala exacta (bilineal en reposo,
  vecino más cercano mientras el dedo se mueve), centrada y sin deformar. El
  vídeo (AVI MJPEG) se decodifica fila a fila al tamaño mostrado.
- **Orientación:** automática, vertical u horizontal. En horizontal el lienzo
  es de 800×480 durante toda la sesión (imagen, barras y tacto en las mismas
  coordenadas) y no hay barra del sistema encima.
- **Gestos:** pellizco con los puntos reales del GT911 (lo que está bajo los
  dedos se queda bajo los dedos), arrastre libre con zoom, doble toque,
  deslizar para el siguiente. Fotos **y** vídeos. El pellizco no cuenta como
  gesto de suspensión.
- **Barras:** arriba volver, nombre y orientación; abajo, en una foto,
  Editar (si se puede) y Papelera; en un vídeo, progreso, −10 s,
  reproducir/pausa, +10 s y Papelera. Se ocultan solas a los 3 s con fundido
  y un toque las muestra u oculta. Mientras suena un vídeo, el progreso se
  repinta como mucho cada 250 ms.
- **Liquid Glass sin apilar:** el contenido limpio vive en su propio lienzo
  (`vwClean`) y las barras se componen **siempre** sobre una copia limpia;
  el fondo desenfocado de cada barra se prepara una vez por cambio de fondo.
  Antes, cada Play/Pausa volvía a desenfocar el propio dibujo del vidrio y
  la barra se iba oscureciendo. El material y los colores son los del tema.
- **Protegidos:** no ofrecen Papelera ni Editar, no se recuerdan al pasar a
  segundo plano, se sueltan de la RAM con el P4 bloqueado (`vwLockTick`), el
  visor se cierra si el elemento se protege mientras se ve, y **Recientes
  no guarda la captura** de la app mientras el visor enseña algo protegido
  (`vwShowsProtected` en `appSuspend`).

## 11. Transferencias grandes y el destello cian

**Reinicios con archivos de 2-5 MB.** Validar una subida y hacer su
miniatura leía el archivo **entero** en un buffer (hasta 6 MB) en la tarea
del servidor y otra vez en la de miniaturas, a la vez; y `mediaAlloc` caía a
la RAM interna con cualquier tamaño cuando la PSRAM no daba. Ahora:

- El decodificador JPEG tiene un **modo flujo** (`flexJpegProbeStream`,
  `flexJpegDecodeStream`, `flexJpegDecode888Stream`): ventana de 8 KB, los
  segmentos que no hacen falta (EXIF, miniaturas incrustadas) se saltan sin
  guardarlos y la salida es idéntica bit a bit a la del modo memoria. Lo usan
  la validación de subidas, las miniaturas (`flexThumbFromJpegStream`), el
  visor y el editor.
- **Trabajo pesado de uno en uno** (`mediaHeavyBegin`/`End`): servidor, tarea
  de miniaturas y visor no decodifican a la vez. Si en un minuto no hay
  turno, la subida responde 503 "en curso" y el móvil la reintenta sola. El
  editor no entra en esa fila (el visor se suelta antes de abrirlo): también
  lee por flujo y elige su resolución con la PSRAM libre real al abrir.
- `mediaAlloc` solo usa la RAM interna para bloques de hasta 4 KB y dejando
  48 KB libres. Los buffers de `handleUpload` van al heap y la tarea web
  tiene 14 KB de pila (`mlStackCheck` avisa por Serie si el margen baja de
  2 KB).

**La hoja "Conectar con el móvil".** Mientras estaba abierta se rehacía
entera (vidrio incluido) cada 400 ms, hubiera cambios o no. Ahora separa una firma de
lo **fijo** (estado, tema, código, URL) y otra de lo **vivo** (sesiones,
subidas, descargas, tarjetas): si solo cambia lo vivo, repinta y envía al
panel solo esa franja; si nada cambia, no pinta nada.

**El destello cian (azul) al recibir archivos.** Causa, leída en el código
del ESP-IDF 5.4 que trae el core 3.2.1 y en su `sdkconfig`:

1. El driver DPI relanza, **desde una interrupción** al final de cada cuadro,
   el DMA que refresca el panel.
2. Cada borrado o escritura de la flash (LittleFS al guardar un archivo, una
   miniatura o el catálogo) **apaga la caché** mientras dura; con ella se
   bloquea toda interrupción que no sea IRAM-safe. Un borrado de sector dura
   decenas de ms.
3. El `sdkconfig` del core 3.2.1 para `esp32p4` trae
   `# CONFIG_LCD_DSI_ISR_IRAM_SAFE is not set`,
   `# CONFIG_SPI_FLASH_AUTO_SUSPEND is not set` y
   `# CONFIG_SPIRAM_XIP_FROM_PSRAM is not set`: la interrupción del panel
   espera a la flash, el puente DSI se queda sin píxeles y el panel se ve
   azul/cian hasta el cuadro siguiente. El propio driver lo dice en el código
   ("when an underrun happens, the LCD display may already becomes blue") y
   la ayuda de la opción: "If you want the LCD driver to keep flushing the
   screen even when cache ops disabled, you can enable this option".

Qué hace el firmware, y qué no puede hacer: el callback del panel ya está en
IRAM (requisito del driver con la opción activada; sin ella no cambia nada),
al arrancar avisa por Serie si el core no trae la opción
(`[HW] aviso: core sin CONFIG_LCD_DSI_ISR_IRAM_SAFE ...`), y las
transferencias ya no reservan ni leen archivos enteros ni repintan la hoja
entera. Pero **mientras la flash se escribe con la caché apagada, ningún
código del sketch puede refrescar el panel**: la corrección completa es de
configuración del core. Cómo aplicarla y cómo comprobarla está en
`INSTALACION-USB-P4.md` §5. No se ha podido comprobar en una placa real.
