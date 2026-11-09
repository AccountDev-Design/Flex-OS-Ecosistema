#!/usr/bin/env python3
"""Genera los textos de Device Care a partir de FlexOS_Ultra/FlexOS_Ultra_DeviceCare.h, extraidos TAL CUAL (no se retecla ni un acento):
  components/flex_ui/src/apps/flex_dc_text_ids.h  (enum DCS_*)
  components/flex_ui/src/apps/flex_dc_text.inc    (tabla DCH[][5]: ES, EN, FR, PT, IT)
`--check` falla si alguno no coincide con los originales (lo usa run_tests.sh)."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
APPS = ROOT / "components" / "flex_ui" / "src" / "apps"
EXTRACT = ROOT / "tests" / "host" / "ref" / "extract_arduino.py"
SRC = "FlexOS_Ultra_DeviceCare.h"


def extract(names: str) -> str:
    with tempfile.NamedTemporaryFile(suffix=".cpp", delete=False) as t:
        tmp = t.name
    subprocess.run([sys.executable, str(EXTRACT), tmp, f"{SRC}:{names}"], check=True)
    body = pathlib.Path(tmp).read_text()
    return "".join(l for l in body.splitlines(True) if not l.startswith("// GENERADO por extract_arduino.py") and not l.startswith("// ---- "))


def build() -> dict:
    hdr = "// GENERADO por tools/gen_devcare_text.py desde FlexOS_Ultra/FlexOS_Ultra_DeviceCare.h (no editar a mano; run_tests.sh lo comprueba).\n#pragma once\n"
    inc = "// GENERADO por tools/gen_devcare_text.py desde FlexOS_Ultra/FlexOS_Ultra_DeviceCare.h (no editar a mano; run_tests.sh lo comprueba).\n"
    return {"flex_dc_text_ids.h": hdr + extract("DCS_APPTITLE"), "flex_dc_text.inc": inc + extract("DCH")}


if __name__ == "__main__":
    files = build()
    if "--check" in sys.argv:
        bad = [n for n, t in files.items() if not (APPS / n).exists() or (APPS / n).read_text() != t]
        if bad:
            print(f"{', '.join(bad)} NO coinciden con {SRC}: regenerar con tools/gen_devcare_text.py")
            sys.exit(1)
        print("OK: los textos de Device Care son identicos a los de FlexOS_Ultra/")
    else:
        for n, t in files.items():
            (APPS / n).write_text(t)
        print("escritos:", ", ".join(files))
