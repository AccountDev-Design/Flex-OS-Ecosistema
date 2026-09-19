#!/bin/bash
# #############################################################
#  Estabilidad de la sesion de Flex Phone
#  ------------------------------------------------------------
#  Compila y ejecuta EN EL PC el WifiLinkServer REAL -- el mismo
#  fichero que va en el APK -- contra sockets TCP de verdad, con
#  tres dobles minimos del SDK de Android (ver stub/).
#
#  Existe porque el fallo que cubre es una CARRERA entre hilos y
#  sockets: reproducirla a mano en el telefono depende de la
#  suerte, y aqui se provoca a proposito.
#
#      ./run.sh                  todas las comprobaciones
#      FLEX_LINK_VERBOSE=1 ./run.sh   con las lineas del enlace
#
#  Necesita un JDK y el compilador de Kotlin. Si no hay ninguno de
#  los dos, la bateria lo DICE y sale con 0: no se finge que paso.
# #############################################################
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
APP="$ROOT/android/FlexPhone/app/src/main/kotlin/com/flexos/flexphone/link/WifiLinkServer.kt"
PROTO="$ROOT/android/FlexPhone/protocol/src/main/kotlin"
BUILD="$HERE/build"

if ! command -v java >/dev/null 2>&1; then
  echo "estabilidad del enlace: no hay JDK; se omite."
  exit 0
fi

# El compilador de Kotlin: el de la caja, o el que Gradle ya bajo.
KOTLINC="$(command -v kotlinc 2>/dev/null || true)"
JARS=""
if [ -z "$KOTLINC" ]; then
  # El compilador embebido arrastra intellij-core, que a su vez pide
  # trove4j y las corrutinas EN SU PROPIO classpath (no en el del
  # codigo que compila). Sin ellos ni siquiera arranca.
  for n in kotlin-compiler-embeddable-2.0.21.jar kotlin-stdlib-2.0.21.jar \
           kotlin-script-runtime-2.0.21.jar kotlin-reflect-2.0.21.jar \
           annotations-13.0.jar trove4j-1.0.20200330.jar; do
    f="$(find "$HOME/.gradle" /root/.gradle -name "$n" 2>/dev/null | head -1)"
    [ -n "$f" ] && JARS="$JARS:$f"
  done
  f="$(find "$HOME/.gradle" /root/.gradle -name 'kotlinx-coroutines-core-jvm-*.jar' 2>/dev/null | head -1)"
  [ -n "$f" ] && JARS="$JARS:$f"
  if [ -z "$JARS" ]; then
    echo "estabilidad del enlace: no hay compilador de Kotlin; se omite."
    echo "   (ejecuta antes 'gradle :protocol:test' en android/FlexPhone para que se descargue)"
    exit 0
  fi
fi

STDLIB="$(find "$HOME/.gradle" /root/.gradle -name 'kotlin-stdlib-2.0.21.jar' 2>/dev/null | head -1)"
rm -rf "$BUILD"; mkdir -p "$BUILD"

echo "compilando el servidor REAL del enlace..."
if [ -n "$KOTLINC" ]; then
  "$KOTLINC" -nowarn -d "$BUILD" "$APP" "$PROTO" "$HERE/stub" "$HERE/LinkStabilityTest.kt" || exit 1
else
  java -cp "${JARS#:}" org.jetbrains.kotlin.cli.jvm.K2JVMCompiler -nowarn \
       -classpath "$STDLIB" -d "$BUILD" \
       "$APP" "$PROTO" "$HERE/stub" "$HERE/LinkStabilityTest.kt" 2>&1 \
     | grep -v '^warning:' | grep -v '^Picked up'
  [ -d "$BUILD" ] || exit 1
fi

java -cp "$BUILD:$STDLIB" LinkStabilityTestKt
