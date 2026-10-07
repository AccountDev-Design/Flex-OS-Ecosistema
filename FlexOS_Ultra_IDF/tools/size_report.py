#!/usr/bin/env python3
"""Tamano REAL del firmware frente a las dos tablas de particiones candidatas.

No elige la tabla: la decision A/B se toma con el tamano medido al final de
las Fases 1-3 (docs/PARTICIONES.md). Este informe da los datos.

Uso: python3 tools/size_report.py <carpeta de build> [--json salida.json]
"""
import csv
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CANDIDATES = {"A": ROOT / "partitions/partitions_a.csv", "B": ROOT / "partitions/partitions_b.csv"}
BOOTLOADER_OFFSET = 0x2000   # ESP32-P4


def app_slot_size(csv_path):
    sizes = []
    with open(csv_path, encoding="utf-8") as f:
        rows = (r for r in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#")))
        for r in rows:
            r = [c.strip() for c in r]
            if len(r) >= 5 and r[1] == "app":
                sizes.append(int(r[4], 0))
    return min(sizes)


def sdkconfig_value(build, key, default=None):
    path = build / "config" / "sdkconfig.json"
    if not path.exists():
        return default
    return json.loads(path.read_text()).get(key, default)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    build = pathlib.Path(sys.argv[1])
    desc = json.loads((build / "project_description.json").read_text())
    app_bin = build / desc["app_bin"]
    boot_bin = build / "bootloader" / "bootloader.bin"
    app = app_bin.stat().st_size
    boot = boot_bin.stat().st_size
    pt_offset = int(str(sdkconfig_value(build, "PARTITION_TABLE_OFFSET", 0x8000)), 0)
    boot_room = pt_offset - BOOTLOADER_OFFSET

    report = {
        "build": str(build),
        "app_bytes": app,
        "bootloader_bytes": boot,
        "bootloader_limit_bytes": boot_room,
        "bootloader_free_bytes": boot_room - boot,
        "partition_table_in_use": sdkconfig_value(build, "PARTITION_TABLE_CUSTOM_FILENAME"),
        "candidates": {},
    }
    print(f"Firmware ({app_bin.name}): {app:,} B ({app / 1048576:.2f} MiB)")
    print(f"Bootloader: {boot:,} B de {boot_room:,} B disponibles ({boot_room - boot:,} B libres)")
    print(f"Tabla en uso (PROVISIONAL): {report['partition_table_in_use']}")
    for name, path in CANDIDATES.items():
        slot = app_slot_size(path)
        free = slot - app
        report["candidates"][name] = {"slot_bytes": slot, "free_bytes": free, "used_pct": round(100 * app / slot, 1)}
        print(f"  Candidata {name}: ranura OTA {slot:,} B -> ocupa {100 * app / slot:.1f} %, libres {free:,} B")
    print("Nota: es el tamano de ESTA fase. La tabla definitiva se elige con el tamano de las Fases 1-3.")

    if len(sys.argv) >= 4 and sys.argv[2] == "--json":
        pathlib.Path(sys.argv[3]).write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
