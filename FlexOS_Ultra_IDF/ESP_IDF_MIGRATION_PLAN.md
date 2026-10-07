# Flex OS Ultra — ESP-IDF · Plan de migración

> **Estado:** plan, sin implementar. **Nada de lo descrito aquí se ha probado en
> hardware real.** Las capacidades del ESP32-P4 que se citan se han comprobado en
> el código fuente de ESP-IDF v5.5.5, de LVGL v9.6.0 y de `esp_h264` (se indica la
> fuente en cada caso); las cifras de rendimiento de terceros son las que publica
> el fabricante, no medidas propias.

---

## 1. Rama y reglas

| | |
|---|---|
| Rama de referencia (Arduino) | `claude/flexos-ota-system-blq9g9-2csacx` @ `5dbc949`. **Solo lectura.** No se hace commit, push ni merge sobre ella. |
| Rama nueva (ESP-IDF) | `flexos-esp-idf`, creada desde `5dbc949`, **sin upstream** hacia la rama Arduino (un `git push` sin argumentos no puede ir a ella). |
| Dónde vive el proyecto ESP-IDF | `FlexOS_Ultra_IDF/` (proyecto ESP-IDF completo: `CMakeLists.txt`, `main/`, `components/`...). |
| Archivos Arduino en esta rama | Se conservan **sin tocar**, como especificación funcional para leer en paralelo. El build de ESP-IDF no los compila. |

**Por qué una carpeta y no la raíz.** La raíz ya tiene los sketches de S3 y Pro,
la app de Android, el servidor, el SDK y las pruebas. Mezclar ahí `main/` y
`components/` haría que el build de ESP-IDF conviviera con `.ino` que no le
pertenecen y confundiría a VS Code (extensión ESP-IDF), que espera abrir la
carpeta del proyecto. `FlexOS_Ultra_IDF/` se abre tal cual como proyecto.

---

## 2. Inventario de la versión Arduino (P4)

### 2.1 Tamaño y organización

* `FlexOS_Ultra/`: **116 489 líneas** en 70 cabeceras `FlexOS_Ultra_*.h` / puentes
  y 40 módulos `.cpp`. Las cabeceras forman **una sola unidad de traducción**
  encadenada (`Types → HAL → Gfx → ... → TheftUI`), con cientos de globales
  `static` compartidas (`fb`, `gState`, `gDark`, `gLand`...).
* `loop()` es el planificador de la UI: atiende táctil, overlays, máquina de
  estados (`ST_*`) y 17 *ticks* de servicios en cada vuelta, con `delay(1..5)`
  como ritmo (`loopPaceMs`).
* **19 apps** (`APP_REG`, ids estables): Reloj, Galería, Multimedia,
  Almacenamiento, Modo PC/DeX, Notas, Navegador, Flex Compass, Paint, Juegos,
  Ajustes, Calculadora, Calendario, Cámara, Clima, Flex Store, Flex Phone,
  Device Care, Música.
* **~25 pantallas de sistema**: splash, OOBE (idioma, nombre, Flex Account),
  bloqueo, escritorio por páginas, personalizar inicio, caja de apps, menú
  contextual, Recientes, configurar bloqueo, kiosco, Wi-Fi, conectividad,
  archivos, ajustes de teclado, apagado (confirmación + animación), modo seguro,
  restablecimiento, protección contra robo, panel rápido (+ edición), isla de
  notificaciones, cápsula del cronómetro, aviso de caída, overlay de OTA,
  emparejar Flex Storage, hoja de Flex Web Server.

### 2.2 Hardware (leído del código, no supuesto)

| Pieza | Detalle | Fuente en el código |
|---|---|---|
| Placa | GUITION **JC4880P443C_I_W** (JC4880P443 V1.0) | cabecera de `FlexOS_Ultra.ino` |
| SoC | ESP32-P4, 360 MHz, Flash 16 MB QIO 80 MHz, PSRAM habilitada (32 MB) | `FlexOS_Ultra.ino` |
| Pantalla | **480×800 nativa**, MIPI-DSI 2 lanes @ 500 Mbps, controlador **ST7701**, DPI 34 MHz RGB565, LDO canal 3 @ 2,5 V para el PHY, RST = GPIO5, backlight PWM = GPIO23 | `FlexOS_Ultra_HAL.h` |
| Táctil | **GT911** I2C 0x5D/0x14, SDA = GPIO7, SCL = GPIO8, RST = GPIO3, coordenadas 0..479 × 0..799 sin girar | `FlexOS_Ultra_HAL.h` |
| Audio | Codec **ES8311** I2C 0x18 (mismo bus) + I2S: MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 48 | `FlexOS_Audio.*` |
| IMU | **GY-BNO085** (SHTP) I2C 0x4A/0x4B, mismo bus | `FlexOS_BNO085.*` |
| Wi-Fi / BLE | **ESP32-C6** por **SDIO** (esp-hosted): CLK 18, CMD 19, D0 14, D1 15, D2 16, D3 17, RST 54. El C6 necesita firmware *slave* de esp-hosted aparte | `FlexOS_Ultra_Network.h` |
| Cámara | La placa tiene MIPI-CSI, pero **el modelo del sensor no está identificado** (`CAM_HAS_SENSOR 0`, la app muestra un patrón "SIN SEÑAL") | `FlexOS_Ultra_AppCamera.h` |
| microSD | Ranura en SDMMC nativo (solo en `futuras-versiones-BETA/MicroSD-Multimedia`, no en el firmware activo) | doc BETA |
| Batería | **No hay medición de batería en el código.** El único "battery" es el del teléfono en Flex Phone | búsqueda en todo el sketch |
| Temperatura | Sensor interno del P4 (`driver/temperature_sensor.h`), usado solo en el puente de apps | `FlexOS_AppHost_Bridge.h` |

### 2.3 Dependencias Arduino y su equivalente ESP-IDF

| Arduino | Uso | Equivalente ESP-IDF |
|---|---|---|
| `Wire` | bus I2C compartido (GT911, ES8311, BNO085) + recuperación manual del bus | `driver/i2c_master.h` (un solo `i2c_master_bus_handle_t`, `i2c_master_bus_reset()`) |
| `Preferences` | NVS, espacio `flexos` | `nvs_flash` / `nvs.h` (mismo formato: los datos son legibles) |
| `LittleFS`, `FS` | archivos del usuario, sesiones, biblioteca de medios | `joltwallet/esp_littlefs` + VFS POSIX (`fopen`, `opendir`) |
| `WiFi` (+ hosted) | estación, escaneo, reconexión | `esp_hosted` + `esp_wifi_remote` → API `esp_wifi_*` y `esp_netif` |
| `HTTPClient`, `WiFiClientSecure` | Account, Cloud, OTA, Clima, Store | `esp_http_client` + `esp-tls` (mbedTLS), `esp_crt_bundle` o CA anclada |
| `WiFiUdp`, lwIP sockets | NTP, descubrimiento de Flex Phone | `esp_sntp` / sockets lwIP |
| `Update` | OTA | `esp_ota_ops` / `esp_https_ota` + rollback del bootloader |
| `BLEDevice` / NimBLE | BLE (vía C6) | NimBLE sobre esp-hosted (fase tardía) |
| `cJSON` | JSON | `json` (cJSON) de ESP-IDF |
| `ledcAttach/ledcWrite` | brillo | `driver/ledc.h` |
| `millis/delay/Serial` | tiempos y trazas | `esp_timer_get_time`, primitivas FreeRTOS, `esp_log` |

### 2.4 Arquitectura gráfica actual

* **Motor propio**, sin LVGL: framebuffer de pantalla `fb` y buffers fuera de
  pantalla en PSRAM, primitivas por software (`fillRect`, `drawText`, iconos
  vectoriales dibujados por código), fuente **Outfit 4 bpp** propia, y una sola
  copia al panel por DMA2D (`num_fbs = 1`, espera al callback de fin).
* **Liquid Glass cacheado**: fondo desenfocado del wallpaper (768 KB), panel
  rápido compuesto, *scratch* de desenfoque; el sistema los suelta bajo presión
  de memoria (`memShedSystem`). Existe un "Modo visual eficiente".
* Orientación horizontal (`gLand`, 191 usos) para Juegos y Modo PC.
* Defecto conocido y documentado en el propio código: **el panel destella al
  escribir en flash**, porque el ISR del DSI no es *cache-safe* en el core de
  Arduino (precompilado). En ESP-IDF se corrige con `CONFIG_LCD_DSI_ISR_CACHE_SAFE`.

### 2.5 Tareas FreeRTOS que ya existen

17 tareas, todas en el núcleo 1 y prioridad 1: `flexphWifi`, `flexEdit`,
`flexVEd`, `flexMedia`, `ntp`, `wifiOff`, `wifiAuto`, `wifiScan`, `wifiConn`,
`flexWeb`, `flex-account` (12 KB), `flexbrNet`, `flex-cloud` (16 KB),
`flex-cloud-st` (10 KB), `flexOTA`, `flex_store` (24 KB), `flexWx` (10 KB).
La UI corre en `loopTask`. Las tareas son buenas (la red ya no va en la UI), pero
**todas compiten en el mismo núcleo y prioridad que la UI**, y cada servicio
reserva su propia pila grande en SRAM interna.

### 2.6 Almacenamiento

* NVS `flexos`: ajustes, idioma, orden del escritorio (con `APPREG_VER`),
  credenciales Wi-Fi, fusible anti-bucle de la radio, hora, brillo...
* LittleFS en la partición `spiffs` (0x510000, 10,9 MB): sesiones de apps,
  notas, dibujos, medios, caché de miniaturas, papelera, paquetes `.flexpkg`.
* Tabla USB actual (`flexos_ultra_usb.csv`): `nvs` 0x9000/20 KB · `otadata`
  0xE000 · `app0` *factory* 5 MB · `spiffs` 0x510000/0xAE0000 · `coredump`
  0xFF0000/64 KB. **Sin ranuras OTA.**

### 2.7 Red y servicios

* **Wi-Fi**: esp-hosted SDIO hacia el C6; el arranque **nunca** enciende la
  radio (fusible NVS contra el bucle de PANIC que había con el C6 sin firmware);
  reconexión diferida una vez por arranque.
* **Flex Account**: vinculación por código (el P4 publica el SHA-256 de una
  credencial aleatoria), respuesta firmada ES256 con clave anclada, estados
  `LINKED / LINKED_OFFLINE / AUTH_REQUIRED / TOKEN_EXPIRED`, persistencia y
  comprobación periódica (6 h), servidor en Flex Developer Studio.
* **Flex Cloud**: dos tareas (control y *streaming* por rangos), diario de
  transferencias con CRC, cuota, subida/descarga, reproducción de AVI desde la
  nube; TLS siempre verificado; 401 → se consulta a Flex Account antes de actuar.
* **Flex Storage / Flex Phone**: descubrimiento UDP + TCP con HMAC-SHA256,
  emparejamiento aprobado en pantalla, notificaciones del teléfono.
* **Navegador**: el contenido lo rasteriza un servicio remoto con Chromium
  (`server/`); el P4 muestra fotogramas y envía entrada.
* **Flex Store / paquetes**: FLXP v1, SHA-256, ECDSA P-256, grants firmados,
  dos runtimes aislados (`flex-ui-1` declarativo y la VM `flex-app-v1`).
* **Clima** (Open-Meteo), **NTP**, **Flex Web Server** (subir medios desde el móvil).

### 2.8 Multimedia

* JPEG baseline por **software** (`FlexOS_JPEG`), AVI MJPEG demultiplexado por
  bloques, WAV PCM 8/16 bits e IMA-ADPCM. MP4/H.264, MP3, AAC, PNG... se
  clasifican como no soportados con un motivo.
* Audio: ES8311 + I2S (ya con `driver/i2s_std.h` de ESP-IDF), reproducción en
  segundo plano alimentada desde `loop()` (`musAudioTick`).
* Editores de imagen y de vídeo (lógica pura en `FlexOS_ImgEdit` / `FlexOS_VidEdit`).

### 2.9 OTA

`FlexOS_OTA.cpp` (común a las tres placas): manifiesto JSON en
`raw.githubusercontent.com/.../ota/manifest.json` con clave por plataforma
(`flex_os_ultra`), MD5, descarga por *streaming* con `Update`, interfaz propia.
Firma y anti-rollback desactivados (`FLEXOS_OTA_REQUIRE_SIGNATURE 0`). La
compilación USB lleva `FLEXOS_OTA_ON=0`.

### 2.10 Sensores, energía y Device Care

* BNO085 compartido por un único servicio (`imuServiceTick`, "UNA muestra para
  todos"), detección de caídas y protección contra robo como **lógica pura**
  (`FlexOS_FallDetect`, `FlexOS_Theft`), brújula sobre ese servicio.
* Energía: brillo PWM, suspensión con fundido, apagado completo con *deep sleep*
  y despertar por `ext1`, RST del GT911 retenido en sueño.
* Device Care: estado, diagnóstico, caídas, salud, optimización, historial,
  pruebas del dispositivo y *Post-Impact Check*; memoria medida con `heap_caps_*`.

### 2.11 Pruebas existentes

`tests/host` compila y ejecuta en el PC buena parte del código del dispositivo
(JPEG contra libjpeg-turbo, memoria, AppVM, paquetes, Flex Link, IMU con bus
simulado, Cloud, Account, presupuesto de pila...). Es un activo: los módulos
puros que se reutilicen conservan sus pruebas.

---

## 3. Revisión de "480×320"

Se ha buscado `480x320`, `480×320`, `320x480` y variantes en todo el repositorio.

| Dónde | Qué es | Veredicto |
|---|---|---|
| `FlexOS_Ultra_S3.ino` (13 apariciones) | El panel **real** de Flex OS Ultra **S3**: TFT SPI 4,0" ST7796/ILI9488 de 480×320 | Correcto para **esa** placa. No es la P4 y pertenece a la versión Arduino: **no se toca**. No se lleva a la versión ESP-IDF. |
| `futuras-versiones-BETA/MicroSD-Multimedia/docs/MICROSD-MULTIMEDIA.md:243` | Recomendación de **resolución del vídeo de origen** ("480×320 a 25–30 fps") | No describe la pantalla. Se conserva. |
| `FlexOS_Ultra/FlexOS_Ultra.ino:9` | "NATIVA 480x800 (sin el modo puente 320x480 de ArduOS)" | Nota histórica que **confirma** 480×800. |

**Conclusión:** el firmware P4 actual **ya es 480×800 nativo** (`SCR_W 480`,
`SCR_H 800` en `FlexOS_Ultra_Types.h`, panel DPI 480×800, GT911 0..479 × 0..799).
No hay ningún layout del P4 diseñado para 480×320. En la versión ESP-IDF la
geometría se define en **un solo sitio** (`flex_display`, 480×800) y todos los
layouts se diseñan de cero para 480×800 con el sistema de diseño de la sección 5.7;
no se convierten coordenadas del motor Arduino una a una.

---

## 4. Capacidades del ESP32-P4 comprobadas (y las que no existen)

| Capacidad | ¿Existe? | Fuente comprobada |
|---|---|---|
| 2 núcleos HP RISC-V | Sí | `soc_caps.h`: `SOC_CPU_CORES_NUM 2` |
| MIPI-DSI con DPI y DMA2D | Sí; el driver admite `num_fbs` > 1 y `use_dma2d` | `esp_lcd_mipi_dsi.h` |
| ISR del DSI segura con caché apagada | Sí, opción `CONFIG_LCD_DSI_ISR_CACHE_SAFE` | `esp_lcd/Kconfig` |
| PPA (escalar, rotar, espejo, mezclar, rellenar) | Sí | `soc_caps.h`: `SOC_PPA_SUPPORTED`; `driver/ppa.h` |
| **JPEG por hardware: decodificar y codificar** | Sí | `SOC_JPEG_DECODE_SUPPORTED`, `SOC_JPEG_ENCODE_SUPPORTED`; `driver/jpeg_decode.h`, `jpeg_encode.h` |
| MIPI-CSI + ISP | Sí | `SOC_MIPI_CSI_SUPPORTED`, `SOC_ISP_SUPPORTED`; `esp_driver_cam` |
| PSRAM accesible por DMA, XIP desde PSRAM | Sí | `SOC_PSRAM_DMA_CAPABLE`, `SOC_SPIRAM_XIP_SUPPORTED` |
| Sensor de temperatura interno | Sí | `SOC_TEMP_SENSOR_SUPPORTED` |
| Wi-Fi / Bluetooth propios | **No.** Van por el C6 (esp-hosted) | ausentes en `soc_caps.h` |
| **Decodificador H.264 por hardware** | **No.** Solo hay **codificador** H.264 por hardware | README de `esp_h264` |
| Decodificador H.264 por software (perfil *constrained baseline*) | Sí, `esp_h264_dec_sw` (tinyH264). Cifras del fabricante en P4: 640×480 → 25 fps (1 tarea) / 31 fps (2 tareas), 2,5 MB; 1280×720 → 7 / 10 fps, 6,2 MB | README de `esp_h264` |
| LVGL: renderizador por PPA | Sí, `LV_USE_PPA` | `lvgl/Kconfig` v9.6.0 |
| LVGL: desenfoque de fondo real | Sí, estilos `blur_radius`, `blur_backdrop`, `blur_quality` | `lv_obj_style_gen.h` v9.6.0 |
| LVGL: modo DIRECT con doble buffer y sincronización de zonas | Sí (`refr_sync_areas`) | `lv_refr.c` v9.6.0 |
| Sensores de cámara con driver | `esp_cam_sensor` trae 40 drivers (sensores y algunos puentes HDMI; entre ellos sc2336, sc202cs, ov5647, ov5640, ov2640, gc2145). Sirve solo si el de la placa está en la lista | `esp-video-components/esp_cam_sensor/sensors` |

**Revisión del chip — punto crítico.** En ESP-IDF v5.5.5 el valor por defecto es
`ESP32P4_REV_MIN_301` (**solo silicio v3.x**). Para chips v0.x/v1.x hay que activar
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` y elegir `REV_MIN_0`, `REV_MIN_1` o
`REV_MIN_100`. Un binario construido para v3 **no arranca** en una placa v1.x.
La placa funciona hoy con el core Arduino 3.2.1 (ESP-IDF 5.4), lo que apunta a
silicio anterior a v3, pero hay que confirmarlo con el log de arranque
(`chip revision: vX.Y`) antes de grabar nada (pregunta abierta 1).

---

## 5. Arquitectura propuesta

### 5.1 Principios

1. **Solo la tarea de UI toca LVGL.** Ningún servicio llama a `lv_*`.
2. **Ninguna operación de red, flash ni decodificación pesada en la tarea de UI.**
3. Servicios con **API asíncrona**: la UI pide (cola, sin bloquear) y recibe el
   resultado como evento; el estado compartido se lee como **copia** bajo mutex.
4. Sin `delay()` como sincronización: colas, *event groups*, notificaciones de
   tarea, temporizadores (`esp_timer`) y semáforos.
5. Memoria **reservada al arrancar** para lo grande (framebuffers, colas de
   fotogramas, *buffers* de audio); nada de reservar/liberar por fotograma.
6. Cada componente con su etiqueta de log y nivel propio; producción en `WARN`.

### 5.2 Estructura

```
FlexOS_Ultra_IDF/
├── CMakeLists.txt
├── sdkconfig.defaults            # P4 rev, PSRAM 200 MHz, DSI cache-safe, TWDT, logs
├── sdkconfig.defaults.release    # logs WARN, asserts silenciosos
├── partitions.csv
├── main/                         # app_main(): orden de arranque y nada más
├── components/
│   ├── flex_core/        # bus de eventos, tareas, registro, logging, errores comunes
│   ├── flex_board/       # pines y alimentación de la JC4880P443C (un solo sitio)
│   ├── flex_i2c/         # bus I2C compartido, mutex, recuperación y estadística
│   ├── flex_display/     # LDO + DSI + ST7701 + DPI 2×FB + puerto LVGL + brillo
│   ├── flex_touch/       # GT911 en su tarea, último punto publicado sin bloqueo
│   ├── flex_ui/          # LVGL: tema Liquid Glass, componentes, shell y apps
│   ├── flex_storage/     # NVS + LittleFS, un único escritor, trabajos en cola
│   ├── flex_wifi/        # esp-hosted, máquina de estados, reconexión con espera
│   ├── flex_net/         # cliente HTTP/TLS común, raíces de confianza, trabajos
│   ├── flex_account/     # Flex Account
│   ├── flex_cloud/       # Flex Cloud (control + streaming)
│   ├── flex_ota/         # OTA, rollback y validación
│   ├── flex_media/       # JPEG HW, demux AVI, reproductor, cola de fotogramas
│   ├── flex_audio/       # ES8311 + I2S, decodificadores, tarea de audio
│   ├── flex_camera/      # abstracción de cámara (CSI + ISP) — sensor pendiente
│   ├── flex_compass/     # BNO085, servicio IMU, brújula
│   ├── flex_power/       # brillo, suspensión, apagado, perfiles de batería
│   ├── flex_system/      # Device Care: métricas, salud, historial, watchdog
│   └── flex_portable/    # módulos puros reutilizados de la versión Arduino
├── sim/                          # simulador de la UI en el PC (LVGL + SDL/headless)
└── docs/
```

### 5.3 Tareas, núcleos y prioridades

El núcleo 1 se reserva a la **interfaz y al vídeo**; el núcleo 0 a la radio,
la red, el almacenamiento y los sensores. Las tareas internas de esp-hosted,
lwIP y Wi-Fi (prioridades altas de ESP-IDF) quedan en el núcleo 0.

| Tarea | Núcleo | Prio | Responsabilidad |
|---|---|---|---|
| `ui` | 1 | 5 | `lv_timer_handler`, navegación, animaciones, bandeja de eventos de la UI |
| `video` | 1 | 4 | demux + decodificación (JPEG HW / H.264 SW) a una cola de 2–3 fotogramas en PSRAM; **por debajo de la UI**: si falta CPU baja el FPS del vídeo, no la UI |
| `touch` | 0 | 6 | lee el GT911 (por INT si la placa lo cablea; si no, cada 8–10 ms) y publica el último punto; un bus I2C trabado nunca para la UI |
| `audio` | 0 | 7 | decodifica y escribe al I2S con *buffers* DMA; prioridad alta porque un hueco se oye |
| `sensors` | 0 | 4 | BNO085, una muestra para todos (caídas, robo, brújula) |
| `wifi` | 0 | 4 | máquina de estados de la radio, escaneo, reconexión con espera creciente |
| `net_worker` ×2 | 0 | 3 | trabajos HTTP/TLS en cola (Account, Clima, OTA-check, Store, Cloud-control). Dos trabajadores en vez de 6 tareas con pila propia: menos SRAM interna ocupada por pilas de TLS |
| `cloud_stream` | 0 | 3 | descargas por rangos para reproducción |
| `storage` | 0 | 3 | **único escritor** de LittleFS/NVS; lecturas grandes por bloques |
| `system` | 0 | 2 | Device Care, muestreo de memoria, temperatura, historial, confirmación del rollback |
| `flex_bus` | 0 | 5 | bucle `esp_event` propio para los eventos del sistema |

Las pilas y prioridades exactas se fijan midiendo (`uxTaskGetStackHighWaterMark`)
en la Fase 2; esta tabla es el punto de partida.

### 5.4 Comunicación entre tareas

```
servicio ──esp_event_post(flex_bus)──► manejador ──xQueueSend(0 ticks)──► bandeja de la UI
                                                                              │
UI (cada fotograma) ◄── vacía la bandeja, actualiza objetos LVGL ◄────────────┘
UI ──flex_xxx_request(...) (cola, sin bloquear; si está llena, "ocupado")──► servicio
estado (Wi-Fi, cuenta, nube, memoria) ──► struct protegida por mutex, la UI lee copias
```

### 5.5 Pipeline de pantalla

```
GT911 ─I2C─► tarea touch ─(último punto, atómico)─► indev LVGL (read_cb no bloquea)
   ─► LVGL (modo DIRECT) dibuja SOLO las zonas inválidas en el FB trasero
   ─► flush de la última zona: esp_lcd_panel_draw_bitmap(FB trasero) = cambio de FB
   ─► el driver DPI muestra ese FB desde el siguiente cuadro (sin copia)
   ─► LVGL copia las zonas cambiadas al otro FB (refr_sync_areas) y sigue
```

* **Cifras**: un FB RGB565 de 480×800 = **768 000 B**; dos = 1,46 MiB en PSRAM.
  Con la temporización actual (34 MHz, 576 × 976 ciclos por cuadro) el panel
  refresca a **60,5 Hz**; la DMA del DPI lee ~46 MB/s de PSRAM; el enlace DSI va
  al ~37 % de su capacidad (2 × 500 Mbps).
* **Sin tearing**: LVGL nunca escribe en el FB que se está mostrando; el cambio
  se sincroniza con el fin de cuadro del DPI.
* **Sin destellos al escribir flash**: `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` (lo que
  el core de Arduino no permitía activar).
* Aceleración: `LV_USE_PPA` para rellenos/mezclas grandes, PPA para escalar
  fotos y vídeo, JPEG HW para miniaturas y visor. Se activa **midiendo**: si la
  PPA no gana en áreas pequeñas, se limita a áreas grandes.
* Rotación (Juegos, Modo PC): en modo DIRECT LVGL dibuja el búfer sin girar y el
  giro corre a cargo del driver (o de la rotación por matriz de LVGL). Se
  evaluará en la Fase 3 girar con PPA frente a usar modo parcial para esas
  pantallas. No se promete hasta medirlo.

### 5.6 Memoria

| SRAM interna (768 KB según la hoja de datos, baja latencia) | PSRAM (32 MB) |
|---|---|
| pilas de tareas, objetos del driver DSI e ISR, colas, estado de servicios, *heap* pequeño de LVGL, *buffers* DMA del I2S, contexto TLS (mbedTLS) y *buffers* de lwIP/esp-hosted | 2 FB de pantalla, cachés de vidrio, imágenes decodificadas, miniaturas, cola de fotogramas de vídeo, *buffers* de cámara, *buffers* grandes de red y de archivos, objetos LVGL grandes |

* Asignador de LVGL propio: pequeñas reservas en interna, grandes en PSRAM.
* Reservas grandes **una vez** y reutilizadas (*pools*); los decodificadores
  escriben en *buffers* ya reservados.
* Presupuesto y política de "qué se suelta" heredados de `FlexOS_Mem` (lógica
  pura, se reutiliza tal cual con sus 66 comprobaciones).

### 5.7 Liquid Glass eficiente

Tres niveles, elegibles en Ajustes (equivale al "Modo visual eficiente" actual):

1. **Vidrio cacheado (por defecto)**: el wallpaper se desenfoca **una vez** al
   cambiar de fondo o de tema (fuera de la UI: reducir con PPA → desenfoque de
   caja → ampliar con PPA) y cada panel de vidrio dibuja su recorte de esa
   textura + tinte translúcido + borde especular + sombra precalculada. Coste
   por cuadro ≈ copiar una imagen.
2. **Vidrio vivo**: `blur_backdrop` de LVGL 9.6 solo en superficies pequeñas o
   quietas (isla, cápsulas), y nunca durante una animación de pantalla completa.
3. **Plano**: tintes sin desenfoque, para memoria baja o batería baja.

Un único **sistema de diseño** en `flex_ui/theme`: rejilla de 8 px, márgenes
laterales de 16/24 px, barra de estado 36 px, barra de navegación 48 px, objetivo
táctil mínimo 48×48 px, radios 12/20/28, tipografía Outfit (OFL) convertida a
fuentes LVGL en los tamaños necesarios con alfabeto latino + acentos.

### 5.8 Particiones (16 MB) — decisión pendiente

El C6 tiene su propia flash; el P4 no necesita `phy_init`.

**Opción A — compatible con los datos de la versión Arduino**

| Nombre | Tipo | Offset | Tamaño |
|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x5000 (igual) |
| otadata | data/ota | 0xE000 | 0x2000 (igual) |
| ota_0 | app | 0x10000 | 0x280000 (2,5 MB) |
| ota_1 | app | 0x290000 | 0x280000 (2,5 MB) |
| spiffs (LittleFS) | data | 0x510000 | 0xAE0000 (igual) |
| coredump | data | 0xFF0000 | 0x10000 (igual) |

Las dos ranuras caben en el antiguo `app0`, así que NVS y LittleFS **no se
mueven**: se podría pasar de Arduino a ESP-IDF (y volver) sin perder notas,
dibujos ni ajustes (hay que verificar en la Fase 2 que el formato de LittleFS es
el mismo). Riesgo: 2,5 MB por ranura.

**Opción B — más margen de firmware**: `ota_0`/`ota_1` de 4 MB, LittleFS de
7,9 MB en 0x810000. Obliga a reformatear LittleFS (copia de seguridad antes).

**Recomendación**: medir el binario real al final de las Fases 1–3 (se esperan
~1,5–2,5 MB con LVGL, esp-hosted y TLS; es una estimación). Si queda por
debajo de ~2,0 MB, opción A; si no, opción B. La tabla se fija **antes del
primer grabado en la placa**, porque cambiarla después cuesta los datos.

### 5.9 OTA

* `esp_https_ota` con TLS verificado y escritura por bloques en la ranura libre.
* `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`: la imagen nueva arranca como
  *pendiente* y solo se confirma (`esp_ota_mark_app_valid_cancel_rollback`)
  cuando la UI ha pintado, el táctil responde y el sistema lleva ~30 s estable;
  si no, el siguiente reinicio vuelve a la anterior.
* Comprobaciones antes de escribir: espacio de la ranura, `project_name` y
  versión de `esp_app_desc_t`, SHA-256 de la imagen (que el bootloader vuelve a
  verificar), y firma opcional (Fase 5).
* **Clave de manifiesto propia `flex_os_ultra_idf`**: un P4 con firmware Arduino
  nunca debe instalar la imagen ESP-IDF (otra tabla de particiones) ni al revés.
  El manifiesto y el `.bin` de Arduino no se tocan.

### 5.10 Logs, watchdog y errores

* `esp_log` con etiqueta por componente (`flex.ui`, `flex.wifi`...), nivel por
  defecto `INFO` en desarrollo y `WARN` en producción; nada se registra por
  fotograma; métricas periódicas (FPS, latencia táctil, memoria) solo con un
  interruptor de depuración.
* TWDT suscrito por las tareas de larga vida (UI, audio, storage, net); los
  bucles de espera usan tiempos límite, nunca `portMAX_DELAY` sin motivo.
* `ESP_ERROR_CHECK` solo en el arranque de lo imprescindible (pantalla); en el
  resto, `esp_err_t` propagado y estado de error visible en la UI.
* Volcado de pila a la partición `coredump`; Device Care muestra el motivo del
  último reinicio (como la "banda forense" actual).

---

## 6. Qué se reutiliza y qué se reescribe

| Categoría | Módulos | Tratamiento |
|---|---|---|
| **Lógica pura, con pruebas de host** | `FlexOS_Mem`, `Media`, `FallDetect`, `Theft`, `CloudCore`, `StorageCore`, `PkgCore`, `AppVM`, `AppGrant`, `AppHost`, `FlexAuth`, `HttpShare`, `ImgEdit`, `JPEGEnc`, `MediaStore`, `MediaThumb`, `QR`, `VidEdit`, `Browser` (núcleo), `FlexPhone_Transport` (~13 000 líneas) | Se **copian** a `flex_portable/` y se compilan sin cambios (si alguno necesita uno, se documenta). Conservan sus pruebas. |
| **Servicios con Arduino debajo** | `Account`, `Cloud`, `CloudTLS`, `OTA`, `Weather`, `FS`, `Passcode`, `Audio`, `BNO085`, `Store`, `Package`, `StorageLink`, `FlexLink`, `FlexPhone(_Link)`, `MediaLib`, `MediaWeb`, `BrowserApp`, `Runtime` | Se **reescriben** sobre ESP-IDF conservando protocolo, estados y textos. La versión Arduino es la especificación. |
| **Interfaz** (todas las `FlexOS_Ultra_*.h`) | 19 apps y ~25 pantallas | Se **rediseñan en LVGL** para 480×800, pantalla a pantalla, con la Arduino como referencia visual y funcional. |
| **Datos del fabricante** | tabla DCS del ST7701, temporización DPI, configuración del ES8311 | Se reutilizan (son datos del hardware). |

---

## 7. Fases

Cada fase termina con: compilación limpia para `esp32p4`, informe de tamaño,
commit(s) pequeños y una lista explícita de lo que queda **NO PROBADO EN
HARDWARE REAL**.

| Fase | Contenido | Verificable aquí | Requiere la placa |
|---|---|---|---|
| **0. Infraestructura** | ESP-IDF fijado, proyecto, `sdkconfig.defaults` (rev del P4, PSRAM, DSI *cache-safe*, TWDT, logs), particiones provisionales, componentes vacíos, dependencias fijadas, script de build | build, `idf.py size`, `gen_esp32part` | arranque |
| **1. Pantalla + táctil + LVGL** | LDO/DSI/ST7701/DPI con 2 FB, puerto LVGL DIRECT, brillo LEDC, tarea GT911, medidor de FPS y de latencia táctil de depuración, pantalla de prueba 480×800 | build, simulador de la UI | imagen, tearing, FPS, latencia, coordenadas táctiles |
| **2. Núcleo del sistema** | `flex_bus`, `flex_storage` (NVS compatible + LittleFS), logging, watchdog, métricas de Device Care, I2C compartido con recuperación | build, pruebas de host | montaje de LittleFS existente, NVS existente |
| **3. Shell LVGL** | tema Liquid Glass, componentes, fuentes, barra de estado y de navegación, splash, OOBE, bloqueo, escritorio por páginas, caja de apps, panel rápido, notificaciones, marco de apps con ciclo de vida, Recientes | build, capturas 480×800 en el simulador | fluidez real |
| **4. Red** | esp-hosted + Wi-Fi (estados, reconexión, pérdida de conexión sin bloquear la UI), NTP, pantalla Wi-Fi | build | enlace con el C6 |
| **5. Account, Cloud, OTA** | reescritura sobre `esp_http_client`, rollback, manifiesto `flex_os_ultra_idf` | build, pruebas de protocolo en el PC contra el servidor de pruebas | servicio real |
| **6. Multimedia** | JPEG HW (galería, visor, miniaturas), AVI MJPEG con cola de fotogramas, audio ES8311 (WAV/ADPCM), Música; H.264 *baseline* por software y MP3 solo tras verificar demultiplexor y decodificador | build | rendimiento real |
| **7. Sensores** | BNO085 sobre `i2c_master`, servicio IMU, Flex Compass ("BNO085 ● Conectado", "AHRS ● Activo"), caídas, robo | build, pruebas del driver con bus simulado | sensor real |
| **8. Apps restantes** | Ajustes, Notas, Paint, Calculadora, Calendario, Reloj/Cronómetro, Clima, Archivos, Almacenamiento, Juegos, Store, Navegador, Flex Phone, Web Server, Modo PC, teclado | build, simulador | — |
| **9. Cámara** | CSI + ISP + `esp_video`, vista previa por PPA, foto con JPEG HW | build | **necesita el modelo del sensor** |
| **10. Energía y cierre** | perfiles 1000–4000 mAh (**estimación**, nunca "medido"), suspensión/apagado, pruebas de estrés, `ESP_IDF_MIGRATION_REPORT.md`, `ESP_IDF_HARDWARE_REQUIREMENTS.md` | build | consumo real |

**Sobre el alcance:** la versión Arduino tiene 116 000 líneas y ~45 pantallas.
Reimplementarla bien en LVGL no cabe en una sola sesión de trabajo; se hará fase
a fase, cada una compilada y documentada, sin dar por hecha ninguna pantalla que
no esté implementada.

---

## 8. Riesgos y limitaciones conocidas

* **Firmware del C6**: el *slave* de esp-hosted del C6 debe ser compatible con la
  versión del *host* que se use en el P4. Si no lo es, habrá que actualizar el C6
  (esp-hosted permite hacerlo desde el P4). Hay que saber qué versión tiene hoy.
* **Registro de componentes**: en este entorno `components.espressif.com` y
  `dl.espressif.com` están bloqueados (403); GitHub sí responde. Las
  dependencias se fijarán de forma que se puedan obtener desde GitHub y el
  build sea reproducible también aquí.
* **Sin emulador del P4 aquí**: lo que se verifica sin placa es compilación,
  tamaño, particiones, pruebas de host y capturas del simulador de UI.
* **H.264**: solo por software, perfil *baseline*; 480×800 a pantalla completa no
  llega a 30 fps según las cifras del fabricante. MJPEG con JPEG HW sigue siendo
  el formato principal. Falta verificar un demultiplexor MP4.
* **Formato de LittleFS** entre `esp_littlefs` de Arduino y el de ESP-IDF: se
  comprueba en la Fase 2 antes de prometer compatibilidad de datos.
* **Batería**: sin hardware de medida conocido no se mostrará ningún porcentaje
  "real".
* **ESP-IDF**: recomiendo **v5.5.x** (última rama 5.x, con soporte maduro del
  P4; v6.x trae cambios incompatibles y queda para más adelante). La
  compatibilidad con esp-hosted, `esp_video` y LVGL 9 se confirma en la Fase 0
  compilando, no se da por supuesta.

---

## 9. Preguntas abiertas

1. **Revisión del chip P4** (bloqueante para grabar): la línea `chip revision:
   vX.Y` del arranque o la salida de `esptool.py chip_id`.
2. **Tabla de particiones**: ¿conservar los datos al cambiar de firmware
   (opción A) o priorizar margen de firmware (opción B)?
3. **Cámara**: modelo del sensor (serigrafía del módulo o del flex) y si viene en
   el kit de la placa.
4. **Batería**: ¿la placa tiene divisor a un ADC o un *fuel gauge*? ¿Qué pin? (El
   esquema JC4880P443 V1.0 que citan los documentos BETA lo diría.)
5. **C6**: versión del firmware esp-hosted que lleva hoy (sale en el log del P4).
6. **GT911 INT**: ¿está cableado a un GPIO del P4? (Permite leer el táctil por
   interrupción en vez de sondear.)
