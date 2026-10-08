#!/usr/bin/env python3
"""Extrae funciones y definiciones de la version Arduino (FlexOS_Ultra/) para
compilarlas en el PC como REFERENCIA en las pruebas de host. No modifica nada:
lee las cabeceras y escribe un .cpp en la carpeta de salida.

  extract_arduino.py <salida.cpp> <archivo.h>:<nombre>[,<nombre>...] ...

<nombre> es una funcion, una variable global o una macro (#define) del archivo.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4] / "FlexOS_Ultra"


def strip_strings_comments(line: str) -> str:
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(\\.|[^'\\])*'", "''", line)
    return re.sub(r"//.*", "", line)


def extract(lines, name):
    pat_func = re.compile(r"^\s*(static\s+)?(inline\s+)?[\w\s\*]+?\b" + re.escape(name) + r"\s*\(")
    pat_def = re.compile(r"^\s*#define\s+" + re.escape(name) + r"\b")
    pat_var = re.compile(r"^\s*(static\s+)?(const\s+)?[\w\s\*]+?\b" + re.escape(name) + r"\s*(\[[^\]]*\])*\s*(=|;|,)")
    pat_struct = re.compile(r"^\s*struct\s+" + re.escape(name) + r"\s*\{")
    for i, line in enumerate(lines):
        is_enum = re.match(r"^\s*enum\s*\{", line) is not None
        if pat_struct.match(line) or is_enum:
            depth, out = 0, []
            for j in range(i, len(lines)):
                code = strip_strings_comments(lines[j])
                out.append(lines[j])
                depth += code.count("{") - code.count("}")
                if depth == 0 and ";" in code:
                    break
            block = "".join(out)
            if pat_struct.match(line) or re.search(r"\b" + re.escape(name) + r"\b", strip_strings_comments(block)):
                return block
            continue
        if pat_def.match(line):
            out = [line]
            while out[-1].rstrip().endswith("\\"):
                i += 1
                out.append(lines[i])
            return "".join(out)
        if pat_func.match(line) and not line.rstrip().endswith(";"):
            depth, started, out = 0, False, []
            for j in range(i, len(lines)):
                code = strip_strings_comments(lines[j])
                out.append(lines[j])
                depth += code.count("{") - code.count("}")
                started = started or "{" in code
                if started and depth == 0:
                    return "".join(out)
        decl = re.sub(r"\[[^\]]*\]", "[]", strip_strings_comments(line).split("=")[0])
        if pat_var.match(line) and "(" not in decl:
            out, j = [line], i
            depth = strip_strings_comments(line).count("{") - strip_strings_comments(line).count("}")
            while depth > 0 or not strip_strings_comments(out[-1]).rstrip().endswith(";"):
                j += 1
                out.append(lines[j])
                code = strip_strings_comments(lines[j])
                depth += code.count("{") - code.count("}")
            return "".join(out)
    raise SystemExit(f"no encuentro '{name}'")


def main():
    out = pathlib.Path(sys.argv[1])
    parts = [f"// GENERADO por extract_arduino.py desde {ROOT.name}/ (no editar)\n"]
    for spec in sys.argv[2:]:
        fname, names = spec.split(":", 1)
        lines = (ROOT / fname).read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)
        for n in names.split(","):
            parts.append(f"// ---- {fname}: {n}\n")
            parts.append(extract(lines, n))
    out.write_text("".join(parts), encoding="utf-8")


if __name__ == "__main__":
    main()
