# Flex Package Runtime para FlexOS Ultra P4

Conecta el formato binario `FLXP v1` del Flex SDK con el firmware real de
FlexOS Ultra para ESP32-P4. Desde esta versión hay **dos runtimes**, y el
antiguo sigue funcionando exactamente igual.

| Runtime | Qué es | Estado |
|---|---|---|
| `flex-ui-1` | pantallas declarativas en JSON | **sin cambios**; las apps ya publicadas se instalan y se abren igual |
| `flex-app-v1` | máquina aislada con lógica real, render 2D, eventos, temporizadores y almacenamiento privado | nuevo |

Documentos relacionados:

* `FlexOS_Ultra/docs/FLEX_RUNTIME_ARCHITECTURE.md` — auditoría, decisión de
  arquitectura y riesgos.
* `FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md` — formato del bytecode y
  contrato de ejecución.
* `FlexOS_Ultra/docs/FLEXBENCH_INTEGRATION.md` — qué necesitará el benchmark.
* `sdk/README.md` — cómo se publica una app.

## Archivos

**Lógica pura (se compila y se ejercita en el PC con ASan/UBSan):**

- `FlexOS_PkgCore.h/.cpp` — validación completa de `FLXP v1`: cabecera, JSON
  canónico, rutas, offsets, SHA-256 por archivo y global, firma ECDSA P-256,
  huella del desarrollador y trailer de permisos. Recibe los bytes por un
  lector abstracto y los entrega por un sumidero abstracto.
- `FlexOS_AppGrant.h/.cpp` — grant `FLXG v1`: campos y firma.
- `FlexOS_AppVM.h/.cpp` — máquina `flex-app-v1`: validador estático e
  intérprete con presupuesto.
- `FlexOS_AppHost.h/.cpp` — `FlexAppManager`: estados, eventos, presupuestos,
  permisos **por llamada** y despacho de syscalls.

**Ligados al hardware:**

- `FlexOS_Package.h/.cpp` — LittleFS: montaje, recuperación, slot temporal,
  cambio de versión, reversión, registro y desinstalación.
- `FlexOS_Runtime.h/.cpp` — runtime declarativo `flex-ui-1`. **No se ha
  tocado.**
- `FlexOS_Store.h/.cpp` — catálogo firmado, descarga en tarea propia,
  comprobación del SHA publicado e instalación.
- `FlexOS_AppHost_Bridge.h` — la única frontera entre una app y el hardware:
  vtable de dibujo, táctil, almacenamiento, orientación y servicios
  privilegiados.
- `FlexOS_Store_Bridge.h` — interfaz de Flex Store; abre cada app por el
  runtime que declare y permite detenerla o desinstalarla. Atiende además la
  petición de apertura que deja la Caja de aplicaciones.
- `FlexOS_Ultra_PkgApps.h` — el modelo de las apps **descargadas** para la Caja
  de aplicaciones: lista cacheada del registro real, estado, icono del paquete
  y petición de apertura. Ver «Las apps descargadas en el cajón», abajo.
- `FlexOS_TrustedKeys.h` — clave pública pinneada de Flex Store, compartida
  por el catálogo y los grants.

## Instalación y actualización

1. Flex Store descarga a `/FlexApps/.download.flexpkg` sin bloquear el hilo
   gráfico.
2. Comprueba que el catálogo lo firmó la clave oficial incrustada.
3. Comprueba el SHA-256 del paquete que ese catálogo declara.
4. Se lee **sólo la cabecera y el manifest** para saber el `id` y decidir
   dónde extraer y si la actualización está permitida (misma clave de
   desarrollador y `versionCode` mayor).
5. Validación **completa** del paquete: encabezado, tamaños, JSON canónico,
   rutas, offsets contiguos, hashes de todos los archivos, hash global, huella
   del desarrollador y firma ECDSA P-256. Es la misma pasada que extrae a
   `/FlexApps/<id>/.stage`.
6. Se guarda el registro junto a los archivos: manifest, índice, SHA-256 del
   paquete y, si viene, el grant.
7. Se comprueba que el entrypoint es del runtime que dice el manifest
   (JSON de pantallas para `flex-ui-1`, cabecera `FLXB` coherente para
   `flex-app-v1`).
8. `rename(active → .old)`, `rename(.stage → active)`, borrar `.old`.
9. Si el cambio falla, se restaura `.old`: **la versión anterior sigue ahí**.
10. Ante cualquier fallo se borra el temporal.

Al arrancar, `flexPkgBegin()` limpia stages huérfanos y recupera `.old` si un
corte de corriente pilló la transacción por la mitad.

## Extensión compatible del formato

Dos cambios, los dos compatibles hacia atrás:

* `manifest.runtime` acepta `"flex-app-v1"` además de `"flex-ui-1"`, y el
  bloque `limits` admite `instrPerTick`, `usPerTick` y `drawPerFrame`
  (opcionales). Un manifest antiguo sigue siendo válido tal cual.
* Los bytes 56..59 de la cabecera, antes reservados a cero, son ahora
  `grantLen`. Vale 0 (paquete sin permisos, comportamiento idéntico al de
  siempre) o exactamente 296. El grant va **después** de la firma, así que no
  entra en el hash firmado: por eso puede referirse al hash del propio
  paquete. Un firmware antiguo que reciba un paquete con grant lo rechaza por
  "bytes reservados inválidos" — falla **cerrado**, que es lo correcto.

## Identidad del desarrollador y permisos

La primera instalación guarda `developerKeySha256`. Una actualización del
mismo `packageId` sólo se acepta con la **misma clave** y un `version.code`
mayor.

El manifest **declara** permisos de sistema; el **grant** los concede. Un
permiso privilegiado que sólo aparezca en el manifest no existe. El grant lo
firma la clave pinneada de Flex Store y ata propósito, `packageId`, versión,
SHA-256 del paquete instalado, huella del desarrollador, máscara de permisos y
ventana de validez. Se verifica al instalar y **en cada arranque de la app**;
cada llamada a un servicio privilegiado consulta la máscara resultante de esa
verificación.

Una app sin grant **se instala y funciona**: dibuja, recibe toques, usa
temporizadores y su memoria. Lo que no hace es medir el sistema.

## Registro de apps instaladas

Junto a la versión activa quedan `.manifest.json`, `.index.json`, `.pkghash`
(SHA-256 del paquete) y `.grant` si lo hay; en la carpeta de la app, `.state`
con el estado (activa / detenida por el usuario / bloqueada por el sistema).
`/FlexApps/<id>/data` es la **carpeta privada**: sobrevive a las
actualizaciones y desaparece al desinstalar.

Desde el detalle de Flex Store se puede **detener** una app (no se abre ni
ejecuta nada) y volver a activarla, además de desinstalarla.

## Runtime `flex-app-v1` en dos líneas

La app corre dentro del tick de Flex Store, en la tarea de interfaz, con un
presupuesto doble de instrucciones y microsegundos. Agotarlo cede el control y
se continúa en el tick siguiente; insistir la detiene con un motivo y **Flex
OS no se reinicia**. La app no ve punteros, no toca el framebuffer, no crea
tareas y no alimenta el watchdog.

## Las apps descargadas en el cajón de aplicaciones

Toda app que Flex Store instale y valide correctamente aparece **sola** en la
Caja de aplicaciones, en una sección `Descargadas` bajo las nativas. No hay
ninguna lista de apps conocidas: la sección sale del registro de `/FlexApps`.

**Cuándo se relee el registro.** `flexPkgRevision()` es un contador que sube
**una** vez por operación que puede cambiar la lista —recuperación en el
arranque, instalar, actualizar, desinstalar, detener y reactivar— y **nunca**
por una lectura. Una instalación que falla revierte y no lo mueve. La caja
compara ese entero; sólo cuando cambia recorre `/FlexApps`. Escanear el
almacenamiento por cuadro habría costado la fluidez del cajón.

**Qué aparece.** Sólo una entrada con identificador seguro, nombre, punto de
entrada y un runtime conocido. Lo demás no se lista. Es defensa en profundidad:
el instalador ya validó cabecera, rutas, hashes, huella del desarrollador, firma
y grant antes de activar la versión.

**Estado.** `válida`, `actualizando` (Flex Store está descargando o instalando
ese paquete concreto) y `error` (detenida por el usuario o por el sistema, o un
intento de apertura que falló). Se marca con un punto y **no bloquea la caja**:
la rejilla no se reordena ni se para por una app así.

**Icono.** Si el paquete trae `icon.f565` en la raíz de su versión activa
—RGB565 little-endian, exactamente 64×64— se usa ese; sus bytes ya están
cubiertos por el hash del archivo y la firma del paquete. Cualquier otro tamaño
se descarta. Sin él, un icono genérico con color estable derivado del
identificador. La caché son 4 ranuras (32 KB de PSRAM como techo) y se suelta
con el barrido de memoria del sistema. **El formato firmado no cambia**: una app
que no traiga el archivo funciona igual.

**Apertura.** La caja no abre una segunda puerta al runtime: anota la petición y
abre Flex Store, que arranca la app por el mismo camino de siempre (bifurcación
por runtime, revalidación del grant en cada arranque, presupuesto por tick). Al
salir se vuelve al **escritorio**, no al listado de la tienda.

### Dos reglas que no se pueden romper en el cajón

Salieron de un reinicio real del firmware al desplazar la caja:

1. **La lista no se construye en la pila.** `pkgAppsRebuild()` lee el registro
   en un buffer de PSRAM y lo libera al terminar. Un `FlexPkgInfo[24]` local son
   20 KB, y el `loopTask` de Arduino tiene 8 KB: desbordaba la pila entera y
   corrompía la memoria de al lado, con el sistema muriendo después, en otro
   sitio. Lo vigila `tests/host/check_stack.py`.
2. **El hilo gráfico no espera a Flex Store.** `flexStoreBusyPackage()` toma el
   mutex de la tienda con espera infinita, así que el estado «actualizando» se
   **muestrea una vez por cuadro** (`pkgAppSampleBusy`) y el dibujo lee esa
   copia. Fuera del pintado se usa `pkgAppStatusLive()`.

## Compilación objetivo

- Arduino IDE 2.3.10
- `esp32 by Espressif Systems` 3.2.0
- `ESP32P4 Dev Module`
- Flash de 16 MB y PSRAM habilitada
- Tabla `flexos_ultra_usb.csv`
- Primera grabación por USB

## Pruebas

```bash
make -C tests/host          # bateria completa, con ASan/UBSan
make -C tests/host tools    # flexpkgcheck y flexapprun
node --test sdk/test/sdk.test.js
```

`make ino` compila el sketch **entero** con `g++ -fsyntax-only` contra los
dobles de `tests/host/inostub/`. **No sustituye a compilar para ESP32-P4**:
en este repositorio no hay toolchain RISC-V, así que la compilación para la
placa hay que hacerla en el Arduino IDE.
