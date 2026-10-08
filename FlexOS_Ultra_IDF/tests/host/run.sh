#!/usr/bin/env bash
# Pruebas de host de Flex OS Ultra (ESP-IDF): la logica portable de los
# componentes, compilada para el PC con AddressSanitizer + UBSan y, aparte,
# ThreadSanitizer para las pruebas con hilos.
#   tests/host/run.sh
set -euo pipefail
cd "$(dirname "$0")"
ROOT=../..
OUT=${OUT:-build}
mkdir -p "$OUT"
SRCS=(test_main.c test_flex_kv.c test_misc.c
      $ROOT/components/flex_storage/src/flex_kv.c
      $ROOT/components/flex_storage/src/flex_fs_path.c)
INC=(-I. -I$ROOT/components/flex_storage/include -I$ROOT/components/flex_storage/src
     -I$ROOT/components/flex_display/src)
CFLAGS=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -pthread)
cc "${CFLAGS[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all "${INC[@]}" "${SRCS[@]}" -o "$OUT/host_asan"
"$OUT/host_asan"
cc "${CFLAGS[@]}" -fsanitize=thread "${INC[@]}" "${SRCS[@]}" -o "$OUT/host_tsan"
"$OUT/host_tsan"
