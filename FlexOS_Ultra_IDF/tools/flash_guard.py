#!/usr/bin/env python3
"""Guardia de grabacion de Flex OS Ultra (ESP-IDF). Usar SIEMPRE en lugar de
`idf.py flash` mientras la revision del chip y la tabla de particiones no esten
cerradas.

  1. Lee la revision REAL del ESP32-P4 conectado (esptool chip-id, solo lectura).
  2. La compara con la ventana de revisiones para la que se compilo el build.
  3. Explica que va a escribir y que NO toca (NVS y LittleFS con la tabla A).
  4. Solo graba si todo cuadra y se acepta la tabla provisional de forma
     explicita. Nunca usa --force.

Uso:
  python3 tools/flash_guard.py --port /dev/ttyUSB0 --solo-comprobar
  python3 tools/flash_guard.py --port /dev/ttyUSB0 --build build_lt_v3 --acepto-tabla-provisional
"""
import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
REV_RE = re.compile(r"revision v(\d+)\.(\d+)")
ARDUINO_DATA = {"nvs": (0x9000, 0x5000), "spiffs": (0x510000, 0xAE0000)}


def esptool(*args):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32p4", *args]
    return subprocess.run(cmd, capture_output=True, text=True)


def read_chip_rev(port):
    res = esptool("-p", port, "chip-id")
    out = res.stdout + res.stderr
    m = REV_RE.search(out)
    if res.returncode != 0 or not m:
        print(out)
        raise SystemExit("No se pudo leer la revision del chip (revisa el puerto y el modo de descarga).")
    return int(m.group(1)) * 100 + int(m.group(2)), out


def fmt(rev):
    return f"v{rev // 100}.{rev % 100}"


def parse_table(path):
    parts = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        c = [x.strip() for x in line.split(",")]
        if len(c) >= 5:
            parts[c[0]] = (int(c[3], 0), int(c[4], 0))
    return parts


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--build", default="build_lt_v3")
    ap.add_argument("--solo-comprobar", action="store_true", help="solo leer y mostrar la revision del chip")
    ap.add_argument("--acepto-tabla-provisional", action="store_true",
                    help="grabar con la tabla de particiones provisional del build")
    ap.add_argument("--acepto-perder-littlefs", action="store_true",
                    help="necesario si la tabla mueve LittleFS respecto a la version Arduino")
    args = ap.parse_args()

    rev, raw = read_chip_rev(args.port)
    print(f"Chip conectado: ESP32-P4 revision {fmt(rev)}")
    if args.solo_comprobar:
        frag = "rev_v3" if rev >= 300 else "rev_lt_v3"
        print(f"Variante de build que corresponde: --rev {frag[4:]} (sdkconfig.defaults.{frag})")
        if rev < 1:
            print("Revision v0.0: cambia CONFIG_ESP32P4_REV_MIN_1 por CONFIG_ESP32P4_REV_MIN_0 en sdkconfig.defaults.rev_lt_v3.")
        return 0

    build = ROOT / args.build
    cfg = json.loads((build / "config" / "sdkconfig.json").read_text())
    rmin, rmax = int(cfg["ESP_REV_MIN_FULL"]), int(cfg["ESP_REV_MAX_FULL"])
    print(f"Build {args.build}: compilado para {fmt(rmin)} .. {fmt(rmax)}")
    if not (rmin <= rev <= rmax):
        other = "--rev v3" if rev >= 300 else "--rev lt_v3"
        raise SystemExit(f"NO SE GRABA: el chip es {fmt(rev)} y el binario no arranca en el. "
                         f"Compila con tools/build.sh {other}.")

    table_rel = cfg["PARTITION_TABLE_CUSTOM_FILENAME"]
    table = parse_table(ROOT / table_rel)
    print(f"Tabla de particiones del build: {table_rel} (PROVISIONAL)")
    for name, (off, size) in table.items():
        print(f"  {name:<9} 0x{off:06X}  {size:>9,} B")
    moved = [n for n, area in ARDUINO_DATA.items() if table.get(n) != area]
    if moved:
        print(f"ATENCION: {', '.join(moved)} cambia de sitio respecto a la version Arduino: sus datos se pierden.")
        if not args.acepto_perder_littlefs:
            raise SystemExit("NO SE GRABA: haz copia de los datos y repite con --acepto-perder-littlefs.")
    else:
        print("NVS y LittleFS quedan en el mismo sitio que en Arduino: no se escriben ni se borran.")
    if not args.acepto_tabla_provisional:
        raise SystemExit("NO SE GRABA: la tabla A/B aun no es definitiva. Repite con --acepto-tabla-provisional "
                         "si quieres probar esta fase en la placa.")

    flash_args = build / "flash_args"
    print("Se escribe (y nada mas):")
    print("  " + " ".join(flash_args.read_text().split()))
    res = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", args.port,
                          "-b", "460800", "--before", "default-reset", "--after", "hard-reset",
                          "write-flash", "@flash_args"], cwd=build)
    return res.returncode


if __name__ == "__main__":
    sys.exit(main())
