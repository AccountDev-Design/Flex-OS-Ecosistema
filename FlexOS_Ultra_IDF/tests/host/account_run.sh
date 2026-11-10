#!/bin/bash
# Flex Account en el host. Desde tests/host:   bash account_run.sh [core|svc|all]
#   core: el nucleo puro (flex_account_model.c + flex_tls.c) contra las funciones de FlexOS_Account.cpp / FlexOS_CloudTLS.cpp del Arduino (test_account.c)
#   mbed: el nucleo con el MISMO backend de la placa (mbedTLS 3.6 de ESP-IDF compilado para el PC): la verificacion ES256 y el SHA-256 que de verdad corren en el P4
#   thr:  el servicio con hilos reales (tarea de fondo + interfaz + nube) bajo ThreadSanitizer y AddressSanitizer (test_account_thr.c)
#   ui:   las decisiones de la pantalla (flex_account_ui_model.c) contra FlexOS_Account_Bridge.h de Arduino (test_account_ui.c)
#   svc:  el servicio (flex_account.c) contra FlexOS_Account.cpp entero, paso a paso sobre el mismo mundo simulado (test_account_svc.cpp)
# Entorno: ACCT_OUT (carpeta de objetos; los de la referencia se reutilizan), SAN (sanitizadores; vacio = sin ellos), FUZZ_N (guiones aleatorios), TMO (segundos).
set -e
cd "$(dirname "$0")"
ROOT=../..
ARD=../../../tests/host
ULT=../../../FlexOS_Ultra
OUT=${ACCT_OUT:-/tmp/flexos_acct_$$}
what=${1:-all}
mkdir -p "$OUT"
SAN="${SAN--fsanitize=address,undefined -fno-sanitize-recover=all}"
INC="-I$ROOT/components/flex_account/include -I$ROOT/components/flex_net/include -I$ARD/vendor/cJSON"
CXXF="-std=gnu++17 -O1 -g -w $SAN -DFLEXOS_HOST_TEST -DFLEX_HOST_TEST -DARDUINO=200 -I$ARD/netstub -I$ARD -I$ARD/vendor/cJSON -I$ULT -I../../.. -I. $INC"
CF="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off $SAN -DFLEX_HOST_TEST $INC"
# la referencia no cambia entre mutantes: se compila una vez por carpeta de objetos
cached() { local o=$1; shift; [ -f "$OUT/$o" ] || "$@" -o "$OUT/$o"; }
cached cloudtls.o c++ $CXXF -c $ULT/FlexOS_CloudTLS.cpp
cached netstub.o c++ $CXXF -c $ARD/netstub/netstub.cpp
cached cjson.o cc -std=gnu11 -O1 -g -w $SAN -c $ARD/vendor/cJSON/cJSON.c
cc $CF -c ${MODEL:-$ROOT/components/flex_account/src/flex_account_model.c} -o "$OUT/model.o"
cc $CF -c $ROOT/components/flex_net/src/flex_tls.c -o "$OUT/tls.o"
if [ "$what" = core ] || [ "$what" = all ]; then
    cached ref_core.o c++ $CXXF -c ref/ref_account_main.cpp
    cc $CF test_account.c "$OUT/model.o" "$OUT/tls.o" "$OUT/ref_core.o" "$OUT/cloudtls.o" "$OUT/netstub.o" "$OUT/cjson.o" -lstdc++ -lm -lcrypto -o "$OUT/test_account"
    timeout "${TMO:-300}" "$OUT/test_account" 2>&1 | tail -n "${TAIL:-40}"
    [ "${PIPESTATUS[0]}" = 0 ]
fi
if [ "$what" = svc ] || [ "$what" = all ]; then
    cached ref_svc.o c++ $CXXF -c ref/ref_account_svc.cpp
    c++ $CXXF -c account_ops_netstub.cpp -o "$OUT/ops.o"
    c++ $CXXF -c test_account_svc.cpp -o "$OUT/svc_test.o"
    cc $CF -c $ROOT/components/flex_account/src/flex_account.c -o "$OUT/svc.o"
    c++ $SAN "$OUT/svc_test.o" "$OUT/ops.o" "$OUT/svc.o" "$OUT/model.o" "$OUT/tls.o" "$OUT/ref_svc.o" "$OUT/cloudtls.o" "$OUT/netstub.o" "$OUT/cjson.o" -lm -lcrypto \
        -o "$OUT/test_account_svc"
    timeout "${TMO:-300}" "$OUT/test_account_svc" 2>&1 | tail -n "${TAIL:-40}"
    [ "${PIPESTATUS[0]}" = 0 ]
fi
if [ "$what" = thr ] || [ "$what" = all ]; then
    for kind in thread address,undefined; do
        T="$OUT/thr_${kind%%,*}"
        mkdir -p "$T"
        TF="-fsanitize=$kind -fno-sanitize-recover=all"
        TC="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off $TF -DFLEX_HOST_TEST $INC"
        cc $TC -c $ROOT/components/flex_account/src/flex_account.c -o "$T/svc.o"
        cc $TC -c ${MODEL:-$ROOT/components/flex_account/src/flex_account_model.c} -o "$T/model.o"
        cc $TC -c $ROOT/components/flex_net/src/flex_tls.c -o "$T/tls.o"
        cc -std=gnu11 -O1 -g -w $TF -c $ARD/vendor/cJSON/cJSON.c -o "$T/cjson.o"
        cc $TC -I. -c test_account_thr.c -o "$T/thr.o"
        cc $TC -I. -c account_fake.c -o "$T/fake.o"
        cc $TF "$T/thr.o" "$T/fake.o" "$T/svc.o" "$T/model.o" "$T/tls.o" "$T/cjson.o" -lpthread -lm -lcrypto -o "$T/test_account_thr"
        timeout "${TMO:-300}" "$T/test_account_thr" 2>&1 | tail -n "${TAIL:-40}"
        [ "${PIPESTATUS[0]}" = 0 ]
    done
fi
if [ "$what" = ui ] || [ "$what" = all ]; then
    cached ref_ui.o c++ $CXXF -c ref/ref_account_ui.cpp
    cc $CF -c $ROOT/components/flex_account/src/flex_account_ui_model.c -o "$OUT/uimodel.o"
    cc $CF -I. test_account_ui.c "$OUT/uimodel.o" "$OUT/model.o" "$OUT/tls.o" "$OUT/ref_ui.o" "$OUT/cloudtls.o" "$OUT/netstub.o" "$OUT/cjson.o" -lstdc++ -lm -lcrypto -o "$OUT/test_account_ui"
    timeout "${TMO:-300}" "$OUT/test_account_ui" 2>&1 | tail -n "${TAIL:-40}"
    [ "${PIPESTATUS[0]}" = 0 ]
    # el dorado de la escena del simulador: lo que pinta accountRender de Arduino por cada caso (sim/scenes/scene_account.c lo lee)
    GOLD="${ACCT_GOLDEN:-$OUT/account_ui_golden.txt}"
    "$OUT/test_account_ui" --golden "$GOLD" | tail -n 1
fi
if [ "$what" = mbed ] || [ "$what" = all ]; then
    MB=${MBEDTLS_SRC:-$HOME/esp/esp-idf/components/mbedtls/mbedtls}
    if [ -d "$MB/library" ]; then
        D="$OUT/mbed"
        mkdir -p "$D"
        if [ ! -f "$D/libmbedhost.a" ]; then
            ls "$MB"/library/*.c | xargs -P4 -I{} sh -c "cc -O1 -w -I$MB/include -I$MB/library -c {} -o $D/\$(basename {} .c).o"
            ar rcs "$D/libmbedhost.a" "$D"/*.o
        fi
        cached ref_core.o c++ $CXXF -c ref/ref_account_main.cpp
        cc $CF -I"$MB/include" -c ${MODEL:-$ROOT/components/flex_account/src/flex_account_model.c} -o "$D/model_mbed.o"
        cc $CF test_account.c "$D/model_mbed.o" "$OUT/tls.o" "$OUT/ref_core.o" "$OUT/cloudtls.o" "$OUT/netstub.o" "$OUT/cjson.o" "$D/libmbedhost.a" -lstdc++ -lm -lcrypto -o "$D/test_account_mbed"
        timeout "${TMO:-300}" "$D/test_account_mbed" 2>&1 | tail -n "${TAIL:-40}"
        [ "${PIPESTATUS[0]}" = 0 ]
    else
        echo "AVISO: sin las fuentes de mbedTLS de ESP-IDF ($MB): no se prueba el backend de la placa" >&2
        [ "$what" = all ] || exit 1
    fi
fi
