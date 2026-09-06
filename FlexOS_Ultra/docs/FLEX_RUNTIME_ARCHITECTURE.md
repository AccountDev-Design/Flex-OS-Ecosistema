# Plataforma de aplicaciones descargables de Flex OS Ultra

**Estado del documento:** auditoría + arquitectura elegida antes de tocar código.
**Placa objetivo:** ESP32-P4 (`FlexOS_Ultra.ino`), Flash 16 MB, PSRAM 32 MB.
**Runtime nuevo:** `flex-app-v1`.
**Runtime que se conserva sin cambios de comportamiento:** `flex-ui-1`.

---

## 1. Qué existe REALMENTE hoy (auditoría de la rama)

Todo lo de esta sección se comprobó leyendo el código de la rama
`claude/flexos-ota-system-blq9g9-2csacx`, commit `fbdf80f`. No hay nada aquí
supuesto.

### 1.1 Estructura del repositorio

* La **raíz** del repositorio contiene el código de las tres placas
  (`FlexOS_Ultra.ino` para P4, `FlexOS_Ultra_S3.ino`, `FlexOS_Pro.ino`) más los
  módulos comunes `FlexOS_*.cpp/.h`.
* `FlexOS_Ultra/` es una **copia byte a byte** de los archivos que necesita el
  sketch P4, lista para abrirse en Arduino IDE (ver `FlexOS_Ultra/README.md`).
  Confirmado con `diff`: `FlexOS_Package.cpp`, `FlexOS_Store.cpp`,
  `FlexOS_Runtime.cpp` y `FlexOS_Ultra.ino` son idénticos en las dos rutas.
  **Consecuencia para este trabajo: todo archivo nuevo o modificado tiene que
  quedar en las dos rutas.**
* `tests/host/` compila y ejecuta en el PC el mismo código que va a la placa,
  con AddressSanitizer y UndefinedBehaviorSanitizer, sin `arduino-cli` ni el
  core de ESP32. Incluye `make ino`, que compila el sketch **entero** con
  `g++ -fsyntax-only` contra los dobles de `tests/host/inostub/`, y
  `check_protos.py` / `check_wiring.py`, que reproducen la regla de
  auto-prototipado del IDE de Arduino y comprueban el cableado de estados.

### 1.2 Punto de entrada y módulos

`FlexOS_Ultra.ino` es un orquestador de ~780 líneas: `setup()`, `loop()`,
`uiTick()` y la cadena lineal de 53 módulos `FlexOS_Ultra_*.h` (Types → HAL →
Gfx → … → Recovery), más los puentes (`FlexOS_*_Bridge.h`) que se incluyen
*después* de las primitivas gráficas porque las necesitan. Los `.cpp` comunes
(`FlexOS_Package.cpp`, `FlexOS_Store.cpp`, `FlexOS_Runtime.cpp`, `FlexOS_FS.cpp`,
`FlexOS_Vault.cpp`, …) son unidades de traducción independientes que **no
conocen el framebuffer**.

Esa separación es la regla del proyecto y este trabajo la respeta:
*los `.cpp` deciden, los `_Bridge.h` dibujan.*

### 1.3 Flex Store, catálogo, descarga e instalación

* `FlexOS_Store.cpp` — catálogo oficial firmado con una clave P-256 **pinneada
  en el firmware** (`CATALOG_PUBLIC_KEY[65]`, `verifyCatalogSignature`), descarga
  a `/FlexApps/.download.flexpkg` en una tarea FreeRTOS propia, comprueba el
  SHA-256 publicado y llama al instalador. Máquina de estados
  `FLEXSTORE_IDLE … ERROR/CANCELLED`.
* `FlexOS_Store_Bridge.h` — interfaz táctil (Descubrir / Instaladas / Detalle /
  Runtime / Búsqueda). Es también el **único** punto desde el que hoy se abre
  una app instalada (`storeOpenInstalled` → `flexRuntimeLoad`).

### 1.4 Formato `.flexpkg` y validación (lo que ya está bien)

`FlexOS_Package.cpp` implementa el formato binario `FLXP v1`:

```
0   "FLXP" | 4 ver(u16)=1 | 6 flags(u16)=0
8   manifestLen(u32) | 12 indexLen(u32) | 16 payloadLen(u32)
20  pubKeyLen(u16)=65 | 22 sigLen(u16)=64
24  signedHash[32]  = SHA-256(manifest || index || payload)
56  reservado[8] = 0
64  manifest JSON (canónico) | index JSON (canónico) | payload | pubKey[65] | sig[64]
```

Ya valida, hoy, y **todo esto se conserva**:

* tamaño total declarado == tamaño real del archivo;
* JSON **canónico** (round-trip contra `cJSON_PrintUnformatted`);
* `id` con forma `a.b.c` estricta (`safeId`), rutas sin `..`, sin `\`, sin
  raíz absoluta y sólo `[A-Za-z0-9._-]` (`safePath`);
* offsets del índice **contiguos y exactos** (`offset == expectedOffset`), suma
  final == `payloadLen`;
* rutas duplicadas rechazadas (comparación O(n²) contra las anteriores);
* SHA-256 por archivo y SHA-256 global comparado con `signedHash`;
* ECDSA P-256 sobre `signedHash` con la clave incrustada en el paquete, y
  huella `SHA-256(pubKey)` comparada contra `developerKeySha256` del manifest;
* actualización sólo con **la misma clave de desarrollador** y `versionCode`
  estrictamente mayor;
* instalación transaccional real: `.stage` → `rename(active,.old)` →
  `rename(.stage,active)` → borrar `.old`, con reversión si el segundo `rename`
  falla, y recuperación al arrancar en `flexPkgBegin()`.

### 1.5 Lo que `flex-ui-1` es y lo que NO es

`FlexOS_Runtime.cpp` (192 líneas) es un **lector de pantallas JSON**: `text` y
`button`, estilos `title/body/caption`, acciones `navigate` y `notify`, tema de
4 colores, un máximo de 32 componentes y 16 pantallas. `flexRuntimeHit()` mira
qué botón contiene el punto tocado.

**No hay ejecución de lógica.** No hay bucle, ni estado, ni temporizador, ni
render 2D, ni almacenamiento, ni medición: una app `.flexpkg` de hoy no puede
contar hasta diez. Esto es exactamente lo que dice el enunciado y la auditoría
lo confirma.

### 1.6 Gráficos, presentación y orientación

* `FlexOS_Ultra_Gfx.h`: cuatro framebuffers de 480×800×2 B en PSRAM alineados a
  64 B (`fb`, `bbuf`, `lockBuf`, `homeBuf`). `setBuf()` elige destino,
  `flxFlush(y0,y1)` sube una banda por DMA2D **esperando el callback**
  (un solo propietario del pipeline), `present(y0,y1)` compone desde `bbuf`.
* Recorte por banda y por columna: `gClipY0/gClipY1/gClipX0/gClipX1`.
* **Landscape real ya existe**: `gLand`. Con `gLand=true` las primitivas mapean
  `(lx 0..799, ly 0..479)` sobre el panel portrait (`putPhys`), y hay rellenos
  por columna lógica (`fillSpanLand`) porque la ruta rotada es la que castiga la
  caché de PSRAM. La app Juegos (`APP_LAND`) ya usa esta ruta.
* `gRtTarget`: redirección del destino de render para hospedar apps en Modo PC.
* La restauración de orientación ya está resuelta en el sistema: `flexLockEnter`,
  el apagado y la suspensión ponen `gLand=false` explícitamente
  (`FlexOS_Ultra_Power.h`).

### 1.7 Táctil

`struct Touch T` (down/pressed/released/tap/moved, cuatro swipes, x/y/dx/dy) y
`flexPollTouch()` en cada vuelta de `loop()`. **Multitáctil real sí existe**:
`gtPollMulti()` lee hasta `KB_MAXPOINTS` contactos del GT911 con id por punto, y
devuelve `-1` cuando el dato no es fiable — el sistema ya distingue "no hay
dato" de "cero dedos", y esa distinción se mantiene.

### 1.8 Almacenamiento

`FlexOS_FS.cpp/.h` sobre LittleFS: listar, crear, borrar, renombrar, papelera,
lectura por desplazamiento, escritura atómica, y una familia `flexFsPriv*`
reservada a Flex Vault. La partición de datos es `spiffs` con **11 136 KB**
(`flexos_ultra_usb.csv`), la de aplicación `app0` con 5 120 KB.

### 1.9 Ciclo de vida de apps del sistema

`FlexOS_Ultra_AppFramework.h`: `APP_REG[APP_N]` con `enter()`, `tick()`, flags
(`APP_CUSTOM_HEADER`, `APP_OWN_TOUCH`, `APP_LAND`, `APP_FLEX`, `APP_BG_KEEP`) y
`AppHooks` (`backLayer`, `backScreen`, `suspend`, `resume`, `close`, `saveSess`,
`loadSess`, `bgWork`, `shed`, `dirty`). Flex Store es la app 17.

### 1.10 Memoria, watchdog, reloj

* `FlexOS_Mem.cpp/.h` — lógica **pura** de presupuesto de PSRAM (cortes 10/6/5 MB,
  fragmentación, veredicto de apertura, avisos con enfriamiento), ya probada en
  el PC con sanitizers.
* Watchdog: `flexFeedWdt()` en `loop()`; el TWDT **lo alimenta el sistema**, no
  las apps.
* Reloj monotónico: `millis()` y `micros()` ya se usan para medir cuadros
  (`dgFrame`, transiciones). No hay `esp_timer` expuesto a nadie.

### 1.11 Notificaciones

Isla dinámica con cola única (`gNotifs`, `notifPush`) y un envoltorio de
sistema `sysNotify(title, sub)`. **Se reutiliza; no se crea una segunda capa.**

### 1.12 Permisos: el hueco real

`FlexPkgPermission` es hoy una máscara de 8 bits que sale **sólo del manifest**
(`permissionBit()` en `parseManifest`). El único consumidor es
`parseAction()` de `flex-ui-1`, que exige `FLEXPERM_NOTIFICATIONS` para aceptar
una acción `notify`.

Es decir: **hoy un permiso existe porque la app lo escribió en su propio
manifest.** No hay grant, no hay firma de Flex Store sobre los permisos, no hay
comprobación por llamada. Éste es el agujero que el paso 5 cierra.

### 1.13 Lo que NO existe (y hace falta)

| Falta | Consecuencia hoy |
|---|---|
| Ejecución de lógica de app | una app sólo puede enseñar pantallas fijas |
| Presupuesto de instrucciones/tiempo | no aplica: no hay ejecución |
| Gestor de ciclo de vida por app | abrir = cargar JSON; cerrar = `memset` |
| Render 2D para apps | sólo `text` y `button` colocados a mano |
| Almacenamiento privado por app | ninguno |
| Grants firmados / permisos aplicados | el manifest se cree a sí mismo |
| Servicios privilegiados (métricas, PSRAM, stress) | ninguno |
| Registro persistente de instalación | sólo el manifest activo; sin hash ni estado |
| Detener / desinstalar con garantías | `flexPkgUninstall` borra el árbol, sin parar nada |
| SDK, empaquetador, firmador, validador | **no existen en este repositorio** |

---

## 2. Arquitectura elegida

### 2.1 La decisión: bytecode propio `flex-app-v1`, NO WebAssembly

Se evaluó WebAssembly (WAMR, `wasm-micro-runtime`, Apache-2.0) como primera
preferencia, tal y como pide el enunciado. Se descarta por razones concretas de
**este** proyecto, no por gusto:

1. **Modelo de compilación.** `FlexOS_Ultra.ino` se compila en **Arduino IDE**
   como un sketch: `.ino` + `.h` encadenados + `.cpp` sueltos en la carpeta del
   sketch. WAMR es un componente ESP-IDF con CMake, cientos de archivos, y su
   integración soportada es `idf_component_register`. Meterlo en un sketch de
   Arduino IDE exigiría convertirlo a librería Arduino a mano y mantener ese
   fork. No es una dependencia: es un segundo proyecto.
2. **Presupuesto.** El intérprete clásico de WAMR ronda 85–100 KB de flash más
   el runtime de módulo; la partición `app0` son 5 120 KB y el sketch ya la usa
   en buena parte. Cabría, pero el coste real no es el flash: es el
   mantenimiento de un fork y la superficie de ataque de un intérprete que este
   proyecto no puede auditar entero.
3. **Lo que de verdad hace falta no lo da Wasm de serie.** El requisito central
   es **presupuesto de instrucciones y de tiempo por tick con recuperación
   limpia del control**. Wasm no tiene "fuel" estándar: hay que instrumentar el
   bytecode o parchear el intérprete. Si de todas formas hay que tocar el
   intérprete para el presupuesto, el argumento de "usar un estándar sin tocar
   nada" se cae.
4. **Verificabilidad.** El proyecto prueba su código en el PC con ASan/UBSan.
   Un VM propio de ~1 500 líneas se puede validar entero — validador estático,
   trampas, límites — con la misma batería. Un intérprete Wasm de terceros no
   se valida: se confía.

**Se implementa por tanto la opción 2 del enunciado: bytecode propio, mínimo y
documentado.** El documento de formato es
`FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md`.

No se llama sandbox a un parser JSON: `flex-app-v1` es una máquina de pila con
memoria lineal acotada, sin punteros, sin acceso a globals del firmware, con
validación estática antes de ejecutar y comprobación de límites en ejecución.

### 2.2 Piezas nuevas y dónde vive cada una

```
                       PUROS (sin Arduino, probados en el PC con ASan/UBSan)
  ┌───────────────────────────────────────────────────────────────────────┐
  │ FlexOS_PkgCore.cpp    parseo + validación completa de FLXP v1         │
  │                       (lector/escritor abstractos: en la placa        │
  │                        LittleFS, en el PC memoria)                    │
  │ FlexOS_AppGrant.cpp   grant binario FLXG v1: campos + ECDSA P-256     │
  │ FlexOS_AppVM.cpp      máquina flex-app-v1: validador + intérprete     │
  │ FlexOS_AppHost.cpp    FlexAppManager: estados, presupuestos,          │
  │                       permisos por llamada, despacho de syscalls      │
  └───────────────────────────────────────────────────────────────────────┘
                                     │  (vtable de host: punteros a función)
                                     ▼
  ┌───────────────────────────────────────────────────────────────────────┐
  │ FlexOS_Package.cpp    (modificado) usa PkgCore + LittleFS + registro  │
  │ FlexOS_AppHost_Bridge.h  (nuevo, se incluye en el .ino tras Gfx/Touch)│
  │                       implementa la vtable con fillRect/drawText/     │
  │                       gLand/sysNotify/flexFs*/T, y la vista de app    │
  └───────────────────────────────────────────────────────────────────────┘
```

**Ninguna app recibe un puntero.** Todo cruce app→sistema es una `syscall` con
identificador numérico, aridad fija, argumentos `int32` y comprobación de
permiso **en el punto de la llamada**.

### 2.3 Extensión compatible del `.flexpkg`

Sólo dos cambios, los dos compatibles hacia atrás:

1. `manifest.runtime` acepta `"flex-app-v1"` además de `"flex-ui-1"`.
   Con `flex-app-v1` el `entry` apunta a un `.flxb` y el manifest lleva un
   bloque `limits` ampliado (memoria lineal, instrucciones/tick, µs/tick,
   comandos de dibujo/frame, cuota de almacenamiento).
2. **Trailer de grant.** Los bytes reservados 56..59 de la cabecera pasan a ser
   `grantLen (u32)`, con valor `0` (paquete sin grant, comportamiento idéntico
   al de hoy) o exactamente `FLEXGRANT_BYTES`. Los bytes 60..63 siguen
   reservados a cero. El grant va **después** de la firma, así que **no entra**
   en `signedHash`: por eso puede referirse al hash del paquete sin morderse la
   cola.

Un firmware viejo que reciba un paquete con grant lo rechaza por "bytes
reservados inválidos" — falla **cerrado**, que es lo correcto.

### 2.4 Permisos reales

* El manifest **declara**; el grant **concede**. Un permiso privilegiado sin
  grant válido no existe, aunque esté escrito en el manifest.
* El grant es un bloque binario de tamaño fijo (sin JSON, sin canonicalización
  ambigua) firmado con la **clave pinneada de Flex Store** — la misma constante
  que ya valida el catálogo (`CATALOG_PUBLIC_KEY`).
* Se comprueban, en este orden: magia y versión, propósito, `packageId`,
  `versionName` + `versionCode`, `packageSha256` **del paquete realmente
  instalado**, `developerKeySha256`, coherencia máscara↔contador de permisos,
  validez temporal si el reloj del sistema es fiable, y por último la firma.
* La verificación criptográfica completa ocurre **al instalar** y **en cada
  arranque de la app**. La comprobación **por llamada** consulta la máscara
  resultante de esa verificación, atada a ese lanzamiento concreto: una app sin
  grant válido no puede llamar a un servicio privilegiado ni una sola vez.
* `storage.app`, `display.landscape` y `display.exclusive` también pasan por el
  grant. Una app **sin ningún grant** sigue funcionando: dibuja, recibe toques,
  usa temporizadores y su memoria lineal. Lo que no hace es medir el sistema.

### 2.5 Presupuestos y cesión cooperativa

* `onTick` recibe un presupuesto **doble**: N instrucciones y M microsegundos.
  El contador de tiempo se consulta cada `FLEXVM_TIME_CHECK` instrucciones para
  no pagar un `micros()` por opcode.
* Agotar el presupuesto **no es un error**: la VM sale con `YIELD` y el sistema
  reanuda en el mismo punto en el tick siguiente (cesión cooperativa real, con
  el PC y la pila conservados).
* Excederse de forma **abusiva** (superar el presupuesto ampliado de gracia, o
  hacerlo N ticks seguidos) sí es un error: `FLEXAPP_STOP_BUDGET`, la app pasa a
  `FLEXAPP_ST_STOPPED_SECURITY`, se dice el motivo y **Flex OS no se reinicia**.
* El watchdog lo sigue alimentando `loop()`. La VM nunca lo toca. No hay
  `delay()` en el runtime.
* Sin reservas por frame: memoria lineal, pila, globals, constantes y el buffer
  de texto se reservan **una vez** al arrancar la app y se reutilizan.
* Una app **no crea tareas FreeRTOS**. Corre dentro del tick de la app en la
  tarea de UI, como cualquier otra app del sistema.

### 2.6 Cómo se preserva `flex-ui-1`

* `FlexOS_Runtime.cpp/.h` **no se tocan**.
* `parseManifest` sigue aceptando `"flex-ui-1"` con exactamente las mismas
  reglas; el `smokeEntrypoint` de `flex-ui-1` sigue siendo el mismo.
* `storeOpenInstalled()` bifurca por `info.runtime`: `flex-ui-1` va al camino de
  siempre; `flex-app-v1` va al nuevo. Un paquete `flex-ui-1` instalado antes de
  este cambio se abre igual, sin reinstalar.
* El campo `runtime` se añade a `FlexPkgInfo` **al final** de la estructura, y
  el manifest de una app vieja da `runtime = "flex-ui-1"` por defecto si no
  estuviera presente… salvo que hoy ya es obligatorio, así que en la práctica el
  valor siempre existe.
* Hay una prueba de host dedicada a esto: "una app `flex-ui-1` existente sigue
  abriendo".

### 2.7 Tamaño y memoria esperados

Estimaciones de diseño, **no medidas** (no hay toolchain ESP32-P4 en este
entorno; ver §5):

| Pieza | Flash aprox. | RAM/PSRAM |
|---|---|---|
| `FlexOS_AppVM.cpp` (validador + intérprete) | 12–18 KB | 0 estático |
| `FlexOS_AppHost.cpp` (manager + syscalls) | 10–14 KB | ~1,5 KB estático |
| `FlexOS_AppGrant.cpp` | 3–5 KB | 0 estático |
| `FlexOS_PkgCore.cpp` (movido, no añadido) | ≈ neutro | 0 estático |
| `FlexOS_AppHost_Bridge.h` (UI) | 8–12 KB | ~1 KB estático |
| **Total añadido** | **~35–50 KB** | **~2,5 KB estático** |

Por app en ejecución (todo en PSRAM, todo con techo duro):

| Región | Techo por defecto |
|---|---|
| Código `.flxb` | 128 KB |
| Memoria lineal | 256 KB (máx. manifest 1 MB) |
| Pila de operandos | 1 024 slots (4 KB) |
| Marcos de llamada | 64 |
| Globals | 256 (1 KB) |
| Constantes | 64 KB |
| Buffer de texto | 512 B |
| Reserva PSRAM privilegiada | 8 MB, y nunca por encima del veredicto de `FlexOS_Mem` |

Con 32 MB de PSRAM y `fb/bbuf/lockBuf/homeBuf` ocupando 3 MB, el techo por app
es holgado y sigue sujeto al presupuesto global existente.

### 2.8 Licencias

Cero dependencias nuevas en el firmware. La única incorporación de terceros es
**cJSON (MIT)** vendorizada en `tests/host/vendor/` **sólo para las pruebas de
host**, porque en la placa cJSON ya viene con el core de ESP32 y las pruebas
tienen que ejercitar exactamente el mismo camino de código. No entra en el
binario de la placa.

---

## 3. Archivos que se crean o se modifican

### Nuevos (en la raíz **y** en `FlexOS_Ultra/`)

| Archivo | Papel |
|---|---|
| `FlexOS_PkgCore.h/.cpp` | validación pura de FLXP v1 + grant trailer |
| `FlexOS_AppGrant.h/.cpp` | grant FLXG v1: campos y firma |
| `FlexOS_AppVM.h/.cpp` | máquina `flex-app-v1` |
| `FlexOS_AppHost.h/.cpp` | `FlexAppManager` + `FlexAppContext` + syscalls |
| `FlexOS_AppHost_Bridge.h` | vtable real y vista de app en el sketch |

### Modificados

| Archivo | Cambio |
|---|---|
| `FlexOS_Package.h/.cpp` | usa `PkgCore`; runtime en `FlexPkgInfo`; registro `.record`; directorio privado `data/`; grant; desinstalación segura |
| `FlexOS_Store_Bridge.h` | bifurca por runtime al abrir; parar app; ciclo de vida |
| `FlexOS_Ultra.ino` | incluye el puente nuevo y despacha su tick |
| `tests/host/Makefile` | tres baterías nuevas + cJSON vendorizado |
| `tests/host/ino_extern_stubs.cpp` | dobles de las funciones nuevas |
| `docs/FLEXPKG_RUNTIME_P4.md` | dice qué cambió |

### Nuevos fuera del firmware

`sdk/` (Node.js, sin dependencias externas): ensamblador, empaquetador,
firmador, emisor de grants, validador de manifest, plantilla y app de ejemplo.
`FlexOS_Ultra/docs/FLEX_APP_V1_BYTECODE.md` y
`FlexOS_Ultra/docs/FLEXBENCH_INTEGRATION.md`.

---

## 4. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Refactorizar `FlexOS_Package.cpp` rompe la instalación que hoy funciona | la API pública (`flexPkgInstall`, `flexPkgList`, …) no cambia de firma; la lógica se mueve tal cual y pasa a estar cubierta por pruebas de host que antes **no existían** |
| Un bug del intérprete cuelga el sistema | validación estática antes de ejecutar (opcodes, rangos, destinos de salto sobre fronteras de instrucción reales) + comprobación de límites en ejecución + presupuesto doble + trampa limpia; ASan/UBSan sobre bytecode aleatorio en la batería |
| Landscape rompe launcher / teclado / retorno | la orientación la pone y la restaura **el gestor**, no la app; se restaura en `stop` por cualquier vía (salir, cerrar forzado, error, cancelación), y hay prueba dedicada |
| Fugas al abrir y cerrar apps | todo lo de una app cuelga de un único bloque de reservas contabilizado; prueba de 25 ciclos abrir/cerrar comparando contadores de reserva |
| Un grant de otra app o de otra versión | el grant ata `packageId` + `versionCode` + `versionName` + `packageSha256` + `developerKeySha256`; hay una prueba por cada campo alterado |
| Reloj no fiable ⇒ grants "caducados" | si el sistema aún no tiene hora de red, la ventana temporal **no se aplica** y se anota; nunca se concede de más por reloj, sólo se omite el chequeo temporal |
| CPU stress bloquea la interfaz | se ejecuta por *slices* con cesión entre ellos, cancelación inmediata y techo de duración; nunca un bucle infinito |
| Reserva de PSRAM agota el sistema | techo por app + veredicto de `FlexOS_Mem` + devolución limpia de `NULL`; nunca se intenta reservar toda la PSRAM |
| Temperatura inventada | si el SoC/SDK no expone lectura real, el servicio devuelve "No disponible". No se sintetiza un número |
| Divergencia SDK ↔ firmware | el SDK escribe los mismos bytes que el firmware valida, y hay vectores dorados compartidos |

---

## 5. Límite honesto de verificación en este entorno

En este contenedor hay `g++ 13`, `python3`, `node 22` y `openssl`.
**No hay `arduino-cli` ni el toolchain `riscv32-esp-elf`**, así que:

* **No se compila el firmware para ESP32-P4** y no se afirma que compile ahí.
* Lo que sí se ejecuta: `make ino` (el sketch **entero** con `g++ -fsyntax-only`
  contra los dobles, más `check_protos.py` y `check_wiring.py`) y la batería de
  host con ASan/UBSan.
* **No hay placa.** No se ha grabado ni probado nada en hardware, y nada de este
  trabajo debe leerse como una medición real de FPS, temperatura o rendimiento.
