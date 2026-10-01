#!/usr/bin/env bash
# #############################################################
#  Flex Cloud punta a punta: el gestor del P4 (FlexOS_Cloud.cpp, compilado
#  aqui) contra el servidor REAL de Flex Cloud (Node) arrancado en local.
#
#    FLEX_CLOUD_SERVER_DIR=../../../Flex-Developer-Studio/cloud ./cloud_e2e.sh
#
#  El servidor vive en el repositorio Flex-Developer-Studio: aqui solo se
#  arranca, en modo desarrollo, con datos en una carpeta temporal.
# #############################################################
set -euo pipefail
cd "$(dirname "$0")"
SERVER_DIR="${FLEX_CLOUD_SERVER_DIR:-../../../Flex-Developer-Studio/cloud}"
if [ ! -f "$SERVER_DIR/src/server.js" ]; then
  echo "cloud_e2e: no encuentro el servidor de Flex Cloud en $SERVER_DIR (define FLEX_CLOUD_SERVER_DIR)" >&2
  exit 2
fi
make -s build/cloud_e2e
DATA="$(mktemp -d)"
PORT="${FLEX_CLOUD_E2E_PORT:-$((20000 + RANDOM % 20000))}"
LOG="$DATA/server.log"
FLEX_ACCOUNT_MODE=dev FLEX_CLOUD_DEV_LOGIN=1 FLEX_CLOUD_PORT="$PORT" FLEX_CLOUD_DATA_DIR="$DATA/data" \
  node --disable-warning=ExperimentalWarning "$SERVER_DIR/src/server.js" >"$LOG" 2>&1 &
PID=$!
cleanup(){ kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -rf "$DATA"; }
trap cleanup EXIT
for _ in $(seq 1 100); do
  if curl -fsS "http://127.0.0.1:$PORT/api/cloud/health" >/dev/null 2>&1; then break; fi
  sleep 0.1
done
set +e
FLEX_CLOUD_E2E_URL="http://127.0.0.1:$PORT" ./build/cloud_e2e
RC=$?
set -e
if [ $RC -ne 0 ]; then echo "--- registro del servidor ---"; tail -40 "$LOG"; fi
exit $RC
