#!/usr/bin/env python3
"""Comprueba que cada modulo de components/flex_portable es identico al de la
version Arduino (FlexOS_Ultra/). Si uno diverge, las pruebas de host ya no
cubren el codigo que lleva el firmware."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PORT = ROOT / "components" / "flex_portable"
ARDUINO = ROOT.parent / "FlexOS_Ultra"

bad = []
files = sorted(list((PORT / "src").glob("*.cpp")) + list((PORT / "include").glob("*.h")))
for f in files:
    orig = ARDUINO / f.name
    if not orig.exists():
        bad.append(f"{f.name}: no existe en {ARDUINO}")
    elif orig.read_bytes() != f.read_bytes():
        bad.append(f"{f.name}: distinto del original Arduino")
if bad:
    print("flex_portable NO es identico a la version Arduino:")
    for b in bad:
        print("  " + b)
    sys.exit(1)
print(f"OK: {len(files)} archivos de flex_portable identicos a FlexOS_Ultra/")
