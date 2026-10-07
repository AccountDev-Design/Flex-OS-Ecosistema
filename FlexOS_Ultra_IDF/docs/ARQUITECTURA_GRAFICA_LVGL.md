# Arquitectura gráfica: toda la interfaz con LVGL

## La regla

```
ESP-IDF → FreeRTOS → LVGL 9.6 → framebuffer en PSRAM → DMA (y PPA cuando se mida) → ST7701 480×800
```

* **LVGL es el único motor gráfico** de Flex OS Ultra en ESP-IDF. Cada pantalla, componente y efecto
  se construye con objetos, widgets, estilos, eventos y animaciones de LVGL.
* **No se envuelve el motor de dibujo de la versión Arduino** ni se mantiene un segundo motor en
  paralelo. El código Arduino (`FlexOS_Ultra/`) sirve solo para estudiar qué hace cada pantalla y
  cómo se comporta.
* **480×800 vertical**, definido en un único sitio (`components/flex_board/include/flex_board.h`).
  Los layouts se diseñan para 480×800 con el sistema de diseño (`flex_ui/src/theme/flex_ui_theme.h`);
  no se convierten coordenadas del motor Arduino una a una.
* **Solo la tarea de UI (núcleo 1) llama a LVGL.** Los servicios no ven `lvgl.h`.

## Equivalencias (Arduino → LVGL)

| Versión Arduino (motor propio) | Versión ESP-IDF |
|---|---|
| `fillRect`, paneles, tarjetas | `lv_obj` con estilos (fondo, radio, borde, sombra) |
| `drawText` | `lv_label` |
| botones dibujados a mano | `lv_button` / `lv_obj` clicable con estados |
| menús y listas | contenedores con *flex/grid*, `lv_list`, `lv_menu` |
| sliders y switches | `lv_slider`, `lv_switch`, `lv_bar`, `lv_arc` |
| toques leídos a mano | eventos de LVGL (`LV_EVENT_CLICKED`, `PRESSING`, gestos) |
| animaciones por fotograma | `lv_anim`, transiciones de estilo y de pantalla |
| overlays (isla, panel rápido, avisos) | objetos en `lv_layer_top()` / capas de LVGL |
| Liquid Glass | estilos y componentes LVGL (ver abajo) |
| iconos vectoriales por código | fuentes de iconos / imágenes LVGL |

Lienzos (`lv_canvas`) solo donde el contenido **es** un lienzo (por ejemplo, la superficie de dibujo de
Paint), con lista explícita en `tools/check_lvgl_only.py`.

## Cómo se hace cumplir

`tools/check_lvgl_only.py` (lo ejecuta `tools/build.sh` en cada build) falla si:

1. algún componente distinto de `flex_display` crea el panel, pide sus framebuffers o le envía píxeles;
2. `lvgl.h` se incluye fuera de `flex_ui` y de los dos puertos (pantalla y táctil);
3. aparece una cabecera de la versión Arduino o una primitiva de dibujo manual (`fillRect`, `drawText`…);
4. se usa `lv_canvas` fuera de la lista de lienzos permitidos (vacía en las Fases 0-1).

## Pipeline de pantalla (Fase 1)

* El driver DPI de ESP-IDF reserva **dos framebuffers** RGB565 de 768 000 B en PSRAM.
* LVGL dibuja en **modo DIRECT** directamente sobre el framebuffer de atrás.
* Al terminar un cuadro, `esp_lcd_panel_draw_bitmap()` con ese framebuffer **no copia nada**: escribe la
  caché a la PSRAM (solo las filas tocadas en este cuadro y el anterior) y lo elige para el **siguiente**
  cuadro del DMA. El cambio ocurre siempre entre dos cuadros completos.
* LVGL no vuelve a tocar el otro framebuffer hasta que la ISR de fin de cuadro confirma que el DMA ya
  lo terminó de leer (`flush_wait_cb`). Mientras espera, la tarea de UI sigue atendiendo el táctil.
* La ISR corre en el mismo núcleo que la UI (el panel se crea desde la tarea de UI), lo que hace
  seguro el aviso de cambio de buffer sin vaciar semáforos.
* `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y`: la ISR sigue funcionando cuando una escritura en flash apaga la
  caché (corrige el destello azul/cian de la versión Arduino).

Detalles y evidencias del código fuente: `docs/FASE_0_1_INFORME.md`.

## Liquid Glass (Fase 3)

Prioridad: calidad visual sin comprometer estabilidad ni fluidez.

1. **Vidrio cacheado** (por defecto): el fondo se desenfoca una vez al cambiar de fondo o de tema (fuera
   del bucle de dibujo, con PPA para escalar si la medición lo justifica) y cada panel muestra su
   recorte + tinte + borde especular + sombra.
2. **Vidrio vivo**: desenfoque real de LVGL 9.6 (`blur_backdrop`, `blur_radius`, `blur_quality`) solo en
   superficies pequeñas o quietas, nunca durante animaciones de pantalla completa.
3. **Plano**: tintes sin desenfoque para memoria o batería bajas.

`LV_USE_PPA` y el render en dos núcleos están **desactivados** en las Fases 0-1: se activarán midiendo en la
placa (si la PPA no gana en áreas pequeñas, se limita a las grandes).
