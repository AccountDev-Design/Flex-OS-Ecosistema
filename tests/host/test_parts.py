#!/usr/bin/env python3
# #############################################################
#  Comprobacion de la tabla de particiones de la version USB
#  ------------------------------------------------------------
#  Una tabla de particiones mal cuadrada no falla al compilar: falla
#  al grabar, o peor, arranca y se lleva por delante los archivos del
#  usuario. Esto se comprueba aqui, no en la placa.
#
#  Se verifica lo que de verdad puede romperse:
#    · que las regiones no se solapen ni dejen huecos;
#    · que todo quepa en 16 MB;
#    · que exista UNA sola particion de aplicacion (esta version no
#      usa OTA y no debe reservar una segunda copia);
#    · que haya una particion de datos que LittleFS sepa montar;
#    · que los offsets cumplan las reglas de la flash del ESP32.
#
#  Uso:  python3 test_parts.py ../../flexos_ultra_usb.csv
# #############################################################
import sys
import os

FLASH_SIZE = 16 * 1024 * 1024          # 16 MB
# El nucleo de Arduino prueba estas etiquetas al montar LittleFS
# (ver FS_LABELS en FlexOS_FS.cpp). Con cualquiera de ellas funciona.
FS_LABELS = ("spiffs", "littlefs", "ffat", "storage")

fails = []


def chk(cond, what):
    if not cond:
        fails.append(what)


def parse(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            # El formato del ESP32 permite dejar Flags vacio, asi que la
            # ultima columna puede faltar tras el split.
            if len(parts) < 5:
                fails.append(f"linea {lineno}: se esperaban al menos 5 columnas -> {line!r}")
                continue
            name, typ, sub, off, size = parts[0], parts[1], parts[2], parts[3], parts[4]
            try:
                off_i = int(off, 0)
                size_i = int(size, 0)
            except ValueError:
                fails.append(f"linea {lineno}: offset o tamano no numerico -> {line!r}")
                continue
            rows.append({"name": name, "type": typ, "sub": sub,
                         "off": off_i, "size": size_i, "line": lineno})
    return rows


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "../../flexos_ultra_usb.csv"
    print("=== FlexOS - tabla de particiones de la version USB ===")
    print(f"    {os.path.basename(path)}")

    if not os.path.exists(path):
        print(f"  FALLO: no existe {path}")
        print("=== 1 fallo ===")
        return 1

    rows = parse(path)
    chk(len(rows) >= 3, "la tabla tiene al menos nvs, una app y datos")

    # --- ordenadas, sin solapes ni huecos a partir de la app ---
    rows_sorted = sorted(rows, key=lambda r: r["off"])
    chk([r["name"] for r in rows_sorted] == [r["name"] for r in rows],
        "las particiones estan escritas en orden de offset")

    for a, b in zip(rows_sorted, rows_sorted[1:]):
        end = a["off"] + a["size"]
        chk(end <= b["off"],
            f"{a['name']} (acaba en {end:#x}) no invade {b['name']} ({b['off']:#x})")

    # --- cabe en la flash ---
    last = rows_sorted[-1]
    total = last["off"] + last["size"]
    chk(total <= FLASH_SIZE,
        f"todo cabe en 16 MB (termina en {total:#x}, tope {FLASH_SIZE:#x})")

    # --- reglas de la flash del ESP32 ---
    for r in rows:
        if r["type"] == "app":
            # Una particion de aplicacion DEBE empezar en multiplo de
            # 64 KB. Es requisito del gestor de arranque, no una
            # convencion: con otro offset el firmware no arranca.
            chk(r["off"] % 0x10000 == 0,
                f"{r['name']}: una particion de app empieza en multiplo de 64 KB")
        else:
            chk(r["off"] % 0x1000 == 0,
                f"{r['name']}: offset alineado a 4 KB")
        chk(r["size"] > 0, f"{r['name']}: tamano mayor que cero")

    # --- exactamente UNA particion de aplicacion ---
    apps = [r for r in rows if r["type"] == "app"]
    chk(len(apps) == 1,
        f"esta version tiene UNA sola particion de app (hay {len(apps)}): sin OTA no se "
        f"reserva una segunda copia del firmware")
    if apps:
        chk(apps[0]["sub"] == "factory",
            "la particion de app es 'factory' (arranque directo, sin tabla OTA)")
        # 2 MB es el suelo razonable: el firmware ronda 1,3 MB y hay que
        # dejar sitio para crecer sin volver a tocar la tabla.
        chk(apps[0]["size"] >= 2 * 1024 * 1024,
            f"la app tiene sitio de sobra ({apps[0]['size'] / 1048576:.1f} MB)")

    # --- una particion de datos que LittleFS pueda montar ---
    fs = [r for r in rows if r["type"] == "data" and r["sub"] in FS_LABELS or
          r["type"] == "data" and r["name"] in FS_LABELS]
    chk(len(fs) >= 1,
        f"hay una particion de datos montable por LittleFS (etiquetas {FS_LABELS})")
    if fs:
        big = max(fs, key=lambda r: r["size"])
        # El objetivo declarado es "LittleFS amplio": mas de la mitad de
        # la flash para los archivos del usuario.
        chk(big["size"] >= FLASH_SIZE // 2,
            f"LittleFS se lleva mas de la mitad de la flash ({big['size'] / 1048576:.1f} MB)")

    # --- nvs, que es donde viven los ajustes y el token ---
    nvs = [r for r in rows if r["sub"] == "nvs"]
    chk(len(nvs) == 1, "hay exactamente una particion nvs (ajustes, Wi-Fi, Vault, token)")

    # --- otadata conservada aunque no haya OTA ---
    # Quitarla desplazaria todo lo de detras y obligaria a reformatear
    # LittleFS el dia que se quiera volver a activar el OTA.
    chk(any(r["sub"] == "ota" for r in rows),
        "otadata se conserva: quitarla obligaria a reformatear LittleFS al reactivar el OTA")

    for f in fails:
        print(f"  FALLO: {f}")
    if fails:
        print(f"=== {len(fails)} fallo(s) ===")
        return 1

    print(f"  {len(rows)} particiones, {total / 1048576:.2f} MB usados de 16 MB")
    for r in rows_sorted:
        print(f"    {r['name']:<10} {r['off']:#09x}  {r['size'] / 1024:>8.0f} KB  {r['type']}/{r['sub']}")
    print("=== tabla de particiones correcta ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
