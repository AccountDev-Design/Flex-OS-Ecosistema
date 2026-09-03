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
#
#  EL SKETCH ESTA REPARTIDO EN MODULOS
#  ------------------------------------------------------------
#  FlexOS_Ultra.ino es solo el orquestador: el resto del sistema vive
#  en las cabeceras FlexOS_Ultra_*.h que el .ino incluye en orden.
#  Como el IDE de Arduino las junta todas en UNA unidad de traduccion,
#  aqui se hace lo mismo antes de comprobar nada: se EXPANDE el .ino
#  siguiendo sus #include de modulos y se trabaja sobre el texto
#  completo. Sin esto, cada gancho de la tabla de abajo daria "no
#  encuentro la funcion" por el simple hecho de haberla movido de
#  archivo, y la comprobacion perderia todo su valor.
#
#  Ademas se verifica la propia estructura modular (ver estructura()):
#  que no haya un modulo huerfano, que ninguno falte y que la cadena
#  de inclusion siga siendo lineal.
# #############################################################
import re, sys
from pathlib import Path

# Estados que NO se despachan en el switch de loop() y por que.
EXENTOS = {
    "ST_SPLASH": None,          # (se despacha; se deja fuera solo si algun dia deja de hacerlo)
}
# Ganchos obligatorios: (funcion que contiene la llamada, llamada que debe aparecer, motivo)
GANCHOS = [
    ("loop",           "hcTick()",        "sin esto el modo de personalizacion no anima, no pinta ni recibe toques"),
    ("loop",           "wgDataTick()",    "los widgets del escritorio no refrescarian sus datos"),
    ("loop",           "flexWeatherTick(", "el clima no se refrescaria nunca: la app, el widget y el bloqueo se quedarian con la cache"),
    ("setup",          "flexWeatherBegin()", "no se cargarian ni las ubicaciones ni la cache del clima, y su tarea de red no existiria"),
    ("clkSetEpoch",    "flexWeatherSetClock(", "el clima no sabria la hora real y no podria decir cuanto hace que se actualizo"),
    ("loop",           "flexPollTouch()", "no habria tactil"),
    # Sin este tick la tarjeta no se detectaria nunca al insertarla, la
    # retirada no cortaria la reproduccion y el indice de medios no
    # avanzaria: Galeria y Multimedia se quedarian vacias para siempre.
    ("loop",           "mediaStorageTick()", "sin esto la microSD no se detecta ni se suelta, y el indice de medios no avanza"),
    ("mediaStorageTick", "flexSdTick()",   "sin atender la peticion explicita no hay montaje bajo demanda"),
    ("mediaStorageTick", "mediaIndexTick()", "el indice de medios nunca terminaria de construirse"),
    ("setup",          "flexSdBegin()",   "el controlador SDMMC no se prepararia y la tarjeta no montaria nunca"),
    ("loop",           "wifiAutoReconnectTick()", "la red guardada no se reconectaria tras arrancar"),
    # MULTITAREA POR MEMORIA. Sin memTick() la medida se quedaria congelada en la
    # del arranque: el selector, Almacenamiento y la puerta de admision de apps
    # decidirian con cifras viejas. Sin memAlertTick() no habria ningun aviso.
    # Sin optTick() el panel de "Optimizar Flex OS" se abriria y no avanzaria
    # nunca de la primera etapa -- y como es dueno exclusivo de la pantalla,
    # dejaria el sistema mirando un panel congelado.
    ("loop",           "memTick()",       "la medida de memoria se quedaria congelada en la del arranque"),
    ("loop",           "memAlertTick()",  "no saldria ningun aviso de memoria, ni el de proteccion"),
    ("loop",           "optTick()",       "el panel de Optimizar Flex OS no avanzaria de etapa"),
    ("loop",           "loopRateTick()",  "el ritmo del sistema que ensena Almacenamiento seria siempre 0"),
    ("enterApp",       "memAdmitApp(",    "se abriria una app pesada invadiendo la reserva de seguridad"),
    ("appSuspend",     "appEnforceMemoryBudget()", "nada recortaria memoria al acumularse apps en segundo plano"),
    ("setup",          "flexAudioBegin()","el codec no se sondearia y el audio quedaria desactivado sin motivo"),
    # Un dibujo recien creado tiene que aparecer en la Galeria sin que
    # nadie refresque a mano: sin esta invalidacion, el indice se queda
    # con la foto de antes hasta que cambie la tarjeta.
    ("paintNew",       "mediaIndexInvalidate()", "un dibujo nuevo no aparecería en la Galeria hasta reindexar por otro motivo"),
    ("flexPollTouch",  "hpzUpdate()",     "el gesto de dos dedos no se detectaria nunca"),
    ("flexPollTouch",  "hpzSwallowing()", "el gesto no se consumiria y el mismo toque llegaria a otra capa"),
    ("flexPollTouch",  "suspGestureUpdate()", "se perderia el gesto de suspension"),
    ("setup",          "homeCfgLoad()",   "el fondo, el tema y la paleta no se restaurarian al arrancar"),
    ("setup",          "homeOrderLoad()", "el escritorio no se restauraria al arrancar"),
    ("homeTick",       "hcEnter()",       "la pulsacion larga en un hueco vacio no abriria la personalizacion"),
    # La limpieza de "volver a Inicio" vive en enterHomeState(): la comparten
    # enterHome() (que ademas vuelca el escritorio) y appClose() (que deja el
    # dibujo a la capa de transicion interrumpible). Comprobar el sitio donde
    # esta de verdad es lo que mantiene util la regla.
    ("enterHomeState", "hcClose(",        "volver al escritorio dejaria el modo abierto y sus buffers reservados"),
    ("autoLockNow",    "hcClose(",        "bloquear con el modo abierto dejaria el estado a medias"),
]

# Llamadas PROHIBIDAS dentro de una funcion: (funcion, llamada, motivo).
# Es la otra mitad de GANCHOS. Un gancho comprueba que algo se llame; esto
# comprueba que algo NO se llame desde donde no debe -- y ese "donde" suele
# ser el hilo equivocado, que es justo lo que ni el compilador ni una prueba
# de host pueden ver.
PROHIBIDOS = [
    # REGLA DE ORO: esp-hosted no se toca desde loopTask. Estas tres corren en
    # el hilo de la interfaz, que esta suscrito al Task Watchdog y solo lo
    # alimenta una vez por vuelta: levantar ahi el enlace SDIO con el C6 puede
    # pasarse del plazo y provocar el PANIC "se reinicia al activar el Wi-Fi".
    # La radio la despiertan wifiScanTask/wifiConnTask/wifiAutoConnTask.
    ("wifiStartScan",    "wifiEnsureStaMode(", "levantaria esp-hosted desde loopTask (TWDT)"),
    ("wifiStartConnect", "wifiEnsureStaMode(", "levantaria esp-hosted desde loopTask (TWDT)"),
    ("connWifiSet",      "wifiEnsureStaMode(", "levantaria esp-hosted desde loopTask (TWDT)"),
    # El apagado habla con el C6 igual que el encendido: va en wifiOffTask.
    ("connWifiSet",      "WiFi.mode(",         "apagar la radio en loopTask bloquea el bucle"),
    ("connWifiSet",      "WiFi.disconnect(",   "apagar la radio en loopTask bloquea el bucle"),
    ("wifiExit",         "WiFi.mode(",         "apagar la radio en loopTask bloquea el bucle"),
    ("wifiExit",         "WiFi.disconnect(",   "apagar la radio en loopTask bloquea el bucle"),
    # RECIENTES ES UN SELECTOR DE APPS, NO UN PANEL DE MEMORIA. Ni la tarjeta
    # ni la pantalla completa pueden volver a ensenar cifras de PSRAM: el
    # diagnostico vive en Almacenamiento -> Detalles de memoria y sistema.
    ("swDrawCard",       "flexMemFmt(",        "la tarjeta volveria a ser diagnostico tecnico"),
    ("swDrawCard",       "memSnap(",           "la tarjeta volveria a ser diagnostico tecnico"),
    ("swRender",         "flexMemFmt(",        "Recientes volveria a ensenar cifras de memoria"),
    ("swRender",         "memSnap(",           "Recientes volveria a ensenar cifras de memoria"),
    ("swRender",         "optStart(",          "el boton Optimizar no vuelve a Recientes"),
    ("swTick",           "optStart(",          "el boton Optimizar no vuelve a Recientes"),
]

RE_MOD = re.compile(r'^#include\s+"(FlexOS_Ultra_(\w+)\.h)"', re.M)


def expandir(path):
    """Devuelve (texto completo del sketch, lista de modulos en orden)."""
    raiz = Path(path).resolve().parent
    ino = Path(path).read_text(encoding="utf-8")
    orden = [m.group(1) for m in RE_MOD.finditer(ino)]
    partes, vistos = [], set()

    def meter(texto, origen):
        pos = 0
        for m in RE_MOD.finditer(texto):
            partes.append(texto[pos:m.start()])
            pos = m.end()
            fich = m.group(1)
            if fich in vistos:            # #pragma once
                continue
            vistos.add(fich)
            hijo = raiz / fich
            if hijo.exists():
                meter(hijo.read_text(encoding="utf-8"), fich)
        partes.append(texto[pos:])

    meter(ino, path)
    return "".join(partes), orden


def estructura(path):
    """Comprueba la modularizacion del sketch. Devuelve lista de fallos."""
    raiz = Path(path).resolve().parent
    ino = Path(path).read_text(encoding="utf-8")
    orden = [m.group(1) for m in RE_MOD.finditer(ino)]
    fallos = []

    if not orden:
        return ["FlexOS_Ultra.ino no incluye ningun modulo FlexOS_Ultra_*.h"]

    # 1. cada modulo incluido existe y se incluye UNA sola vez
    vistos = set()
    for f in orden:
        if not (raiz / f).exists():
            fallos.append("el .ino incluye %s, que no existe" % f)
        if f in vistos:
            fallos.append("%s se incluye dos veces en el .ino" % f)
        vistos.add(f)

    # 2. ningun modulo huerfano: todo FlexOS_Ultra_*.h del proyecto se usa
    for f in sorted(p.name for p in raiz.glob("FlexOS_Ultra_*.h")):
        if f not in vistos:
            fallos.append("%s existe pero el .ino no lo incluye (codigo muerto)" % f)

    # 3. cadena LINEAL: cada modulo lleva #pragma once e incluye al anterior
    prev = None
    for f in orden:
        ruta = raiz / f
        if not ruta.exists():
            continue
        txt = ruta.read_text(encoding="utf-8")
        if "#pragma once" not in txt:
            fallos.append("%s no tiene #pragma once" % f)
        propios = RE_MOD.findall(txt)
        nombres = [n for n, _ in propios]
        if prev is None:
            if nombres:
                fallos.append("%s es el primer modulo y no debe incluir a ningun otro" % f)
        elif nombres != [prev]:
            fallos.append("%s debe incluir exactamente a %s (incluye %s)"
                          % (f, prev, nombres or "nada"))
        prev = f
    return fallos


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
    src, modulos = expandir(path)
    fallos = estructura(path)

    # El perfil generico P4 no conoce la ranura de esta placa. Estas opciones
    # hacen que Arduino-ESP32 compile SD_MMC para el slot dedicado 0 y active
    # el LDO 4. El enlace esp-hosted lleva sus propios pines GPIO/SDIO.
    build_opt = Path(path).resolve().parent / "build_opt.h"
    required_sd_opts = {
        "-DBOARD_HAS_SDMMC",
        "-DBOARD_SDMMC_SLOT=0",
        "-DBOARD_SDMMC_POWER_CHANNEL=4",
    }
    if not build_opt.exists():
        fallos.append("falta build_opt.h: la microSD perderia slot 0/LDO 4")
    else:
        actual_opts = {line.strip() for line in build_opt.read_text(encoding="utf-8").splitlines()}
        for opt in sorted(required_sd_opts - actual_opts):
            fallos.append("build_opt.h no contiene %s" % opt)

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

    # --- 2b. llamadas prohibidas (el hilo equivocado, o UI ya retirada) ---
    for fn, llamada, motivo in PROHIBIDOS:
        c = cuerpo(src, fn)
        if c is None:
            fallos.append("no encuentro la funcion %s()" % fn)
            continue
        codigo = re.sub(r"/\*.*?\*/|//[^\n]*", "", c, flags=re.S)
        if llamada in codigo:
            fallos.append("%s() llama a %s -> %s" % (fn, llamada, motivo))

    # --- 2c. quitar la interfaz de memoria NO puede apagar el sistema ------
    # Se retiro el panel permanente de Recientes; lo que NO puede irse con el
    # es la proteccion automatica. Si alguien borra estas llamadas creyendo
    # que eran "parte del panel", el sistema se queda sin medir y sin avisar.
    for fn, llamada, motivo in [
        ("memAlertTick", "memShedAll(",      "sin alivio automatico solo quedaria el aviso"),
        ("memAlertTick", "flexMemLevelStep(", "sin la maquina de nivel volveria el aviso por tick"),
        ("enterApp",     "memAdmitApp(",      "sin presupuesto se abriria cualquier app pesada"),
    ]:
        c = cuerpo(src, fn)
        if c is None:
            fallos.append("no encuentro la funcion %s()" % fn)
        elif llamada not in c:
            fallos.append("%s() ya no llama a %s -> %s" % (fn, llamada, motivo))

    # --- 3. el reposo NO puede despertar esp-hosted -----------------
    # En ESP32-P4, WiFi.status()/getMode() no son getters inocuos: pasan por
    # el transporte SDIO del C6. Los ticks globales
    # deben leer el estado publicado gNetOnline, nunca el driver.
    for fn in ("wgDataTick", "ntpTick"):
        c = cuerpo(src, fn)
        if c is None:
            fallos.append("no encuentro la funcion %s()" % fn)
        else:
            codigo = re.sub(r"/\*.*?\*/|//[^\n]*", "", c, flags=re.S)
            if "WiFi." not in codigo:
                continue
            fallos.append("%s() toca WiFi desde reposo -> colision SDIO con microSD" % fn)

    # 3.2.0 ignora los pines hosted del variant. En 3.2.1 deben fijarse los
    # siete antes de cualquier inicializacion, y el autoarranque debe llevar
    # marcador persistente para que un PANIC no se repita en cada boot.
    if "WiFi.setPins(18, 19, 14, 15, 16, 17, 54)" not in src:
        fallos.append("faltan los pines SDIO explicitos del C6 para core 3.2.1")
    if ("ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 1)" not in src
            or "#define FLEXOS_ENABLE_WIFI 0" not in src):
        fallos.append("un core P4 anterior a 3.2.1 podria volver a iniciar el Wi-Fi inseguro")
    if "WIFI_NVS_AUTOTRY" not in src or "wifiAutoGuardWrite(true, true)" not in src:
        fallos.append("falta el fusible persistente del autoarranque Wi-Fi")

    # Sin pin CD, la SD no puede reintentar begin() por tiempo: en el video
    # ese reintento coincidia a los 6 s con esp-hosted. Solo flexSdPoke()
    # puede armar el proximo intento de flexSdTick().
    sd_cpp = Path(path).resolve().parent / "FlexOS_SD.cpp"
    if not sd_cpp.exists():
        fallos.append("falta FlexOS_SD.cpp")
    else:
        sd_src = sd_cpp.read_text(encoding="utf-8")
        tick = cuerpo(sd_src, "flexSdTick") or ""
        if "sdProbeRequested" not in tick or "millis()" in tick:
            fallos.append("flexSdTick() vuelve a montar la SD por temporizador")
        if "SD_MMC.begin()" not in sd_src or "SD_MMC.begin(FLEXSD_MOUNT" in sd_src:
            fallos.append("el montaje SD ya no coincide con el unico begin() 4-bit del fabricante")
    # --- 4. la SD reclama su slot antes que los servicios de red -----
    # En esta placa no basta con preparar setPins() y montar en el primer
    # loop: para entonces una tarea de servicio ya puede haber consultado
    # esp-hosted. El orden dentro de setup() es parte del cableado.
    csetup = cuerpo(src, "setup")
    if csetup is None:
        fallos.append("no encuentro la funcion setup()")
    else:
        sd_mount = csetup.find("flexSdMount()")
        ota_begin = csetup.find("flexOtaBegin()")
        store_begin = csetup.find("flexStoreBegin()")
        account_begin = csetup.find("flexAccountBegin()")
        if sd_mount < 0:
            fallos.append("setup() no monta la microSD antes del primer loop")
        if store_begin >= 0 and (sd_mount < 0 or sd_mount > store_begin):
            fallos.append("flexStoreBegin() arranca antes de que la microSD reclame SDIO")
        if account_begin >= 0 and (sd_mount < 0 or sd_mount > account_begin):
            fallos.append("flexAccountBegin() arranca antes de que la microSD reclame SDIO")
        if ota_begin >= 0 and (sd_mount < 0 or sd_mount > ota_begin):
            fallos.append("flexOtaBegin() arranca antes de que la microSD reclame SDIO")

    if fallos:
        print("check_wiring: CABLEADO INCOMPLETO")
        for f in fallos:
            print("  - " + f)
        return 1
    print("Cableado del sketch: %d estados despachados, %d ganchos verificados, "
          "%d modulos encadenados." % (len(estados), len(GANCHOS), len(modulos)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "../../FlexOS_Ultra.ino"))
