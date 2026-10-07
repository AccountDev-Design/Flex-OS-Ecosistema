# Flex OS Ultra · versión ESP-IDF (ESP32-P4)

Reescritura nativa en ESP-IDF de Flex OS Ultra para la GUITION JC4880P443C (ESP32-P4, pantalla
MIPI-DSI 480×800 con ST7701, táctil GT911). La interfaz se dibuja **solo con LVGL 9.6**
(`docs/ARQUITECTURA_GRAFICA_LVGL.md`). La versión Arduino (`../FlexOS_Ultra/`) es la referencia
funcional y no se modifica.

**Estado: Fases 0 y 1** (infraestructura + pantalla, táctil y LVGL con una pantalla de prueba).
Todo compila y se ha verificado en el PC; **nada está probado en hardware real**
(`docs/FASE_0_1_INFORME.md`). Plan completo: `ESP_IDF_MIGRATION_PLAN.md`.

## Requisitos

* ESP-IDF **v5.5.5** (`git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git`, `./install.sh esp32p4`).
* LVGL 9.6.0 se descarga solo (por git, fijado en `dependencies.lock`).

## Compilar

```
. $IDF_PATH/export.sh
tools/build.sh                 # chip < v3 (por defecto)
tools/build.sh --rev v3        # chip v3.x
tools/build.sh --release       # logs WARN, asserts silenciosos
```

Cada variante va en su carpeta (`build_lt_v3`, `build_v3`…) con su propio `sdkconfig`. El script además
comprueba que todas las opciones de `sdkconfig.defaults*` llegan al sdkconfig, que la UI solo usa LVGL y
el tamaño real frente a las tablas de particiones.

Con VS Code / `idf.py` directamente también funciona (`idf.py build` usa la variante `lt_v3`), pero un
`sdkconfig` existente manda sobre los defaults: para cambiar de revisión, borra `sdkconfig` o usa el script.

## Grabar (antes de nada: revisión del chip)

```
python3 tools/flash_guard.py --port /dev/ttyUSB0 --solo-comprobar
python3 tools/flash_guard.py --port /dev/ttyUSB0 --build build_lt_v3 --acepto-tabla-provisional
```

La guardia lee la revisión real con esptool, se niega si el binario no corresponde, muestra qué se
escribe y comprueba que NVS y LittleFS (datos de la versión Arduino) no se tocan. No uses `idf.py flash`
mientras la revisión y la tabla no estén cerradas (`docs/REVISION_CHIP.md`, `docs/PARTICIONES.md`).

## Simulador de la interfaz en el PC

```
cmake -S sim -B sim/build && cmake --build sim/build -j
sim/build/flexos_sim sim/out && python3 sim/ppm2png.py sim/out/*.ppm
```

Dibuja la misma pantalla de prueba 480×800 con un guion de toques, guarda capturas y comprueba el
modelo de doble framebuffer y de caché del puerto de pantalla (ver el informe).

## Estructura

| Carpeta | Contenido |
|---|---|
| `main/` | `app_main()`: orden de arranque y nada más |
| `components/flex_board` | pines, geometría y temporización de la placa (un solo sitio) |
| `components/flex_core` | reparto de tareas, datos de arranque, métricas de depuración |
| `components/flex_i2c` | bus I2C compartido con recuperación |
| `components/flex_display` | LDO + DSI + ST7701 + DPI con 2 framebuffers + puerto LVGL + brillo |
| `components/flex_touch` | GT911 en su propia tarea + entrada de LVGL |
| `components/flex_ui` | LVGL: sistema de diseño y pantallas (Fase 1: pantalla de prueba) |
| `components/flex_*` (resto) | reservados para las fases siguientes; cada uno dice qué recibirá |
| `partitions/` | tablas candidatas A y B (provisionales) |
| `tools/` | build, guardia de grabación, informes y comprobaciones |
| `sim/` | simulador de la UI en el PC |
| `docs/` | arquitectura, revisión del chip, particiones, informe de fases |
