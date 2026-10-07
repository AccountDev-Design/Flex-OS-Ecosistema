# Fases 0 y 1 · informe

> **NO PROBADO EN HARDWARE REAL.** Todo lo de este documento se ha compilado y comprobado en el PC
> (build de ESP-IDF, simulador de LVGL, lectura del código fuente de ESP-IDF 5.5.5 y LVGL 9.6.0). Nada se
> ha ejecutado en la placa. La sección "Pruebas en la placa" dice qué hay que comprobar allí.

## Qué hay

### Fase 0 · infraestructura

* Proyecto ESP-IDF en `FlexOS_Ultra_IDF/`, fijado a **ESP-IDF v5.5.5** y **LVGL 9.6.0** (por git, commit
  `80ca777e` en `dependencies.lock`; no depende del registro de componentes).
* Variantes por revisión del chip (`lt_v3` / `v3`) y de compilación (`dev` / `release`), cada una en su
  carpeta y con su sdkconfig (`tools/build.sh`).
* `sdkconfig.defaults` (+ `.rev_lt_v3`, `.rev_v3`, `.release`): PSRAM HEX 200 MHz, flash QIO 80 MHz 16 MB,
  `LCD_DSI_ISR_CACHE_SAFE`, FreeRTOS a 1000 Hz, TWDT, coredump en flash, rollback de OTA en el bootloader,
  logs por componente. Cada símbolo verificado contra el Kconfig real; `tools/check_sdkconfig.py` comprueba
  en cada build que las 68 opciones llegan al sdkconfig (un nombre mal escrito no da error en ESP-IDF).
* Tablas de particiones candidatas A y B, ambas **provisionales** (`docs/PARTICIONES.md`).
* Componentes del plan creados; los de fases futuras están vacíos y cada uno documenta qué funcionalidad
  de la versión Arduino recibirá, para que ninguna se pierda.
* Herramientas: `build.sh`, `flash_guard.py` (revisión del chip + datos del usuario), `size_report.py`,
  `check_sdkconfig.py`, `check_lvgl_only.py`.

### Fase 1 · pantalla + táctil + LVGL

* `flex_display`: LDO canal 3 a 2,5 V → bus DSI 2×500 Mbps → ST7701 (tabla DCS del fabricante, idéntica
  byte a byte a la de Arduino, comprobado con un script) → panel DPI 34 MHz RGB565 con **2 framebuffers en
  PSRAM** → puerto LVGL en **modo DIRECT** → brillo por LEDC (20 kHz, 10 bits, misma curva que Arduino).
* `flex_touch`: GT911 en su **propia tarea** (núcleo 0, prioridad 6, sondeo cada 8 ms porque el INT no está
  identificado), hasta 5 dedos, escalado según la resolución configurada en el chip, reinicio del GT911 si
  se pierde. La UI copia el último cuadro sin bloquear nunca.
* `flex_i2c`: bus compartido (GT911, ES8311, BNO085) con mutex, tiempos límite cortos, estadística y
  recuperación del bus (reset de la máquina de estados + 9 pulsos de SCL) tras 8 fallos seguidos.
* `flex_ui`: tarea de UI en el núcleo 1 (la única que llama a LVGL) y **pantalla de prueba 480×800** hecha
  solo con objetos LVGL: rejilla con esquinas marcadas (0,0 · 479,0 · 0,799 · 479,799) y centro, barras de
  color R/G/B/blanco, métricas en vivo, deslizador de brillo, interruptores de animación y estrés, prueba de
  tearing (barra en movimiento) y un círculo por dedo con su coordenada.
* Métricas de depuración (pantalla siempre; log cada 5 s con `CONFIG_FLEX_DEBUG_METRICS`): Hz reales del
  panel, cuadros nuevos por segundo, tiempo de render, espera de fin de cuadro, CPU de la tarea de UI,
  latencia táctil (lectura I2C → LVGL → cambio de framebuffer → cuadro mostrado), memoria, chip, GT911, I2C.

## Decisiones tomadas con evidencia del código fuente

| Decisión | Por qué (fuente) |
|---|---|
| `in_color_format = LCD_COLOR_FMT_RGB565` y no `pixel_format` | `pixel_format` a 0 significa RGB888 (`hal/color_types.h:51`); el driver lo llama "deprecated" (`esp_lcd_panel_dpi.c:193`) |
| Sin DMA2D (`use_dma2d = 0`) | Con DIRECT nunca se copia; DMA2D/CPU copian sobre el FB que se está mostrando (`esp_lcd_panel_dpi.c:489, 529-579`) |
| Cambio de FB con `draw_bitmap(FB)` y espera a `on_frame_buf_complete` | El índice nuevo solo lo lee la ISR de fin de cuadro, que relanza el DMA sobre él y luego avisa (`esp_lcd_panel_dpi.c:71-91, 504-528`) |
| Panel creado desde la tarea de UI (núcleo 1) | Las interrupciones se reservan en el núcleo que las crea (`intr_alloc.rst:85`); así la ISR y el cambio de buffer no se solapan |
| Escribir a PSRAM las filas de este cuadro y del anterior | `draw_bitmap` solo escribe las filas pedidas (`esp_lcd_panel_dpi.c:519-522`) y LVGL copia sin avisar las zonas del cuadro anterior (`lv_refr.c:677-790`) |
| `flush_wait_cb` en vez de esperar dentro de `flush_cb` | LVGL espera justo antes de copiar al otro buffer (`lv_refr.c:691, 1418`): mientras, la UI sigue leyendo el táctil |
| No usar `esp_lvgl_port` | Su callback de DSI no está en IRAM y con `CACHE_SAFE` el registro falla y el flush se bloquea para siempre (`esp_lvgl_port_disp.c:46-50, 168, 755`) |
| No usar el driver `esp_lcd_st7701` | Lee el ID del panel con `rx_param`, que fuerza modo comando y espera sin límite (`mipi_dsi_hal.c:209-218`) |
| `phy_clk_src = 0` | El driver elige la referencia del PLL según la revisión (`esp_lcd_mipi_dsi_bus.c:41-48`) |
| `REV_MIN_1` para chips < v3 | `REV_MIN_0` elimina la flash a 80 MHz (`spi_flash/esp32p4/Kconfig.flash_freq:11`) |
| Bootloader en WARN | Con INFO solo quedaban 416 B antes de la tabla de particiones (medido) |
| `LV_OS_NONE` (todo el dibujo en la tarea de UI) | LVGL crea sus hilos de dibujo sin fijar núcleo (`lv_freertos.c:95`); el render en dos núcleos se medirá |
| `SPIRAM_XIP_FROM_PSRAM` apagado | Cargaría el bus de PSRAM que ya usa el barrido del panel (~46 MB/s); `CACHE_SAFE` corrige el destello sin ese coste. Se medirá en la Fase 2 |

## Diferencias respecto a la versión Arduino (documentadas)

* Dos framebuffers y modo DIRECT en lugar de un framebuffer propio + copia por DMA2D.
* La tabla DCS del ST7701, los tiempos de reset y la temporización DPI son los mismos. El reloj DPI real es
  34,2857 MHz (240/7) en ambos casos y el driver compensa el pórtico: ~60,46 Hz.
* El retroiluminado se enciende cuando LVGL ya ha mostrado su primer cuadro (antes: al acabar el arranque).
* El bus I2C se recupera con el reset por hardware del controlador (`i2c_master_bus_reset`: máquina de
  estados + 9 pulsos de SCL); la versión Arduino lo hacía moviendo los pines a mano.
* En el mapeo del táctil se recorta antes de espejar (en Arduino, un valor fuera de rango espejado daba la
  vuelta como entero sin signo).
* Textos de la pantalla de prueba sin tildes: las fuentes integradas de LVGL solo traen ASCII. La
  tipografía con alfabeto latino completo (Outfit) llega con el sistema de diseño en la Fase 3.

## Verificación hecha aquí

| Comprobación | Resultado |
|---|---|
| Build `lt_v3` y `v3` con `-Werror` en la app | sin errores ni avisos |
| sdkconfig (68 opciones) | todas presentes con su valor |
| Regla "toda la UI con LVGL" | cumple |
| Tablas de particiones (`gen_esp32part`, 16 MB) | A y B válidas |
| Simulador: 1 051 comparaciones de lo "mostrado" contra un render completo, con animación, toques, dos dedos, modo estrés y brillo | 0 diferencias |
| Simulador: LVGL nunca escribe en el FB visible ni lo vuelve a entregar | 0 casos |
| Simulador: pantalla quieta (sin animación ni toques) | 0 cuadros nuevos |
| Tabla DCS frente a la de Arduino | idéntica (40 comandos) |

El simulador modela la caché: cada framebuffer tiene una vista de CPU y otra de "PSRAM"; en cada cambio
de buffer solo se copian las filas que calcula el puerto real (`flex_flip_rows.h`, el mismo archivo que
compila el firmware). Si faltara alguna fila, lo mostrado diferiría de la referencia. Las métricas que
enseña el simulador (Hz, latencias, memoria, chip) son **valores simulados**.

Capturas del simulador: `docs/capturas/`.

## Mediciones

| | lt_v3 | v3 |
|---|---|---|
| Firmware | 832 544 B | 834 720 B |
| Bootloader (límite 24 576 B) | 21 408 B | 22 144 B |
| RAM interna estática usada (DIRAM) | 99 880 B de 576 464 B | — |

## NO PROBADO EN HARDWARE REAL

Todo. En particular:

1. Que el binario arranque en la placa (depende de la revisión del chip, sin confirmar).
2. Imagen en el ST7701 con ESP-IDF 5.5.5 (orden de arranque, reloj DSI continuo o no).
3. Ausencia de tearing y de destellos al escribir en flash.
4. Frecuencia real del panel (~60,5 Hz esperados) y FPS de la UI.
5. Latencia táctil real y calibración de coordenadas.
6. Coste real de la escritura de caché por cuadro y ancho de banda de la PSRAM.
7. Comportamiento del bus I2C con el ES8311 y el BNO085 conectados y al desconectar el IMU en caliente.
8. PSRAM a 200 MHz en esta placa con ESP-IDF 5.5.5.
9. Consola: por qué conector USB salen los logs (UART0 y USB-Serial/JTAG activados).

## Pruebas en la placa (cuando se grabe)

1. `tools/flash_guard.py --solo-comprobar` → anotar la revisión. Compilar la variante que diga.
2. Grabar con la guardia (tabla A provisional, no toca NVS ni LittleFS).
3. Log de arranque: revisión del chip, `panel 480x800 listo`, `GT911 en 0x..`, `interfaz LVGL 9.6.0 en marcha`.
4. Imagen: las cuatro esquinas visibles con su etiqueta, centro en (240,400), barras R/G/B en ese orden
   (si salen cambiadas, el orden de color del panel no es el esperado).
5. Tearing: la barra blanca en movimiento debe verse siempre entera; activar "Estres" y repetir.
6. Métricas: "Panel" ~60,5 Hz; FPS; CPU de la UI; espera de fin de cuadro; vencidas = 0.
7. Táctil: tocar las esquinas y el centro y comparar la coordenada mostrada; arrastrar; dos dedos.
8. Latencia: fila "Toque > pantalla" (media/máx).
9. Brillo: deslizador de 5 a 100 %.
10. Si el panel queda en negro: activar `CONFIG_FLEX_DISPLAY_BOOT_TEST_PATTERN` (barras del propio DSI);
    si se ven, probar `CONFIG_FLEX_DISPLAY_DCS_BEFORE_VIDEO`; si no, `CONFIG_FLEX_DSI_CLOCK_LANE_FORCE_HS`.

## Preguntas abiertas (del plan)

1. Revisión del chip (bloqueante para grabar).
2. Tabla A o B (se decide con el tamaño de las Fases 1-3).
3. Modelo del sensor de la cámara.
4. Medida de batería (pin/ADC o *fuel gauge*).
5. Versión del firmware esp-hosted del C6.
6. ¿El INT del GT911 está cableado a un GPIO? (permitiría leer por interrupción).
