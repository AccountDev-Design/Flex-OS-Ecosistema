#!/usr/bin/env python3
# #############################################################
#  CODIGO COMPARTIDO ENTRE PLACAS: NI UN BYTE DE DIFERENCIA
#  ------------------------------------------------------------
#  POR QUE EXISTE ESTE ARCHIVO.
#
#  Arduino IDE compila TODOS los .ino de la carpeta del sketch, y un
#  sketch no puede incluir archivos de una carpeta hermana. Con tres
#  placas (ESP32-P4, Flex OS Ultra S3 y Flex OS Pro) eso deja una sola
#  salida honesta:
#
#    · FlexOS_Ultra/  es la carpeta del sketch P4, y es la UNICA copia
#      del codigo del P4 (ver docs/REPOSITORY_CONSOLIDATION_PLAN.md);
#    · la raiz guarda FlexOS_Ultra_S3.ino, FlexOS_Pro.ino y los pocos
#      modulos que ESOS dos incluyen.
#
#  Once de esos modulos los necesitan las dos partes, asi que existen en
#  los dos sitios. No es un descuido: es lo que Arduino obliga. Lo que si
#  seria un fallo grave es que una copia cambiara y la otra no -- alguien
#  arregla un fallo del OTA para el S3 y el P4 se queda con el fallo, en
#  silencio, hasta que aparece en una placa.
#
#  Esta comprobacion convierte ese riesgo silencioso en un fallo de la
#  bateria: compara por HASH, no por fecha ni por tamano, y dice
#  exactamente que archivo se separo.
# #############################################################
import hashlib, sys
from pathlib import Path

# Los modulos que la raiz y FlexOS_Ultra/ comparten a la fuerza. Sale del
# cierre transitivo de FlexOS_Ultra_S3.ino y FlexOS_Pro.ino:
#   S3   -> OTA (+ su puente)
#   Pro  -> OTA (+ puente), FS, navegador (+ puente + app) y, por el
#           navegador, el decodificador JPEG.
COMPARTIDOS = [
    "FlexOS_OTA.h", "FlexOS_OTA.cpp", "FlexOS_OTA_Bridge.h",
    "FlexOS_FS.h", "FlexOS_FS.cpp",
    "FlexOS_Browser.h", "FlexOS_Browser.cpp", "FlexOS_Browser_Bridge.h",
    "FlexOS_BrowserApp.cpp",
    "FlexOS_JPEG.h", "FlexOS_JPEG.cpp",
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main(raiz):
    raiz = Path(raiz).resolve()
    ultra = raiz / "FlexOS_Ultra"
    fallos = []

    if not ultra.is_dir():
        print("FALLO: no existe %s, que es la carpeta canonica del sketch P4" % ultra)
        return 1

    for nombre in COMPARTIDOS:
        a, b = raiz / nombre, ultra / nombre
        if not a.exists():
            fallos.append("%s falta en la raiz: Flex OS Ultra S3 / Flex OS Pro no compilarian" % nombre)
            continue
        if not b.exists():
            fallos.append("%s falta en FlexOS_Ultra/: el sketch P4 no compilaria" % nombre)
            continue
        ha, hb = sha256(a), sha256(b)
        if ha != hb:
            fallos.append("%s SE HA SEPARADO:\n"
                          "      raiz          %s\n"
                          "      FlexOS_Ultra/ %s\n"
                          "    Copia la version buena sobre la otra antes de seguir." % (nombre, ha, hb))

    # Y al reves: la raiz no puede volver a acumular copias del codigo del P4.
    # Es justo lo que se acaba de consolidar; sin esta guarda, volveria a pasar.
    sobrantes = []
    for p in sorted(raiz.glob("FlexOS_*")):
        if not p.is_file():
            continue
        if p.name in COMPARTIDOS:
            continue
        if p.name in ("FlexOS_Pro.ino", "FlexOS_Ultra_S3.ino"):
            continue
        sobrantes.append(p.name)
    for nombre in sobrantes:
        if (ultra / nombre).exists():
            fallos.append("%s ha vuelto a aparecer en la raiz, y ya vive en FlexOS_Ultra/. "
                          "El codigo del P4 tiene UN solo sitio." % nombre)
        else:
            fallos.append("%s esta suelto en la raiz y no lo usa ni Ultra S3 ni Pro. "
                          "Si es del P4, va en FlexOS_Ultra/." % nombre)

    if fallos:
        print("Codigo compartido entre placas: %d problema(s)" % len(fallos))
        for f in fallos:
            print("  · %s" % f)
        return 1

    print("Codigo compartido: %d modulos identicos entre la raiz y FlexOS_Ultra/."
          % len(COMPARTIDOS))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "../.."))
