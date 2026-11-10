#!/bin/bash
# Runner de mutacion del motor y el modelo de la OTA: compila tests/host/test_ota.c con los dos fuentes y las cuatro copias de Arduino (ASan/UBSan; SAN/SFX
# eligen otro sanitizador). Imprime `checks=N fails=M`. Uso: FLEX_ROOT=<arbol> OUT=<carpeta> tools/mutation/qota.sh [lineas]
set -e
ROOT=${FLEX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$ROOT/tests/host"
OUT=${OUT:-/tmp/flex_mut_out}
mkdir -p "$OUT"
python3 ref/extract_arduino.py "$OUT/ref_ota.inc" FlexOS_OTA.cpp:$(cat "$ROOT/tools/mutation/ota_names.txt")
SAN="${SAN:--fsanitize=address,undefined -fno-sanitize-recover=all}"
SFX="${SFX:-q}"
for v in 0 1 2 3; do
  case $v in 0) D="";; 1) D="-DREF_SIG -DREF_ROLLBACK";; 2) D='-DREF_CA=""';; 3) D='-DREF_CA="" -DREF_INSECURE';; esac
  c++ -std=gnu++17 -O1 -g -w -ffp-contract=off $SAN -Iref -I"$OUT" -I../../components/flex_portable/include $D -DREFV=_v$v -c ref/ref_ota_main.cpp -o "$OUT/ref_ota_v${v}_$SFX.o"
done
cat > "$OUT/qmain_ota.c" <<'EOT'
#include "flex_test.h"
int g_fails, g_checks;
void test_ota(void);
int main(void){ test_ota(); printf("checks=%d fails=%d\n", g_checks, g_fails); return g_fails != 0; }
EOT
cc -std=gnu11 -O1 -g -ffp-contract=off -Wall -Wextra -Werror $SAN -I. -I../../components/flex_ota/include "$OUT"/ref_ota_v[0-3]_$SFX.o "$OUT/qmain_ota.c" test_ota.c \
   ../../components/flex_ota/src/flex_ota_model.c ../../components/flex_ota/src/flex_ota.c -lstdc++ -lm -o "$OUT/qota_$SFX"
timeout 120 "$OUT/qota_$SFX" 2>&1 | head -${1:-40}
