#!/usr/bin/env python3
# #############################################################
#  COMPROBACION DE CABLEADO DEL SKETCH  (host, sin placa)
#  ------------------------------------------------------------
#  Existe por un fallo real: una funcion nueva puede estar escrita,
#  compilar y pasar las pruebas de host... y no llamarse desde
#  ninguna parte. El sistema entonces entra en un estado del que no
#  sale, o una preferencia no se carga nunca.
#
#  Ni el compilador ni check_protos.py lo detectan:
#   · -Wunused-function NO avisa de una funcion static que tiene un
#     PROTOTIPO previo, y en este sketch casi todas lo tienen (hace
#     falta por el orden de definicion);
#   · las pruebas de host llaman a las funciones DIRECTAMENTE, asi
#     que pasan igual aunque loop() no las despache.
#
#  Aqui se comprueban dos cosas sobre el texto del sketch:
#   1. TODO estado del enum de gState tiene su "case" en el switch
#      de loop(), salvo los que se declaran exentos con su motivo;
#   2. una tabla de GANCHOS obligatorios (quien tiene que llamar a
#      quien) se cumple de verdad.
# #############################################################
import re, sys

# Estados que NO se despachan en el switch de loop() y por que.
EXENTOS = {
    "ST_SPLASH": None,          # (se despacha; se deja fuera solo si algun dia deja de hacerlo)
}
# Ganchos obligatorios: (funcion que contiene la llamada, llamada que debe aparecer, motivo)
GANCHOS = [
    ("loop",           "hcTick()",        "sin esto el modo de personalizacion no anima, no pinta ni recibe toques"),
    ("loop",           "wgDataTick()",    "los widgets del escritorio no refrescarian sus datos"),
    ("loop",           "flexPollTouch()", "no habria tactil"),
    ("flexPollTouch",  "hpzUpdate()",     "el gesto de dos dedos no se detectaria nunca"),
    ("flexPollTouch",  "hpzSwallowing()", "el gesto no se consumiria y el mismo toque llegaria a otra capa"),
    ("flexPollTouch",  "suspGestureUpdate()", "se perderia el gesto de suspension"),
    ("setup",          "homeCfgLoad()",   "el fondo, el tema y la paleta no se restaurarian al arrancar"),
    ("setup",          "homeOrderLoad()", "el escritorio no se restauraria al arrancar"),
    ("homeTick",       "hcEnter()",       "la pulsacion larga en un hueco vacio no abriria la personalizacion"),
    ("enterHome",      "hcClose(",        "volver al escritorio dejaria el modo abierto y sus buffers reservados"),
    ("autoLockNow",    "hcClose(",        "bloquear con el modo abierto dejaria el estado a medias"),
]

def cuerpo(src, nombre):
    """Devuelve el cuerpo de la funcion de nivel superior `nombre`, por conteo de llaves."""
    m = re.search(r"^[A-Za-z_][\w \*&:]*\b" + re.escape(nombre) + r"\s*\([^;{]*\)\s*\{", src, re.M)
    if not m:
        return None
    i = src.index("{", m.start())
    prof = 0
    for j in range(i, len(src)):
        if src[j] == "{": prof += 1
        elif src[j] == "}":
            prof -= 1
            if prof == 0:
                return src[i:j + 1]
    return None

def main(path):
    src = open(path, encoding="utf-8").read()
    fallos = []

    # --- 1. todo estado tiene su case en el switch de loop() ---
    menum = re.search(r"enum\s*\{\s*(ST_SPLASH\b.*?)\}\s*;", src, re.S)
    if not menum:
        print("check_wiring: no encuentro el enum de gState"); return 1
    estados = re.findall(r"\bST_[A-Z0-9_]+\b", re.sub(r"//[^\n]*", "", menum.group(1)))
    estados = list(dict.fromkeys(estados))
    cloop = cuerpo(src, "loop")
    if cloop is None:
        print("check_wiring: no encuentro loop()"); return 1
    for st in estados:
        if st in EXENTOS and EXENTOS[st]:
            continue
        if not re.search(r"case\s+" + st + r"\s*:", cloop):
            fallos.append("el estado %s no se despacha en el switch de loop()" % st)

    # --- 2. ganchos obligatorios ---
    for fn, llamada, motivo in GANCHOS:
        c = cuerpo(src, fn)
        if c is None:
            fallos.append("no encuentro la funcion %s()" % fn)
            continue
        if llamada not in c:
            fallos.append("%s() no llama a %s -> %s" % (fn, llamada, motivo))

    if fallos:
        print("check_wiring: CABLEADO INCOMPLETO")
        for f in fallos:
            print("  - " + f)
        return 1
    print("Cableado del sketch: %d estados despachados, %d ganchos verificados." %
          (len(estados), len(GANCHOS)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "../../FlexOS_Ultra.ino"))
