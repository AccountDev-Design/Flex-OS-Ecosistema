#!/usr/bin/env bash
# #############################################################
#  Flex Storage punta a punta: el P4 (FlexOS_StorageLink + FlexOS_Cloud con
#  destino telefono, compilados aqui) contra el servidor REAL de Flex Cloud
#  del telefono (CloudServer.kt, modulo :storage de Flex Phone, en la JVM).
#
#    ./phone_e2e.sh
#
#  Necesita Java 17+, Gradle (el mismo que compila android/FlexPhone) y Node.
#  El servidor arranca ya emparejado con el id del P4 de las pruebas y con una
#  clave aleatoria, sobre una carpeta temporal, y se para al terminar. Despues,
#  pair_e2e.js empareja la app de verdad contra el servidor web del P4.
# #############################################################
set -euo pipefail
cd "$(dirname "$0")"
PHONE_DIR="../../android/FlexPhone"
make -s build/phone_e2e
CP_LINE="$(cd "$PHONE_DIR" && gradle -q --console=plain :storage:printTestClasspath | grep '^CLASSPATH=')"
CP="${CP_LINE#CLASSPATH=}"
if [ -z "$CP" ]; then echo "phone_e2e: no se pudo obtener el classpath de :storage" >&2; exit 2; fi
DATA="$(mktemp -d)"
# Lo multimedia que el telefono prepara: se generan los archivos y el servidor arranca con la cola multimedia.
MEDIA="$DATA/media"; mkdir -p "$MEDIA"
java -Djava.awt.headless=true -cp "$CP" com.flexos.flexphone.cloud.E2eMediaFixturesCliKt "$MEDIA" 2>/dev/null
KEY="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
QUOTA=$((6 * 1024 * 1024))           # pequena a proposito: tambien se prueba "no cabe"
P4ID="flexos-a1b2c3d4e5f6"              # el de netstub (ESP.getEfuseMac)
FIFO="$DATA/in"
mkfifo "$FIFO"
LOG="$DATA/telefono.log"
# El servidor vive mientras su entrada estandar este abierta.
FLEX_DEV_MEDIA=1 java -Djava.awt.headless=true -cp "$CP" com.flexos.flexphone.cloud.DevServerKt "$DATA/store" "$P4ID" "$KEY" "$QUOTA" <"$FIFO" >"$DATA/out" 2>"$LOG" &
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
PHONE_E2E_PORT="$PORT" PHONE_E2E_KEY="$KEY" PHONE_E2E_QUOTA="$QUOTA" PHONE_E2E_MEDIA_DIR="$MEDIA" ./build/phone_e2e
RC=$?
set -e
if [ $RC -ne 0 ]; then echo "--- registro del telefono ---"; tail -40 "$LOG"; fi
# Lo que el P4 recibio del telefono lo juzga el FIRMWARE: demux AVI, decodificador JPEG y parser WAV de verdad.
if [ $RC -eq 0 ] && [ -d "$MEDIA/out" ]; then
  make -s build/mediacheck
  echo "=== lo que el P4 recibio del telefono, abierto por el firmware (mediacheck) ==="
  RCM=0
  for f in h264.avi native.avi; do
    R="$(./build/mediacheck avi "$MEDIA/out/$f")"; echo "   $f: $R"
    echo "$R" | python3 -c 'import sys,json; r=json.loads(sys.stdin.read()); sys.exit(0 if r.get("ok") and r["bad"]==0 and r["decoded"]==r["frames"] and r["frames"]>0 else 1)' || RCM=1
  done
  for f in png.jpg prog.jpg; do
    R="$(./build/mediacheck thumb "$MEDIA/out/$f")"; echo "   $f: $R"
    echo "$R" | python3 -c 'import sys,json; sys.exit(0 if json.loads(sys.stdin.read()).get("ok") else 1)' || RCM=1
  done
  for f in wav24.wav aac.wav; do
    R="$(./build/mediacheck wav "$MEDIA/out/$f" "$MEDIA/out/$f.raw")"; echo "   $f: $R"
    echo "$R" | python3 -c 'import sys,json; r=json.loads(sys.stdin.read()); sys.exit(0 if r.get("ok") and r["samples"]>1000 else 1)' || RCM=1
  done
  [ $RCM -ne 0 ] && { echo "FALLO: el firmware no abre lo que el telefono preparo"; RC=1; }
fi
# El emparejamiento: la app (AttachClient.kt) contra el servidor web del P4
# (flexweb_host), con la aprobacion "en pantalla" por stdin.
make -s build/flexweb_host
set +e
node pair_e2e.js
RC2=$?
set -e
[ $RC -ne 0 ] && exit $RC
exit $RC2
