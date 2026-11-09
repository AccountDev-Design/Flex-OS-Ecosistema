#!/bin/bash
set -e
cd /home/user/Flex-OS-Ecosistema/FlexOS_Ultra_IDF/tests/host
ROOT=../..
OUT=/tmp/claude-0/sp/out
mkdir -p $OUT
python3 ref/extract_arduino.py $OUT/ref_ota.inc FlexOS_OTA.cpp:$(cat /tmp/claude-0/sp/ota_names.txt)
SAN="${SAN:--fsanitize=address,undefined -fno-sanitize-recover=all}"
SFX="${SFX:-q}"
for v in 0 1 2 3; do
  case $v in 0) D="";; 1) D="-DREF_SIG -DREF_ROLLBACK";; 2) D='-DREF_CA=""';; 3) D='-DREF_CA="" -DREF_INSECURE';; esac
  c++ -std=gnu++17 -O1 -g -w -ffp-contract=off $SAN -Iref -I$OUT -I$ROOT/components/flex_portable/include $D -DREFV=_v$v -c ref/ref_ota_main.cpp -o $OUT/ref_ota_v${v}_$SFX.o
done
cat > $OUT/qmain_ota.c <<'EOT'
#include "flex_test.h"
int g_fails, g_checks;
void test_ota(void);
int main(void){ test_ota(); printf("checks=%d fails=%d\n", g_checks, g_fails); return g_fails != 0; }
EOT
cc -std=gnu11 -O1 -g -ffp-contract=off -Wall -Wextra -Werror $SAN -I. -I$ROOT/components/flex_ota/include $OUT/ref_ota_v0_$SFX.o $OUT/ref_ota_v1_$SFX.o $OUT/ref_ota_v2_$SFX.o $OUT/ref_ota_v3_$SFX.o $OUT/qmain_ota.c test_ota.c $ROOT/components/flex_ota/src/flex_ota_model.c $ROOT/components/flex_ota/src/flex_ota.c -lstdc++ -lm -o $OUT/qota_$SFX
$OUT/qota_$SFX 2>&1 | head -${1:-40}
