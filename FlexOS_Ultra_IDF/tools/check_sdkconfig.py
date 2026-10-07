#!/usr/bin/env python3
"""Comprueba que cada linea de los sdkconfig.defaults llega, con el mismo
valor, al sdkconfig generado. ESP-IDF ignora en silencio un simbolo mal
escrito o que otra opcion deja oculto; este script lo convierte en error.

Uso: python3 tools/check_sdkconfig.py <sdkconfig generado> <defaults> [<defaults>...]
     (tools/build.sh lo llama con los mismos fragmentos que usa el build)
"""
import re
import sys

LINE_SET = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
LINE_UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def parse(path, keep_last=True):
    values = {}
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            m = LINE_SET.match(line)
            if m:
                values[m.group(1)] = m.group(2)
                continue
            m = LINE_UNSET.match(line)
            if m:
                values[m.group(1)] = "n"
    return values


def norm(v):
    v = v.strip()
    if v.lower().startswith("0x"):
        try:
            return str(int(v, 16))
        except ValueError:
            return v
    return v


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    generated = parse(sys.argv[1])
    wanted = {}
    for frag in sys.argv[2:]:
        wanted.update(parse(frag))   # el ultimo fragmento manda, como en ESP-IDF
    bad = []
    for key, val in wanted.items():
        got = generated.get(key)
        if val == "n":
            if got not in (None, "n"):
                bad.append(f"{key}: se pidio desactivado y esta {got}")
        elif got is None:
            bad.append(f"{key}: no existe o queda oculto (se pidio {val})")
        elif norm(got) != norm(val):
            bad.append(f"{key}: se pidio {val} y quedo {got}")
    if bad:
        print("sdkconfig NO coincide con los defaults:")
        for b in bad:
            print("  " + b)
        return 1
    print(f"OK: las {len(wanted)} opciones pedidas estan en {sys.argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
