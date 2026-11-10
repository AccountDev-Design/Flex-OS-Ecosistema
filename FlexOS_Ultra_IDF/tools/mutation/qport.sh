#!/bin/bash
# Runner de mutacion del transporte de la placa (flex_ota_port.c compilado en el host contra tests/host/ota_port_stub). Imprime OK / fallos / error de compilacion
# en el formato que espera automut.py (`checks=N fails=M`). Uso: FLEX_ROOT=<arbol> OUT=<carpeta> tools/mutation/qport.sh
ROOT=${FLEX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$ROOT/tests/host"
OUT=${OUT:-/tmp/flex_mut_out}
mkdir -p "$OUT"
SAN="${SAN:--fsanitize=address,undefined -fno-sanitize-recover=all}"
out=$(cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off $SAN -DFLEX_OTA_HAVE_CA -Iota_port_stub -I../../components/flex_ota/include -I../../components/flex_net/include \
   -I../../components/flex_system/include test_ota_port.c ../../components/flex_ota/src/flex_ota_port.c ../../components/flex_ota/src/flex_ota.c \
   ../../components/flex_ota/src/flex_ota_model.c -lm -o "$OUT/ota_port" 2>&1)
if [ $? -ne 0 ]; then echo "error de compilacion"; exit 0; fi
res=$(timeout 120 "$OUT/ota_port" 2>&1)
if echo "$res" | grep -q "^OK: transporte"; then echo "checks=1 fails=0"; elif echo "$res" | grep -q "fallos de"; then echo "checks=1 fails=1"; else echo "CRASH"; fi
