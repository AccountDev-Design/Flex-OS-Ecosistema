#!/usr/bin/env python3
# #############################################################
#  Comprobacion del AUTO-PROTOTIPADO de Arduino
#  ------------------------------------------------------------
#  El IDE de Arduino genera un prototipo de CADA funcion del .ino
#  y los inserta todos juntos cerca del principio del fichero,
#  antes de la primera funcion. Por eso, todo tipo que aparezca en
#  la FIRMA de una funcion tiene que estar definido ANTES de esa
#  primera funcion -- aunque en el .ino se use mucho mas abajo.
#
#  g++ no hace nada de esto, asi que "make ino" compila
#  perfectamente codigo que el IDE rechaza con
#      error: 'X' does not name a type
#  Este script cierra ese hueco: busca los tipos definidos DESPUES
#  del punto de insercion y avisa si alguno se usa en la firma de
#  alguna funcion.
#
#  Uso:  python3 check_protos.py ../../FlexOS_Ultra.ino
# #############################################################
import re
import sys

# Definiciones de tipo que el .ino puede introducir.
RE_STRUCT  = re.compile(r'^\s*(?:typedef\s+)?(?:struct|class|union)\s+([A-Za-z_]\w*)\s*(?:\{|:[^;]*\{)')
RE_ENUM    = re.compile(r'^\s*(?:typedef\s+)?enum(?:\s+class)?\s+([A-Za-z_]\w*)\s*(?:\{|:)')
RE_TYPEDEF = re.compile(r'^\s*typedef\s+.*?\b([A-Za-z_]\w*)\s*;')
RE_USING   = re.compile(r'^\s*using\s+([A-Za-z_]\w*)\s*=')

# Definicion de funcion a nivel de fichero: "tipo nombre(args){" en columna 0.
RE_FUNC = re.compile(
    r'^(?:static\s+|inline\s+|const\s+|volatile\s+)*'      # calificadores
    r'([A-Za-z_][\w:]*(?:\s*[*&])?)\s+'                    # tipo de retorno
    r'(?:[*&]\s*)?'
    r'([A-Za-z_]\w*)\s*'                                   # nombre
    r'\(([^;{]*)\)\s*'                                     # argumentos
    r'(?:const\s*)?\{')                                    # abre cuerpo


def strip_comments(src):
    """Sustituye comentarios por espacios, conservando el numero de lineas."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
        elif c in '"\'':
            q, j = c, i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(''.join(ch if ch == '\n' else ' ' for ch in src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def main(path):
    src = strip_comments(open(path, encoding='utf-8', errors='replace').read())
    lines = src.split('\n')

    # 1) Tipos definidos en el fichero, con su linea.
    types = {}
    for idx, ln in enumerate(lines, 1):
        for rx in (RE_STRUCT, RE_ENUM, RE_USING):
            m = rx.match(ln)
            if m:
                types.setdefault(m.group(1), idx)
        m = RE_TYPEDEF.match(ln)
        if m and '(' not in ln:
            types.setdefault(m.group(1), idx)

    # 2) Funciones definidas a nivel de fichero.
    funcs = []
    for idx, ln in enumerate(lines, 1):
        m = RE_FUNC.match(ln)
        if m:
            funcs.append((idx, m.group(2), m.group(1), m.group(3)))
    if not funcs:
        print('check_protos: no se encontro ninguna funcion; revisa el patron.')
        return 1

    # 3) Punto de insercion: la primera funcion del fichero. Ahi es donde el
    #    IDE deja TODOS los prototipos.
    insert_at = funcs[0][0]

    # 4) Cualquier tipo definido despues del punto de insercion no puede
    #    aparecer en la firma de ninguna funcion.
    late = {t: l for t, l in types.items() if l > insert_at}
    problems = []
    for fline, fname, fret, fargs in funcs:
        sig = fret + ' ' + fargs
        for word in set(re.findall(r'[A-Za-z_]\w*', sig)):
            if word in late:
                problems.append((fline, fname, word, late[word]))

    if problems:
        print('ERROR: tipos usados en firmas pero definidos DESPUES del punto')
        print('de insercion de prototipos del IDE de Arduino (linea %d).' % insert_at)
        print('El IDE fallara con "does not name a type" aunque g++ compile.\n')
        for fline, fname, word, tline in sorted(problems):
            print('  %s:%d: %s() usa \'%s\', definido en la linea %d'
                  % (path.split('/')[-1], fline, fname, word, tline))
        print('\nSolucion: mueve la definicion COMPLETA del tipo al bloque de')
        print('tipos del principio del fichero (antes de la linea %d).' % insert_at)
        return 1

    print('Auto-prototipado de Arduino: %d funciones, %d tipos, sin conflictos.'
          % (len(funcs), len(types)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '../../FlexOS_Ultra.ino'))
