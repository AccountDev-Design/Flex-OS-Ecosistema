#!/usr/bin/env bash
# Pruebas de host de Flex OS Ultra (ESP-IDF): la logica portable de los
# componentes, compilada para el PC con AddressSanitizer + UBSan y, aparte,
# ThreadSanitizer para las pruebas con hilos. Donde hay una version Arduino de
# la misma logica, se extrae su codigo (ref/extract_arduino.py) y se compara
# el resultado bit a bit.
#   tests/host/run.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
OUT=${OUT:-build}
mkdir -p "$OUT"

# Referencias extraidas de FlexOS_Ultra/ (solo lectura)
python3 ref/extract_arduino.py "$OUT/ref_wall.inc" \
    FlexOS_Ultra_Gfx.h:rgb565,DIV255,mix565,isqrt32,hLine,hLineA,rowsInClip,fillCircleA,TC,wallGradLut,wallTxLut,wallLutReady,wallpaperEnsureLut \
    FlexOS_Ultra_Wallpaper.h:WLUT_N,WLUT_SHIFT,wlut,WLUT_DMAX,wlutRMax,wlutDisc,wlutRing,wallRadial,wallDisc,wallDiag3,wallCorners4,wallFlexOriginal,wallAurora,wallNocturno,wallHalo,wallOnyx,wallOceano,wallVioleta,wallNaturaleza,lum565,onColor,gWallPalOk,gWallAcc,gWallAcc2,wallPaletteBuild

python3 ref/extract_arduino.py "$OUT/ref_glass.inc" \
    FlexOS_Ultra_Gfx.h:rgb565,isqrt32 \
    FlexOS_Ultra_Theme.h:glassBuf,glLine,un565,pk565,glassLuma,GLB_RMAX,GLB_STRIP,glbRecip,glbSR,glbRing,glassBlur,glInset,GLASS_TINT_DIFF_MAX,GLASS_LVL_DEF,GLASS_LVL_STEP,gGlassLvl,gGlR,gGlSpec,glassLevelApply,gGlMinMix,glassTintMix,glassShadeRow

SRCS=(test_main.c test_flex_kv.c test_misc.c test_wallpaper.c test_glass.c
      $ROOT/components/flex_storage/src/flex_kv.c
      $ROOT/components/flex_storage/src/flex_fs_path.c
      $ROOT/components/flex_ui/src/theme/flex_wallpaper.c
      $ROOT/components/flex_ui/src/theme/flex_glass_math.c)
INC=(-I. -I$ROOT/components/flex_storage/include -I$ROOT/components/flex_storage/src
     -I$ROOT/components/flex_display/src -I$ROOT/components/flex_ui/src/theme)
CFLAGS=(-O1 -g -Wall -Wextra -Werror -pthread)

build() {   # $1 = sufijo, resto = flags de sanitizador
    local sfx=$1; shift
    local refs=()
    for r in wall glass; do
        c++ -std=gnu++17 -O1 -g -w "$@" -Iref -I"$OUT" -c ref/ref_${r}_main.cpp -o "$OUT/ref_${r}_$sfx.o"
        refs+=("$OUT/ref_${r}_$sfx.o")
    done
    cc -std=gnu11 "${CFLAGS[@]}" "$@" "${INC[@]}" "${SRCS[@]}" "${refs[@]}" -lstdc++ -lm -o "$OUT/host_$sfx"
}
build asan -fsanitize=address,undefined -fno-sanitize-recover=all
"$OUT/host_asan"
build tsan -fsanitize=thread
"$OUT/host_tsan"
