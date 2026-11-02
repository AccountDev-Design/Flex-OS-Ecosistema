#!/usr/bin/env python3
"""Progreso global de la migracion, calculado desde el inventario de MIGRATION_STATUS.md.

  python3 tools/progress.py          # dos lineas: barra y desglose
  python3 tools/progress.py --table  # ademas, lo pendiente por peso

Formula (congelada, ver MIGRATION_STATUS.md): cada fila vale su `Peso`;
una fila cuenta 85 % cuando Impl = 1 (implementada, integrada y verificada en
host o simulador, con las regresiones pasando) y el 15 % restante solo cuando
Hw = 1 (probada en la placa real). Sin filas parciales: codigo a medias = 0.
"""
import re
import sys
from pathlib import Path

IMPL, HW = 0.85, 0.15
doc = Path(__file__).resolve().parent.parent / "MIGRATION_STATUS.md"
rows = []
for line in doc.read_text(encoding="utf-8").splitlines():
    c = [x.strip() for x in line.strip().strip("|").split("|")]
    if len(c) >= 5 and re.fullmatch(r"[A-Z]+\d*[a-z]?", c[0]) and c[2].isdigit() and c[3] in "01" and c[4] in "01":
        rows.append((c[0], c[1], int(c[2]), int(c[3]), int(c[4])))
total = sum(r[2] for r in rows)
if not rows or total != 2000:
    sys.exit(f"inventario invalido: {len(rows)} filas, peso total {total} (debe ser 2000)")
done_impl = sum(r[2] * IMPL for r in rows if r[3])
done_hw = sum(r[2] * HW for r in rows if r[4])
pct = (done_impl + done_hw) / total * 100
n = 18
fill = round(pct / 100 * n)
print("Flex OS Ultra · ESP-IDF")
print(f"`[{'█' * fill}{'░' * (n - fill)}] {pct:.1f} %`")
print(f"`Completado: {pct:.1f} % · Pendiente: {100 - pct:.1f} %` (placa: {done_hw / total * 100:.1f} % de {HW * 100:.0f} % posible)")
if "--table" in sys.argv:
    for r in sorted(rows, key=lambda r: -r[2]):
        if not r[3]:
            print(f"  pendiente {r[2]:4d}  {r[0]:4s} {r[1][:80]}")
