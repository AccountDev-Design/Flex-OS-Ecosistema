#!/usr/bin/env bash
# #############################################################
#  Flex Storage punta a punta: el P4 (FlexOS_StorageLink + FlexOS_Cloud con
#  destino telefono, compilados aqui) contra el servidor REAL de Flex Cloud
#  del telefono (CloudServer.kt, modulo :storage de Flex Phone, en la JVM).
#
#    ./phone_e2e.sh
#
#  Necesita Java 17+ y Gradle (el mismo que compila android/FlexPhone). El
#  servidor arranca ya emparejado con el id del P4 de las pruebas y con una
#  clave aleatoria, sobre una carpeta temporal, y se para al terminar.
# #############################################################
set -euo pipefail
cd "$(dirname "$0")"
PHONE_DIR="../../android/FlexPhone"
make -s build/phone_e2e
CP_LINE="$(cd "$PHONE_DIR" && gradle -q --console=plain :storage:printTestClasspath | grep '^CLASSPATH=')"
CP="${CP_LINE#CLASSPATH=}"
if [ -z "$CP" ]; then echo "phone_e2e: no se pudo obtener el classpath de :storage" >&2; exit 2; fi
DATA="$(mktemp -d)"
KEY="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
QUOTA=$((6 * 1024 * 1024))           # pequena a proposito: tambien se prueba "no cabe"
P4ID="flexos-a1b2c3d4e5f6"              # el de netstub (ESP.getEfuseMac)
FIFO="$DATA/in"
mkfifo "$FIFO"
LOG="$DATA/telefono.log"
# El servidor vive mientras su entrada estandar este abierta.
java -cp "$CP" com.flexos.flexphone.cloud.DevServerKt "$DATA/store" "$P4ID" "$KEY" "$QUOTA" <"$FIFO" >"$DATA/out" 2>"$LOG" &
PID=$!
exec 3>"$FIFO"
cleanup(){ exec 3>&- 2>/dev/null || true; kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -rf "$DATA"; }
trap cleanup EXIT
PORT=""
for _ in $(seq 1 200); do
  if grep -q '"port"' "$DATA/out" 2>/dev/null; then PORT="$(sed -n 's/.*"port":\([0-9]*\).*/\1/p' "$DATA/out" | head -1)"; break; fi
  sleep 0.1
done
if [ -z "$PORT" ]; then echo "phone_e2e: el servidor del telefono no arranco" >&2; cat "$LOG" >&2; exit 2; fi
set +e
PHONE_E2E_PORT="$PORT" PHONE_E2E_KEY="$KEY" PHONE_E2E_QUOTA="$QUOTA" ./build/phone_e2e
RC=$?
set -e
if [ $RC -ne 0 ]; then echo "--- registro del telefono ---"; tail -40 "$LOG"; fi
exit $RC
