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
   Ver §7. **El de vídeo, también y solo allí**
   (`FlexOS_Ultra_GalleryVideoEdit.h`, §15): Multimedia no tiene editor.

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
| `FlexOS_VidEdit` | el editor de vídeo: parámetros e historial (tramos, encuadre, giro, velocidad, volumen, texto, filtro, portada), análisis del AVI y de su audio, decodificar solo la región del encuadre, y exportar AVI MJPEG por fotogramas (copia byte a byte si la imagen no cambia; si no, render con `FlexOS_ImgEdit` y JPEG por bandas) con el audio original remuestreado, portada `IFCV` y comprobación de lo escrito |

### Placa (partes del sketch único)

| Módulo | Qué hace |
|---|---|
| `FlexOS_Ultra_MediaLib.h` | el almacén sobre LittleFS, tarea de fondo `flexMedia` (reconciliar, miniaturas, guardar), caché de miniaturas, menú/diálogo y ruta de la clave compartidos |
| `FlexOS_Ultra_FileKit.h` | menú, nombre, confirmación y Papelera (hasta 256 elementos; la lista vive en PSRAM y se suelta al cerrarla) |
| `FlexOS_Ultra_MediaKit.h` | kit de listas: selección, "Seleccionar todo"/"Deseleccionar todo" (sin protegidos), menú por elemento, acciones y la hoja del servidor. Menú, diálogos y Papelera con los colores y el material del tema |
| `FlexOS_Ultra_MediaViewer.h` | **el** visor de fotos, dibujos y vídeos del sistema (§10); lo usan la Galería y Multimedia |
| `FlexOS_Ultra_AppGallery.h` | Galería (fotos, vídeos, dibujos); un toque abre el elemento en su propio visor, sin salir de la app |
| `FlexOS_Ultra_GalleryEdit.h` | editor de la Galería (§7) |
| `FlexOS_Ultra_GalleryVideoEdit.h` | editor de vídeo de la Galería (§15): interfaz, trabajador persistente por sesión y publicación |
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
| `test_media` | clasificación, AVI/MJPEG (índice perezoso y aprendido, búsqueda por tramos, trozos vacíos, fotogramas grandes, grabaciones cortadas, tamaños absurdos), WAV PCM e IMA ADPCM por bloques |
| `test_fuzzmedia` | 1500 JPEG y 1500 AVI dañados y sus miniaturas con ASan y UBSan **sin recuperación** (cualquier comportamiento indefinido aborta) |
| `test_imgedit` | el núcleo del editor: identidad exacta, giros/volteos/recorte, historial (incluido restablecer y deshacer), cada ajuste en su sentido, filtros, trazos/formas/textos pegados a la foto, copia reducida, guardado por bandas que el firmware abre y cancelación sin memoria viva |
| `test_videdit` | el núcleo del editor de vídeo (113 comprobaciones): parámetros, historial, tramos y velocidades; la geometría igual que el editor de fotos; el análisis (audio delante, MP3, vacío, cortado, otro códec, basura, fallo de lectura); decodificar solo la región y las miniaturas con arena; exportar copiando byte a byte, por partes y a 2×/0,5×/1,5×; recodificar (giro, encuadre 9:16, B/N, texto, divisor); audio exacto, al 50 %, en silencio, a otra velocidad, entre partes e IMA ADPCM; portada; fallos (escritura, tope de tamaño, cancelar, fotograma dañado, sin memoria, lectura) y la comprobación de lo escrito; estimaciones que cubren lo real; **ni una reserva por fotograma** |
| `test_ino` | el sketch entero enlazado: kit de listas, Música y el **editor de punta a punta** sobre un disco en memoria (abrir, toques reales, copia, reemplazo, protegidos, cancelar, disco lleno, escritura que falla, soltar memoria y releer, guardado en segundo plano, trabajador que tarda); el **visor** mirando el framebuffer (ajuste y centrado en las dos orientaciones, vidrio idéntico tras repintar y tras Play/Pausa ×10, auto-ocultado, pellizco, arrastre, deslizar, Papelera, protegidos, sin captura en Recientes, vídeo, PSRAM devuelta); la hoja web que solo repinta su zona viva; el menú contextual sin vidrio apilado; la Papelera con más de 16 elementos; `testVideoRobusto` (el visor real con archivos largos, grandes, cortados y dañados), `testGuardadoRafaga` y `testVariasFotos` (8 fotos de 0,3 a 12 MP por el servidor real), `testPulsacionLargaVidrio`, `testTactoGlobal` (registros del GT911 → app) y `testArrastresSinFlash`; y el **editor de vídeo de punta a punta** (`testEditorVideoGaleria`, `testEditorVideoGrande`, §15) |
| `check_wiring.py` | ganchos obligatorios y llamadas prohibidas (p. ej. el trabajador del editor no pinta, no avisa por la isla y no cambia el catálogo) |
| `tests/web` | interfaz del móvil (conversión, subida, biblioteca, bloqueo y **tamaño por archivo**, §14) contra el servidor real compilado para el PC; lo que genera la web lo juzgan los analizadores del firmware (`mediacheck`) |

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
| Vídeo | solo AVI MJPEG (también el de cámara sin tablas DHT); fotogramas de hasta 1 MB (`FLEXAVI_FRAME_MAX`) |
| Editor de vídeo | abre AVI MJPEG de hasta 8 MB; exporta AVI MJPEG (+ el audio original en PCM 16 bits) de hasta 8 MB: si la salida pasaría, se detiene sin guardar nada. Hasta 8 partes y 24 pasos de historial. La vista previa no suena (el P4 no reproduce el audio de un AVI). Solo se ajusta el audio que ya trae el vídeo (PCM o IMA ADPCM); un MP3/AAC dentro del AVI se dice y sale sin sonido |
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
  Editar (si se puede) y Papelera; en un vídeo, progreso, Editar (a la
  izquierda, solo si la app lo ofrece: la Galería; §15), −10 s,
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

## 12. Vídeo: archivos largos, grandes, cortados o dañados

Causas encontradas en el camino del vídeo y lo que se hizo (commit
`fix(video)`, pruebas en `test_media` y `testVideoRobusto`):

| Antes | Ahora |
|---|---|
| Cada lectura abría el archivo **por su ruta** (stat + recorrido de carpetas de LittleFS, con su cerrojo): la cabecera de 8 bytes y los datos de cada fotograma | el flujo queda abierto mientras el vídeo está delante y solo se busca si la lectura no es consecutiva (0 aperturas por ruta durante la reproducción) |
| Un trozo con tamaño absurdo (`len ≈ 0xFFFFFFF8`) desbordaba `cursor + 8 + len`: bucle sin fin en el hilo de la interfaz (reinicio por watchdog) | se rechaza con error; además cada llamada recorre como mucho `FLEXAVI_SCAN_MAX` trozos |
| Buscar sin `idx1` recorría el archivo entero de una vez en el hilo de la interfaz | presupuesto por llamada (`flexAviSeekFrameMax`), el visor avanza por tramos de `VW_SEEK_TICK` por vuelta de `loop()` y las posiciones se aprenden al reproducir |
| `idx1` se leía entero al abrir (también para una miniatura o para validar una subida) | se lee la primera vez que se busca |
| Fotogramas de más de 192 KB: pantalla congelada y miniatura "sin MJPEG" | el fotograma que no cabe no se consume: visor y miniatura amplían su buffer hasta 1 MB |
| Un trozo vacío (repetir el anterior: ffmpeg, cámaras) era "AVI dañado" | cuenta como fotograma y se mantiene la imagen |
| Grabación cortada: buscar cerca del final ponía el visor en error | se queda en el último fotograma real y la duración pasa a ser la real |
| Un error a mitad se ignoraba y se releía el mismo trozo roto cada vuelta | se para, se avisa una vez y queda el último cuadro bueno |
| MJPEG de cámara sin segmento DHT ("AVI1"): rechazado al subirlo | tablas Huffman estándar (ITU-T T.81, anexo K.3), como libjpeg-turbo |

La duración o el peso de un vídeo ya no cambian la memoria que usa el visor:
el vídeo nunca se carga entero; se lee fotograma a fotograma con un buffer
del tamaño del mayor fotograma (tope 1 MB).

**Aceleración por hardware:** el P4 tiene un códec JPEG por hardware
(`esp_driver_jpeg`) y un PPA para escalar. Se evaluó y **no se usa todavía**:
no hay placa en este entorno para validar su comportamiento con los
fotogramas reales (tamaños no múltiplos de 16, restart markers, memoria
DMA), y cambiar el decodificador sin medirlo iría contra la estabilidad. Es
el siguiente paso con placa delante.

## 13. Ráfagas de fotos y el catálogo

Cada foto recibida dejaba el catálogo sucio y la tarea de medios lo
reescribía entero 1,5 s después: N fotos seguidas = N reescrituras
(tirones y, con el panel DSI sin la opción del §11, un destello por
borrado de sector). Ahora el servidor marca la ráfaga (`mlBurstNote` en el
inicio, el progreso y la publicación de cada subida) y el guardado espera
3 s sin señales (`ML_BURST_GAP_MS`), con un tope duro de 20 s
(`ML_SAVE_MAX_MS`). Las descargas al móvil no cuentan. Al probarlo apareció
un fallo real de marcas de tiempo (`millis() | 1` leído en el mismo
milisegundo daba 4 294 967 295 ms al restar sin signo): `mlAge()` resta con
signo y acota a 0, y lo mismo `gedTicking()` en el editor.

`testVariasFotos` sube 8 fotos de 0,3 a 12 MP por el servidor real con la
Galería abierta: la subida más cara usa 525 KB de PSRAM (se lee por trozos:
no crece con la foto), tras las 8 solo quedan las miniaturas en caché
(acotadas por `ML_CACHE_N`) y abrirlas todas en el visor y cerrarlo devuelve
toda la PSRAM. No hay límite artificial de fotos por subida.

## 14. Tamaño por archivo al subir desde el móvil

La hoja "Preparar para Flex OS" tiene, debajo de los perfiles, un control
de **tamaño por archivo**:

| Modo | Qué promete |
|---|---|
| Sin límite | lo de siempre: la calidad del perfil elegido |
| **Límite máximo** | "No superar X": ningún archivo pasa de X; si el perfil da más, se baja paso a paso |
| **Objetivo** | "Intentar quedar aproximadamente en X" (±10 %): la calidad se ajusta sola hasta «Alta calidad»; lo que ya pesa menos no se infla |

Tamaños: 500 KB, 750 KB, 1 MB, 2 MB, 5 MB, 10 MB u **Otro** (KB o MB, de
50 KB a 1024 MB). Ningún modo pasa del tope del P4 por clase (§5) ni del
sitio libre, y el plan lo dice cuando es eso lo que manda. La elección se
recuerda en ese navegador.

**Búsqueda progresiva con bytes reales.** Cada intento lo codifica el
navegador y se mide; las mismas funciones, con un modelo aproximado,
anuncian en el plan lo que probablemente hará falta ("puede que" cuando es
estimación; el audio se sabe al byte):

- **Fotos** (JPEG, el único formato que muestra el P4): calidad hasta 0,70 y
  resolución hasta la pantalla (800 px); luego calidad hasta 0,50; luego
  resolución por debajo de la pantalla; y por último calidad hasta 0,30.
  Interpolación logarítmica: como mucho ~12 JPEG por foto.
- **Vídeo** (AVI MJPEG): escalera calidad → resolución → fps, elegida
  midiendo 4 fotogramas de muestra del propio vídeo; después, un control
  fotograma a fotograma reparte el presupuesto según la complejidad medida
  y **garantiza** que el total no pasa del límite (si una escena imprevista
  no cabe ni al mínimo, otra pasada un escalón más abajo). No se recorta: si
  ni lo mínimo cabe, se dice. Un AVI MJPEG que pasa del límite se
  recomprime en el navegador (hasta 64 MB); un fotograma dañado repite el
  anterior.
- **Audio** (WAV): PCM 16 bits → PCM 8 bits (con *dither*) → IMA ADPCM a
  22, 16, 11 y 8 kHz; nunca por encima de la frecuencia del original.

**Pérdida exagerada** (audio a 8 kHz; foto por debajo de 640 px o calidad
< 0,50; vídeo en la zona mínima): el archivo convertido **no se sube** hasta
que el usuario pulsa «Subir así» (o «Descartar»); la cola sigue con los
demás. Una reducción notable se sube y la tarjeta queda con el aviso hasta
que se lee.

**Lo que enseña la tarjeta:** tamaño original → resultante y el porcentaje
("6,7 MB → 98 KB (−99 %)"), el máximo u objetivo, los parámetros usados
("899×674 · calidad 77 %", "480×360 · 10 fps · calidad 56 %",
"IMA ADPCM · 8 kHz") y el aviso si la calidad se redujo de forma notable.

Todo ocurre en el móvil y de forma asíncrona: cada intento es una llamada
asíncrona del navegador (`toBlob`, `createImageBitmap`, búsqueda en el
`<video>`) y entre una y otra la página atiende lo demás, así que la
biblioteca, el visor y las demás transferencias siguen respondiendo. En el
P4 no cambia nada: recibe un archivo normal y lo valida como siempre. Pruebas: `webui.test.js` (miles de
casos con codificadores sintéticos, uno no monótono: nunca pasa del límite,
nunca agranda; «Objetivo» dentro del ±10 % en 165/165 casos alcanzables) y
`e2e.test.js` en Chromium (foto de 6,7 MB en ≤ 100 KB, WAV aceptado a 8 kHz y
otro descartado, vídeo con grano y AVI de cámara con fotogramas dañados en
≤ 300 KB, «Objetivo» 500 KB dentro del ±10 %; todo decodificado por el P4).

## 15. El editor de vídeo de la Galería

**Es de la Galería, y solo de la Galería** (`FlexOS_Ultra_GalleryVideoEdit.h`,
una capa de la app como el editor de fotos). Se entra por dos sitios y los
dos abren **el mismo** editor (`vedOpen`):

- pulsación larga sobre un vídeo de la rejilla → el menú (el Action Sheet de
  Liquid Glass de siempre) → **Editar**; el menú desaparece entero antes de
  abrir (cambio de pantalla con el marco completo: ni restos ni vidrio encima);
- el botón **Editar** de la barra flotante del visor, a la izquierda, cuando
  el visor lo abrió la Galería.

Multimedia usa el mismo visor, pero su anfitrión no ofrece Editar
(`VID_VW` sin `edit`): allí no hay botón, ni menú, ni editor. Solo se ofrece
para vídeos AVI MJPEG no protegidos que el P4 reproduce y que no pasan de
8 MB (`vedEditable`). **Una sola instancia**: abrir otra vez el mismo vídeo
reutiliza la sesión, y nunca hay dos editores (ni el de fotos y el de vídeo)
a la vez.

**Pantalla (480×800).** La barra `← Editar vídeo  ↶ ↷  ✓` va en la franja de
la cabecera del sistema (el chevrón es el del sistema). Debajo: el vídeo, la
línea de tiempo (reproducir, posición/duración, duración final, 8
miniaturas, los extremos para recortar, las divisiones, la parte del
cabezal, la marca de la portada y el cabezal), el panel de la herramienta y
las ocho herramientas. Colores y superficies del tema (el vidrio global:
`uiSurfaceFlat`/`uiSurface`); solo lo que va encima del vídeo (marco del
encuadre, cabezal, marco del texto) es blanco/negro fijo. Cada zona se
rehace entera sobre su fondo y se publica sola. La hoja de exportar
(superficie elevada) solo la pinta el render completo, que acaba de rehacer
la pantalla: el vidrio no se apila (la prueba la repinta y compara píxeles).

**Herramientas.** Todo son **parámetros** con deshacer/rehacer (24 pasos):
no se procesa ni un fotograma hasta exportar.

| Herramienta | Qué hace |
|---|---|
| Cortar | arrastrar los extremos de color recorta el principio y el final (un paso al soltar); **Dividir aquí** parte el tramo del cabezal; **Quitar parte** quita ese tramo (hasta 8 partes); **Restablecer** |
| Encuadre | libre (esquinas, lados y mover) o con proporción Original, 16:9, 4:3, 1:1 o 9:16; se ve sobre el fotograma entero y en vivo, sin recalcular el fotograma al arrastrar |
| Girar | 0/90/180/270° y "Girar 90°"; el encuadre gira con la imagen |
| Velocidad | 0,25×, 0,5×, 0,75×, 1× (por defecto), 1,25×, 1,5× y 2×. Sin fotogramas inventados: más rápido toma uno de cada N (mismos fps); más lento alarga cada fotograma (menos fps) |
| Volumen | 0/25/50/75/100 % y Silenciar, **solo del audio original**; ni música ni audio externo |
| Texto | el teclado del sistema, 8 colores y 3 tamaños; se arrastra sobre el vídeo y no se sale de la imagen |
| Filtros | los del editor de fotos (Original, B/N, Sepia, Vívido, Frío, Cálido, Vintage), con miniaturas del fotograma a la vista |
| Portada | el fotograma del cabezal (o volver al primero); va como `IFCV` en `LIST INFO` y la miniatura de la biblioteca la usa |

ATRÁS cierra primero la hoja; con cambios, pregunta antes de descartar;
durante la apertura o la exportación, cancela.

**El trabajador.** Uno por sesión (`flexVEd`, núcleo 1, prioridad 1, 8 KB
de pila) hace todo lo que lee el archivo: analizarlo, decodificar el
fotograma de la vista previa a la escala justa (1, 1/2, 1/4 u 1/8), las
miniaturas (una por vuelta) y exportar. No pinta, no avisa por la isla y no
toca el catálogo (`check_wiring.py`). Publica con `__atomic`: las banderas
viven en la RAM interna y los datos en la PSRAM. La vista previa es de uno a
la vez (`baseOwner`): la interfaz nunca pinta desde una base que se está
escribiendo (mientras, se sigue viendo la última imagen buena) y al
arrastrar o reproducir se pide siempre el **último** fotograma, sin cola.
Cede la CPU cada ~20 ms, suelta el archivo tras 300 ms sin trabajo, y
cancelar y cerrar son banderas que mira entre fotogramas. Al cerrar se le
espera (acotado); si no terminara, su memoria no se toca hasta que salga
(`vedReclaim`) y el editor dice "ocupado" en vez de abrirse encima.

**Exportar (✓).** Una hoja con las resoluciones **Original** (la del
encuadre), **1080p**, **720p** y **480p** (solo las que no amplían y caben
en la memoria libre), el tamaño estimado y avisos honestos (se copia sin
pérdida / se recodifica / el audio no se puede procesar / puede pasar del
tope); **Guardar como copia**, **Reemplazar original** (con confirmación) y,
con varias partes, **Guardar las N partes por separado**. Sale AVI MJPEG, lo
único que el P4 reproduce, con el audio original en PCM 16 bits si lo hay.

1. Antes de empezar: espacio (lo estimado, con el tope de un vídeo por
   salida, más la reserva de 512 KB) y memoria.
2. Se escribe **por fotogramas** en `/System/Media/tmp/ve-<id>-<k>.avi`:
   se lee el fotograma, se decodifica **solo la región del encuadre** al
   divisor justo, se pinta por bandas y se codifica por bandas, con una
   arena fija (ni una reserva por fotograma). Si la imagen no cambia, los
   fotogramas se **copian byte a byte**. Progreso real y Cancelar.
3. Lo escrito se vuelve a leer (`flexVeVerify`: cabecera, medidas,
   fotogramas, índice y el primer y el último fotograma decodificables).
4. Solo entonces se publica, en `loopTask` y también con la Galería en
   segundo plano (`vedBgTick`). Con partes: si una falla al exportar, no se
   publica ninguna.

Cancelar, un error, quedarse sin espacio o un corte de luz dejan el original
intacto y ningún temporal publicado.

**Memoria.** Todo se reserva al abrir, con comprobación previa (PSRAM y RAM
interna para la pila del trabajador): la base de la vista previa (≤ 640×480
RGB888), la vista ya pintada (RGB565), 8 miniaturas pequeñas, la arena de
decodificar y el buffer del fotograma comprimido, este del tamaño **real**
del mayor fotograma en cuanto el análisis lo sabe. Nada se lee entero.
Medido en el PC (`testEditorVideoGrande`): la memoria pico sobre la del
sistema es la misma con un vídeo de 2 MB que con uno de 3 MB (~780 KB) y
~1,2 MB con uno de 5 MB exportando con recodificación. Exportar pide su
memoria al empezar y la suelta al acabar. `shed` (Galería suspendida) suelta
fotogramas, miniaturas y trabajador y conserva los parámetros y el
historial: al volver se relee. Cerrar devuelve **toda** la PSRAM (lo
comprueban las pruebas, también tras 25 aperturas y 10 vueltas rápidas
visor ↔ editor).

**Protegidos y cambios de fuera.** Si el vídeo se protege, se borra o
cambia de tamaño con el editor abierto, el editor se cierra, suelta todo y
lo dice; a mitad de exportar no se publica nada. Antes de que otra parte del
sistema lo mueva, lo borre o lo sustituya, el trabajador lo suelta y no lo
vuelve a abrir hasta que el cambio termina (`gMlBeforeChange2`, un segundo
oyente junto al de Música); una exportación en curso se corta y se dice por
qué. Un renombrado no cierra el editor.

**Registro por Serie.** Eventos, no fotogramas: abrir, listo (medidas,
fotogramas, duración y audio), exportar (resolución, salidas, copia o
recodificar), el resultado (KB y ms) y los fallos.

**Pruebas.** `test_videdit` (el núcleo) y, en `test_ino`, el editor de punta
a punta sobre el disco en memoria y con toques reales: los dos accesos (y
que Multimedia no tiene Editar), una sola instancia, la línea de tiempo, el
encuadre y el texto arrastrados, todas las herramientas, copia (byte a byte
si no cambia la imagen), partes y reemplazar, cancelar al 10/50/90 % y con
ATRÁS, disco que falla, sin espacio, protegido y borrado con el editor
abierto, sin memoria (PSRAM, una reserva que falla, RAM interna), soltar y
releer, exportar en segundo plano, trabajador colgado, y vídeos de 2, 3 y
5 MB. **Pendiente de medir en la placa:** tiempos de apertura, de la vista
previa y de exportar en el P4 real.
