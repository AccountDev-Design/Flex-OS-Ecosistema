#!/usr/bin/env bash
# Prueba de compatibilidad del LittleFS de la version Arduino con el firmware
# ESP-IDF (ver lfs_compat.c). Descarga littlefs v2.8.0 (el minimo que puede
# llevar la version Arduino: arduino-esp32 3.2.x pide joltwallet/littlefs
# ^1.10.2) y usa el littlefs de managed_components (el del firmware).
#   tests/littlefs_compat/run.sh
set -euo pipefail
cd "$(dirname "$0")"
OUT=${OUT:-build}
mkdir -p "$OUT"
NEW=../../managed_components/joltwallet__littlefs/src/littlefs
[ -f "$NEW/lfs.c" ] || { echo "falta $NEW: compila antes el firmware (tools/build.sh)" >&2; exit 2; }
OLD="$OUT/littlefs-v2.8.0"
[ -f "$OLD/lfs.c" ] || git clone -q --depth 1 -b v2.8.0 https://github.com/littlefs-project/littlefs.git "$OLD"
CF=(-std=gnu11 -O1 -g -Wall -fsanitize=address,undefined -DLFS_NO_DEBUG -DLFS_NO_WARN)
cc "${CF[@]}" -I"$OLD" lfs_compat.c "$OLD/lfs.c" "$OLD/lfs_util.c" -o "$OUT/lfs_old"
cc "${CF[@]}" -I"$NEW" lfs_compat.c "$NEW/lfs.c" "$NEW/lfs_util.c" -o "$OUT/lfs_new"
IMG="$OUT/spiffs.img"
rm -f "$IMG"
echo "== 1. La version Arduino formatea y guarda sus archivos"
"$OUT/lfs_old" "$IMG" fill
"$OUT/lfs_old" "$IMG" info
echo "== 2. El firmware ESP-IDF monta y lee sin cambiar nada"
"$OUT/lfs_new" "$IMG" check
"$OUT/lfs_new" "$IMG" info
echo "== 3. El firmware ESP-IDF escribe (temporal + renombrar), crea y borra"
"$OUT/lfs_new" "$IMG" modify
"$OUT/lfs_new" "$IMG" info
echo "== 4. Vuelta a la version Arduino: lo ve todo"
"$OUT/lfs_old" "$IMG" check modified
echo "== 5. Imagen formateada por el firmware ESP-IDF, leida por la version Arduino"
rm -f "$IMG"
"$OUT/lfs_new" "$IMG" fill
"$OUT/lfs_old" "$IMG" check
echo "OK: compatibilidad de LittleFS comprobada (v2.8.0 <-> $(grep -m1 'define LFS_VERSION ' $NEW/lfs.h | awk '{print $3}'))"
