# Multitarea real, App Switcher y diagnóstico de memoria

Flex OS Ultra (ESP32‑P4, 480×800, 32 MB de PSRAM) deja de tener un tope fijo
de aplicaciones abiertas: quien decide cuántas caben es la **memoria medida**.
Este documento describe qué hay, dónde vive y — sobre todo — **de dónde sale
cada cifra que el usuario ve**.

## Índice

1. [Arquitectura en tres capas](#1-arquitectura-en-tres-capas)
2. [Ciclo de vida de una app](#2-ciclo-de-vida-de-una-app)
3. [Cómo se mide la memoria](#3-cómo-se-mide-la-memoria)
4. [Presupuesto: qué se abre y qué no](#4-presupuesto-qué-se-abre-y-qué-no)
5. [Qué se suelta, y en qué orden](#5-qué-se-suelta-y-en-qué-orden)
6. [App Switcher (Recientes)](#6-app-switcher-recientes)
7. [Almacenamiento → Detalles de memoria y sistema](#7-almacenamiento--detalles-de-memoria-y-sistema)
8. [Memoria automática](#8-memoria-automática-primero-actuar-avisar-solo-si-hace-falta)
9. [Avisos secundarios](#9-avisos-secundarios)
10. [Modo visual eficiente](#10-modo-visual-eficiente)
11. [Límites conocidos](#11-límites-conocidos)
12. [Wi‑Fi y memoria](#12-wifi-y-memoria-cómo-se-coordinan)

---

## 1. Arquitectura en tres capas

La separación es deliberada y es lo que hace que las reglas se puedan
verificar sin una placa delante:

| Capa | Dónde vive | Qué hace |
|---|---|---|
| **Decidir** | `FlexOS_Mem.h` / `FlexOS_Mem.cpp` | Cortes de 10/6/5 MB, bloque contiguo mínimo, fragmentación, veredicto de apertura y enfriamiento de los avisos. **Lógica pura**: no toca Arduino, ni el SDK, ni reserva un solo byte. |
| **Medir** | `FlexOS_Ultra.ino`, bloque *Gestor de memoria y multitarea* | `heap_caps_*` y el sistema de archivos. Publica una única medida (`memSnap()`) de la que bebe todo lo que se dibuja. |
| **Soltar** | `FlexOS_Ultra.ino`, `memShedSystem()` (junto a `themeChanged`) y los ganchos `shed` de cada app | Libera buffers reconstruibles. Los punteros están repartidos por todo el sketch, así que la acción se define donde se les ve. |

`FlexOS_Mem.cpp` se compila y se ejercita **en el PC** con AddressSanitizer:
`tests/host/test_mem.cpp` (66 comprobaciones). El cableado entre esas reglas y
el sistema real se comprueba en `tests/host/ino_compile.cpp`
(`testMultitareaMemoria`), que corre sobre el sketch entero enlazado.

---

## 2. Ciclo de vida de una app

Los cuatro estados que pide el diseño se expresan con `AppLife` + una marca:

| Estado del diseño | En el motor | Qué conserva |
|---|---|---|
| **ACTIVA** | `ALIFE_RUNNING` | Todo. Es la única que dibuja y recibe toques. |
| **PAUSADA** | `ALIFE_SUSPENDED`, `gAppShed[id] == false` | Estado lógico **y** sus recursos cargados. Volver es instantáneo. |
| **SUSPENDIDA** (*«estado guardado»*) | `ALIFE_SUSPENDED`, `gAppShed[id] == true` | Estado lógico. Los recursos pesados se soltaron y se rehacen al volver. |
| **CERRADA** | `ALIFE_CLOSED` | Nada. Sale de Recientes y libera todo lo suyo. |

`gAppShed[]` se pone **solo cuando la app ha soltado algo de verdad**
(`memShedApp` mide la PSRAM antes y después y además consulta lo que devuelve
el gancho). Es lo que permite que la tarjeta diga «Pausada» o «Estado
guardado» sin inventárselo.

### Contrato por app (`struct AppHooks`)

A los ocho ganchos que ya existían se añaden dos **al final**, para que ningún
inicializador anterior tenga que tocarse:

```c
size_t (*shed)();   // suelta recursos PESADOS y RECONSTRUIBLES sin perder estado
bool   (*dirty)();  // true = la app tiene cambios del usuario sin guardar
```

Metadatos que el sistema lleva por app:

| Dato | Dónde | Cómo se obtiene |
|---|---|---|
| ID y nombre | `APP_REG`, `appName()` | Registro estático de siempre |
| Estado de ciclo de vida | `gAppState[]` | Motor |
| Último uso / orden LRU | `gAppSeenMs[]` | `millis()` al pasar a primer plano |
| Clase de peso | `APP_WEIGHT[]` (tabla `const`, en flash) | Declarada por app |
| Memoria propia | `gAppMem[]` / `gAppMemKnown[]` | **Medida**: PSRAM libre antes y después de su `enter()`/`resume()`, menos lo que devuelve al suspenderse |
| Cambios sin guardar | `appUnsaved()` | `gSessNeedSave[]` + gancho `dirty` de la app |
| Captura de App Switcher | `swTasks[].thumb` | Reducción 480×800 → 150×250, tomada al suspender |
| Puede soltar recursos | gancho `shed` | Declarado por app |

### Estado que guarda y restaura cada app

| App | Qué conserva | Qué suelta al pasar a segundo plano |
|---|---|---|
| Notas | Texto, cursor, selección, scroll, documento, estado del teclado | — (4 KB: soltarlos no cambia nada y arriesga el texto) |
| Paint | Lienzo (en disco), color, herramienta, grosor, scroll | — (2 KB de trazo) |
| Navegador | Pestaña, dirección, historial de sesión, estado de conexión | Caché de fotogramas: **cero imágenes remotas decodificadas** en segundo plano |
| Galería | Carpeta, foto seleccionada, zoom/scroll | Caché de miniaturas decodificadas |
| Multimedia | Archivo, segundo de reproducción, volumen, filtro | Fotograma, descriptor del archivo y buffers |
| Cámara | Modo y ajustes | Buffer del sensor |
| DeX / Modo PC | Disposición de ventanas, tamaños, minimizadas, orden y foco | Lienzo de cada ventana (768 KB c/u) y fondo compuesto |
| Calendario | — (solo muestra el mes en curso) | — |
| Ajustes, Reloj, Calculadora, Clima, Flex Phone, Flex Store | Su estado propio de siempre | — |
| Juegos | Lo que su propio guardado ya soportaba | — |

Las apps sin gancho `shed` **no lo tienen porque no hay nada que soltar**, no
porque falte implementarlo.

---

## 3. Cómo se mide la memoria

Una sola medida publicada (`memSnap()`), con **tres cadencias** y ninguna
dentro de la ruta de dibujo:

| Campo | Fuente | Cadencia |
|---|---|---|
| PSRAM total / libre | `heap_caps_get_total_size` / `..._free_size(MALLOC_CAP_SPIRAM)` | 1 s (`MEM_TICK_MS`) |
| SRAM interna total / libre | ídem con `MALLOC_CAP_INTERNAL` | 1 s |
| Mayor bloque contiguo | `heap_caps_get_largest_free_block` | 2 s (`MEM_BLOCK_MS`) — recorre la lista de huecos |
| Pico de PSRAM usada | máximo de `total − libre` desde el arranque | con cada muestreo |
| Mínimo de SRAM libre | mínimo de `libre` desde el arranque | con cada muestreo |
| Flash usada / total | `flexFsUsedBytes()` / `flexFsTotalBytes()` | **solo** con la pantalla de detalle a la vista, cada 15 s y nunca con el dedo apoyado (recorre el sistema de archivos: es una lectura de flash) |
| Tamaño del firmware | `ESP.getSketchSize()` | una vez por arranque |
| Ritmo del sistema | vueltas completas de `loop()` por segundo | 1 s |

El pico y el mínimo se calculan con **exactamente la misma medida que se
enseña**, en vez de pedirlos al SDK: así el histórico y el valor del momento no
pueden venir de dos contadores distintos y contradecirse.

---

## 4. Presupuesto: qué se abre y qué no

**Reserva del sistema: 6 MB de PSRAM** para el doble buffer, el táctil, el
Wi‑Fi, las tareas de FreeRTOS y las cargas repentinas.

| PSRAM libre | Política |
|---|---|
| > 10 MB | Multitarea normal |
| 6 – 10 MB | Aviso discreto; se sigue abriendo todo |
| 5 – 6 MB | Aviso visible; una app **pesada** exige soltar recursos antes |
| < 5 MB, sin bloque contiguo de 1 MB, o SRAM interna < 40 KB | Protección: no se abre una app pesada |

Clases de peso (`APP_WEIGHT[]`):

* **Ligeras** — Reloj, Almacenamiento, Notas, Ajustes, Calculadora, Calendario, Clima, Flex Phone
* **Medias** — Educación, Code IDE, Bienestar, Paint, Juegos, Flex Store
* **Pesadas** — Galería, Multimedia, Modo PC/DeX, Navegador, Cámara

Dos reglas que no se negocian:

* **Una app ligera nunca se bloquea.** Ajustes, Notas o el Reloj son justo lo
  que hace falta poder abrir cuando el sistema está apurado.
* **Volver a una app ya abierta nunca se bloquea.** Su memoria ya está
  contada; dejar al usuario sin poder recuperar su nota sería peor que el
  apuro que se intenta evitar.

Antes de negar nada, `memAdmitApp()` suelta recursos reconstruibles, vuelve a
medir y pregunta otra vez — **una** sola vez. Si sigue sin caber, se niega y
la notificación dice qué falta (memoria, reparto o SRAM interna), no un «sin
memoria» genérico.

---

## 5. Qué se suelta, y en qué orden

Del menos invasivo al más:

1. **Cachés del sistema** (`memShedSystem`), cada una con su guardia de «no
   mientras se esté viendo»:
   fondo desenfocado del wallpaper (768 KB) · panel compuesto y vidrio
   expandido de la cortina (hasta 2,3 MB) · *scratch* del desenfoque Liquid
   Glass (768 KB) · tarjeta Liquid Glass cacheada · páginas de la animación de
   Ajustes (768 KB × 2) · hoja de la caja de aplicaciones (768 KB) · fondo
   compuesto de DeX (768 KB) · banda del deslizador de apagado · miniaturas de
   Recientes sobrantes.
2. **Recursos de las apps suspendidas**, en orden **LRU** (la menos usada
   primero). Nunca la activa, nunca una con trabajo real en segundo plano.
3. **Cerrar**, y solo como último recurso, la app suspendida menos reciente —
   **y únicamente si su sesión se pudo guardar**. Si no se pudo, se conserva:
   perder el trabajo del usuario en silencio para «liberar recursos» no es una
   opción.

Nada de esto toca notas, dibujos, ajustes, archivos personales ni apps instaladas.

---

## 6. App Switcher (Recientes)

Recientes es un **selector de aplicaciones**, no un panel de memoria.

Aquí vivía una cabecera permanente con el recuento de apps por estado,
«PSRAM disponible: X / 32 MB», una barra de ocupación y un botón «Optimizar
Flex OS». **Se ha retirado entera.** Ocupaba 132 px —una sexta parte de la
pantalla— en todas las aperturas, y lo que informaba no era accionable: quien
abre Recientes quiere cambiar de app, no auditar la RAM.

Lo que **no** se retiró es el sistema que hay debajo: `memTick()` sigue
midiendo, el presupuesto sigue decidiendo qué se abre y la protección
automática sigue actuando (§8). El diagnóstico completo y el botón Optimizar
viven donde corresponde: Almacenamiento → Detalles de memoria y sistema.

Con esos 132 px la tarjeta sube y crece: de 384 a **480 px** de alto,
empezando 100 px más arriba. El área de imagen queda en 244×404 —exactamente
la proporción de la miniatura (150×250)—, así que además de verse más grande
deja de estar **verticalmente comprimida**, que es como se veía antes.

```
              Recientes
        ┌──────────────────┐
        │                  │
        │    miniatura     │   ← o el icono de la app si no hay captura
        │                  │
        │                  │
        │     Notas   ●    │   ← ● ámbar = cambios sin guardar
        │     Pausada      │   ← estado, y nada más
        └──────────────────┘
           [ Cerrar todas ]
   Desliza una tarjeta arriba para cerrar
```

**Interacciones**: tocar restaura (`enterApp` reanuda, no reinicia) ·
deslizar arriba cierra de verdad · **mantener pulsada** abre la ficha de la app
—estado, clase, consumo medido, tamaño de la miniatura, última actividad y
cambios sin guardar— que es donde alguien busca esos datos a propósito ·
«Cerrar todas» pide confirmación **solo si hay trabajo sin guardar**.

**Miniaturas racionadas.** Cada captura son 150×250×2 = 73 KB. Con una tarjeta
por app serían 1,4 MB solo en capturas, así que se conservan las **4 más
recientes** (`SW_THUMB_MAX`) y el resto de tarjetas caen al respaldo: marco +
icono. Se toman **al suspender**, nunca por cuadro.

`tests/host/check_wiring.py` impide que la cabecera vuelva: `swDrawCard()` y
`swRender()` tienen prohibido llamar a `flexMemFmt()`, `memSnap()` y
`optStart()`.

## 7. Almacenamiento → Detalles de memoria y sistema

La app conserva su nombre y su pantalla de siempre. Debajo de la barra de
PSRAM aparece una fila nueva:

```
Detalles de memoria y sistema            [ Optimizar ]
● Óptimo
```

El botón *Optimizar* está **en la pantalla principal**, no escondido: es la
acción que hace falta cuando el sistema va lento. El resto de la fila abre la
pantalla de detalle.

> **Por qué una pantalla y no un despliegue en línea.** El detalle son más de
> treinta cifras. Desplegarlas en la lista empujaría las
> categorías y los archivos grandes fuera de una pantalla de 480×800 que hoy no
> tiene desplazamiento: el resultado sería justo la «pantalla confusa» que
> había que evitar. La pantalla de detalle vive **dentro** de la app, con su
> propio scroll, y el botón atrás del sistema vuelve a la lista.

Secciones: **PSRAM** (total, en uso, libre, pico, mayor bloque libre,
fragmentación, estado y las dos apps de mayor consumo medido) · **SRAM
interna** (total, en uso, libre, mínimo desde el arranque, y que no conserva
datos al reiniciar) · **Almacenamiento interno** (capacidad, usado, libre,
firmware, recursos del sistema, datos de apps, caché temporal, archivos de
usuario) · **Estado del sistema** (tiempo encendido, último arranque y su causa, versión,
Wi‑Fi, ritmo del sistema y recuento de apps por estado).

**Refresco con dirty regions.** El contenido está partido en secciones de
altura fija; cada una lleva una **firma** de sus valores. Una vez por segundo
se recalculan las firmas y solo se repinta la banda de las que han cambiado **y
están a la vista**. Si no cambia nada, no se publica ni una fila. La barra de
estado, la cabecera de la app y la barra de navegación no se tocan nunca. Los
textos se componen con `snprintf` en buffers de pila: **cero reservas por
refresco**.

---

## 8. Memoria automática: primero actuar, avisar solo si hace falta

**El principio: el usuario no tiene por qué pensar en la RAM.** Con memoria
suficiente no se ve absolutamente nada —ni banner, ni barra, ni icono, ni
recordatorio—. Cuando el sistema se aprieta, primero se arregla solo; y
únicamente si después de arreglarlo sigue apretado se dice una vez.

La secuencia vive en `memAlertTick()` y es, en orden:

1. **Nivel sostenido** con histéresis (`flexMemLevelStep`). Se actualiza en
   cualquier pantalla: es una comparación de enteros sin efectos secundarios.
2. **OK y NOTICE** (por encima de 6 MB libres): no se hace ni se dice nada.
3. Al **empeorar** a WARN o CRITICAL —o cada `FLEXMEM_RELIEF_MS` (60 s)
   mientras siga apretado— se sueltan recursos reconstruibles (§5) y se
   vuelve a medir. **Esto no se ve.**
4. **Solo si tras el alivio sigue** en WARN o CRITICAL sale una tarjeta por la
   isla de notificaciones, que es temporal y no cambia el layout.

| Nivel tras aliviar | Qué ve el usuario |
|---|---|
| OK / NOTICE | Nada |
| WARN | *Memoria casi llena* — Flex OS liberó recursos en segundo plano. |
| CRITICAL | *Memoria crítica* — Flex OS está protegiendo el sistema. Cierra una app. |

### Histéresis: los umbrales de entrada y de salida no son los mismos

Con un único corte en 6 MB, una app que oscila entre 5,99 y 6,01 MB haría
entrar y salir del estado de aviso varias veces por segundo. **Empeorar es
inmediato** (proteger tarde no protege). **Mejorar exige margen.**

| Nivel | Se entra por debajo de | Se sale por encima de |
|---|---|---|
| NOTICE | 10 MB | 11 MB |
| WARN | 6 MB | 7 MB |
| CRITICAL | 5 MB, o sin bloque contiguo de 1 MB, o SRAM < 40 KB | 6 MB **y** bloque de 1,5 MB **y** SRAM > 56 KB |

Y **se baja de un nivel por evaluación**. Sin esa regla queda un hueco real: el
umbral de salida de CRITICAL (6 MB) coincide con el de entrada a WARN, así que
recuperarse hasta 6,06 MB saltaba a NOTICE y el primer repintado que bajara a
5,94 volvía a ser un *empeoramiento* —y con él, una notificación—. Es el mismo
parpadeo, desplazado un nivel. Bajando de uno en uno, salir de CRITICAL deja en
WARN y de WARN solo se sale por encima de 7 MB. No cuesta tiempo perceptible:
cada vuelta de `loop()` evalúa una vez, así que recuperarse del todo son tres
vueltas —milisegundos—. `tests/host/test_mem.cpp` reproduce ese vaivén exacto y
comprueba que no produce ni un empeoramiento.

### Anti-spam

Tres mecanismos, y hacen falta los tres:

* **Transición, no estado.** Solo se avisa cuando el nivel *acaba* de empeorar.
  Mientras la condición dura, no se repite.
* **Histéresis**, para que «acabar de empeorar» no ocurra por ruido.
* **Enfriamiento por clase** en los avisos secundarios (§9), más una
  separación global de 30 s.

`memAlertTick()` se llama en cada vuelta de `loop()` y en la inmensa mayoría no
hace nada más que una comparación de enteros. La prueba
`testMultitareaMemoria` comprueba que 40 vueltas con 20 MB libres no producen
**ni una notificación ni un solo byte reservado**.

### Optimizar Flex OS

El botón permanente de Recientes **ya no existe**: el sistema gestiona la RAM
por su cuenta. La herramienta sigue disponible, a propósito, en
**Almacenamiento → Detalles de memoria y sistema**, donde alguien la busca
para diagnosticar.

Máquina de etapas no bloqueante (una por vuelta de `loop()`, separadas por
tiempo con `millis()`: cero `delay()`, cero `while()` de espera). Mientras su
panel está a la vista es dueño exclusivo de la pantalla.

| Etapa | Qué hace **de verdad** |
|---|---|
| 1. Analizando memoria | Medida completa, bloque contiguo incluido |
| 2. Liberando caché temporal | `/System/Cache` en flash + cachés reconstruibles de las apps **suspendidas** |
| 3. Suspendiendo recursos no usados | Cachés del sistema y miniaturas sobrantes |
| 4. Verificando estabilidad | Vuelve a medir; si sigue apretado, enciende el modo eficiente |
| 5. Optimización finalizada | Cifras **reales**, o «No se encontraron recursos temporales seguros para liberar» |

---

## 9. Avisos secundarios

Fragmentación, SRAM interna y flash. Son condiciones distintas de
«queda poca PSRAM» y las decide `flexMemAlertPick()`, con enfriamiento por
clase —5 min memoria, 10 min fragmentación, 15 min flash— más una
separación global de 30 s. **Los de PSRAM ya no salen de aquí**: los gobierna
la máquina de nivel.

Solo se muestran desde Inicio o desde una app, nunca en el bloqueo, en Modo
seguro, durante un borrado ni en mitad de una transición.

| Condición | Mensaje |
|---|---|
| Fragmentación alta (con memoria de sobra) | *Memoria libre repartida en trozos pequeños* — Las imágenes o apps pesadas pueden tardar más |
| SRAM interna baja | *Memoria interna del sistema baja* — Se limitan cargas pesadas para proteger Wi‑Fi y táctil |
| Flash > 80 % | *Almacenamiento interno en uso elevado* — Limpiar la caché temporal puede ayudar |
| Flash > 90 % | *Almacenamiento interno casi lleno* — Las actualizaciones y los datos nuevos podrían fallar |

Nunca se dice que se ha «desfragmentado la RAM»: no se puede, y no se finge.

---

## 10. Modo visual eficiente

Temporal, y **no es una preferencia**. Lo enciende *Optimizar* solo si, después
de soltar todo lo seguro, la presión sigue alta; y lo apaga el propio sistema
en cuanto el nivel vuelve a ser **óptimo**. No se guarda en NVS, no aparece en
Ajustes y no cambia el estilo elegido: si el usuario tiene Liquid Glass sigue
teniendo Liquid Glass, y si tiene Plano aquí no cambia nada.

Qué hace, y por qué sí ahorra:

* **Menos desenfoque** (radio 6 → 2). `drawLiquidGlassPanelEx` trabaja sobre
  las filas visibles *más `blurR` filas de margen a cada lado*, así que bajar
  el radio reduce de verdad las filas que se desenfocan en cada panel.
* **Sombras a la mitad** (`effShadow`). Cada sombra es un relleno con alpha que
  se repinta en cada cuadro de un arrastre de ventana en Modo PC.
* **Una sola miniatura viva** en Recientes en vez de cuatro (73 KB cada una).

---

## 11. Límites conocidos

* **El consumo por app es una medida, con su margen.** Sale de la PSRAM que
  desaparece mientras corre el `enter()`/`resume()` de la app, menos lo que
  devuelve al suspenderse. No captura lo que la app reserve *después* de
  construirse (una foto que se abre más tarde), y otro subsistema podría
  reservar en la misma ventana. Por eso la interfaz dice «consumo estimado», y
  cuando no hay medida escribe `--` en la tarjeta y «No disponible» en la
  ficha.
* **No hay medidor global de FPS.** Cada pantalla publica su propia banda
  cuando le toca; no existe un único punto de presentación que contar. En su
  lugar se enseña el **ritmo del sistema** (vueltas de `loop()` por segundo),
  que sí es una medida real de capacidad de respuesta, y la pantalla lo dice.
* **El tamaño del firmware depende del SDK** (`ESP.getSketchSize()`). Si no
  devuelve un valor útil, la fila dice «No disponible».
* **La lectura de flash es cara** (`flexFsUsedBytes` recorre el sistema de
  archivos). Por eso solo ocurre con la pantalla de detalle a la vista, como
  mucho cada 15 s, y nunca con el dedo apoyado.
* **El PANIC del Wi‑Fi NO está demostrado, solo su causa más probable.** El
  arranque de esp‑hosted en `loopTask` es un defecto real y verificable
  leyendo el código —la llamada bloqueante estaba ahí, y `loopTask` alimenta
  el TWDT una vez por vuelta—, y está corregido. Pero **no hay un backtrace de
  la placa** que lo confirme como la causa del reinicio que se observó. Puede
  haber más de una. Para cerrarlo hace falta grabar con
  `FLEXOS_DIAG_WIFI 1` y mirar cuál es el último checkpoint antes del
  reinicio.
* **La cámara conserva sus ajustes; las Notas y Paint dependen de LittleFS.**
  El viaje completo a disco solo se puede comprobar en la placa: el arnés de
  host no tiene sistema de archivos. Lo que sí se comprueba aquí es que
  suspender no toca el contenido en RAM.
* **8192 bytes de pila para las tareas de Wi‑Fi son la cifra de la placa**, no
  de una prueba de host: aquí no hay radio ni pila de driver que consuman pila
  de verdad. Si en hardware apareciera un desbordamiento, es el primer número
  que hay que revisar.
* **Las pruebas de host no sustituyen a la placa.** Verifican las reglas, el
  cableado, el ciclo de vida del navegador y que no haya fugas de PSRAM en el
  código nuevo; el comportamiento del panel, del táctil y del controlador
  Wi-Fi real solo se ve en el ESP32‑P4.
* **En DeX, al volver se rehacen los lienzos** de las ventanas en orden de
  apilado y **solo mientras quede margen de memoria**. Si no cabe la cuarta,
  sus tres compañeras se ven igual y esa ventana se rehace cuando el sistema
  tenga aire.

---

## 12. Wi‑Fi y memoria: cómo se coordinan

### La regla de oro: esp‑hosted nunca se toca desde `loopTask`

`loopTask` está **suscrito al Task Watchdog** y solo lo alimenta una vez por
vuelta (`flexFeedWdt`). Cualquier llamada que despierte el transporte hosted
—`WiFi.getMode()`, `WiFi.mode()`, `WiFi.begin()`, `WiFi.scanNetworks()`,
`WiFi.disconnect()`— bloquea esa vuelta durante todo el arranque del enlace
SDIO con el C6: reset del co‑procesador, negociación y handshake. Si el C6 no
contesta rápido, la vuelta se pasa del plazo del TWDT y el chip entra en
**PANIC y reinicia**.

Por eso hay dos funciones y no una:

| Función | Coste | Quién puede llamarla |
|---|---|---|
| `wifiTransportReady()` | Barata. Solo mira banderas propias y la SRAM interna. **No habla con el driver.** | La interfaz (`loopTask`) |
| `wifiEnsureStaMode()` | Cara. Toca esp‑hosted. | **Solo** una tarea de Wi‑Fi |

Encendido y apagado pasan ahora por una tarea: `wifiScanTask`, `wifiConnTask`,
`wifiAutoConnTask` y el nuevo `wifiOffTask`. `check_wiring.py` lo impone con
una tabla de **llamadas prohibidas**: si alguien vuelve a poner
`wifiEnsureStaMode()` en `wifiStartScan()`, `wifiStartConnect()` o
`connWifiSet()`, la batería falla y dice por qué.

### Guarda de SRAM interna

esp‑hosted, el driver SDIO y la pila de la tarea salen de la **SRAM interna**,
no de la PSRAM. `wifiTransportReady()` exige `FLEXOS_WIFI_MIN_SRAM` (48 KB)
libres antes de empezar: con la interna en las últimas, `WiFi.begin()` falla
dentro del driver en un sitio donde ya no hay vuelta atrás. Es una **guarda**
—evita entrar en una operación condenada—, no un arreglo del fallo de fondo.

### Cesión, no candado

El alivio automático y el desalojo por presupuesto **ceden el paso** mientras
hay una operación de radio en vuelo (`wifiRadioBusy()`). No es que una cosa
corrompa a la otra —la radio vive en SRAM interna y lo que se suelta es
PSRAM—: es que el handshake SDIO es sensible al tiempo y el alivio puede
esperar unos segundos sin que nadie lo note. La cesión es **no bloqueante y
local** (se reintenta en la vuelta siguiente) y se compone de banderas que ya
existían: `gWifiAutoBusy`, `gWifiOffBusy` y `wifiUIState`. No se añadió estado
nuevo ni un mutex global.

### Modo de diagnóstico

`#define FLEXOS_DIAG_WIFI 1` imprime por Serial, en cada punto crítico, la
PSRAM y SRAM libres con su mayor bloque y el estado de la radio. Checkpoints:
`setPins`, entrada de cada tarea
de Wi‑Fi, antes y después de `WiFi.mode(STA)`, `WiFi.scanNetworks()` y
`WiFi.begin()`, más `memShedSystem()` y `appEnforceMemoryBudget()`. A 0 —el
valor por defecto— compila a **nada**. Nunca imprime dentro de una ISR ni de
una sección crítica.

---

## Archivos

| Archivo | Qué cambia |
|---|---|
| `FlexOS_Mem.h` / `.cpp` | **Nuevos.** Núcleo de decisión (puro, sin Arduino) |
| `FlexOS_Ultra.ino` | Gestor de memoria, presupuesto, App Switcher, Almacenamiento, Optimizar, avisos, ciclo de vida de DeX y Calendario, y la regla de oro de Wi‑Fi |
| `FlexOS_Browser.h`, `FlexOS_BrowserApp.cpp`, `FlexOS_Browser_Bridge.h` | Suspender/reanudar sin reiniciar la sesión y liberación de la caché de fotogramas |
| `tests/host/test_mem.cpp` | **Nuevo.** 66 comprobaciones del núcleo de decisión |
| `tests/host/ino_compile.cpp` | Contabilidad real de PSRAM en el arnés + `testMultitareaMemoria` |
| `tests/host/test_app.cpp` | `testSuspendResume` del navegador |
| `tests/host/check_wiring.py` | Ganchos obligatorios de los ticks nuevos |
| `tests/host/Makefile`, `inostub/Arduino.h` | Compilación y enlazado de lo anterior |
