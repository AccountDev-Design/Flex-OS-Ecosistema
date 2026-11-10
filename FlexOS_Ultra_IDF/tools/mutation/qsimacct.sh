#!/bin/bash
# Runner de mutacion de la pantalla de Flex Account: reconstruye el simulador (carpeta SIMB) y ejecuta la escena `cuenta` (que ademas compara el dibujo con el de Arduino).
# Imprime `checks=1 fails=<fallos>`; "error de compilacion" o "CRASH o bucle" cuentan como mutante muerto.
# Uso: FLEX_ROOT=<arbol> SIMB=<build> ACCT_GOLDEN=<dorado> tools/mutation/qsimacct.sh   (el dorado: tests/host/account_run.sh ui con ACCT_GOLDEN=<ruta>)
ROOT=${FLEX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
SIMB=${SIMB:-/tmp/flex_simb_acct}
SOUT=${SOUT:-/tmp/flex_simout_acct}
mkdir -p "$SOUT"
cd "$ROOT"
[ -f "$SIMB/CMakeCache.txt" ] || cmake -S sim -B "$SIMB" -DCMAKE_BUILD_TYPE=Debug > /dev/null
out=$(cmake --build "$SIMB" -j3 --target flexos_ui_sim 2>&1)
if echo "$out" | grep -q "error"; then echo "error de compilacion"; exit 0; fi
res=$(FLEX_ACCOUNT_GOLDEN=${ACCT_GOLDEN:-/tmp/flex_acct_golden.txt} timeout 150 "$SIMB/flexos_ui_sim" "$SOUT" cuenta 2>&1)
if ! echo "$res" | grep -q "escenas con fallos"; then echo "CRASH o bucle"; exit 0; fi
echo "checks=1 fails=$(echo "$res" | grep -c "FALLO\|GOLDEN")"
