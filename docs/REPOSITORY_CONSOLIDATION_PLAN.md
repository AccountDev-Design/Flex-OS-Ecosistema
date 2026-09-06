# Plan de consolidación del repositorio

Documento **previo** a mover o borrar nada. Recoge el inventario real, la
clasificación de cada archivo, las duplicidades **demostradas por hash**, las
referencias reales encontradas con búsquedas de `#include` y de uso, y la lista
de lo que **no se toca**.

Método: primero se inspecciona y se escribe esto; después se implementa el cajón
de aplicaciones; y sólo al final se consolida, respaldando cada movimiento con
referencias y con la batería de pruebas de `tests/host` en verde.

Rama: `claude/flexos-ota-system-blq9g9-2csacx`. No se crean ramas, no se hace PR,
merge, rebase ni force-push.

---

## 1. Inventario

449 archivos versionados. Por zonas:

| Zona | Archivos | Qué es |
|---|---:|---|
| Raíz del repositorio | 108 | los tres sketches + los módulos compartidos |
| `FlexOS_Ultra/` | 107 | carpeta lista para Arduino IDE (sketch P4) |
| `tests/` | 92 | pruebas de host, dobles, ficheros de prueba y herramientas |
| `android/FlexPhone/` | 50 | app de Android (Kotlin/Gradle) |
| `sdk/` | 13 | ensamblador, empaquetado, firma y grants (Node) |
| `server/` | 10 | servicio de render remoto del navegador (Node) |
| `futuras-versiones-BETA/` | 24 | Flex-Intelligence y MicroSD-Multimedia (fuera del firmware activo) |
| `docs/` | 8 | documentación |
| `ota/` | 2 | binario publicado y su manifiesto |

### 1.1 Puntos de entrada, comprobados

| Sketch | Ubicación | Placa |
|---|---|---|
| `FlexOS_Ultra.ino` | raíz **y** `FlexOS_Ultra/` (idénticos) | ESP32-P4 |
| `FlexOS_Ultra_S3.ino` | raíz | Flex OS Ultra S3 |
| `FlexOS_Pro.ino` | raíz | Flex OS Pro (ESP32 clásico) |

**Flex OS Ultra S3 y Flex OS Pro no tienen carpeta propia**: viven sueltos en la
raíz y toman de ahí los módulos que incluyen. Se conservan tal cual.

### 1.2 Cierre transitivo de cada sketch

Calculado siguiendo los `#include "..."` de cada `.ino` y añadiendo el `.cpp`
compañero de cada `.h` alcanzado (regla real de Arduino: todo `.cpp` de la
carpeta del sketch se compila).

* **`FlexOS_Ultra.ino` (P4)** — 103 archivos: los 53 módulos `FlexOS_Ultra_*.h`,
  los puentes `FlexOS_*_Bridge.h` y los módulos `.cpp` portables
  (Package, PkgCore, Runtime, AppVM, AppGrant, AppHost, Store, Account, FS,
  Vault, JPEG, Browser, Weather, FlexPhone, FlexLink, Media, Mem, Audio, OTA).
* **`FlexOS_Ultra_S3.ino`** — 4 archivos: `FlexOS_OTA.h/.cpp`,
  `FlexOS_OTA_Bridge.h` y él mismo.
* **`FlexOS_Pro.ino`** — 9 archivos por `#include` + 2 más por pertenencia de
  carpeta: `FlexOS_OTA.h/.cpp`, `FlexOS_OTA_Bridge.h`, `FlexOS_FS.h/.cpp`,
  `FlexOS_Browser.h/.cpp`, `FlexOS_Browser_Bridge.h`, y además
  `FlexOS_BrowserApp.cpp` (llama a `flexBrowser*`, 3 usos en el `.ino` y 28 en
  el puente) y `FlexOS_JPEG.h/.cpp` (los incluye `FlexOS_BrowserApp.cpp`).

### 1.3 Lo que consume `tests/host`

`tests/host/Makefile` define `ROOT := ../..` y compila **las copias de la raíz**,
incluidos `$(ROOT)/FlexOS_Ultra.ino` y `$(ROOT)/FlexOS_Ultra_*.h`. La batería no
mira `FlexOS_Ultra/` en ningún momento.

---

## 2. Clasificación archivo a archivo

| Clase | Contenido |
|---|---|
| **Punto de entrada P4 / FlexOS_Ultra** | `FlexOS_Ultra.ino` + los 53 `FlexOS_Ultra_*.h` + `flexos_ultra_usb.csv` |
| **Punto de entrada Flex OS Ultra S3** | `FlexOS_Ultra_S3.ino` |
| **Punto de entrada Flex OS Pro** | `FlexOS_Pro.ino` |
| **Compartido S3/Pro (obligatorio en la raíz)** | `FlexOS_OTA.h/.cpp`, `FlexOS_OTA_Bridge.h`, `FlexOS_FS.h/.cpp`, `FlexOS_Browser.h/.cpp`, `FlexOS_Browser_Bridge.h`, `FlexOS_BrowserApp.cpp`, `FlexOS_JPEG.h/.cpp` |
| **Compartido sólo P4** | Package, PkgCore, Runtime, TrustedKeys, AppVM, AppGrant, AppHost(+Bridge), Store(+Bridge), Account(+Bridge), Vault, Weather, FlexPhone(+Bridge, Link), FlexLink, Media, Mem, Audio, Jumper, Jumper_Level |
| **SDK / pruebas / documentación / OTA / servidor / Android** | `sdk/`, `tests/`, `docs/`, `ota/`, `server/`, `android/FlexPhone/` |
| **Fuera del firmware activo** | `futuras-versiones-BETA/Flex-Intelligence/`, `futuras-versiones-BETA/MicroSD-Multimedia/` |
| **Duplicado idéntico comprobado** | los 106 pares raíz ↔ `FlexOS_Ultra/` de §3 |
| **Obsoleto no referenciado** | **ninguno**. No se ha encontrado un solo archivo del firmware sin referencia real |

---

## 3. Duplicados demostrados por hash

Comparación SHA-256 de los 449 archivos versionados, sin deducir nada por el
nombre.

**Resultado: 106 pares byte a byte idénticos, todos entre la raíz y
`FlexOS_Ultra/`.** Es decir: `FlexOS_Ultra/` es hoy una **copia completa** de la
parte P4 de la raíz, sin una sola diferencia. Los únicos archivos que la raíz
tiene y `FlexOS_Ultra/` no son `FlexOS_Pro.ino` y `FlexOS_Ultra_S3.ino`; el único
que `FlexOS_Ultra/` tiene de más es su `README.md` (más `FlexOS_Ultra/docs/`, tres
documentos que no existen en `docs/`).

Fuera de ese conjunto sólo aparece **un** par idéntico más:
`tests/host/inostub/esp_random.h` == `tests/host/stub/esp_random.h`. **No se
tocan**: son dos entornos Arduino simulados distintos y deliberadamente
independientes (`stub/` para el navegador, `inostub/` para el sketch completo);
que hoy un archivo coincida es casualidad, no duplicación de diseño.

### 3.1 Por qué existe la copia

Arduino IDE compila **todos** los `.ino` de la carpeta del sketch. Con
`FlexOS_Ultra.ino`, `FlexOS_Ultra_S3.ino` y `FlexOS_Pro.ino` juntos, **la raíz no
es una carpeta abrible en el IDE para ninguna de las tres placas**. `FlexOS_Ultra/`
existe precisamente para eso: es la copia que sí se puede abrir y compilar.
Su `README.md` lo dice con todas las letras.

O sea: la raíz es la **fuente que compilan las pruebas**, y `FlexOS_Ultra/` es la
**copia compilable**. Ninguna de las dos sobra hoy; lo que sobra es que sean dos.

---

## 4. Consolidación (ejecutada)

> Lo que sigue se propuso antes de tocar nada y **ya está aplicado**. Resultado
> comprobado con `make -C tests/host` y `make -C tests/host all-boards`, las dos
> en verde: 95 archivos consolidados, 11 copias forzadas y vigiladas.


**Un solo origen canónico por módulo, y que la duplicación que Arduino obliga a
mantener deje de poder divergir en silencio.**

1. `FlexOS_Ultra/` pasa a ser el **destino P4 canónico**. Recibe el contenido de
   la raíz mediante `git mv` (conserva historia y garantiza que se lleva el
   trabajo del cajón de apps hecho antes en la raíz).
2. Se **borran de la raíz** los 95 archivos que sólo usa el P4 (§1.2 menos §2
   «compartido S3/Pro»). Demostrado: idénticos por hash y sin ninguna referencia
   fuera del sketch P4 y del `Makefile` de pruebas.
3. `tests/host/Makefile` se repunta a `../../FlexOS_Ultra` para esos archivos.
   Si la ruta quedara mal, `make ino` y `test_ino` fallan **en voz alta**: la
   corrección es verificable, no una apuesta.
4. La raíz conserva `FlexOS_Ultra_S3.ino`, `FlexOS_Pro.ino` y los **11 módulos
   compartidos** que esos dos sketches necesitan (§2). Esos 11 siguen existiendo
   también dentro de `FlexOS_Ultra/` **porque Arduino lo exige**: un sketch no
   puede incluir archivos de una carpeta hermana.
5. Para que esa duplicación forzada no pueda divergir se añade
   `tests/host/check_shared.py`, enganchado a `make ino`: **compara por hash** los
   11 módulos entre la raíz y `FlexOS_Ultra/` y **falla la batería** si alguno se
   separa. La duplicación pasa de riesgo silencioso a error de prueba.

Resultado: **106 copias sobrantes → 11 copias forzadas y vigiladas.**

### 4.1 Lo que NO se hace, y por qué

* **No** se crean `FlexOS_Ultra_S3/` ni `FlexOS_Pro/`. Haría falta una tercera
  copia de los 11 módulos compartidos, que es justo lo que se quiere evitar. Se
  deja documentado como decisión abierta.
* **No** se usan enlaces simbólicos para el código compartido: git los versiona,
  pero en Windows sin modo desarrollador Arduino IDE recibe un archivo de texto
  con una ruta dentro y la compilación falla de una forma difícil de diagnosticar.
* **No** se convierte el código compartido en una librería de Arduino: cambia el
  procedimiento de compilación e instalación de las tres placas. Es una decisión
  del proyecto, no una limpieza.

---

## 5. Archivos que NO se tocan

* `.pem`, claves privadas, paquetes del usuario, configuraciones e historiales.
  **En el repositorio no hay ninguna clave privada** (las pruebas y el SDK generan
  claves efímeras en cada ejecución); `FlexOS_TrustedKeys.h` es una clave
  **pública** pinneada y es código del firmware.
* `futuras-versiones-BETA/` completo, incluidos `Flex-Intelligence` y
  `MicroSD-Multimedia`.
* `sdk/`, `tests/`, `docs/`, `ota/`, `server/`, `android/FlexPhone/`.
* `FlexOS_Ultra_S3.ino`, `FlexOS_Pro.ino` y los 11 módulos que necesitan.
* `FlexOS_Ultra/README.md` y `FlexOS_Ultra/docs/` (no existen en `docs/`: no son
  duplicados).
* `tests/host/stub/` e `inostub/` por separado, según §3.
* Cualquier archivo fuera del repositorio.

## 6. Dudas registradas en vez de arriesgadas

1. **Los 11 módulos compartidos siguen duplicados.** Es una limitación real de
   Arduino, no un descuido. Mitigado con `check_shared.py`; la solución de fondo
   (librería o carpetas por placa) es una decisión del proyecto.
2. **S3 y Pro siguen sin ser abribles desde la raíz**, exactamente igual que
   antes de este cambio. No se ha tocado para no triplicar el código compartido
   sin permiso.
3. **`FlexOS_Ultra/docs/`** contiene tres documentos que `docs/` no tiene. No se
   mueven: no se puede demostrar que sobren en su sitio actual, y el resto de la
   documentación del runtime ya los referencia por esa ruta.
