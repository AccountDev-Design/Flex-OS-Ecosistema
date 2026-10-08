# Fase 2 · arquitectura base · informe

> **NO PROBADO EN HARDWARE REAL.** Compilado para `esp32p4` (las tres variantes), probado en el PC
> (pruebas de host con sanitizadores, compatibilidad de LittleFS con dos versiones de littlefs,
> simulador de la interfaz) y revisado contra el código fuente de ESP-IDF 5.5.5 y esp_littlefs 1.22.3.
> Nada se ha ejecutado en la placa.

## Qué hay

| Pieza | Dónde | Qué hace |
|---|---|---|
| Bus de eventos | `flex_core/flex_bus.*` | Bucle `esp_event` propio en la tarea `flex_bus` (núcleo 0). 12 bases (`FLEX_EV_SYSTEM`, `STORAGE`, `SETTINGS`, `NET`, `ACCOUNT`, `CLOUD`, `MEDIA`, `AUDIO`, `SENSOR`, `POWER`, `OTA`, `NOTIF`). Publicar no bloquea nunca: si la cola (48) está llena el evento se descarta y se cuenta. Datos copiados, máx. 128 B. |
| Buzón de la UI | `flex_core/flex_inbox.*` | Cola de `(función, argumento)` que solo vacía la tarea de UI (hasta 16 por vuelta). Publicar despierta a la UI al momento (notificación de tarea). Es el único camino por el que un servicio entrega algo a la pantalla: los servicios no ven LVGL. |
| Almacenamiento | `flex_storage/flex_storage.*` | NVS + LittleFS con **un único escritor** (tarea `storage`, núcleo 0, prioridad 3). Archivos: escritura atómica (temporal `.tmp~` + `rename`), borrar, renombrar, crear carpeta, leer; todo en cola y con respuesta por el buzón. |
| Caché de ajustes | `flex_storage/flex_kv.*` | Los espacios de NVS `flexos`, `flexcare` y `flexphone` se leen una vez al arrancar y viven en RAM. Leer un ajuste nunca toca la flash; cambiarlo marca la entrada con una versión y el escritor la graba 300 ms después del último cambio. Si cambia mientras se graba, no se pierde (versión). |
| Monitor del sistema | `flex_system/flex_system.*` | Tarea `system` (núcleo 0, prioridad 2) cada 2 s: RAM interna y PSRAM (libre, mínimo, bloque mayor), temperatura del chip, carga de cada núcleo, tareas (pila libre, prioridad, núcleo, % de CPU), motivo del último reinicio, cuenta de arranques y resumen del último fallo guardado en la partición `coredump`. Avisos por el bus: RAM interna < 48 KB, temperatura ≥ 80 °C, fallo encontrado. Es la base de Device Care (Fase 12). |
| Lógica portable | `flex_portable/` | 22 módulos de lógica pura de la versión Arduino (44 archivos) copiados **sin cambiar un byte**; `tools/check_portable.py` lo comprueba en cada build. Compilan con mbedTLS en la placa. |
| Arranque | `main/app_main.c` | bus → almacenamiento → monitor → I2C/táctil → UI. Un fallo de almacenamiento no impide arrancar. |
| UI | `flex_ui/flex_ui.c` | Crea el buzón, vacía entregas en cada vuelta, duerme en su notificación (la dan el fin de cuadro, el buzón y el plazo de LVGL), aplica el brillo guardado (`bright`, como la versión Arduino) y lo guarda al moverlo. |

### Tareas

| Tarea | Núcleo | Prioridad | Pila | Papel |
|---|---|---|---|---|
| `ui` | 1 | 5 | 16 KB | única que llama a LVGL |
| `touch` | 0 | 6 | 4 KB | GT911 cada 8 ms |
| `flex_bus` | 0 | 5 | 4 KB | manejadores de eventos (cortos, sin LVGL) |
| `storage` | 0 | 3 | 6 KB | único escritor de NVS y LittleFS |
| `system` | 0 | 2 | 4 KB | métricas para Device Care |

Las pilas son el punto de partida; el monitor del sistema da el high-water mark real de cada una en la
placa para ajustarlas (Fase 17).

## Seguridad de los datos del usuario

Reglas implementadas (y por qué):

1. **Nunca se borra la NVS ni se formatea LittleFS por un fallo.** Si `nvs_flash_init` falla (incluidos
   `NO_FREE_PAGES` y `NEW_VERSION_FOUND`, que es lo que el ejemplo de ESP-IDF "arregla" borrando), el
   sistema sigue con los ajustes en RAM y lo publica (`FLEX_STORAGE_EV_NVS_UNAVAILABLE`). LittleFS se
   monta con `format_if_mount_failed = false`; si no monta, `FLEX_STORAGE_EV_FS_UNAVAILABLE`.
2. **Reparar solo con confirmación explícita**: la UI pide una ficha aleatoria
   (`flex_storage_repair_token`), muestra el aviso y solo si el usuario confirma la devuelve a
   `flex_storage_nvs_erase_confirmed` / `flex_storage_fs_format_confirmed`. La ficha sirve una vez.
3. **Mismos tipos que `Preferences` de Arduino** (`putInt`→I32, `putBool`/`putUChar`→U8, `putChar`→I8,
   `putUInt`→U32, `putString`→STR, `putBytes`→BLOB). Leer con otro tipo devuelve el valor por defecto,
   como `Preferences`. Así se leen los ajustes que dejó la versión Arduino y, si se vuelve a ella, lee los
   que se cambien aquí.
4. **Claves nuevas, nunca pisar las de Arduino**: la cuenta de arranques de esta versión es
   `flexcare/idfboots` (la versión Arduino no la tiene). El brillo usa `flexos/bright` (I32), la misma
   clave y tipo que Arduino, solo cuando el usuario lo cambia.
5. **Rutas**: ninguna operación puede salir de la raíz de LittleFS (`..` rechazado; probado).

### LittleFS: compatibilidad comprobada

`tests/littlefs_compat/run.sh` comprueba con una flash NOR simulada (borrar = 0xFF, programar solo pasa
bits de 1 a 0) y la geometría real de la partición `spiffs` (0xAE0000 B, bloques de 4096, lectura y
escritura 128, caché 512, lookahead 128, block_cycles 512, `block_count` autodetectado — los valores por
defecto de esp_littlefs en las dos versiones):

| Paso | Resultado |
|---|---|
| littlefs **v2.8.0** (el más antiguo que puede llevar la versión Arduino: arduino-esp32 3.2.x pide `joltwallet/littlefs ^1.10.2`) formatea y guarda dibujos, notas, fotos, música con nombres largos y acentuados | OK, disco 2.1 |
| littlefs **v2.11** (el del firmware ESP-IDF, esp_littlefs v1.22.3) monta y lee todo | OK, **la imagen no cambia ni un byte** |
| El firmware ESP-IDF reemplaza un archivo (temporal + rename), crea uno y borra otro | OK, sigue en disco 2.1 (no hay subida de versión) |
| Vuelta a v2.8.0: ve todos los cambios | OK |
| Imagen formateada por v2.11, leída por v2.8.0 | OK |
| Escrituras sin borrar (no válidas en NOR) | 0 |

Lo que **no** se ha podido comprobar aquí: la versión exacta de esp_littlefs que lleva la compilación de
Arduino instalada en el PC del usuario (el rango va de 1.10.2 a la última 1.x; todas usan disco 2.1) y
el montaje real en la placa.

## Pruebas

| Prueba | Resultado |
|---|---|
| `tests/host/run.sh`: `flex_kv` (tipos de Preferences, versiones, cambios durante la grabación, borrados, límites de NVS, 2 hilos con 20 000 cambios), rutas, filas de caché | 95 comprobaciones, 0 fallos, con ASan + UBSan y con TSan |
| `tests/littlefs_compat/run.sh` | ver tabla anterior: OK |
| Simulador de la interfaz (`sim/`) | 1051 comparaciones, 0 diferencias (la pantalla de prueba no cambia en esta fase) |
| `check_sdkconfig.py` (69 opciones), `check_lvgl_only.py`, `check_portable.py` | OK en las tres variantes |
| `tools/run_tests.sh` | ejecuta todo lo anterior |

## Tamaño y memoria

| Variante | Firmware | Bootloader (libre) | Ranura A (2,5 MB) | Ranura B (4 MB) |
|---|---|---|---|---|
| `lt_v3` dev | 918 864 B | 21 408 B (3 168) | 35,1 % | 21,9 % |
| `v3` dev | 921 056 B | 22 144 B (2 432) | 35,1 % | 22,0 % |
| `lt_v3` release | ver `ESP_IDF_MIGRATION_REPORT.md` | | | |

Los módulos portables aún no los llama nadie, así que el enlazador no los incluye: su peso real se verá
cuando las apps los usen. RAM estática: la caché de ajustes vive en el heap (unos pocos KB con los
ajustes de la versión Arduino); el monitor reserva ~1,3 KB para su tabla de tareas.

## Decisiones

| Decisión | Por qué |
|---|---|
| `esp_event` con bucle propio, no el bucle por defecto | El por defecto es de Wi-Fi/IP; mezclarlos haría que un manejador lento de la UI retrasara eventos de red |
| Buzón de funciones para la UI, no eventos | La UI necesita respuestas dirigidas (la pantalla que pidió algo), no difusión; y debe vaciarlas en su tarea |
| Ajustes en RAM con escritura diferida (300 ms) | Un deslizador de brillo genera decenas de cambios por segundo; NVS en flash a cada uno bloquearía y gastaría la flash. Coste: un corte de corriente en esos 300 ms–1,3 s pierde el último cambio (la versión Arduino escribía al momento). |
| Solo tres espacios de NVS en caché | Son los que usa la versión Arduino; los de Wi-Fi (`nvs.net80211`) y del sistema los gestiona ESP-IDF |
| LittleFS con la etiqueta `spiffs` | La misma que encuentra primero la versión Arduino (`FlexOS_FS.cpp:110`) |
| `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID` | Device Care muestra el núcleo de cada tarea |
| Dos avisos de GCC en código heredado, desactivados solo en su archivo | Revisados a mano: falsos positivos (`flex_portable/CMakeLists.txt`) |

## Revisión adversarial

Ver la tabla al final de este documento (se rellena con el resultado de la revisión).

## Pruebas en la placa (pendientes)

1. Arranque con la NVS de la versión Arduino: el log debe decir "NVS lista: N claves en caché" y el brillo
   debe ser el que había.
2. LittleFS monta (`LittleFS montado en /flex: X KB usados`) y los archivos de Paint/Notas siguen ahí.
3. Mover el brillo, reiniciar: se conserva. Volver a la versión Arduino: también lo ve.
4. El log del monitor: carga por núcleo, pila libre de cada tarea, temperatura.
5. Provocar un fallo (opción de depuración) y comprobar que al arrancar aparece "hay un fallo guardado".
