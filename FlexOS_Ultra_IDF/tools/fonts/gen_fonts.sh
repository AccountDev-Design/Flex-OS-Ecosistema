#!/usr/bin/env bash
# Genera las fuentes LVGL de Flex OS Ultra desde Outfit Regular (OFL, ver
# OFL.txt), la misma familia que la version Arduino. Necesita Node:
#   npx lv_font_conv@1.5.3 (se descarga solo)
#   tools/fonts/gen_fonts.sh
# Las salidas (components/flex_ui/src/fonts/*.c) se guardan en el repositorio:
# el build del firmware no necesita Node.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
OUT=$ROOT/components/flex_ui/src/fonts
LVF=$ROOT/managed_components/lvgl__lvgl/scripts/generators/built_in_font
[ -f "$LVF/DejaVuSans.ttf" ] || { echo "falta LVGL en managed_components: compila antes (tools/build.sh)" >&2; exit 2; }
CONV=(npx --yes lv_font_conv@1.5.3)

# Texto: ASCII + Latin-1 (tildes, ñ, ¿ ¡ ° ·) + tipografia que llega en
# notificaciones (comillas, guiones, puntos suspensivos, €, flechas, ✓).
TXT=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026,0x2039-0x203A,0x20AC,0x2190-0x2193,0x2212,0x2713
# Outfit no trae estos: salen de DejaVu Sans (los usa p. ej. "BNO085 ● Conectado").
EXTRA=0x25CF,0x25CB,0x2264,0x2265,0x221E
# Simbolos de LVGL (LV_SYMBOL_*): los usan el teclado y otros widgets.
SYMS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650

for SIZE in 11 13 16 19 25 31 38; do
    NAME=flex_font_outfit_${SIZE}
    "${CONV[@]}" --bpp 4 --size "$SIZE" --no-compress \
        --font Outfit-Regular.ttf -r "$TXT" \
        --font "$LVF/DejaVuSans.ttf" -r "$EXTRA" \
        --font "$LVF/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYMS" \
        --format lvgl --lv-include lvgl.h --lv-font-name "$NAME" -o "$OUT/$NAME.c"
done

# Reloj grande del bloqueo/escritorio: solo digitos y ':' (mayusculas de ~139 px,
# como el reloj vectorial de la version Arduino con capH = 140).
"${CONV[@]}" --bpp 4 --size 200 \
    --font Outfit-Regular.ttf -r 0x30-0x3A \
    --format lvgl --lv-include lvgl.h --lv-font-name flex_font_clock_200 -o "$OUT/flex_font_clock_200.c"
ls -la "$OUT"
