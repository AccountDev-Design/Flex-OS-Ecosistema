# Flex OS Ultra (ESP32-P4) — instalación por cable USB

Esta versión se instala **por cable**, no por OTA. Este documento
explica cómo se compila en modo USB, qué tabla de particiones usar en
una flash de 16 MB, y por qué el sistema OTA sigue estando en el
repositorio aunque esta compilación no lo use.

> **Nada de esto toca a Flex OS Ultra S3 ni a Flex OS Pro.** Los dos
> siguen compilándose exactamente igual que antes, con OTA activado y
> con sus mismas particiones.

---

## 0. Tamaños reales (compilación verificada)

Compilado de verdad para `esp32:esp32:esp32p4` con **core ESP32 3.1.3**,
PSRAM activada, flash de 16 MB y la tabla de particiones de este
repositorio:

| | |
|---|---|
| Firmware (`.bin`) | **1 493 765 B** (1,42 MB) |
| Partición `app0` | 5 242 880 B (5,00 MB) |
| Ocupación | **28,5 %** |
| Libre en `app0` | **3 749 115 B** (3,58 MB) |
| RAM estática (interna) | **90 652 B** de 327 680 (27,7 %) |
| Libre para variables locales | 237 028 B |

Las correcciones de render (una sola notificación a la vez, recorte por
viewport de las listas, cabecera común, teclado dentro del panel) cuestan
**+645 bytes de flash y 0 bytes de RAM interna** sobre la medición
anterior: son cambios de composición, no estructuras nuevas.

Coste de Flex Intelligence sobre la versión anterior, medido compilando
las dos con el mismo core y los mismos flags: **+56,8 KB de flash** y
**+10,8 KB de RAM interna**. Los buffers de trabajo grandes (el fichero
que se abre para buscar dentro, los snapshots, el historial) viven en
**PSRAM**, no en RAM interna.

PSRAM adicional, reservada bajo demanda y sólo si hace falta: ~1,7 MB en
cachés de imagen (página vecina del escritorio, captura del panel, fondo
de la tarjeta flotante) y ~27 KB de datos (cola de trabajos, chat,
buffers de trabajo). Sin PSRAM el sistema arranca igual y esas funciones
se degradan diciéndolo.

---

## 1. Compilar en modo USB (sin OTA)

El módulo OTA ya tenía su interruptor maestro desde que se escribió
(`FLEXOS_OTA_ON`, en `FlexOS_OTA.h`). No hace falta borrar ni tocar una
línea de él: basta con apagarlo al compilar.

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=16M \
  --build-property "compiler.cpp.extra_flags=-DFLEXOS_OTA_ON=0" \
  <carpeta-del-sketch>
```

(El comando completo, con la tabla de particiones, está en el apartado 2.)

Con `FLEXOS_OTA_ON=0`:

* `FlexOS_OTA.cpp` compila **a casi nada**: sus funciones quedan como
  hooks vacíos (ver el bloque `#else` al final del fichero). No entra el
  cliente HTTPS de descarga, ni el lector del manifiesto, ni la interfaz
  de actualización, ni `Update.h`.
* `flexOtaStatusText()` devuelve `"No disponible"`, así que la fila de
  Ajustes → Sistema → Actualizaciones dice la verdad en vez de ofrecer
  algo que no existe.
* **El firmware ya no necesita una segunda partición `app`**: sin
  `Update.write()` no hay a dónde escribir ni por qué reservar el
  espacio.

### Flex Intelligence no depende del OTA

Es una comprobación deliberada, no una coincidencia. El bloque de Flex
Intelligence del `.ino` sólo llama a `flexOta*` para **cederle la
pantalla** cuando el OTA la tiene en exclusiva (`flexOtaOwnsScreen()`), y
esas funciones existen igual con el módulo apagado. Compilado con
`-DFLEXOS_OTA_ON=0`, Flex Intelligence funciona igual: la app, el panel,
el corrector, Cowork y las notificaciones no cambian.

---

## 2. Tabla de particiones recomendada para 16 MB

Sin OTA sobra la segunda partición de aplicación, y ese espacio se puede
repartir entre la app y LittleFS. Esta es la distribución recomendada
para la compilación USB:

```csv
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  factory,  0x10000,  0x500000,
spiffs,   data, spiffs,   0x510000, 0xAE0000,
coredump, data, coredump, 0xFF0000, 0x10000,
```

| Región     | Offset       | Tamaño        | Para qué                       |
|------------|--------------|---------------|--------------------------------|
| `nvs`      | `0x009000`   | 20 KB         | Ajustes, Wi-Fi, Flex Vault, Flex Intelligence |
| `otadata`  | `0x00E000`   | 8 KB          | Se conserva (ver más abajo)    |
| `app0`     | `0x010000`   | **5 MB**      | El firmware                    |
| `spiffs`   | `0x510000`   | **~10,9 MB**  | LittleFS: archivos del usuario |
| `coredump` | `0xFF0000`   | 64 KB         | Volcado en caso de fallo       |

Notas de por qué es así:

* **5 MB para la app** es holgado a propósito. Flex OS Ultra mide
  1,42 MB medidos (28,5 % de la partición); dejar 5 evita tener que
  cambiar la tabla de particiones —
  y por tanto borrar LittleFS — durante mucho tiempo. Cambiar la tabla
  **es** lo que hace perder los archivos del usuario.
* **`otadata` se conserva aunque no haya OTA.** Ocupa 8 KB y quitarla
  desplazaría todo lo de detrás, lo que obligaría a reformatear
  LittleFS. Se deja donde está para que volver a activar el OTA en el
  futuro sea sólo un cambio de tabla, sin coste para el usuario.
* La subtipo `spiffs` es la que el núcleo de Arduino monta como
  LittleFS por omisión; el nombre es histórico.

### Guardar esto como CSV

El fichero está en la raíz del repositorio como
`flexos_ultra_usb.csv`, y `tests/host/test_parts.py` (que corre con
`make`) comprueba que cuadra: sin solapes, sin salirse de los 16 MB, con
las particiones de app alineadas a 64 KB y con una sola de aplicación.

Copia `flexos_ultra_usb.csv` a la carpeta del sketch como
`partitions.csv` y compila:

```bash
cp flexos_ultra_usb.csv <carpeta-del-sketch>/partitions.csv

arduino-cli core install esp32:esp32@3.1.3
arduino-cli compile \
  --fqbn esp32:esp32:esp32p4:PartitionScheme=custom,PSRAM=enabled,FlashSize=16M \
  --build-property "compiler.cpp.extra_flags=-DFLEXOS_OTA_ON=0" \
  <carpeta-del-sketch>
```

`PartitionScheme=custom` es lo que hace que se lea el `partitions.csv`
de la carpeta del sketch. Se puede comprobar que se aplicó mirando el
`.partitions.bin` generado: debe dar `app0` de 5120 KB y `spiffs` de
11136 KB, no el esquema por defecto.

> **Nota sobre el aviso de tamaño.** Con `PartitionScheme=custom`,
> `arduino-cli` imprime «Maximum is 16777216 bytes» — el tamaño de la
> flash entera, no el de `app0`. El límite real es la partición: 5 MB.
> Con 1,42 MB de firmware sobra de largo, pero el porcentaje que imprime
> la herramienta (8 %) no es el que cuenta; el real es 28,5 %.

---

## 3. Los archivos del usuario NO se borran

Dos reglas, y las dos importan:

1. **Una actualización normal no formatea LittleFS.** `flexFsBegin()`
   monta el volumen y sólo formatea si el montaje falla — es decir, si
   el sistema de archivos no existía o está corrupto. Grabar un firmware
   nuevo por USB **no** toca la partición de datos.
2. **Mientras la tabla de particiones no cambie, los datos siguen ahí.**
   Los archivos (`/Notas`, `/Paint`, `/Documentos`, `/System`), los
   ajustes (NVS) y Flex Vault sobreviven a cualquier reinstalación del
   firmware.

Lo que **sí** borra los datos: cambiar los offsets o los tamaños de la
tabla de particiones, o usar `esptool erase_flash`. Si en algún momento
hay que cambiar la tabla, hay que avisarlo como lo que es.

Al grabar por USB, usa el modo que escribe **sólo la partición de la
app**, no un borrado completo del chip.

---

## 4. Ficheros nuevos de Flex Intelligence en LittleFS

Todos viven en `/System` y son pequeños y regenerables:

| Fichero               | Qué guarda                                    | Tamaño |
|-----------------------|-----------------------------------------------|--------|
| `/System/spell.txt`   | Diccionario personal (palabras aceptadas)     | < 1 KB |
| `/System/cowork.dat`  | Resultados terminados de Cowork               | < 2 KB |
| `/System/aichat.txt`  | Historial de conversación (12 turnos)         | < 2 KB |

Borrar cualquiera de los tres no rompe nada: el sistema arranca con esa
parte vacía. **El token del servidor no está aquí**: vive en NVS, en su
propio namespace (`flexos_ai`), y se borra entero desde
Flex Intelligence → Ajustes → *Olvidar este servidor*.

---

## 5. Volver a activar el OTA

Nada de lo anterior destruye el sistema OTA. Para recuperarlo:

1. Compilar sin `-DFLEXOS_OTA_ON=0` (el valor por omisión ya es 1).
2. Usar una tabla de particiones con dos particiones `app` (`ota_0` y
   `ota_1`) del mismo tamaño.
3. Definir `FLEXOS_OTA_ROOT_CA` con el PEM de la CA raíz del servidor
   del manifiesto: sin CA, el módulo se niega a descargar
   (`OTA_ERR_TLS`), que es el comportamiento correcto.

**Ojo:** el paso 2 cambia la tabla de particiones y por tanto **sí**
borra LittleFS. Hay que avisar al usuario antes.
