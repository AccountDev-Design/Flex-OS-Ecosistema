#!/usr/bin/env python3
"""Comprueba que cada linea de los sdkconfig.defaults llega, con el mismo
valor, al sdkconfig generado. ESP-IDF ignora en silencio un simbolo mal
escrito o que otra opcion deja oculto; este script lo convierte en error.

  - Un simbolo que no existe en el arbol Kconfig del build es error, tambien
    en las lineas "# CONFIG_X is not set".
  - Entiende los grupos "choice": si un fragmento posterior elige otro miembro
    del mismo grupo, la eleccion anterior deja de exigirse (igual que kconfgen).

Uso: python3 tools/check_sdkconfig.py --build <carpeta de build> <defaults> [<defaults>...]
     (la carpeta debe contener sdkconfig y config/kconfig_menus.json;
      tools/build.sh lo llama con los mismos fragmentos que usa el build)
"""
import argparse
import json
import pathlib
import re
import sys

LINE_SET = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
LINE_UNSET = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")


def parse(path):
    items = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            m = LINE_SET.match(line)
            if m:
                items.append((m.group(1), m.group(2)))
                continue
            m = LINE_UNSET.match(line)
            if m:
                items.append((m.group(1), "n"))
    return items


def kconfig_tree(path):
    symbols, choice_of = set(), {}

    def walk(node):
        if isinstance(node, list):
            for n in node:
                walk(n)
            return
        name = node.get("name")
        if name:
            symbols.add("CONFIG_" + name)
        children = node.get("children") or []
        if node.get("type") == "choice":
            members = ["CONFIG_" + c["name"] for c in children if c.get("name")]
            for m in members:
                choice_of[m] = members
        for c in children:
            walk(c)

    walk(json.loads(pathlib.Path(path).read_text()))
    return symbols, choice_of


def norm(v):
    v = v.strip()
    if v.lower().startswith("0x"):
        try:
            return str(int(v, 16))
        except ValueError:
            return v
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("fragments", nargs="+")
    args = ap.parse_args()
    build = pathlib.Path(args.build)
    generated = dict(parse(build / "sdkconfig"))
    symbols, choice_of = kconfig_tree(build / "config" / "kconfig_menus.json")

    bad, wanted = [], {}
    for frag in args.fragments:
        for key, val in parse(frag):
            if key not in symbols:
                bad.append(f"{key} ({frag}): simbolo desconocido, no existe en el arbol Kconfig")
                continue
            if val == "y" and key in choice_of:
                for other in choice_of[key]:
                    wanted.pop(other, None)
            wanted[key] = val

    for key, val in wanted.items():
        got = generated.get(key)
        if val == "n":
            if got not in (None, "n"):
                bad.append(f"{key}: se pidio desactivado y esta {got}")
        elif got is None:
            bad.append(f"{key}: queda oculto por otra opcion (se pidio {val})")
        elif norm(got) != norm(val):
            bad.append(f"{key}: se pidio {val} y quedo {got}")
    if bad:
        print("sdkconfig NO coincide con los defaults:")
        for b in bad:
            print("  " + b)
        return 1
    print(f"OK: las {len(wanted)} opciones pedidas estan en {build / 'sdkconfig'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
