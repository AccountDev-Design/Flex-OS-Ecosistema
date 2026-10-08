#!/usr/bin/env python3
"""Comprueba la regla de arquitectura grafica de Flex OS Ultra (ESP-IDF).

  ESP-IDF -> FreeRTOS -> LVGL -> framebuffer/PSRAM -> DMA/PPA -> ST7701 480x800

1. Solo el puerto de pantalla (flex_display) habla con el panel: crear el panel
   DPI, pedir sus framebuffers o enviarle pixeles.
2. Solo la interfaz (flex_ui) y los dos puertos de LVGL (pantalla y tactil)
   pueden incluir lvgl.h. Un servicio que no ve LVGL no puede llamarlo desde
   otra tarea.
3. Ningun archivo del proyecto ESP-IDF incluye cabeceras del motor grafico
   Arduino ni reimplementa sus primitivas de dibujo.
4. lv_canvas (dibujo por pixeles) solo donde el contenido ES un lienzo
   (lista explicita, vacia en las Fases 0-1).

Uso: python3 tools/check_lvgl_only.py   (sale con 1 si hay infracciones)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCAN_DIRS = ["main", "components"]
SOURCE_EXT = {".c", ".h", ".cpp", ".hpp", ".cc"}

PANEL_OWNERS = ("components/flex_display/",)
LVGL_ALLOWED = (
    "components/flex_ui/",
    "components/flex_display/src/flex_display_lvgl.c",
    "components/flex_display/include/flex_display_lvgl.h",
    "components/flex_touch/src/flex_touch_lvgl.c",
    "components/flex_touch/src/flex_touch_feed.c",   # arbitraje del tactil -> indev de LVGL
    "components/flex_touch/include/flex_touch_lvgl.h",
)
# main/ solo puede ver la API publica de flex_ui (flex_ui.h), nunca LVGL.
UI_CONSUMERS = ("main/app_main.c",)
CANVAS_ALLOWED: tuple = ()

PANEL_API = re.compile(
    r"\b(esp_lcd_panel_draw_bitmap|esp_lcd_dpi_panel_get_frame_buffer|esp_lcd_new_panel_dpi"
    r"|esp_lcd_new_dsi_bus|esp_lcd_new_panel_io_dbi)\s*\("
)
# Cualquier cabecera de LVGL (lvgl.h, lvgl_private.h, core/lv_obj.h, lvgl__lvgl/...)
# o de los puertos que la reexportan.
LVGL_INCLUDE = re.compile(
    r'#\s*include\s*[<"]([^">]*/)?(lvgl[^">/]*\.h|lv_[^">/]*\.h|flex_display_lvgl\.h|flex_touch_lvgl\.h'
    r'|flex_ui[^">/]*\.h)[">]|#\s*include\s*[<"]lvgl__lvgl/'
)
# Llamadas o tipos de LVGL fuera de la UI (aunque llegaran por otra cabecera).
LVGL_SYMBOL = re.compile(r"\blv_[a-z0-9_]+\s*\(|\blv_[a-z0-9_]+_t\b")
ARDUINO_GFX_INCLUDE = re.compile(r'#\s*include\s*[<"](FlexOS_Ultra_[^">]*|Arduino\.h)[">]')
ARDUINO_GFX_PRIMITIVE = re.compile(
    r"\b(fillRect|drawRect|drawText|drawPixel|fillRoundRect|drawRoundRect|fillCircle|drawCircle"
    r"|drawLine|blitRect|gfxFlush|fbFlush)\s*\("
)
CANVAS_API = re.compile(r"\blv_canvas_\w+\s*\(")


def rel(path: pathlib.Path) -> str:
    return path.relative_to(ROOT).as_posix()


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main() -> int:
    problems = []
    for d in SCAN_DIRS:
        base = ROOT / d
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in SOURCE_EXT or not path.is_file():
                continue
            r = rel(path)
            code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
            for no, line in enumerate(code.splitlines(), 1):
                if PANEL_API.search(line) and not r.startswith(PANEL_OWNERS):
                    problems.append(f"{r}:{no}: API del panel fuera de flex_display: {line.strip()}")
                if LVGL_INCLUDE.search(line) and not r.startswith(LVGL_ALLOWED + UI_CONSUMERS):
                    problems.append(f"{r}:{no}: cabecera de LVGL fuera de la UI/puertos: {line.strip()}")
                if LVGL_SYMBOL.search(line) and not r.startswith(LVGL_ALLOWED):
                    problems.append(f"{r}:{no}: uso de LVGL fuera de la UI/puertos: {line.strip()}")
                if ARDUINO_GFX_INCLUDE.search(line):
                    problems.append(f"{r}:{no}: cabecera de la version Arduino: {line.strip()}")
                if ARDUINO_GFX_PRIMITIVE.search(line):
                    problems.append(f"{r}:{no}: primitiva de dibujo manual (motor paralelo): {line.strip()}")
                if CANVAS_API.search(line) and not r.startswith(CANVAS_ALLOWED):
                    problems.append(f"{r}:{no}: lv_canvas fuera de la lista de lienzos permitidos: {line.strip()}")
    if problems:
        print("Regla 'toda la UI con LVGL' INCUMPLIDA:")
        for p in problems:
            print("  " + p)
        return 1
    print("OK: la UI solo se dibuja con LVGL y solo flex_display toca el panel.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
