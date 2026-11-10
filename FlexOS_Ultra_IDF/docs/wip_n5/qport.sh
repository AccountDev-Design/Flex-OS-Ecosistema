#!/bin/bash
set -e
cd /home/user/Flex-OS-Ecosistema/FlexOS_Ultra_IDF/tests/host
ROOT=../..
OUT=${OUT:-/tmp/claude-0/sp/outp}
mkdir -p $OUT
SAN="${SAN:--fsanitize=address,undefined -fno-sanitize-recover=all}"
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off $SAN -DFLEX_OTA_HAVE_CA -Iota_port_stub -I$ROOT/components/flex_ota/include -I$ROOT/components/flex_net/include -I$ROOT/components/flex_system/include \
   test_ota_port.c $ROOT/components/flex_ota/src/flex_ota_port.c $ROOT/components/flex_ota/src/flex_ota.c $ROOT/components/flex_ota/src/flex_ota_model.c -lm -o $OUT/ota_port
timeout 120 $OUT/ota_port 2>&1 | head -${1:-40}
