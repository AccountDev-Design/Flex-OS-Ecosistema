#!/usr/bin/env python3
"""Convierte a PNG los PPM que deja el laboratorio optico (make lab).

Sin dependencias: solo zlib y struct de la biblioteca estandar. Existe
porque un PPM no se abre en cualquier visor y el objetivo de esos cuadros
es justamente mirarlos.
"""
import os, struct, sys, zlib


def leer_ppm(ruta):
    d = open(ruta, "rb").read()
    partes = d.split(b"\n", 3)
    if partes[0] != b"P6":
        raise ValueError("%s no es un PPM binario" % ruta)
    w, h = map(int, partes[1].split())
    return w, h, partes[3]


def escribir_png(ruta, w, h, rgb):
    crudo = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def trozo(tipo, datos):
        c = tipo + datos
        return struct.pack(">I", len(datos)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    with open(ruta, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(trozo(b"IDAT", zlib.compress(crudo, 9)))
        f.write(trozo(b"IEND", b""))


def main(carpeta):
    n = 0
    for nombre in sorted(os.listdir(carpeta)):
        if not nombre.endswith(".ppm"):
            continue
        origen = os.path.join(carpeta, nombre)
        w, h, rgb = leer_ppm(origen)
        escribir_png(origen[:-4] + ".png", w, h, rgb)
        n += 1
    print("  %d cuadros convertidos a PNG" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/lab"))
