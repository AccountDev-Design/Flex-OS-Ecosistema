#!/usr/bin/env python3
"""Genera components/flex_ui/src/i18n/flex_i18n_data.c con las tablas de textos
de la version Arduino (FlexOS_Ultra/FlexOS_Ultra_Session.h), copiadas tal cual:
idiomas, cadenas del sistema (CH/S_*), nombres de apps y de dias y meses.

  tools/gen_i18n.py            regenera
  tools/gen_i18n.py --check    falla si el archivo no coincide con Arduino
"""
import importlib.util
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("ex", ROOT / "tests/host/ref/extract_arduino.py")
ex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ex)

SRC = ex.ROOT / "FlexOS_Ultra_Session.h"
OUT = ROOT / "components/flex_ui/src/i18n/flex_i18n_data.c"


def build() -> str:
    lines = SRC.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
    text = "".join(lines)
    m = re.search(r"enum\s*\{\s*S_SELLANG.*?S_NSTR\s*\};", text, re.S)
    if not m:
        raise SystemExit("no encuentro el enum S_*")
    enum = m.group(0)
    parts = ["// GENERADO por tools/gen_i18n.py desde FlexOS_Ultra/FlexOS_Ultra_Session.h. NO EDITAR.\n",
             "// Textos de la version Arduino, copiados tal cual (UTF-8).\n",
             '#include "flex_i18n.h"\n\n']
    for name in ["LANG_ENDONYM", "CH", "APP", "WD_FULL", "WD_SHORT", "MO_FULL", "MO_SHORT"]:
        src = ex.extract(lines, name)
        src = re.sub(r"^static const char\*", "const char *const", src.lstrip(), count=1)
        src = src.replace("[NLANG]", "[FLEX_NLANG]").replace("[S_NSTR]", "[FLEX_S_NSTR]")
        src = src.replace("[APP_N]", "[FLEX_APP_N]")
        src = re.sub(r"\b(CH|APP|WD_FULL|WD_SHORT|MO_FULL|MO_SHORT|LANG_ENDONYM)\b", lambda k: "flex_i18n_" + k.group(1).lower(), src, count=1)
        parts.append(src + "\n")
    header_enum = re.sub(r"\bS_", "FLEX_S_", enum)
    return "".join(parts), header_enum


def main():
    data, enum = build()
    hdr = ROOT / "components/flex_ui/src/i18n/flex_i18n_strings.h"
    hdr_text = ("// GENERADO por tools/gen_i18n.py (enum de cadenas de la version Arduino). NO EDITAR.\n"
                "#pragma once\n\n" + enum + "\n")
    if "--check" in sys.argv:
        ok = OUT.read_text(encoding="utf-8") == data and hdr.read_text(encoding="utf-8") == hdr_text
        print("OK: textos sincronizados con la version Arduino" if ok else "FALLO: regenera con tools/gen_i18n.py")
        return 0 if ok else 1
    OUT.write_text(data, encoding="utf-8")
    hdr.write_text(hdr_text, encoding="utf-8")
    print(f"escrito {OUT.relative_to(ROOT)} y {hdr.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
