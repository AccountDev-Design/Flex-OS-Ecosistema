# Flex OS · Ecosistema

Firmware de Flex OS para tres placas, más el SDK de aplicaciones, las pruebas de
host, el servicio de render del navegador y la app de Android.

## Dónde está cada cosa

| Ruta | Qué es |
|---|---|
| `FlexOS_Ultra/` | **sketch completo del ESP32-P4.** Se abre tal cual en Arduino IDE. Es la única copia del código del P4 |
| `FlexOS_Ultra_S3.ino` | sketch de Flex OS Ultra S3 |
| `FlexOS_Pro.ino` | sketch de Flex OS Pro (ESP32 clásico) |
| `FlexOS_OTA*`, `FlexOS_FS*`, `FlexOS_Browser*`, `FlexOS_JPEG*` | los módulos que S3 y Pro incluyen |
| `sdk/` | ensamblador, empaquetado, firma y grants de las apps `.flexpkg` |
| `tests/` | pruebas que compilan y ejecutan **en el PC** el mismo código que va a la placa |
| `docs/` | documentación del sistema |
| `server/` | servicio de render remoto del navegador |
| `android/FlexPhone/` | app de Android de Flex Phone |
| `ota/` | binario publicado y su manifiesto |
| `futuras-versiones-BETA/` | trabajo fuera del firmware activo |

## Compilar el P4

Abre `FlexOS_Ultra/FlexOS_Ultra.ino` en Arduino IDE (ver `FlexOS_Ultra/README.md`).

## Por qué el S3 y el Pro están en la raíz

Arduino IDE compila **todos** los `.ino` de la carpeta del sketch, y un sketch no
puede incluir archivos de una carpeta hermana. Con `FlexOS_Ultra_S3.ino` y
`FlexOS_Pro.ino` en la raíz, los once módulos que esos dos necesitan tienen que
existir también ahí, además de dentro de `FlexOS_Ultra/`.

Esa duplicación es la que Arduino obliga a mantener, y **no puede divergir en
silencio**: `tests/host/check_shared.py` compara los once por hash y falla la
batería si alguno se separa. El razonamiento completo, con el inventario y la
comprobación de duplicados de todo el repositorio, está en
[`docs/REPOSITORY_CONSOLIDATION_PLAN.md`](docs/REPOSITORY_CONSOLIDATION_PLAN.md).

## Pruebas

```bash
make -C tests/host            # todas, perfil P4
make -C tests/host all-boards # el código de dispositivo en los tres perfiles
node --test sdk/test/sdk.test.js
```
