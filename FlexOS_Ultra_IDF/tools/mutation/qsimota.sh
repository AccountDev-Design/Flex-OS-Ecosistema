#!/bin/bash
# Runner de mutacion de la interfaz OTA y su pegamento: reconstruye el simulador (carpeta SIMB) y ejecuta la escena `ota`. Imprime `checks=1 fails=<fallos>`;
# "error de compilacion" o "CRASH o bucle" cuentan como mutante muerto. Uso: FLEX_ROOT=<arbol> SIMB=<carpeta de build> tools/mutation/qsimota.sh
ROOT=${FLEX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
SIMB=${SIMB:-/tmp/flex_simb}
SOUT=${SOUT:-/tmp/flex_simout}
mkdir -p "$SOUT"
cd "$ROOT"
[ -f "$SIMB/CMakeCache.txt" ] || cmake -S sim -B "$SIMB" -DCMAKE_BUILD_TYPE=Debug > /dev/null
out=$(cmake --build "$SIMB" -j3 --target flexos_ui_sim 2>&1)
if echo "$out" | grep -q "error"; then echo "error de compilacion"; exit 0; fi
res=$(timeout 120 "$SIMB/flexos_ui_sim" "$SOUT" ota 2>&1)
if ! echo "$res" | grep -q "escenas con fallos"; then echo "CRASH o bucle"; exit 0; fi
echo "checks=1 fails=$(echo "$res" | grep -c "FALLO linea")"
