#!/usr/bin/env bash
# #############################################################
#  Perfil multimedia de Flex OS Ultra, juzgado por el FIRMWARE.
#
#    ./media_profile_e2e.sh
#
#  El codigo Kotlin del telefono (modulo :storage: analizador, escritor AVI MJPEG, WAV IMA, remuxes) genera
#  archivos y da su veredicto; despues el demux AVI, el decodificador JPEG y el parser/decodificador WAV del P4
#  (build/mediacheck, compilados aqui) los abren de verdad. Si Kotlin dice "esto ya vale" y el firmware no lo
#  abre -- o al reves en los fallos de decodificacion -- la prueba falla.
#  Necesita Java 17+, Gradle (el de android/FlexPhone) y python3.
# #############################################################
set -euo pipefail
cd "$(dirname "$0")"
PHONE_DIR="../../android/FlexPhone"
make -s build/mediacheck
CP_LINE="$(cd "$PHONE_DIR" && gradle -q --console=plain --offline :storage:printTestClasspath | grep '^CLASSPATH=')"
CP="${CP_LINE#CLASSPATH=}"
if [ -z "$CP" ]; then echo "media_profile_e2e: no se pudo obtener el classpath de :storage" >&2; exit 2; fi
DATA="$(mktemp -d)"
trap 'rm -rf "$DATA"' EXIT
echo "=== Perfil multimedia: Kotlin (analizador, escritores, remuxes) <-> firmware (mediacheck) ==="
java -Djava.awt.headless=true -cp "$CP" com.flexos.flexphone.cloud.media.MediaFixturesCliKt "$DATA" 2>/dev/null | grep '^{' > "$DATA/items.json"
python3 media_profile_check.py ./build/mediacheck "$DATA" "$DATA/items.json"
