#!/usr/bin/env bash
# #############################################################
#  El enlace de Flex Phone de punta a punta, en el PC:
#
#      P4   fpwTask REAL + maquina de estados REAL   (C++, hilos y sockets)
#       |   TCP 127.0.0.1:47820
#      A55  WifiLinkServer REAL                       (Kotlin, JVM)
#
#      ./link_e2e.sh            version completa (~2 min)
#      E2E_QUICK=1 ./link_e2e.sh   plazos recortados (~45 s)
#      E2E_VERBOSE=1 ./link_e2e.sh con la secuencia de estados del P4
#
#  Necesita g++, un JDK y el compilador de Kotlin (kotlinc, o el que Gradle ya
#  bajo). Si no hay Kotlin lo DICE y sale con 0: no finge que paso.
#  No sustituye a probar con el P4 y el A55 de verdad: ver
#  docs/DIAGNOSTICO-ENLACE.md ("Lo que esto NO demuestra").
# #############################################################
set -u
cd "$(dirname "$0")"
HERE="$PWD"
ROOT="$HERE/../.."
ULTRA="$ROOT/FlexOS_Ultra"
APP="$ROOT/android/FlexPhone/app/src/main/kotlin/com/flexos/flexphone/link/WifiLinkServer.kt"
PROTO="$ROOT/android/FlexPhone/protocol/src/main/kotlin"
STUBS="$ROOT/tests/link/stub"
DEV="$ROOT/tests/link/LinkDevServer.kt"
BUILD="$HERE/build/link_e2e"

command -v java >/dev/null 2>&1 || { echo "enlace de punta a punta: no hay JDK; se omite."; exit 0; }
KOTLINC="$(command -v kotlinc 2>/dev/null || true)"
STDLIB="$(find "$HOME/.gradle" /root/.gradle -name 'kotlin-stdlib-2.0.21.jar' 2>/dev/null | head -1)"
CP_JARS=""
if [ -z "$KOTLINC" ]; then
  for n in kotlin-compiler-embeddable-2.0.21.jar kotlin-stdlib-2.0.21.jar kotlin-script-runtime-2.0.21.jar \
           kotlin-reflect-2.0.21.jar annotations-13.0.jar trove4j-1.0.20200330.jar; do
    f="$(find "$HOME/.gradle" /root/.gradle -name "$n" 2>/dev/null | head -1)"; [ -n "$f" ] && CP_JARS="$CP_JARS:$f"
  done
  f="$(find "$HOME/.gradle" /root/.gradle -name 'kotlinx-coroutines-core-jvm-*.jar' 2>/dev/null | head -1)"; [ -n "$f" ] && CP_JARS="$CP_JARS:$f"
  [ -z "$CP_JARS" ] && { echo "enlace de punta a punta: no hay compilador de Kotlin; se omite."; exit 0; }
elif [ -z "$STDLIB" ]; then
  KHOME="$(cd "$(dirname "$(readlink -f "$KOTLINC")")/.." && pwd)"; STDLIB="$KHOME/lib/kotlin-stdlib.jar"
fi

rm -rf "$BUILD"; mkdir -p "$BUILD/kt"
echo "compilando el telefono (WifiLinkServer REAL)..."
if [ -n "$KOTLINC" ]; then
  "$KOTLINC" -nowarn -d "$BUILD/kt" "$APP" "$PROTO" "$STUBS" "$DEV" 2>&1 | grep -v '^warning:' | grep -v '^Picked up'
else
  java -cp "${CP_JARS#:}" org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -nowarn -classpath "$STDLIB" -d "$BUILD/kt" \
       "$APP" "$PROTO" "$STUBS" "$DEV" 2>&1 | grep -v '^warning:' | grep -v '^Picked up'
fi
[ -f "$BUILD/kt/LinkDevServerKt.class" ] || { echo "no compilo el telefono"; exit 2; }

echo "compilando el P4 (fpwTask REAL + maquina de estados REAL)..."
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -Ilinkstub -I"$ULTRA" -o "$BUILD/p4" test_flexphone_wifi_e2e.cpp \
    "$ULTRA/FlexOS_FlexPhone_Link.cpp" "$ULTRA/FlexOS_FlexPhone.cpp" "$ULTRA/FlexOS_FlexLink.cpp" \
    "$ULTRA/FlexOS_FlexAuth.cpp" "$ULTRA/FlexOS_FlexPhone_Transport.cpp" || exit 2

KEY="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
FIFO="$BUILD/ctrl"; mkfifo "$FIFO"
LOG="$BUILD/telefono.log"
java -cp "$BUILD/kt:${STDLIB}" LinkDevServerKt "$KEY" phone-prueba <"$FIFO" >"$LOG" 2>"$BUILD/telefono.err" &
PID=$!
exec 3>"$FIFO"                       # el telefono vive mientras este abierto
cleanup(){ exec 3>&- 2>/dev/null; kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; }
trap cleanup EXIT
for _ in $(seq 1 100); do grep -q "READY listening" "$LOG" 2>/dev/null && break; sleep 0.1; done
grep -q "READY listening" "$LOG" || { echo "el telefono no arranco"; cat "$BUILD/telefono.err"; exit 2; }

# Las ordenes de control del telefono pasan por un fichero que este script
# reenvia a su entrada: el programa del P4 no puede abrir el FIFO dos veces.
CTRLF="$BUILD/ctrl.cmd"; : > "$CTRLF"
( tail -n0 -f "$CTRLF" >&3 ) & TAILPID=$!
trap 'kill $TAILPID 2>/dev/null; cleanup' EXIT

ASAN_OPTIONS=detect_leaks=0 E2E_KEY="$KEY" E2E_PORT=47820 E2E_CTRL="$CTRLF" "$BUILD/p4"
RC=$?

# ---- Lo que el telefono vio ----
echo "--- lo que vio el telefono ---"
grep -E "SessionOpen|SessionClosed|DUMP|Error|PairingFailed" "$LOG" | tail -n 30
OPENED=$(grep -c "EVENT SessionOpen" "$LOG"); CLOSED=$(grep -c "EVENT SessionClosed" "$LOG")
echo "sesiones abiertas=$OPENED cerradas=$CLOSED"
FIRST_DUMP=$(grep "DUMP" "$LOG" | head -1)
RS=$(echo "$FIRST_DUMP" | sed -n 's/.*t60=\([0-9]*\).*/\1/p'); FS=$(echo "$FIRST_DUMP" | sed -n 's/.*t42=\([0-9]*\).*/\1/p')
FAIL=0
if [ "${RS:-0}" != "1" ]; then echo "  FALLO  el telefono recibio RELAY_START ${RS:-0} veces (se mando 1)"; FAIL=1; fi
if [ "${FS:-0}" != "1" ]; then echo "  FALLO  el telefono recibio FIND_START ${FS:-0} veces (se mando 1)"; FAIL=1; fi
[ $RC -ne 0 ] && FAIL=1
exit $FAIL
