#!/bin/bash
# Runner de mutacion de Flex Account. Uso: FLEX_ROOT=<arbol> tools/mutation/qacct.sh [core|svc]   (por defecto core)
#   core: tests/host/test_account.c (nucleo puro + flex_tls) contra las funciones de Arduino      -> mutar flex_account_model.c / flex_tls.c
#   svc:  tests/host/test_account_svc.cpp (servicio paso a paso contra FlexOS_Account.cpp)       -> mutar flex_account.c
# Un fallo de compilacion se imprime como "error de compilacion" (INVALIDO); un fallo de la prueba, una caida del sanitizador o un cuelgue (timeout) son MUERTES.
ROOT=${FLEX_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}
cd "$ROOT/tests/host" || exit 1
what=${1:-core}
out=$(ACCT_OUT=${ACCT_OUT:-/tmp/flex_mut_acct_$what} TMO=${TMO:-150} TAIL=400 FUZZ_N=${FUZZ_N:-250} bash account_run.sh "$what" 2>&1)
rc=$?
if echo "$out" | grep -qE '^[^ ]+:[0-9]+:[0-9]+: (fatal )?error:'; then echo "error de compilacion"; exit 0; fi
if [ $rc -eq 0 ] && echo "$out" | grep -q '^OK: Flex Account'; then echo "checks=1 fails=0"; else echo "checks=1 fails=1"; fi
