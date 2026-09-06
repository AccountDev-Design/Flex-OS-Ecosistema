# Flex SDK — publicar apps en Flex Store

Herramientas para escribir, empaquetar y firmar aplicaciones de Flex OS Ultra.
Sólo necesitas **Node 18 o superior**: no hay dependencias externas y la
criptografía es la del propio Node (ECDSA P-256 y SHA-256), que produce
exactamente los bytes que verifica el firmware.

```bash
node sdk/bin/flexpkg.js            # ayuda
node --test sdk/test/sdk.test.js   # pruebas del SDK
```

## Los dos runtimes

| | `flex-ui-1` | `flex-app-v1` |
|---|---|---|
| Qué es | pantallas declarativas en JSON | máquina aislada con lógica real |
| Puede | texto, botones, navegar, notificar | bucles, estado, temporizadores, render 2D, almacenamiento, eventos |
| Sigue funcionando | **sí, sin cambios** | — |
| Permisos de sistema | no puede pedirlos | con grant firmado |

Una app `flex-ui-1` que ya esté publicada **no hay que tocarla**: se instala
y se abre igual que antes.

---

## 1. Crear tu clave de desarrollador

```bash
node sdk/bin/flexpkg.js keygen misclaves/dev.pem
```

Esa clave **es la identidad de tus apps**. Una actualización sólo se acepta si
va firmada con la misma clave y con un `versionCode` mayor: es lo que impide
que otra persona publique una versión de tu app.

* Guárdala fuera del repositorio. `sdk/.gitignore` ya ignora `*.pem`, `*.key`
  y `keys/`, pero la responsabilidad de no subirla es tuya.
* Si la pierdes, no puedes actualizar lo que ya está instalado.
* **Las claves de las pruebas son efímeras**: se generan al ejecutarlas y no
  se escriben en disco. En este repositorio no hay ninguna clave privada.

## 2. Estructura de un proyecto

```
mi-app/
  flexapp.json        descripcion de la app
  src/main.flexasm    codigo (solo flex-app-v1)
  app/                lo que va DENTRO del paquete (su raiz)
```

`flexapp.json`:

```json
{
  "id": "com.ejemplo.miapp.contador",
  "name": "Mi contador",
  "versionName": "1.0.0",
  "versionCode": 1,
  "minFlexOS": "1.0.0",
  "runtime": "flex-app-v1",
  "entry": "main.flxb",
  "source": "src/main.flexasm",
  "filesDir": "app",
  "summary": "Cuenta cosas",
  "category": "Utilidades",
  "limits": {
    "memoryKB": 128, "storageKB": 16,
    "instrPerTick": 60000, "usPerTick": 4000, "drawPerFrame": 256
  },
  "permissions": [],
  "systemPermissions": ["storage.app"]
}
```

* `id`: minúsculas, forma `a.b.c`, de 3 a 8 segmentos.
* `entry`: ruta **dentro del paquete**, es decir, relativa a `filesDir`.
* `permissions`: los de siempre (`notifications`, `clipboard`, …).
* `systemPermissions`: los que abren servicios del firmware. **Declararlos no
  los concede**; hace falta un grant firmado (paso 5).

## 3. Escribir la lógica

El lenguaje es una máquina de pila pequeña y documentada:
`FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md`. Empieza copiando
`sdk/examples/plantilla/`, y mira `sdk/examples/contador/` para un ejemplo
completo (contador, cronómetro por temporizador, toque, pausa/reanudación y
guardado en la carpeta privada).

```
.mem 256
.const K_HOLA "Hola"
.global n

.func onStart args=
  pushk K_HOLA
  sys ui.nav_title
  drop
  ret
.end

.func onTick args=ms
  push 0
  sys gfx.clear
  drop
  pushk K_HOLA
  push 24
  push 40
  sys ui.title
  drop
  sys gfx.present
  drop
  ret
.end
```

Reglas que ahorran disgustos:

* **Cada `onTick` es un cuadro.** Un trabajo largo se parte en trozos que
  avanzan un poco por tick; si prefieres cortar tú, usa `yield`.
* Un `while` que no acaba lo detiene el sistema, con motivo, y la app queda
  `Detenida por seguridad`.
* Toda función deja **un** valor en la pila: `drop` lo tira si no lo quieres.
* Toda syscall deja **un** valor, también.

```bash
node sdk/bin/flexpkg.js validate mi-app   # manifest + codigo, antes de firmar
node sdk/bin/flexpkg.js asm      mi-app   # solo ensamblar
```

`validate` también comprueba que el manifest declara **exactamente** los
permisos de sistema que el código usa: ni de más ni de menos.

## 4. Empaquetar y firmar

```bash
node sdk/bin/flexpkg.js build mi-app -k misclaves/dev.pem -o miapp.flexpkg
node sdk/bin/flexpkg.js inspect miapp.flexpkg
```

El paquete lleva manifest e índice en **JSON canónico**, el SHA-256 de cada
archivo, el SHA-256 global y tu firma ECDSA P-256. El firmware comprueba todo
eso antes de escribir un solo byte en la flash.

Para comprobar que la placa lo va a aceptar, sin placa:

```bash
make -C tests/host tools
tests/host/build/flexpkgcheck miapp.flexpkg
```

`flexpkgcheck` compila **el mismo `FlexOS_PkgCore.cpp` que corre en el
ESP32-P4**: si dice que sí, la validación criptográfica del firmware dice que
sí.

## 5. Permisos de sistema (grants)

| Permiso | Abre |
|---|---|
| `benchmark.run` | sesión de medición |
| `system.cpu.stress` | carga de CPU por rodajas |
| `system.psram.measure` | medir y reservar PSRAM |
| `system.temperature.read` | temperatura (hoy "no disponible", ver abajo) |
| `system.performance.metrics` | métricas reales del sistema |
| `display.landscape` | orientación horizontal |
| `display.exclusive` | pantalla completa sin marco |
| `storage.app` | carpeta privada de la app |

El circuito es:

1. `build` sin grant y `inspect` para copiar el **`packageSha256`**.
2. Pedir el grant en **Flex Developer Studio**, que lo firma con la clave de
   Flex Store (la privada vive sólo en su backend; la pública está incrustada
   en el firmware).
3. Volver a empaquetar: `build ... --grant miapp.flexgrant`.

El grant ata `packageId`, `versionName`, `versionCode`, el SHA-256 del
paquete y la huella de tu clave. **Cada versión necesita su grant**: por eso
un grant no se puede reutilizar en otro binario ni en otra app.

Sin grant, tu app **se instala y funciona**; lo único que no puede es tocar
los servicios de sistema. Es un modo perfectamente válido para la mayoría de
las apps.

> `system.temperature.read` está concedido y comprobado en el firmware, pero
> el servicio devuelve **"no disponible"** mientras no haya una lectura del
> sensor del P4 contrastada en placa. Es a propósito: un número sin verificar
> es peor que no dar ninguno.

## 6. Probar sin placa

```bash
make -C tests/host tools
tests/host/build/flexapprun mi-app/app/main.flxb --json
```

Ejecuta tu bytecode con el **mismo `FlexAppManager`** del firmware y un
anfitrión de mentira: arranque, tres cuadros, un toque, un segundo de reloj,
pausa, reanudación y cierre. Te dice si arrancó, qué permisos se concedieron,
cuántos cuadros publicó, qué guardó y cómo terminó.

Con `--nogrant` se comprueba lo importante: que tu app **siga funcionando**
cuando no le conceden los permisos privilegiados.

## 7. Actualizar una app

1. Sube `versionCode` (y normalmente `versionName`).
2. Firma con **la misma clave**.
3. Pide un grant nuevo si usas permisos de sistema.

El firmware instala en un slot temporal, verifica todo y sólo entonces cambia
de versión. Si algo falla a mitad, **la versión anterior sigue ahí** y la
carpeta privada de la app no se toca.

## Qué hay en cada archivo

```
sdk/
  bin/flexpkg.js        herramienta de linea de ordenes
  lib/isa.js            opcodes, syscalls y permisos (espejo del firmware)
  lib/asm.js            ensamblador .flexasm -> .flxb
  lib/manifest.js       validador y escritura canonica del manifest
  lib/flexpkg.js        contenedor FLXP v1, hashes y firma
  lib/grant.js          bloque FLXG v1 de permisos firmados
  examples/plantilla/   lo minimo para empezar
  examples/contador/    ejemplo completo y funcionando
  test/sdk.test.js      pruebas, cruzadas contra el nucleo del firmware
```
