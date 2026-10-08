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

python3 ref/extract_arduino.py "$OUT/ref_home.inc" \
    FlexOS_Ultra_Types.h:HOME_WG_MAX,HOME_WG_MAX_V1,HomeWidget,WG_NONE,WgDesc \
    FlexOS_Ultra_Prefs.h:HOME_EMPTY,HOME_PKG_BASE,gAppFav,gAppHidden,gAppLock,homeIsPkg,homePkgSlot,appIsFav,appIsHidden \
    FlexOS_Ultra_Home.h:HOME_LEGACY_PAGES,HOME_PAGES_MAX,HOME_COLS_MAX,HOME_ROWS_MAX,HOME_STRIDE,HOME_TOTAL,HOME_HDR_Y,HOME_HDR_H,HOME_GY0,HOME_ROWSTEP,homeOrder,gHomePage,gHomePageN,gHomeMain,gHomeCols,gHomeIconSz,homeSlotCount,homeIdx,gHomeWg,gHomeWgN,homeGrid,homeDotsY \
    FlexOS_Ultra_Widgets.h:WG_REG,wgRect,homeCellMask,homeHdrMask,homeWgFits,homeWgFreeOfWidgets,homeWgSizeOk,homeWgPlaceOk,homeWgNormalize \
    FlexOS_Ultra_Home.h:homeFirstFree,homePageAppendQuiet,homeFirstFreeGrow,gHomePkgSeen,homePkgSeen,homePkgMarkSeen,homeOrderNormalize

python3 ref/extract_arduino.py "$OUT/ref_home_load.inc" \
    FlexOS_Ultra_Types.h:HOME_WG_MAX,HOME_WG_MAX_V1,HomeWidget,WG_NONE,WgDesc \
    FlexOS_Ultra_Prefs.h:HOME_EMPTY,HOME_PKG_BASE,gAppFav,gAppHidden,gAppLock,homeIsPkg,homePkgSlot,appIsFav,appIsHidden \
    FlexOS_Ultra_Home.h:HOME_LEGACY_PAGES,HOME_LEGACY_SLOTS,HOME_LEGACY_TOTAL,HOME_PAGES_MAX,HOME_COLS_MAX,HOME_ROWS_MAX,HOME_STRIDE,HOME_TOTAL,HOME_HDR_Y,HOME_HDR_H,HOME_GY0,HOME_ROWSTEP,APPREG_VER,HOME_FACTORY,APPREG_V1_N,APPREG_MAP_V1,APPREG_V2_N,APPREG_MAP_V2,homeOrder,gHomePage,gHomePageN,gHomeMain,gHomeCols,gHomeIconSz,gHomeLabels,gHomeLocked,gHomeDots,gHomePinch,gHomeReduce,homeSlotCount,homeIdx,gHomeWg,gHomeWgN,HOME_WG_BLOB,HOME_WG_BLOB_V1,homeGrid \
    FlexOS_Ultra_Widgets.h:WG_REG,wgRect,homeCellMask,homeHdrMask,homeWgFits,homeWgFreeOfWidgets,homeWgSizeOk,homeWgPlaceOk,homeWgNormalize,homeWgFactory,homeWgSerialize,homeWgParse,homeWgDeserialize,homeWgDeserializeV1 \
    FlexOS_Ultra_AppFramework.h:drawerRegistryDefaults,drawerRegistryAdopt \
    FlexOS_Ultra_Home.h:homeFirstFree,homePageAppendQuiet,homeFirstFreeGrow,gHomePkgSeen,homePkgSeen,homePkgMarkSeen,homeOrderNormalize,homeOrderLoad

SRCS=(test_main.c test_flex_kv.c test_misc.c test_wallpaper.c test_glass.c test_home.c
      $ROOT/components/flex_storage/src/flex_kv.c
      $ROOT/components/flex_storage/src/flex_fs_path.c
      $ROOT/components/flex_ui/src/theme/flex_wallpaper.c
      $ROOT/components/flex_ui/src/theme/flex_glass_math.c
      $ROOT/components/flex_ui/src/shell/flex_home_model.c
      stub_cfg.c)
INC=(-I. -I$ROOT/components/flex_storage/include -I$ROOT/components/flex_storage/src
     -I$ROOT/components/flex_display/src -I$ROOT/components/flex_ui/src/theme
     -I$ROOT/components/flex_ui/src/shell -I$ROOT/components/flex_ui/src/widgets -I../../sim/stubs/include)
CFLAGS=(-O1 -g -Wall -Wextra -Werror -pthread)

build() {   # $1 = sufijo, resto = flags de sanitizador
    local sfx=$1; shift
    local refs=()
    for r in wall glass home home_load; do
        c++ -std=gnu++17 -O1 -g -w "$@" -Iref -I"$OUT" -c ref/ref_${r}_main.cpp -o "$OUT/ref_${r}_$sfx.o"
        refs+=("$OUT/ref_${r}_$sfx.o")
    done
    cc -std=gnu11 "${CFLAGS[@]}" "$@" "${INC[@]}" "${SRCS[@]}" "${refs[@]}" -lstdc++ -lm -o "$OUT/host_$sfx"
}
build asan -fsanitize=address,undefined -fno-sanitize-recover=all
"$OUT/host_asan"
build tsan -fsanitize=thread
"$OUT/host_tsan"

# Clave del sistema: la implementacion de ESP-IDF y la de Arduino
# (FlexOS_Ultra/FlexOS_Passcode.cpp, solo lectura) sobre la MISMA NVS en memoria
# (el doble de Preferences del arnes Arduino): lo que guarda una lo abre la otra.
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)
PINC=(-I$ROOT/components/flex_security/include -I$ROOT/components/flex_storage/include -I../../sim/stubs/include)
cc -std=gnu11 "${CFLAGS[@]}" "${SAN[@]}" -DFLEX_HOST_TEST "${PINC[@]}" \
    -c $ROOT/components/flex_security/src/flex_passcode.c -o "$OUT/flex_passcode_asan.o"
c++ -std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -DARDUINO=200 "${SAN[@]}" \
    -I$ROOT/../tests/host/inostub -I$ROOT/../FlexOS_Ultra "${PINC[@]}" \
    test_passcode.cpp $ROOT/../FlexOS_Ultra/FlexOS_Passcode.cpp "$OUT/flex_passcode_asan.o" -lcrypto \
    -o "$OUT/passcode_asan"
"$OUT/passcode_asan"
