# Navegador de Flex OS Ultra

Guía completa: qué es, cómo se compila, cómo se despliega, cómo se
prueba y qué **no** hace.

---

## 1. Qué es

La app *Navegador* (posición 7 del escritorio) era una maqueta: pintaba
una barra de direcciones falsa con el texto `https://`, tres pestañas
decorativas y un globo. Ahora es un navegador funcional con una
**arquitectura híbrida**:

```
   ┌─────────────────────────── Flex OS ────────────────────────────┐
   │  interfaz · táctil · teclado · pestañas · historial ·          │
   │  favoritos · caché · páginas flex:// · reproductor             │
   │                                                                 │
   │        FlexOS_BrowserApp.cpp  ·  FlexOS_Browser_Bridge.h        │
   │                        │                    ▲                  │
   │              eventos   │                    │  bandas JPEG      │
   └────────────────────────┼────────────────────┼──────────────────┘
                            │   WebSocket (wss)  │
                            ▼      FBP/1         │
   ┌─────────────────── servicio remoto (server/) ───────────────────┐
   │  Node.js + Playwright + Chromium de verdad                      │
   │  guardián anti-SSRF · sesiones aisladas · límites               │
   └─────────────────────────────────────────────────────────────────┘
```

**Por qué híbrida.** Un motor web moderno son cientos de MB de código y
necesita cientos de MB de RAM. El ESP32-P4 tiene 32 MB de PSRAM en el
mejor caso; el ESP32 clásico de Flex OS Pro, unos 300 KB de SRAM (y
150 KB ya se los lleva el framebuffer). No es cuestión de optimizar: no
cabe. Lo que sí cabe, y va sobrado, es decodificar JPEG y dibujar.

---

## 2. Ficheros

| Fichero | Qué es | Dónde compila |
|---|---|---|
| `FlexOS_JPEG.h/.cpp` | Decodificador JPEG baseline, por filas de MCU, con escalado 1/1…1/8 | placa **y** PC |
| `FlexOS_Browser.h` | API, tipos, protocolo FBP/1, capacidades, presupuestos | ambos |
| `FlexOS_Browser.cpp` | Núcleo **puro**: omnibox, validación de URL, páginas `flex://`, codec, SHA-1/base64 | ambos |
| `FlexOS_BrowserApp.cpp` | Lado dispositivo: memoria, persistencia, WebSocket, decodificación, interfaz, gestos | solo placa |
| `FlexOS_Browser_Bridge.h` | Puente con las primitivas `static` del `.ino` + teclado del omnibox | solo placa |
| `server/` | Servicio remoto | Node.js |
| `tests/` | Pruebas de host | PC |

Cambios en ficheros que ya existían:

* `FlexOS_FS.h/.cpp` — se añaden `flexFsReadBin` / `flexFsWriteBin`. Las
  funciones de texto no valen para el historial y los favoritos, que son
  registros binarios con bytes 0 dentro.
* Los tres `.ino` — se sustituye `navEnter()` por `navEnter()/navTick()`
  reales, la app pasa a `APP_FLEX | APP_OWN_TOUCH`, se llama a
  `flexBrowserBegin()` en `setup()` y `appClose()` libera los recursos
  del navegador. **No se ha tocado nada más**: ni OTA, ni arranque, ni
  particiones, ni las otras quince apps.

---

## 3. Compilar el firmware

Mismo entorno de siempre (no cambia nada respecto al que ya usas):

| | Ultra | Ultra S3 | Pro |
|---|---|---|---|
| Placa | ESP32P4 Dev Module | ESP32S3 Dev Module | ESP32 Dev Module |
| CPU | 360 MHz | 240 MHz | 240 MHz |
| Flash | 16 MB QIO 80 MHz | 16 MB QIO 80 MHz | 4 MB |
| PSRAM | Enabled | OPI PSRAM | *(no tiene)* |
| Partición | cualquiera con zona de datos | 16M (3 MB APP / 9,9 MB FATFS) | Minimal SPIFFS (1.9 MB APP con OTA) |
| Core | arduino-esp32 v3.2.0 | arduino-esp32 v3.x | arduino-esp32 v3.x |

**Los ficheros nuevos no hay que añadirlos a mano**: el IDE de Arduino
compila todos los `.cpp` que estén junto al `.ino`. Copia a la carpeta
del sketch, además del `.ino` que vayas a usar:

```
FlexOS_OTA.h  FlexOS_OTA.cpp  FlexOS_OTA_Bridge.h
FlexOS_FS.h   FlexOS_FS.cpp
FlexOS_JPEG.h FlexOS_JPEG.cpp
FlexOS_Browser.h  FlexOS_Browser.cpp  FlexOS_BrowserApp.cpp  FlexOS_Browser_Bridge.h
```

### Interruptores de compilación

Todos con el mismo patrón que `KIOSK_ON` o `FLEXOS_OTA_ON`: se pueden
bajar a 0 de uno en uno para aislar un problema en la placa.

| Macro | Por defecto | A 0 |
|---|---|---|
| `FLEXBR_ON` | 1 | La app queda como un aviso; el resto del sistema no se entera |
| `FLEXBR_REMOTE_ON` | 1 | Sin sesión remota: solo páginas internas, historial y favoritos |
| `FLEXBR_TILES_ON` | 1 | Solo fotogramas completos |
| `FLEXBR_MEDIA_ON` | 1 | Sin detección multimedia ni reproductor |
| `FLEXBR_TELEMETRY_ON` | 1 | `flex://about` sin contadores |
| `FLEXBR_REQUIRE_TLS` | 1 | Permite `ws://` sin pedir la opción de desarrollo |

---

## 4. Poner en marcha el servicio

Ver `server/README.md`. Resumen:

```bash
cd server
npm install && npm run browsers
cp config.example.json config.json
node -e "console.log(require('crypto').randomBytes(24).toString('base64url'))"   # token
# ...pégalo en devices[].token
node src/server.js --config config.json
```

En el dispositivo: **Navegador → ⋮ → Ajustes del navegador**

* **Servidor** → `wss://tu-servidor:8443/v1/session`
* **Credencial** → el token
* **Probar el servidor** → hace un `GET /v1/health` y dice si responde

---

## 5. Qué se puede hacer con él

**Barra superior**: atrás, adelante, recargar/detener, omnibox con
candado HTTPS y menú. Barra de pestañas cuando el lienzo da de sí
(≥360 px de ancho) y la memoria permite más de una.

**Omnibox** — el orden de resolución es exactamente este:

1. Vacío → no se navega.
2. `flex://…` → página interna, **nunca** sale hacia el servidor.
3. `http://` / `https://` → se valida y se navega.
4. Cualquier otro esquema (`file:`, `javascript:`, `data:`, `ftp:`,
   `about:`, `chrome:`…) → **bloqueado**.
5. Dominio sin esquema (`example.com`) → se completa con `https://`.
6. IP o nombre de red local → **bloqueado** salvo con la opción
   explícita *Permitir red local*.
7. Todo lo demás → búsqueda, con el texto codificado entero (`%20`, no
   `+`; UTF-8 byte a byte).

**Páginas internas**: `flex://newtab`, `history`, `bookmarks`,
`settings`, `downloads`, `offline`, `about`.

**Menú**: nueva pestaña, añadir/quitar de favoritos, favoritos,
historial, descargas, reproducir vídeo, teclado para la página, ajustes,
acerca de, cerrar.

**Gestos**: tocar = clic en la página; arrastrar = desplazamiento
(agrupado, un mensaje cada ~50 ms); el botón *atrás* del sistema
retrocede en el historial y solo cierra la app cuando ya no hay a dónde
volver.

**Teclado**: el del sistema, tal cual, con sus cuatro capas y sus
tamaños configurables. Se abre solo cuando la página remota pone el foco
en un campo de texto. En ese modo **lo que se teclea no se guarda en el
dispositivo**: va directo al servicio, así que una contraseña escrita en
un formulario no deja rastro en la RAM del navegador.

**Modo PC / DeX**: la app es `APP_FLEX`, o sea que maqueta contra el
lienzo real. Dentro de una ventana de DeX se redistribuye sola: la barra
de pestañas desaparece si la ventana es estrecha y los botones mantienen
su tamaño táctil mínimo.

---

## 6. Presupuesto de memoria (exacto, por construcción)

| | Flex OS Ultra (P4) | Ultra S3 | Pro (ESP32) |
|---|---|---|---|
| Buffer de recepción | 196 KB *(PSRAM)* | 132 KB *(PSRAM)* | **32 KB (interna)** |
| Caché de página (keyframe) | 192 KB *(PSRAM)* | 128 KB *(PSRAM)* | — *(sin PSRAM: se pide de nuevo)* |
| Historial | 70 KB · 120 entradas | 47 KB · 80 | **14 KB · 24** |
| Favoritos | 35 KB · 60 | 23 KB · 40 | **7 KB · 12** |
| Pestañas (estático) | 3,6 KB · 6 | 2,4 KB · 4 | 0,6 KB · 1 |
| Fila de escalado | 1 KB | 1 KB | 1 KB |
| Trabajo del decodificador (transitorio) | ~12 KB | ~12 KB | ~6 KB |
| Heap interno exigido al abrir | 46 KB | 46 KB | **99 KB** |
| Heap interno exigido al conectar | 46 KB | 46 KB | 46 KB |

Todo lo marcado *(PSRAM)* sale de la PSRAM y **no** toca el heap
interno. En Pro todo es heap interno, y por eso el umbral de apertura es
mucho más alto: es la suma de lo que el navegador va a reservar más lo
que después necesita mbedTLS.

**La caché de página es el JPEG comprimido, no el mapa de bits.** Un
fotograma de 480×800 en RGB565 son 768 KB; el mismo en JPEG útil, 20–60 KB.
Guardando el comprimido y volviendo a decodificar al repintar (cerrar un
menú, salir del reproductor) se ahorran esos 768 KB. El coste es CPU en
un repintado ocasional, no por fotograma.

### Tamaño del código

Medido en el PC (`-Os`, x86-64) sobre los tres módulos nuevos. **Es
indicativo, no la cifra final de la placa**: el compilador de Xtensa/
RISC-V genera otro código. La cifra real de flash hay que leerla en la
salida del IDE al compilar.

| | texto | datos | BSS |
|---|---|---|---|
| Ultra (P4) | ~56,8 KB | 488 B | 9,8 KB |
| Ultra S3 | ~56,8 KB | 488 B | 8,6 KB |
| Pro | ~56,2 KB | 488 B | 6,8 KB |

---

## 7. Pruebas

### 7.1 Pruebas de host (no hace falta la placa)

```bash
cd tests/host
make                 # las cinco baterías, perfil Ultra
make all-boards      # el código de dispositivo en los tres perfiles
```

| Batería | Qué comprueba | Comprobaciones |
|---|---|---|
| `test_jpeg` | El decodificador contra **libjpeg-turbo**, y **que quepa en la pila del `loopTask`** (decodifica 480×800 dentro de un hilo con 16 KB de pila). 4:4:4, 4:2:0, 4:2:2, 4:4:0, gris, dimensiones impares, marcadores de reinicio, escalado 1/2·1/4·1/8, progresivo rechazado, 70 truncamientos, datos corruptos, cancelación, fallo de reserva. Con ASan+UBSan. | 193 |
| `test_browser` | Omnibox (los 7 casos, esquemas prohibidos, IP en todas sus formas, inyección de control, límites), codec FBP/1 byte a byte con todos los truncamientos, SHA-1 contra los vectores del RFC 3174, `Sec-WebSocket-Accept` contra el vector del RFC 6455. Con ASan+UBSan. | 382 |
| `test_app` | Ciclo de vida (Enter reserva / Exit libera **todo**, tres vueltas), el dibujo nunca sale del área de contenido, capacidades medidas con motivo legible, omnibox y teclado con UTF-8, persistencia agrupada. En los tres perfiles de placa. | 41 |
| `test_bridge` | El puente compila y funciona contra un `.ino` simulado: geometría del teclado, recorte de `brHostBlitRow`, **el teclado se dibuja visible** (se cuentan píxeles en su franja) a pantalla completa y en Modo PC/DeX, **al cerrarse no deja ni un píxel** por debajo de `WIN_BOT`, **con el teclado abierto un arrastre no mueve Ajustes**, y el navegador no deja instalada una banda de recorte estrecha. | 31 |
| `test_net` | **El transporte de verdad**: `WiFiClient` sobre sockets TCP POSIX, `xTaskCreatePinnedToCore` lanzando hilos reales, semáforos y colas reales, y al otro lado un servidor WebSocket que habla FBP/1 y **manda el primer FRAME troceado con una pausa de 400 ms en mitad del mensaje**. Comprueba el apretón de manos, el HELLO, el NAVIGATE, la recepción del JPEG, su decodificación y su volcado — sin una sola reconexión. | 8 |

**Resultado actual: 655 comprobaciones, 0 fallos.**

`test_net` es la única batería de host que ejercita `wsHandshake()`,
`wsReadMessage()` y `brNetTask()` completos. Es la que reproduce —y
cierra— el fallo de «se conecta, autentica y no llega ni un frame»: con
el lector antiguo da `frames ok=0 malos=7`; con el actual,
`frames ok=1 malos=0 reconexiones=0`.

Precisión del decodificador frente a libjpeg-turbo, medida en **pasos de
RGB565** (0 = idéntico, 1 = el mínimo representable en pantalla):

```
grad444    64x48    err medio 0,220  max 1
grad420    64x48    err medio 0,448  max 1
odd444     63x47    err medio 0,209  max 1
odd420     61x45    err medio 0,468  max 1
page480   480x800   err medio 0,320  max 4
page240   240x320   err medio 0,074  max 1
gray       40x24    err medio 0,000  max 0
restart    80x64    err medio 0,000  max 0     (DRI=3)
samp422    64x24    err medio 0,000  max 0
samp440    24x64    err medio 0,000  max 0
rst1       48x48    err medio 0,000  max 0     (DRI=1)
```

El máximo de 4 pasos en `page480` está en el borde de un bloque azul
saturado: libjpeg-turbo interpola el croma («fancy upsampling») y este
decodificador replica la muestra vecina, que es lo que hacen los
decodificadores de microcontrolador (la mitad de CPU y sin una fila
extra de croma en memoria).

### 7.2 Pruebas del servicio

```bash
cd server && npm test
```

* `protocol.test.js` — 24 casos, desplazamientos byte a byte.
* `guard.test.js` — el guardián anti-SSRF rango por rango, incluidos los
  bordes de cada CIDR y las formas raras de escribir una IP.
* `e2e.test.js` — **extremo a extremo**: levanta el servicio con
  Chromium de verdad, sirve una página en local, se conecta hablando
  FBP/1, navega, toca un botón, se desplaza, intenta cuatro destinos
  internos, y **decodifica cada banda JPEG recibida con el decodificador
  del propio firmware**.

Resultado real de esa última prueba en este repositorio:

```
banda 0: y=0   h=160  7701 B -> OK 480 160 3 1
banda 1: y=160 h=160  2556 B -> OK 480 160 3 1
banda 2: y=320 h=160  1369 B -> OK 480 160 3 1
banda 3: y=480 h=160  1345 B -> OK 480 160 3 1

http://169.254.169.254/latest/meta-data/ -> bloqueado (enlace local / metadatos de nube)
http://10.0.0.1/admin                    -> bloqueado (privada)
file:///etc/passwd                       -> bloqueado (esquema no permitido)
http://127.0.0.1:1/                      -> bloqueado (loopback)
```

Para que `e2e.test.js` funcione hace falta compilar la herramienta que
usa (si no, se salta sola):

```bash
make -C tests/host tools
```

### 7.3 Pruebas en hardware (reproducibles)

Estas **no** se han podido ejecutar en este entorno: no hay placa. Están
descritas para poder repetirlas exactamente, y hay un hueco para anotar
lo medido.

| # | Prueba | Pasos | Qué debe pasar |
|---|---|---|---|
| 1 | Arranque limpio | Flashear · abrir Navegador | Se abre `flex://newtab` con «Falta configurar el servidor en Ajustes» |
| 2 | Configuración | Ajustes → Servidor y Credencial → *Probar el servidor* | «Servidor accesible» |
| 3 | Primera carga | Omnibox → `example.com` → Ir | Texto e imágenes legibles en menos de ~4 s |
| 4 | Búsqueda con espacios | `como hacer pan` | Resultados de DuckDuckGo |
| 5 | Búsqueda con Unicode | `qué es el mañana` | La URL lleva `%C3%B1`, no basura |
| 6 | Enlaces | Tocar un enlace | Navega; el omnibox cambia |
| 7 | Desplazamiento | Arrastrar arriba/abajo | Se desplaza; el dedo se siente atendido |
| 8 | Atrás / recargar | Botones de la barra | Vuelve y recarga |
| 9 | Redirección | Un sitio que redirige (`http://` → `https://`) | El candado pasa a HTTPS |
| 10 | Formulario | Tocar un campo de texto | El teclado se abre **solo** |
| 11 | Pestañas | `+` en la barra de pestañas | Se abre una nueva; el límite lo pone la memoria |
| 12 | Favoritos | ⋮ → Añadir a favoritos → `flex://bookmarks` | Aparece; sobrevive al reinicio |
| 13 | Historial | `flex://history` | Sin duplicados, la más reciente arriba |
| 14 | Borrar datos | Ajustes → Borrar todos los datos | Historial y favoritos vacíos, y siguen vacíos tras reiniciar |
| 15 | **Wi-Fi perdido** | Desconectar el router con la página cargada | Aparece «Sin conexión»; al volver, reconecta solo (retroceso exponencial hasta 16 s) |
| 16 | **Servidor caído** | Parar el servicio | Error claro y reintentos; el sistema no se bloquea |
| 17 | **Sesión caducada** | Dejar la app 11 min sin tocar | «La sesión caducó por inactividad»; recargar reconecta |
| 18 | **Fotograma corrupto** | Con `tc qdisc netem corrupt 2%` en el servidor | Bandas mal → el dispositivo pide un fotograma completo; el contador de «frames corruptos» de `flex://about` sube; no hay bloqueo |
| 19 | **Entradas maliciosas** | Probar en el omnibox `file:///etc/passwd`, `javascript:alert(1)`, `http://169.254.169.254/`, `http://10.0.0.1/`, `https://a:b@banco.com/`, una URL de 600 caracteres | Todas bloqueadas con su motivo; ninguna llega al servidor |
| 20 | **Memoria** | Abrir y cerrar el navegador 20 veces seguidas mirando `flex://about` | El heap libre vuelve al mismo valor cada vez (±2 KB de fragmentación) |
| 21 | Cerrar libera | Cerrar con el botón atrás | El heap vuelve al valor previo a abrir |
| 22 | Modo PC / DeX | Abrirlo en una ventana y redimensionarla | Se remaqueta; la barra de pestañas aparece/desaparece por el ancho |
| 23 | Multimedia | Una página con `<video>` sin DRM → ⋮ → Reproducir vídeo | Se abre el reproductor; play/pausa y barra responden; dice «sin audio» |
| 24 | Multimedia no disponible | Lo mismo en Flex OS Pro | «El reproductor necesita PSRAM» — no un fallo mudo |
| 25 | **Las demás apps y el OTA** | Recorrer las 16 apps y lanzar una búsqueda de OTA | Todo igual que antes |
| 26 | **Diagnóstico de sesión** | Poner una dirección de servidor con un puerto donde no escucha nadie y abrir `example.com` | El área de página **no** se queda en negro: dice «Conexión rechazada por *host*:*puerto*», con contadores a cero |
| 27 | **Credencial inválida** | Cambiar un carácter de la credencial y navegar | «Credencial rechazada por el servicio», y el mismo texto por Serial |
| 28 | **El servicio no manda imágenes** | Servidor accesible pero con Chromium parado | Tras 12 s: «El servicio no envía imágenes de la página», con el número de mensajes recibidos |
| 29 | **Teclado: cierre limpio** | Tocar la barra de direcciones, escribir, pulsar **Ir**; repetir volviendo atrás y guardando ajustes | No queda **ninguna** parte del teclado dibujada — en particular la fila inferior (shift · ?123 · Es · espacio · borrar · Ir) |
| 30 | **Primera carga sin reinicio** | Con el servicio en marcha, `example.com` → Ir | La página se dibuja. **No** debe aparecer pantalla de un solo color ni `PANIC / fatal exception`: eso era el desbordamiento de pila del decodificador (D33) |
| 31 | **Teclado y desplazamiento** | `flex://settings` → tocar «Servidor de navegación» → escribir una dirección larga arrastrando el dedo entre teclas | La lista de Ajustes **no se mueve** ni un píxel mientras el teclado está abierto, y al cerrarlo no hay salto |

**Plantilla para anotar lo medido** (rellenar en la placa):

```
Variante:            Ultra / Ultra S3 / Pro
Firmware:            ______ bytes  (salida del IDE)
Heap libre en Home:  ______ KB
Heap con el navegador abierto:  ______ KB
PSRAM libre:         ______ KB
Latencia de la primera carga (example.com): ______ ms
Bandas por segundo al desplazar:            ______
ms de decodificación por banda (flex://about): ______
Frames descartados en 5 min de uso:         ______
Heap tras 20 aperturas/cierres:             ______ KB
```

---

## 7.4 Diagnóstico del teclado en hardware

El teclado se dibuja desde `brKbRender()` en `FlexOS_Browser_Bridge.h`.
Si en la placa **responde al tacto pero no se ve**, el firmware trae un
diagnóstico que dice exactamente dónde se corta el camino.

**Viene apagado** (`FLEXBR_KBDEBUG 0`) desde que el teclado se confirmó
visible en la ESP32-P4. Para volver a activarlo, pon el `#define` a 1 en
el puente o compila con `-DFLEXBR_KBDEBUG=1`. Con él apagado no queda
nada en el binario.

### Marcas en pantalla

Al dibujarse el teclado aparecen dos cuadrados de 12×12 px:

* **magenta** en la esquina superior izquierda de la franja del teclado,
* **cian** en la esquina inferior derecha.

Se escriben **directamente en el framebuffer** (`fb[y*SCR_W+x]`),
saltándose `px()`, la banda de recorte, `gBuf` y cualquier rotación.
Por eso separan el problema en dos mitades limpias:

| Qué se ve | Qué significa |
|---|---|
| Marcas **y** teclas | Todo bien. |
| Marcas **sí**, teclas **no** | El volcado al panel funciona; el fallo está en las primitivas de dibujo o en la banda de recorte. |
| **Ninguna** marca | El fallo está en el volcado (`flxFlush`/presenter) o en la región que se sube: los píxeles ni siquiera llegan al panel. |

### Traza por Serial (115200 baudios)

Una línea cada 700 ms mientras el teclado esté abierto:

```
[KB] #142 buf=fb(0x48000000) fb=0x48000000 land=0 hosted=0 gAppH=800
     franja=508..799  ky=546  clip=y[508..799] x[0..479]  kbSize=1 45x60
     pixel(28,608)=0x35BA  esperado tecla=0x35BA panel=0x2124  opac=100 glass=0 hiCon=0 estilo=0
```

Cómo leerla:

* **si no sale ninguna línea** → `brKbRender()` no se está ejecutando.
  Mira si `flexBrowserKeyboardOpen()` es cierto y si `navTick` es el
  tick registrado en `APP_REG[7]`.
* **`buf=` distinto de `fb`** → se está dibujando en otro lienzo (el de
  una ventana de Modo PC, por ejemplo) y por eso no llega al panel.
* **`clip=` distinto de la franja** → alguien dejó puesta otra banda de
  recorte y los píxeles se descartan.
* **`pixel(...)` ≠ `esperado tecla`** → el dibujo **no** llegó al
  framebuffer: el problema está antes del volcado.
* **`pixel(...)` = `esperado tecla` pero no se ve nada** → el dibujo sí
  llegó al framebuffer; el problema está en el volcado o en que algo
  reescribe esa franja después.
* **`land=1`** → el motor está en horizontal y las coordenadas se
  reinterpretan; el teclado fuerza vertical, así que esto no debería
  salir nunca.

### Por qué el teclado se repinta en cada vuelta

`navTick()` llama a `brKbRender()` **al final de cada cuadro** mientras
el teclado está abierto, no solo al abrirlo ni solo al pulsar. Es una
decisión deliberada (ver D26): con la versión condicional bastaba con
que un camino de repintado se escapara del análisis para que el teclado
desapareciera y no volviera nunca. Cuesta unas 36 teclas por cuadro y
garantiza que el teclado sea siempre la última capa.

### Si has actualizado y no cambia nada

El navegador son **cuatro ficheros** que se copian a mano a la carpeta
del sketch. Si actualizas solo alguno, antes compilaba igual y fallaba
en silencio. Ahora **no**: hay una guardia de versión que da un error
claro en compilación o en enlace:

```
error: static assertion failed: FlexOS_BrowserApp.cpp y FlexOS_Browser.h
son de versiones distintas: copia otra vez LOS CUATRO ficheros del
navegador a la carpeta del sketch.
```

o, si el que se quedó viejo es `FlexOS_BrowserApp.cpp`:

```
undefined reference to `flexBrVersionGuard_v4_copia_los_4_ficheros_del_navegador'
```

Los cuatro son: `FlexOS_Browser.h`, `FlexOS_Browser.cpp`,
`FlexOS_BrowserApp.cpp` y `FlexOS_Browser_Bridge.h`.

---

## 7.5 Diagnóstico de la sesión de red en hardware

Si el navegador **se queda cargando y no llega ninguna página**, ya no
hay que adivinar: la propia pantalla lo dice, y por Serial hay el
detalle completo.

### En pantalla

Mientras la pestaña activa no tenga ninguna imagen, el área de página
pinta un panel con:

* un punto de color y el estado (`Sin sesión`, `Sin Wi-Fi`,
  `Conectando`, `Autenticando`, `Sesión activa`, `Con errores`);
* la **frase de la etapa actual**, que es la que distingue un caso de
  otro;
* la dirección del servidor configurada (la credencial **nunca** se
  pinta);
* contadores: mensajes recibidos, imágenes dibujadas, errores y KB.

Frases y qué significan:

| Frase | Qué pasa |
|---|---|
| `Sin servidor configurado (flex://settings)` | No hay dirección guardada. |
| `Sin Wi-Fi: conéctate desde Ajustes` | La placa no está asociada. |
| `La dirección del servidor no es válida` | `ws://…`/`wss://…` mal escrito. |
| `Conectando con host:puerto` | TCP en curso. |
| `Conexión rechazada por host:puerto` | Nadie escucha ahí, o un cortafuegos. **El caso más común es que el servicio esté escuchando solo en `127.0.0.1`**: pon `"host": "0.0.0.0"` en `config.json` (el servicio ahora lo avisa al arrancar). |
| `El servicio no aceptó el WebSocket (ruta …)` | Llegó al puerto pero la ruta no es `/v1/session`, o hay un proxy delante. |
| `Autenticando (con/SIN credencial)` | HELLO enviado. Si dice «SIN credencial», no hay token guardado. |
| `Sin respuesta al saludo: revisa la credencial` | 10 s sin WELCOME. Se corta y se reintenta. |
| `Credencial no válida` | El servicio rechazó el token. |
| `Sesión abierta: esperando la página` | Todo bien; falta navegar. |
| `Cargando la página...` | NAVIGATE enviado. |
| `Sin imágenes tras N s (M mensajes)` | El servicio habla pero no manda bandas. Mira el log del servicio. |
| `Imagen no válida del servicio` / `Imagen ilegible (código N)` | Llegó un FRAME que no se pudo interpretar o decodificar. |
| `Se perdió la conexión con el servicio` | Lectura fallida: se cierra y se reconecta con retroceso exponencial. |

### Por Serial (115200 baudios)

Activo por defecto (`FLEXBR_NETDEBUG 1`; se apaga con
`-DFLEXBR_NETDEBUG=0`). Una sesión normal se ve así:

```
[NET] destino=192.168.1.7:8443 ruta=/v1/session tls=0 permitirWs=1
[NET] TCP abierto, negociando WebSocket...
[NET] HELLO enviado: 78 B  vista=480x548  cred=si
[NAV] Autenticando (con credencial)
[NET] WELCOME: sesion aceptada
[NAV] Sesión abierta: esperando la página
[NET] NAVIGATE canal=1 -> https://example.com/ (45 B)
[NAV] Cargando la página...
[NET] primer FRAME: canal=1 480x200 en (0,0) 14452 B
```

Sólo se traza el **primer** frame de cada navegación: una línea por
banda inunda el monitor serie sin aportar nada.

Y un fallo dice exactamente dónde:

```
[NET] destino=192.168.1.7:8443 ruta=/v1/session tls=0 permitirWs=1
[NAV] Conexión rechazada por 192.168.1.7:8443
```

Un aviso especial salta si pasan 12 s desde un NAVIGATE sin recibir una
sola imagen:

```
[NET] AVISO: 12030 ms desde NAVIGATE sin ningun FRAME (estado=4 mensajes=6 bytes=812)
```

`estado=4` es `BRN_READY`: la sesión está viva y el servicio responde —
así que el problema está en el servicio, no en la red. El log del
servicio (`node src/server.js --config config.json`) dirá el resto.

### Nada de esto bloquea la interfaz

Toda la red vive en su propia tarea de FreeRTOS. El hilo gráfico nunca
llama a `connect()`, `read()` ni `write()`: las frases de estado se
escriben desde la tarea de red y el hilo gráfico solo las pinta cuando
cambian, y repintando **solo** el área de página.

---

## 8. Limitaciones reales

Dichas sin rodeos, porque saberlas de antemano evita perder el tiempo:

1. **Hace falta el servicio remoto.** Sin él el navegador funciona, pero
   solo con las páginas internas, el historial y los favoritos. No hay
   motor web en la placa y no lo va a haber.
2. **El vídeo es MJPEG y no tiene audio.** Ninguna de las tres placas
   tiene salida de audio cableada (no hay I2S ni DAC en los `.ino`).
   Sin decodificador de vídeo por hardware, fotogramas JPEG es lo único
   que las tres pueden mostrar. Es una ruta de **prototipo** y está
   etiquetada como tal.
3. **DRM fuera de alcance.** No se elude ninguna protección. Netflix,
   Disney+, Widevine y el contenido protegido **no** funcionan y no se
   intenta que funcionen. YouTube no se promete: no se extraen ni se
   rodean URL protegidas.
4. **Flex OS Pro va muy justo.** Sin PSRAM, con 150 KB de framebuffer y
   ~300 KB de SRAM totales, la sesión remota exige 99 KB de heap interno
   libre al abrir. Es posible que en tu configuración no lo haya: la app
   lo dice con el número exacto en vez de reiniciarse. Sin sesión
   remota, sigue siendo útil (páginas internas, historial, favoritos).
5. **Una sola pestaña sin PSRAM.** Cada pestaña remota es memoria en las
   dos puntas.
6. **No se descargan ficheros.** El dispositivo no tiene sitio ni gestor
   remoto. Las descargas se cancelan y se listan en `flex://downloads`.
7. **Sin IDNA.** Un dominio con acentos (`españa.es`) se manda al
   buscador en vez de inventar la conversión a punycode.
8. **El certificado del servicio no se valida por defecto**
   (`setInsecure()`): protege contra escuchas pasivas, no contra un
   intermediario activo. Cómo fijar un CA está en `server/README.md`.
9. **Las ventanas emergentes se bloquean** y se avisa. Abrir pestañas sin
   pedirlo en una pantalla de 480×800 es insoportable.
10. **No hay perfiles ni sincronización.** No es un Chrome.

---

## 9. Qué garantiza el diseño frente a una página hostil

* **El teclado se dibuja siempre por encima de la app.** El navegador
  repone la banda de recorte a pantalla completa al terminar de pintar,
  el teclado abre la suya propia antes de dibujar, y el puente lo
  repinta después de cada repintado del contenido. Hay un test que
  cuenta píxeles reales en la franja del teclado, porque «se llamó a la
  función de dibujo» y «se ve» no son lo mismo.
* Una página externa **no puede pintar fuera del área de contenido**.
  Es una comprobación, no una convención: `brHostBlitRow` recorta contra
  el rectángulo de la app y contra la banda de recorte del motor
  gráfico, y hay un test (`test_bridge`, `test_app`) que lo verifica
  contando píxeles.
* Una página **no puede provocar un `flex://`**. Los esquemas internos se
  resuelven en el dispositivo, antes de tocar la red, y nunca se envían
  al servicio.
* Una página **no puede falsificar la interfaz**: los títulos y las URL
  que llegan del servicio se limpian de caracteres de control y se
  recortan sin partir UTF-8 antes de dibujarse.
* Los mensajes de error que se muestran son **los del dispositivo**,
  elegidos por código; el texto del servicio no se pinta tal cual.
* Un destino interno se bloquea **dos veces**: en el dispositivo antes de
  enviar, y en el servicio con resolución DNS y comprobación de cada
  redirección.
* Un mensaje de red mal formado no puede desbordar nada: cada lector
  comprueba los bytes disponibles antes de tocarlos, y hay tests con
  todos los truncamientos posibles de cada mensaje.
