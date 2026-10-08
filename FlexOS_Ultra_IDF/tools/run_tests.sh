#!/usr/bin/env bash
# Todas las comprobaciones que se pueden hacer sin la placa:
#   1. pruebas de host de la logica portable (ASan/UBSan + TSan)
#   2. compatibilidad del LittleFS de la version Arduino
#   3. simulador de la interfaz 480x800 (doble framebuffer y cache)
#   4. reglas de arquitectura (LVGL solo, modulos portables identicos)
#   tools/run_tests.sh
set -euo pipefail
cd "$(dirname "$0")/.."
echo "### 1. Pruebas de host"
tests/host/run.sh
echo "### 2. LittleFS Arduino <-> ESP-IDF"
LOG="$(mktemp)"
if ! tests/littlefs_compat/run.sh >"$LOG" 2>&1; then
    cat "$LOG"
    exit 1
fi
grep -E "^(==|OK)|FALLO|NO " "$LOG"
rm -f "$LOG"
echo "### 3. Simulador de la interfaz"
cmake -S sim -B sim/build -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build sim/build -j >/dev/null
sim/build/flexos_sim sim/out | tail -4
echo "### 4. Reglas"
python3 tools/check_lvgl_only.py
python3 tools/check_portable.py
echo "TODO OK"
