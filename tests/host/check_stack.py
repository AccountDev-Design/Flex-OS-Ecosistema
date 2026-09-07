#!/usr/bin/env python3
# #############################################################
#  PRESUPUESTO DE PILA POR FUNCION  (host, sin placa)
#  ------------------------------------------------------------
#  POR QUE EXISTE ESTE ARCHIVO.
#
#  Un fallo REAL: pkgAppsRebuild() declaraba `FlexPkgInfo raw[24]`
#  como variable local. Son 844 x 24 = 20.256 bytes de datos que, con
#  el resto del marco, daban 21.360 bytes de PILA en una sola llamada.
#
#  El loopTask de Arduino en ESP32 tiene 8.192 bytes. El marco se comia
#  la pila entera y ~13 KB de lo que hubiera debajo: pilas de otras
#  tareas, metadatos del heap, descriptores de la DMA2D.
#
#  Lo peor de ese fallo es que NO revienta donde ocurre. Corrompe en
#  silencio y el sistema muere despues, en un sitio que no tiene nada
#  que ver -- al desplazar la caja de aplicaciones, con la pantalla en
#  cian y un reinicio. Ni el compilador, ni las pruebas de host (donde
#  la pila del proceso son megabytes), ni AddressSanitizer lo ven.
#
#  Lo que si lo ve es -fstack-usage, que es lo que se comprueba aqui:
#  el compilador dice cuanta pila necesita CADA funcion, y esta prueba
#  falla si alguna se pasa del presupuesto.
# #############################################################
import subprocess, sys, os, tempfile

# Techo por funcion. El loopTask de Arduino tiene 8192 bytes y por el pasan
# ademas las llamadas anidadas, el contexto de interrupcion y el propio
# framework: una sola funcion no puede acercarse a ese numero.
#
# 6144 deja sitio de sobra sobre la funcion mas glotona que el firmware ya
# tenia (galThumbBuild, 4464) y sigue muy por debajo del tamano de la pila.
# Si una funcion nueva se pasa de aqui, o el dato va al heap/PSRAM, o se
# trocea: NO se sube este numero sin entender por que.
LIMITE = 6144

def main(root):
    root = os.path.abspath(root)
    aqui = os.path.dirname(os.path.abspath(__file__))
    ultra = os.path.join(root, "FlexOS_Ultra")
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, "stack.o")
        cmd = ["g++", "-std=c++17", "-O1", "-fstack-usage",
               "-I" + os.path.join(aqui, "inostub"), "-I" + ultra, "-I" + root,
               "-DARDUINO=200", "-w", "-x", "c++", "-c",
               os.path.join(aqui, "ino_compile.cpp"), "-o", obj]
        r = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True)
        if r.returncode != 0:
            print("FALLO: no se pudo compilar para medir la pila")
            print(r.stderr[-2000:])
            return 1
        su = os.path.join(tmp, "stack.su")
        if not os.path.exists(su):
            cand = [f for f in os.listdir(tmp) if f.endswith(".su")]
            if not cand:
                print("FALLO: el compilador no genero el informe de pila (.su)")
                return 1
            su = os.path.join(tmp, cand[0])
        peores, total = [], 0
        for linea in open(su, encoding="utf-8", errors="replace"):
            partes = linea.rstrip("\n").split("\t")
            if len(partes) < 2:
                continue
            try:
                usa = int(partes[1])
            except ValueError:
                continue
            total += 1
            if usa > LIMITE:
                peores.append((usa, partes[0]))

    if peores:
        peores.sort(reverse=True)
        print("Presupuesto de pila: %d funcion(es) por encima de %d bytes" % (len(peores), LIMITE))
        for usa, donde in peores:
            print("  · %6d bytes  %s" % (usa, donde))
        print("  El loopTask de Arduino tiene 8192 bytes. Un marco asi lo desborda y")
        print("  corrompe memoria ajena: el sistema muere despues, en otro sitio.")
        print("  Mueve el dato grande al heap/PSRAM, o trocea la funcion.")
        return 1

    print("Presupuesto de pila: %d funciones, ninguna por encima de %d bytes." % (total, LIMITE))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "../.."))
