#!/usr/bin/env bash
# Compila Flex OS Ultra (ESP-IDF) para una revision del ESP32-P4 y comprueba
# el resultado: sdkconfig, regla "toda la UI con LVGL" y tamanos.
#
#   tools/build.sh                    # rev < v3 (lt_v3), desarrollo
#   tools/build.sh --rev v3           # rev v3.x
#   tools/build.sh --release          # logs WARN, asserts silenciosos
#   tools/build.sh --clean            # borra antes la carpeta de la variante
#
# Cada variante usa su carpeta (build_<rev>[_release]) y su propio sdkconfig:
# un sdkconfig existente manda sobre los defaults, asi que mezclar variantes en
# una sola carpeta acabaria compilando para la revision equivocada.
set -euo pipefail
cd "$(dirname "$0")/.."

REV=lt_v3
FLAVOR=dev
CLEAN=0
while [ $# -gt 0 ]; do
    case "$1" in
        --rev) REV="$2"; shift 2 ;;
        --release) FLAVOR=release; shift ;;
        --clean) CLEAN=1; shift ;;
        -h|--help) sed -n '2,13p' "$0"; exit 0 ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done
case "$REV" in lt_v3|v3) ;; *) echo "--rev debe ser lt_v3 o v3" >&2; exit 2 ;; esac

if ! command -v idf.py >/dev/null 2>&1; then
    echo "No encuentro idf.py: carga antes ESP-IDF 5.5.5 (. \$IDF_PATH/export.sh)" >&2
    exit 2
fi
IDF_VER="$(idf.py --version 2>/dev/null | tail -1)"
case "$IDF_VER" in
    *v5.5.5*) ;;
    *) echo "AVISO: el proyecto esta fijado a ESP-IDF v5.5.5 y este entorno es '$IDF_VER'" >&2 ;;
esac

DIR="build_${REV}"
FRAGS=(sdkconfig.defaults "sdkconfig.defaults.rev_${REV}")
if [ "$FLAVOR" = release ]; then
    DIR="${DIR}_release"
    FRAGS+=(sdkconfig.defaults.release)
fi
[ "$CLEAN" = 1 ] && rm -rf "$DIR"
DEFAULTS="$(IFS=';'; echo "${FRAGS[*]}")"

idf.py -B "$DIR" \
    -D IDF_TARGET=esp32p4 \
    -D SDKCONFIG="$DIR/sdkconfig" \
    -D SDKCONFIG_DEFAULTS="$DEFAULTS" \
    -D FLEX_P4_REV="$REV" \
    -D FLEX_BUILD="$FLAVOR" \
    build

python3 tools/check_sdkconfig.py "$DIR/sdkconfig" "${FRAGS[@]}"
python3 tools/check_lvgl_only.py
python3 tools/size_report.py "$DIR"
