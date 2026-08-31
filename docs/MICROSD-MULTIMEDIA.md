# microSD, Galería y Multimedia en Flex OS Ultra (ESP32-P4)

Placa **JC4880P443 V1.0**. Todo lo que hay aquí sale del esquema
oficial y del código; lo que no está verificado se dice que no lo
está.

---

## 1. Hardware: cómo está cableada la tarjeta

La ranura microSD va al **controlador SDMMC nativo del P4, en modo de
4 bits**. No es SPI: no hay `SD.begin()`, no hay pin CS y no hay bus
SPI compartido.

| Señal | GPIO | Pin del módulo | Pin de J1 (TF_Card) |
|---|---|---|---|
| `SD_CMD`   | **44** | 64 | 3 · CMD |
| `SD_CLK`   | **43** | 63 | 5 · CLK |
| `SD_DATA3` | **42** | 62 | 2 · CD/DATA3 |
| `SD_DATA2` | **41** | 61 | 1 · DATA2 |
| `SD_DATA1` | **40** | 60 | 8 · DATA1 |
| `SD_DATA0` | **39** | 59 | 7 · DATA0 |

> ### ⚠️ DATA2 y DATA3 se confunden con facilidad
>
> En el esquema **DATA2 es GPIO41 y DATA3 es GPIO42**, no al revés.
> Es el error típico al transcribir la tabla, y en modo de 4 bits
> intercambiarlos **no da un fallo limpio**: la tarjeta responde a
> los comandos (que van por `CMD`) y el montaje puede llegar a
> funcionar, pero los datos salen corruptos de forma intermitente.
> Eso es mucho más difícil de diagnosticar que un montaje que falla.
>
> Los pines viven en un solo sitio: `SDPIN_*` en `FlexOS_SD.cpp`.

### Alimentación

`TF_VCC` sale de **`ESP_LDO_VO4`** (pin 58) a través del P-MOSFET Q1
(AO3401). Su puerta cuelga de R13 (10 K a masa): con la puerta baja el
MOSFET conduce, así que **la tarjeta tiene tensión siempre que el
regulador interno 4 del P4 esté levantado**.

El ejemplo Arduino oficial de esta misma placa usa `SD_MMC.setPins()`
seguido directamente de `SD_MMC.begin()`. Flex OS sigue esa misma ruta
y deja que el driver del core gestione el slot 0 y su control de potencia;
no adquiere el LDO 4 manualmente.

**GPIO45 no alimenta la tarjeta.** R10, que llevaría GPIO45 a esa
puerta, está marcada **NC** en esta revisión. El firmware no toca
GPIO45.

### No hay detección física de inserción

El pin CD del conector es **CD/DATA3**, y aquí se usa como DATA3. No
existe una línea independiente y fiable que diga "hay tarjeta".

* **Sin volumen montado** — `flexSdTick()` intenta montar de forma
  espaciada. `flexSdPoke()` adelanta ese intento al abrir
  Almacenamiento, Galería o Multimedia.
* **Con volumen montado** — no se abre la raíz ni se hace ninguna
  lectura por temporizador. Solo las operaciones reales solicitadas
  por el usuario acceden al bus. Si una falla, se invalida el volumen,
  se cierran sus rutas por generación y se notifica la retirada. En
  reproducción ocurre al fallar la lectura del fotograma.

Esta distinción es necesaria: la apertura periódica de la raíz causaba
reinicios `PANIC` reproducibles con algunas tarjetas/controladores del
ESP32-P4. Tampoco se consideran errores las carpetas opcionales que no
existan (`DCIM`, `Pictures`, `Movies`, etc.); simplemente se indexan como
vacías. No hay ningún bucle que lea la tarjeta continuamente.

### Separación microSD ↔ Wi-Fi remoto del C6

En el ESP32-P4, `WiFi.status()` y `WiFi.getMode()` no son consultas locales:
atraviesan **esp-hosted por SDIO** hasta el C6. El P4 dispone de dos slots y
deben compilarse con propietarios distintos:

* microSD integrada: **SDMMC slot 0**, pines IOMUX 39–44 y LDO interno 4;
* esp-hosted/C6: **SDIO slot 1**.

El perfil genérico `ESP32P4 Dev Module` no conoce el cableado particular de
esta placa. Por ello `build_opt.h` define `BOARD_HAS_SDMMC`,
`BOARD_SDMMC_SLOT=0` y `BOARD_SDMMC_POWER_CHANNEL=4`. Sin esas opciones,
Arduino enviaría también la tarjeta al slot 1 y encender Wi-Fi podría producir
un `PANIC`.

Flex OS mantiene además estas reglas:

* los ticks de escritorio no consultan continuamente el driver Wi-Fi remoto;
* el Wi-Fi guardado vuelve a conectarse una sola vez, seis segundos después
  del arranque, nunca desde `setup()`;
* la microSD se monta antes de crear tareas de red y ambos subsistemas pueden
  permanecer activos porque ya no reclaman el mismo slot.

### Generación de montaje

`flexSdGeneration()` sube en cada montaje y en cada desmontaje. Quien
tiene un fichero abierto guarda esa generación y la compara antes de
leer: si no coincide, la tarjeta que hay ahora no es la de entonces.
**Es lo que hace imposible leer con un descriptor de una tarjeta ya
retirada**, sin depender de que cada pantalla se acuerde de comprobarlo.

---

## 2. Formatos: qué se reproduce de verdad

| Formato | Estado | Dónde |
|---|---|---|
| **JPEG baseline** (`.jpg`, `.jpeg`) | ✅ real | `FlexOS_JPEG.cpp`, ya probado contra libjpeg-turbo |
| **AVI con vídeo MJPEG** (`.avi`, `MJPG`/`JPEG`/`dmb1`/`mjpa`) | ✅ real | `FlexOS_Media.cpp` demultiplexa, `FlexOS_JPEG` decodifica |
| **WAV PCM** 8/16 bits, mono/estéreo (`.wav`) | ⚠️ condicional | solo si el códec ES8311 contesta (ver §6) |
| **Dibujos de Paint** (`.fxp`) | ✅ real | se reproducen sus trazos |
| MP4 / H.264 / HEVC / MOV / MKV / WebM | ❌ **no** | no hay decodificador |
| MP3 / AAC / FLAC / OGG / Opus / WMA | ❌ **no** | no hay decodificador |
| PNG / GIF / BMP / WebP / HEIC / TIFF / RAW | ❌ **no** | solo se decodifica JPEG |

Un archivo no compatible **no se intenta abrir**. `flexMediaClassify()`
lo reconoce y `flexMediaUnsupportedReason()` da un motivo concreto
("MP4/H.264: esta placa no tiene decodificador de vídeo"), que se
enseña en una notificación real.

### Por qué no hay MP4/H.264

No es una decisión de gusto. H.264 necesita un decodificador con
memoria de fotogramas de referencia y transformada propia; el ESP32-P4
**no trae decodificador de vídeo por hardware**, y en software no da el
ritmo para nada utilizable a esta resolución. Anunciarlo como soportado
sería mentir.

MJPEG sí funciona porque **cada fotograma es un JPEG completo**: no hay
predicción entre cuadros, así que se puede empezar en cualquier punto y
—esto es lo importante— **saltar fotogramas sin que la imagen se
rompa**, que es lo que permite ir tarde sin congelar el sistema.

### Cómo preparar un vídeo compatible

```bash
ffmpeg -i entrada.mp4 -an -c:v mjpeg -q:v 6 -vf scale=640:-2 -r 20 salida.avi
```

* `-c:v mjpeg` — el único códec de vídeo que esta placa decodifica.
* `-an` — sin pista de audio (el reproductor la salta, pero no la suena).
* `scale` y `-r` — ver los números medidos en §5.

---

## 3. Formato de la tarjeta y carpetas

* **FAT32 es el formato recomendado y documentado.** exFAT puede
  montar si el core está compilado con soporte, pero no es lo probado.
* **Flex OS nunca formatea una tarjeta**, ni automáticamente ni a
  petición: `SD_MMC.begin()` se llama siempre con
  `format_if_mount_failed = false`.
* **Flex OS nunca borra ni mueve archivos del usuario.** En la tarjeta
  el explorador es de **solo lectura**: borrar, renombrar, mover a la
  papelera y cifrar en la bóveda están desactivados sobre `/sdcard`, y
  se dice por qué. (La papelera y la bóveda además viven en la
  partición interna, así que "mover" ahí sería copiar entre volúmenes.)

### Carpetas que crea Flex OS

Solo cuando se va a **guardar** algo, nunca al montar:

```
/sdcard/FlexOS/Media/Photos
/sdcard/FlexOS/Media/Videos
/sdcard/FlexOS/Media/Audio
```

### Carpetas que Flex OS indexa sin tocar

```
/sdcard/DCIM        /sdcard/Download    /sdcard/Pictures
/sdcard/Movies      /sdcard/Music
```

Se recorren para construir el índice; no se crean si no existen, no se
modifican y no se mueve nada de dentro.

La memoria interna (LittleFS) sigue **separada**: `/Documentos` y
`/Paint`. Los dos volúmenes se presentan aparte en Almacenamiento
("Memoria interna" y "Tarjeta SD") y sus números no se suman.

---

## 4. El índice de medios

`FlexOS_Media.cpp` construye el índice **por lotes pequeños desde el
bucle principal** (`mediaStorageTick()` → `flexMediaIndexStep()`), con
un presupuesto de 12 entradas de directorio por vuelta. Una tarjeta con
miles de fotos no congela la interfaz: se ve el progreso real
("Indexando… N revisados, M encontrados") mientras se rellena.

* **Capacidad: 384 elementos** (~46 KB de PSRAM). Es un tope **real**,
  no "infinito": si hay más, la Galería lo dice ("Hay más archivos de
  los que caben en el índice") en vez de enseñar 384 y callar.
* **Profundidad: 3 niveles** (`/sdcard/DCIM/100APPLE/foto.jpg`).
* Los archivos ocultos (los que empiezan por `.`) se saltan.
* Si la tarjeta desaparece a mitad del recorrido, **se abandona esa
  raíz, no el índice entero**: lo ya encontrado en la memoria interna
  sigue valiendo.
* Al retirar la tarjeta, `flexMediaIndexDropSd()` quita solo lo suyo.
* Un dibujo nuevo de Paint invalida el índice, así que aparece en la
  Galería sin refrescar a mano.

---

## 5. Rendimiento: lo que está medido y lo que no

### Lo que el firmware mide en la placa

El reproductor cuenta, **por ventanas de un segundo**: fotogramas
presentados, fotogramas saltados y errores de lectura. Se enseña en el
panel de controles (`12.5 fps · 3 saltados`) y no se imprime en Serial
en cada cuadro.

**No se afirma ninguna cifra de FPS aquí porque no se ha ejecutado en
la placa.** La instrumentación está para que el número que se vea sea
medido, no estimado.

### Lo que sí se puede acotar con fundamento

El coste por fotograma es, casi entero, **la decodificación JPEG** más
el volcado:

* El decodificador (`FlexOS_JPEG`) trabaja **por filas de MCU** y
  escribe **directamente sobre el framebuffer**: no hay ningún buffer
  intermedio del tamaño de la imagen, ni doble buffer de vídeo. (El
  esqueleto anterior gastaba 2 × 220 KB de PSRAM en un patrón sintético.)
* En **vertical**, una fila de la imagen es una fila de memoria: un
  `memcpy`.
* En **horizontal** el panel es físicamente vertical, así que una fila
  lógica cae en una **columna** de memoria. Escribir píxel a píxel
  serían ~384 000 escrituras sueltas por cuadro, cada una en una línea
  de caché distinta. En su lugar se acumulan 8 filas en una tira de RAM
  interna y se vuelcan juntas, con lo que cada escritura pasa a ser una
  **tira contigua de 8 píxeles**. Si no hay RAM interna para la tira se
  usa el camino directo: más lento, pero correcto.

**Recomendación de partida para MJPEG:** **640×480 a 15–20 fps**, o
**480×320 a 25–30 fps**. Empieza por ahí, mira el contador de FPS real
del propio reproductor y sube o baja. Un vídeo más pesado de lo que la
placa da **no congela nada**: se salta fotogramas de forma controlada
(hasta 4 seguidos) y se ve como un vídeo a tirones, no como un sistema
bloqueado.

### Presupuestos de memoria

| Qué | Cuánto | Cuándo |
|---|---|---|
| Fotograma comprimido de vídeo | 192 KB (PSRAM) | mientras hay un vídeo abierto |
| Tira del volcado girado | 12,8 KB (RAM interna) | ídem, y es opcional |
| Foto abierta (JPEG comprimido) | el tamaño del archivo, tope **6 MB** | mientras se ve la foto |
| Índice de medios | ~46 KB (PSRAM) | mientras Flex OS está encendido |
| Caché de miniaturas | 16 × 25 KB = ~400 KB (PSRAM) | solo con la Galería abierta |

La foto es la única excepción a "no cargar archivos completos en RAM":
el decodificador necesita los bytes comprimidos completos. Por eso
lleva **tope** y **comprobación de PSRAM libre antes de reservar**: por
encima del límite se dice el motivo en vez de dejar al sistema sin
memoria.

Nada de esto se reserva por fotograma. Todos los caminos de salida —
volver, cambiar de vídeo, pausar, suspender, cerrar la app, retirar la
tarjeta — pasan por `vidReleaseMedia()`, que es el **único** sitio que
suelta el descriptor, los buffers y la tira.

---

## 6. Audio: estado honesto

`FlexOS_Audio.cpp` implementa el códec **ES8311** por I2S.

| Señal | GPIO |
|---|---|
| MCLK | 13 |
| SCLK (BCLK) | 12 |
| LRCK (WS) | 10 |
| DSDIN (salida del P4) | 9 |
| ASDOUT (entrada al P4) | 48 |
| I2C SDA / SCL | **7 / 8** |
| PA_CTRL (NS4150) | 11 |

Dirección I2C **0x18** (CE a masa por R1 10 K).

**El bus I2C es el mismo del panel táctil GT911.** Por eso el módulo no
crea su propio bus ni habla desde otra tarea: usa el `Wire` que ya
inicializó el sketch y sus llamadas salen del mismo hilo que sondea el
táctil, igual que la detección I2C incremental que ya existía. Las
escrituras al códec son unos pocos bytes y solo ocurren al arrancar y
al cambiar el volumen; el audio en sí va por I2S y no toca I2C.

### Qué está verificado y qué no

* ✅ El **cableado y la dirección** salen del esquema oficial.
* ✅ La **detección es real**: `flexAudioBegin()` lee los registros de
  identificación (0xFD/0xFE deben valer 0x83/0x11). Un ACK suelto no
  basta — esos dos registros solo leen eso si es un ES8311 **y** si el
  bus funciona en los dos sentidos.
* ❌ **No se ha podido probar con altavoz.** La secuencia de arranque
  del códec es la del fabricante, pero no está confirmada en placa.

**Por eso todo cuelga de la comprobación anterior.** Si el chip no
contesta lo que tiene que contestar:

* `flexAudioAvailable()` devuelve `false`;
* el deslizador de volumen y el interruptor de silencio **no existen**
  — ni en el panel rápido ni en el catálogo de "Añadir un control";
* Ajustes → Sonido dice "Sin salida de audio" y el motivo;
* un `.wav` se abre y explica que no hay salida, en vez de fingir que
  suena.

No hay ni un control decorativo.

### Volumen real

El deslizador escribe el **registro 0x32 del DAC del ES8311**, no un
número de la interfaz. 0 % → registro 0x00 (mudo de verdad, −95,5 dB);
100 % → 0xBF, que es **0 dB**. Se para ahí a propósito: por encima el
códec aplica ganancia digital y satura. El valor se guarda en NVS y se
restaura al arrancar.

El amplificador (PA_CTRL) se enciende **solo mientras hay algo
sonando**: un NS4150 alimentado sin señal es ruido de fondo y consumo.

---

## 7. Orientación

Tres modos, aplicados al **archivo actual**; al abrir otro se vuelve a
Auto.

| Modo | Comportamiento |
|---|---|
| **Auto** | ancho > alto → horizontal a pantalla completa · alto > ancho → vertical · cuadrado → la última orientación de la sesión |
| **Vertical** | fuerza vertical |
| **Horizontal** | fuerza horizontal |

**No hay un segundo motor de rotación.** Se usa el mismo `gLand` +
`putPhys` que Modo PC y Juegos, y el mismo mapeo de táctil
(`lx = T.y`, `ly = SCR_W-1-T.x`). El lienzo lógico, los controles, la
barra de progreso, los botones y el tacto salen todos de las mismas
funciones (`mediaCanvasW/H`, `vidBtnGeom`, `mediaTouchXY`), así que
giran juntos **por construcción**: no puede pasar que se vea horizontal
y el dedo responda como si estuviera vertical.

Eso está **probado**: `testMediosOrientacion()` en
`tests/host/ino_compile.cpp` recorre el lienzo horizontal punto a punto
comprobando que el táctil deshace exactamente la rotación del dibujo, y
que los botones caen dentro de su panel en las dos orientaciones.

En horizontal la barra del sistema no se dibuja (la desactiva `gLand`),
así que la flecha de volver del propio lienzo es la salida — y gira con
todo lo demás. El framework, además, devuelve `gLand = false` al cerrar
o suspender cualquier app, así que no hay forma de quedarse atrapado en
horizontal.

---

## 8. Arquitectura

```
FlexOS_SD.h/.cpp        Volumen microSD: montaje, estado, capacidad,
                        listado por lotes, descriptor abierto.
                        Depende de SD_MMC. No se prueba en el PC.

FlexOS_Media.h/.cpp     PORTABLE (sin Arduino, sin FS, sin pantalla).
                        Clasificación, demultiplexor AVI/MJPEG, parser
                        WAV, indexador incremental. Todo entra por dos
                        interfaces de funciones (FlexMediaIO para bytes,
                        FlexMediaVolume para directorios), que es lo que
                        permite probarlo de verdad en el PC.

FlexOS_Audio.h/.cpp     ES8311 + I2S. Depende de Wire y driver/i2s_std.

FlexOS_Ultra.ino        Núcleo de medios (volúmenes, lector unificado,
                        índice, caché de miniaturas, orientación,
                        puente con las notificaciones) + las tres
                        pantallas: Almacenamiento, Galería y Multimedia.
```

Un solo lector (`MediaStream` + `FlexMediaIO`) sirve para los dos
volúmenes; un solo decodificador JPEG; un solo visor a pantalla
completa (el de Multimedia, que la Galería reutiliza); un solo motor de
orientación; un solo índice.

---

## 9. Pruebas

```bash
cd tests/host
make            # todo, incluido test_media
make all-boards # los tres perfiles de placa
```

`tests/host/test_media.cpp` — **279 comprobaciones con AddressSanitizer
y UndefinedBehaviorSanitizer** sobre el módulo real (no una copia):

* clasificación y motivos, con casos borde (`.jpg` a secas, extensiones
  engañosas, mayúsculas);
* AVI: cabecera, dimensiones, fps y duración; lectura secuencial con
  comparación **byte a byte** de cada fotograma; fotogramas de tamaño
  impar (relleno de alineación); pista de audio intercalada;
  `LIST rec ` de los AVI entrelazados;
* AVI: búsqueda con y sin `idx1`, hacia delante y hacia atrás;
* AVI: rechazo limpio de lo que no es MJPEG, de lo truncado, de lo que
  no es RIFF y de un fotograma que no cabe — y que leer un AVI truncado
  **termina** en vez de dar vueltas;
* **el medio desaparece a mitad**: toda lectura posterior falla, nunca
  devuelve datos viejos;
* WAV: PCM 8 y 16 bits, mono y estéreo, `data` truncado, rechazo de
  audio comprimido y de PCM de 24 bits;
* índice: el resultado **no depende del presupuesto** (con lotes de 1,
  2, 3, 7 o 100 sale exactamente lo mismo); hermanos que van *detrás*
  de una subcarpeta dentro del mismo lote; tope de capacidad; límite de
  profundidad; rutas demasiado largas (se descartan, no se guardan
  truncadas); directorio ilegible; y **la tarjeta que se saca a mitad
  del recorrido** sin perder lo ya indexado.

`tests/host/ino_compile.cpp` → `testMediosOrientacion()` — orientación,
ajuste sin deformar, mapeo del táctil y geometría de los botones. Y que
sin códec **no se ofrece ningún control de volumen**.

---

## 10. Limitaciones conocidas

1. **MP4 / H.264 no está soportado** y no lo estará sin decodificador
   por hardware. Ver §2.
2. **Audio no verificado en placa.** Ver §6.
3. **El índice tiene un tope de 384 elementos.** Se dice en pantalla
   cuando se llega.
4. **La tarjeta es de solo lectura** para los archivos del usuario. Es
   deliberado.
5. **`SD_MMC.begin()` no distingue "no hay tarjeta" de "el sistema de
   archivos no se reconoce"**: el core de Arduino descarta esa
   diferencia antes de devolver. Cuando nunca hubo una tarjeta válida,
   el mensaje dice las dos posibilidades y nombra el formato que sí
   funciona ("Sin tarjeta, o formato no compatible (usa FAT32)"). Lo
   que sí se distingue es que una tarjeta que **estaba** montada deje
   de responder.
6. **El número de elementos de una subcarpeta de la tarjeta no se
   muestra** ("Carpeta" en vez de "N elementos"). Contarlo obligaría a
   abrir todas las subcarpetas, y entrar en `DCIM` costaría segundos.
   Preferimos no dar el dato a darlo mal o a costa de bloquear la
   pantalla.
7. **No hay captura de cámara**, así que no hay archivos de cámara. No
   se simula ninguno.
8. **Fondos de pantalla desde fichero** siguen pendientes.
9. **La retirada en reposo no puede notificarse instantáneamente** porque
   esta placa no expone una línea CD independiente. Se detecta en la
   siguiente operación real sobre la tarjeta. Volver a sondearla por
   tiempo reintroduciría el reinicio `PANIC` que se eliminó.
10. **Wi-Fi remoto y microSD requieren slots distintos.** `build_opt.h` fija
    la tarjeta en slot 0 + LDO 4 y deja esp-hosted/C6 en slot 1. El archivo es
    obligatorio al compilar desde Arduino IDE.
11. **La tarjeta insertada durante el arranque se monta antes de iniciar los
    servicios de red.** Así SD_MMC obtiene el bus de forma determinista, sin
    competir durante el primer `loop()` con una tarea que consulte esp-hosted.
    La inserción posterior continúa funcionando mediante el sondeo en caliente.
