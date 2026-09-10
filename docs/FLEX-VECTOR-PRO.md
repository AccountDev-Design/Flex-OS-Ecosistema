# Flex Vector Pro · editor vectorial de Flex OS Ultra

Editor vectorial profesional integrado en FlexOS Ultra (ESP32-P4). Este
documento es el **contrato de diseño**: qué se reutiliza del sistema, qué se
construye nuevo, qué función de Adobe Illustrator entra y con qué recorte, y
cuánta memoria puede gastar cada pieza.

Se escribió **antes** que el código, y el código no puede contradecirlo sin
actualizarlo aquí primero.

---

## 0. El hardware, y solo estas cifras

| Pieza | Valor real | Consecuencia de diseño |
|---|---|---|
| MCU | ESP32-P4, dual-core RISC-V, hasta 400 MHz | todo el render es **CPU**: rasterizado por líneas de barrido, sin sombreadores |
| Radio | ESP32-C6 por `esp-hosted` (SDIO) | la red **nunca** se toca desde `loopTask`; va en tarea propia |
| Pantalla | IPS 480×800, objetivo 60 FPS | presupuesto de **16,6 ms** por cuadro |
| Táctil | GT911 (I2C), multipunto real | pinza y rotación de dos dedos vienen de `gtPollMulti()` |
| Almacenamiento | **16 MB NOR Flash**, LittleFS. **No hay MicroSD** | el SVG exportado vive en `/Vector`, nunca en un medio extraíble |
| Memoria | **32 MB PSRAM** | presupuesto explícito por subsistema (§4) |

Cualquier cifra distinta que aparezca en documentación histórica del
repositorio **no aplica**. Estas son las únicas válidas.

---

## 1. Inventario: qué ya existe y se reutiliza

La inspección del repositorio (54 módulos `FlexOS_Ultra_*.h` + los `.cpp`
portables) da este reparto. **No se duplica nada de la columna izquierda.**

| Necesidad de Flex Vector Pro | Sistema existente que se reutiliza | Dónde vive |
|---|---|---|
| Registro y ciclo de vida de la app | `APP_REG` + `struct AppHooks` (`enter/tick/suspend/resume/close/saveSess/loadSess/shed/dirty`) | `FlexOS_Ultra_AppFramework.h` |
| Marco de ventana, cabecera, barra de navegación | `appDrawChrome()`, `navBarHandle()`, `WIN_TOP`/`WIN_BOT` | `FlexOS_Ultra_AppFramework.h` |
| Framebuffer y volcado al panel | `fb`, `setBuf()`, `flxFlush(y0,y1)`, `present()` | `FlexOS_Ultra_Gfx.h` |
| Primitivas de dibujo | `hLine/hLineA/fillRect/fillRoundRect/strokeSegAA/fillCircleAA/mix565` | `FlexOS_Ultra_Gfx.h` |
| Recorte por banda | `gClipX0/gClipX1/gClipY0/gClipY1` | `FlexOS_Ultra_Gfx.h` |
| **Estética Liquid Glass** | `uiSurface()`, `uiGlassPanelCached()`, `drawLiquidGlassPanel()`, `thCard()/thCard2()`, tokens `TH_*` | `FlexOS_Ultra_Theme.h` |
| Tipografía | `drawText/drawTextC/drawTextR/textW` (fuente Outfit 4bpp) | `FlexOS_Ultra_Font.h` |
| Táctil de alto nivel | `struct Touch T` (tap/drag/swipe) y `flexPollTouch()` | `FlexOS_Ultra_Touch.h` |
| **Multitáctil real** (pinza, rotación) | `gtPollMulti()` + `gtPts[]` + `gtFingers` | `FlexOS_Ultra_HAL.h` |
| Sistema de archivos (NOR Flash) | `flexFsWriteBinAtomic/flexFsList/flexFsNewName/flexFsDelete/...` | `FlexOS_FS.h/.cpp` |
| Diálogos de archivo (nombre, confirmar, papelera) | `fkNameOpen/fkNameTick/fkAskOpen/fkAskTick/fkMenuOpen` | `FlexOS_Ultra_FileKit.h` |
| Teclado en pantalla | `kbOpen/kbTick` (4 capas) | `FlexOS_Ultra_Keyboard.h` |
| Presupuesto y presión de memoria | `FlexMemSnap`, `memAdmitApp()`, `memShedApp()`, `APP_WEIGHT[]` | `FlexOS_Mem.h/.cpp` + `FlexOS_Ultra_Core.h` |
| Wi-Fi sobre el C6 | `WiFi.setPins(18,19,14,15,16,17,54)`, `gNetOnline`, tareas `wifi*Task` | `FlexOS_Ultra_Network.h` |
| Persistencia de sesión de app | `SessHdr` + `sessMarkDirty()` + `saveSess/loadSess` | `FlexOS_Ultra_AppFramework.h` |
| Iconografía vectorial | `drawAppIcon()` + enum `IC_*` | `FlexOS_Ultra_Icons.h` |
| Localización | `APP[APP_N][5]`, `LI()` | `FlexOS_Ultra_Session.h` |

### 1.1 Qué NO existe y hay que construir

| Falta | Por qué no existe | Qué se construye |
|---|---|---|
| Motor vectorial (Bézier, transformaciones, rasterizado por relleno, booleanas) | Paint guarda **trazos de pincel**, no geometría vectorial | `FlexOS_Vector.h/.cpp` — núcleo **portable**, con pruebas de host |
| Serializador SVG | nunca hubo formato de intercambio vectorial | dentro de `FlexOS_Vector.cpp` |
| **Generador de QR** | se buscó en todo el repositorio: no hay ninguno (`isqrt32` es raíz cuadrada, no QR) | `FlexOS_QR.h/.cpp` — **módulo del OS**, portable y reutilizable |
| **Servidor HTTP local** | el navegador es *cliente*; el OTA es *cliente*; Flex Store es *cliente*. No hay servidor | `FlexOS_HttpShare.h/.cpp` (protocolo, portable) + `FlexOS_Ultra_HttpShare.h` (tarea y sockets) — **módulo del OS**, no de la app |

Los tres nuevos módulos portables siguen la frontera que el proyecto ya usa:
lógica pura en `.cpp` con pruebas de host y sanitizers
(`FlexOS_Media`, `FlexOS_Mem`, `FlexOS_FlexLink`…), interfaz en un
`FlexOS_Ultra_*.h` del sketch.

---

## 2. Modelo de datos

```
FlexVecDoc
 ├─ Metadata      (ancho/alto de mesa de trabajo, unidades, nombre)
 ├─ 1 Artboard    (Fase 1: uno solo; varios = Futura, §3)
 └─ Layers[]      (máx. FLEXVEC_MAX_LAYERS)
     ├─ visible / bloqueada / nombre / orden
     └─ Elements[] (índices al pool global, máx. FLEXVEC_MAX_ELEMS)
         ├─ Geometry  : subtrazados de nodos Bézier cúbicos (ancla + 2 tiradores)
         ├─ Appearance: relleno, trazo, grosor, opacidad, cap/join
         └─ Transform : matriz afín 2D (a b c d e f)
```

Geometría y apariencia van **separadas**, como en Illustrator: cambiar un color
no toca ni un nodo, y una transformación no reescribe la geometría (se compone
en la matriz). Eso es lo que hace posible la edición no destructiva básica.

El pool de nodos es **global y preasignado**: los elementos apuntan a un rango
(`first`, `count`). No hay `malloc` por objeto ni por nodo, así que no hay
fragmentación de PSRAM por editar mucho rato — que es exactamente el fallo que
`check_stack.py` y `FlexOS_Mem` existen para evitar en este proyecto.

---

## 3. Clasificación de las funciones de Adobe Illustrator

Cuatro categorías: **Completa** (fidelidad funcional, adaptada a táctil),
**Simplificada** (conserva el valor central, recorta parámetros o casos
límite), **Futura** (viable pero fuera de estas dos fases), **Omitida** (no
tiene sentido en este hardware).

### 3.1 Completas

| Función | Justificación |
|---|---|
| Trazados Bézier cúbicos, edición de anclas y tiradores | coste por segmento O(1); es el núcleo del editor |
| Herramienta Pluma (tap = esquina, tap-arrastre = suave, cerrar trazado) | mapea 1:1 a gestos del GT911 |
| Añadir/eliminar ancla, convertir esquina↔suave | aritmética trivial sobre el pool |
| Formas primitivas (línea, rect, rect redondeado, elipse, polígono, estrella) | se generan como trazados Bézier: un solo camino de render |
| Transformaciones afines (mover, escalar, rotar, reflejar, inclinar) | matriz 2×3; coste despreciable |
| Relleno y trazo sólidos, grosor, opacidad por objeto | una pasada de rasterizado |
| Selección directa e indirecta, marco delimitador, selección múltiple | recorrido acotado por `FLEXVEC_MAX_ELEMS` |
| Alineación y distribución | son cajas delimitadoras |
| Deshacer / rehacer | diario de comandos con presupuesto en bytes (§4) |
| Zoom, encuadre, cuadrícula y ajuste a cuadrícula | transformación de vista, sin coste de geometría |
| Exportación SVG | texto plano: el formato ideal para un embebido |
| Gestos táctiles (pinza, encuadre a dos dedos, tap, pulsación larga) | `gtPollMulti()` ya da multipunto real |
| Capas (crear, visibilidad, bloqueo, reordenar, capa activa) | acotadas a `FLEXVEC_MAX_LAYERS` |

### 3.2 Simplificadas

| Función | Recorte y por qué |
|---|---|
| Pathfinder (Unir, Menos frente, Intersecar, Excluir) | se resuelven sobre una **rejilla de cobertura** de 256×256 y se vuelve a trazar el contorno, que después se simplifica (Douglas–Peucker). El resultado es **poligonal y aproximado** a la resolución de la rejilla. Se eligió así porque una booleana exacta sobre Bézier es un problema de degeneraciones sin cota superior de tiempo, y en un MCU con watchdog eso no es peor calidad: es un reinicio |
| Pathfinder Fase 2 (Dividir, Recortar) | mismo motor, mismos límites |
| Gradientes lineal y radial | tabla de 256 colores precomputada por gradiente; el relleno cuesta un `lut[t]` por píxel. Máx. `FLEXVEC_MAX_STOPS` paradas |
| Máscara de recorte | Fase 1 rectangular; Fase 2 forma arbitraria, con el mismo límite de nodos de las booleanas |
| Apariencia | Fase 1: 1 relleno + 1 trazo + opacidad. Fase 2: hasta `FLEXVEC_MAX_APPEAR` entradas. **No** hay pila multinivel con efectos vivos |
| Texto puntual | una fuente (Outfit, la del sistema), sin *shaping*. **No** se convierte a curvas al exportar: el atlas 4bpp del sistema no tiene contornos que convertir, así que el SVG lleva un `<text>` con una pila de fuentes genérica |
| Texto de área (Fase 2) | ajuste de línea por palabras dentro de un rectángulo, alineación de párrafo. Sin columnas ni flujo entre marcos |
| Símbolos | definición maestra + instancias **vinculadas de verdad**: la instancia no copia geometría, apunta al maestro, así que editar el maestro cambia todas y cincuenta instancias no gastan ni un nodo. Cada instancia tiene matriz y color propios; esa es la única sustitución, y es deliberada |
| Motivos (patterns) | **procedurales**: seis tramas (puntos, rayas, rejilla, damero, diagonales, cruzada) con celda, desplazamiento y giro. Un motivo hecho de un dibujo repetido obligaría a rasterizarlo a un bitmap y muestrearlo por píxel (~92 KB por motivo vivo, rehecho en cada cambio de zoom); una trama procedural se evalúa con aritmética pura, no gasta memoria y se exporta a SVG como un `<pattern>` con geometría de verdad |
| Pincel de dispersión / de motivo | estampa una figura a lo largo de un trazado. Todas las copias caben en **un** objeto (una por subtrazado), así que cien estampas gastan un elemento del presupuesto y no cien. Si el trazado es tan largo que no caben, el espaciado **se abre** hasta que caben, en vez de negarse |
| Fusión (blend) | interpolación de forma y color con pasos acotados (`FLEXVEC_MAX_BLEND_STEPS`) |
| Repetir (repeat) | rejilla y espejo, instancias acotadas, recalculado **bajo demanda** |
| Transformar cada uno | trivial: ya existen las afines |

### 3.3 Futuras (no se implementan en Fase 1 ni Fase 2)

| Función | Condición para revisarla |
|---|---|
| **Máscara de opacidad (por luminosidad)** | estaba prevista como Simplificada para la Fase 2 y se movió aquí **con medida en mano**. Componer dos coberturas por fila (la del objeto y la de la máscara) es imposible con un rasterizador que emite los tramos de una figura de una vez: haría falta un búfer de cobertura de pantalla completa —**384 KB de PSRAM retenidos durante todo el render**— o rasterizar la máscara dos veces por objeto. La máscara de **recorte** cubre el caso real (limitar un dibujo a una forma) sin ninguno de esos dos costes, así que la de opacidad no entra hasta que haya un motivo de peso |
| Varias mesas de trabajo | requiere un modelo de vista por mesa y multiplica el coste de exportación |
| Gradiente de malla / libre | solo si se demuestra con medidas reales que la interpolación 2D cabe en el presupuesto de CPU. Hoy no hay evidencia de que quepa |
| Rejilla de perspectiva | necesita transformación proyectiva en el render, que hoy es afín puro |
| Efectos vivos re-evaluados | requeriría un grafo de apariencia; contradice el modelo "horneado" de §5 |

### 3.4 Omitidas, con el motivo técnico

| Función | Por qué no en este hardware |
|---|---|
| Gradiente de malla y gradiente libre | interpolación 2D por píxel sobre una malla irregular: sin GPU es el orden de magnitud de un cuadro entero por objeto |
| Desenfoques, resplandores y sombras rasterizados | rasterizar a alta resolución y convolucionar; el sistema ya paga un `glassBlur` de 768 KB para el Liquid Glass y ese es el techo |
| Tipografía OpenType completa (*shaping*, ligaduras, glifos alternos) | la fuente del sistema es un atlas 4bpp sin tablas GSUB/GPOS. Añadirlas es un motor de texto entero |
| Calco de imagen (Image Trace) | visión por computador: umbralizado, seguimiento de contornos y ajuste de curvas sobre un bitmap completo |
| Distorsión de marioneta, deformación de envolvente | mallas deformables + re-triangulación por cuadro |
| Pinceles de cerdas / caligráficos complejos | miles de instancias por trazo |
| Funciones generativas de IA (Firefly) | dependen de servicios en la nube. Si algún día entran, serán un **cliente de red opcional**, jamás cómputo local |
| Bibliotecas CC, documentos en la nube, colaboración | no aplican a este dispositivo |
| Apariencia multinivel con varios efectos vivos | ver §3.3 |
| Cualquier cosa que exija GPU de escritorio o MicroSD | no existen aquí |

---

## 4. Presupuesto de memoria (PSRAM)

De los 32 MB, el sistema ya reserva 6 MB (`FLEXMEM_RESERVE_BYTES`) y los
framebuffers ocupan 4 × 768 KB = 3 MB. Flex Vector Pro se compromete a **no
pasar de 2 MB** y a declararse `FLEXMEM_W_MEDIUM` en `APP_WEIGHT[]`.

| Subsistema | Presupuesto | Cómo se acota |
|---|---|---|
| Pool de nodos | 4096 nodos × 28 B = **112 KB** | `FLEXVEC_MAX_NODES`; `FLEXVEC_MAX_NODES_PATH` por trazado |
| Elementos | 256 × 104 B = **26 KB** (dentro de `FlexVecDoc`) | `FLEXVEC_MAX_ELEMS` |
| Capas | 8 × 44 B (dentro de `FlexVecDoc`) | `FLEXVEC_MAX_LAYERS` |
| Diario de deshacer/rehacer | **256 KB** | expulsa lo más viejo por bytes **y** por pasos; `FLEXVEC_UNDO_BYTES`, `FLEXVEC_UNDO_STEPS` |
| Pool de texto | **4 KB** | `FLEXVEC_TEXT_BYTES` |
| Caché de render 480×800 RGB565 | **768 KB** | **es lo que suelta `shed()`**; el documento no se toca |
| Rasterizador (un solo bloque) | **~86 KB** | `flexVecRasterBytes()`; `FLEXVEC_MAX_FLATPTS`, `FLEXVEC_MAX_EDGES`, `FLEXVEC_MAX_SUBS` |
| Gradientes (tablas de color) | 8 × 1,3 KB ≈ **11 KB** (dentro de `FlexVecDoc`) | `FLEXVEC_MAX_GRADS`, `FLEXVEC_MAX_STOPS` |
| **Total residente** | **~1,32 MB** | |

Tres bloques **no** son residentes y se piden solo mientras hacen falta,
porque son operaciones puntuales y explícitas del usuario:

| Bloque temporal | Tamaño | Cuándo existe |
|---|---|---|
| Rejillas del Pathfinder | **~210 KB** | solo durante una booleana o un recorte (`flexVecBoolBytes()`). Son tres rejillas de 256×256: la tercera la exige *Dividir*, que necesita las dos figuras a la vez además del acumulador |
| Serialización del documento | hasta **~175 KB** | solo al guardar o al abrir un `.fxv` |
| Exportación PNG | **512 KB** de salida + **48 KB** de compresor | solo durante la exportación. El píxel sale de la caché de render que ya existe, así que **no** hay un pico de 1,1 MB en RGB888 |
| Servidor HTTP (petición + página) | **8 KB** | solo mientras se comparte |

Todo lo residente se reserva **al abrir la app** y se libera **al cerrarla**.
El bucle de render no llama a `malloc` ni a `new`: usa exclusivamente estos
bloques. Si alguno de los bloques temporales no cabe, la operación se niega
con un aviso y el documento no se toca.

Al alcanzarse un límite la operación **falla de forma controlada** — un aviso
en la interfaz y la operación no se realiza — nunca con un reinicio ni con la
interfaz congelada. Esa es la regla que verifican las pruebas de límites.

---

## 5. Render

* **CPU + PSRAM + framebuffer.** Rasterizador por **líneas de barrido**:
  se aplanan las Bézier a polilíneas con tolerancia dependiente del zoom, se
  construye una lista de bordes activos y se emiten **tramos horizontales**
  (`span`) que el dispositivo pinta con `hLine`/`hLineA`. Regla par-impar y
  regla de no-cero, las dos.
* **El núcleo no dibuja.** Emite tramos por *callback*, exactamente igual que
  `flexPaintReplay()` hace con sus segmentos. Por eso el rasterizador se
  puede ejercitar entero en el PC, con sanitizers.
* **Regiones sucias.** Solo se vuelca la banda tocada (`flxFlush(y0,y1)`), que
  es lo que ya hacen Paint, el teclado y el navegador.
* **Caché de capa estática.** Todo lo que no se está editando vive en un lienzo
  RGB565 aparte. Arrastrar un objeto = restaurar la banda desde la caché +
  pintar el objeto: no se re-rasteriza el documento entero por cuadro.
* **Nada costoso se recalcula por cuadro.** Fusión, repetición y pinceles se
  **hornean** a geometría cuando se aplican.

---

## 6. Reparto en archivos

| Archivo | Qué es | Pruebas |
|---|---|---|
| `FlexOS_Vector.h/.cpp` | núcleo portable: modelo, Bézier, transformaciones, rasterizador, booleanas, gradientes, deshacer, SVG | `tests/host/test_vector.cpp` (ASan+UBSan) |
| `FlexOS_QR.h/.cpp` | codificador QR del OS (reutilizable) | `tests/host/test_qr.cpp` (ASan+UBSan) |
| `FlexOS_HttpShare.h/.cpp` | protocolo HTTP/1.1 del servidor local del OS | `tests/host/test_httpshare.cpp` (ASan+UBSan) |
| `FlexOS_Ultra_HttpShare.h` | servicio del OS: `WiFiServer` en tarea propia | `test_ino` (compila y enlaza) |
| `FlexOS_Ultra_AppVector.h` | la app: interfaz Liquid Glass, herramientas, gestos, render | `test_ino` (compila y enlaza) |

---

## 7. Compartir por Wi-Fi, sin MicroSD

1. El documento se exporta a SVG en `/Vector/<nombre>.svg` (NOR Flash) y a la
   vez queda en el buffer de PSRAM.
2. `FlexOS_Ultra_HttpShare.h` levanta un `WiFiServer` **en su propia tarea**
   — nunca en `loopTask`, que es la regla de oro del proyecto con
   `esp-hosted` (`check_wiring.py` la vigila) — y publica el archivo en
   `http://<ip>:8080/<token>/<nombre>.svg`.
3. `FlexOS_QR` codifica esa URL y la app la dibuja con `fillRect`.
4. Se descarga desde cualquier navegador de la misma red. La ruta lleva un
   token aleatorio por sesión: el archivo no queda expuesto de forma
   permanente, y el servidor se apaga al cerrar la app.

---

## 8. Fases

**Fase 1 — núcleo profesional.** Modelo de documento y capas, Pluma con Bézier
reales, edición de nodos, primitivas, apariencia sólida, transformaciones por
gestos, zoom/encuadre/cuadrícula, deshacer/rehacer, rasterizador con regiones
sucias, presupuestos de memoria con fallo controlado, integración con el OS,
exportación SVG, servidor HTTP + QR.

**Fase 2 — expansión profesional.** Pathfinder ampliado, gradientes con
edición de paradas, máscaras de forma arbitraria y de opacidad, apariencia
múltiple, pinceles de dispersión/motivo, motivos, símbolos, texto de área,
transformar cada uno / reflejar / inclinar, fusión, repetición, SVG optimizado
y exportación PNG, y el ajuste de límites con las medidas reales de la Fase 1.

Los criterios de aceptación de cada fase están en `tests/host/test_vector.cpp`
y en el apartado de verificación manual del final de este documento.

---

## 9. Verificación de la Fase 1

Las pruebas viven en `tests/host/` y se ejecutan **en el PC**, con
AddressSanitizer y UndefinedBehaviorSanitizer, sobre el mismo código que va a
la placa:

```bash
make -C tests/host          # todo, perfil P4
make -C tests/host all-boards
```

| Criterio de aceptación de la Fase 1 | Dónde se verifica |
|---|---|
| Documento nuevo, 3 capas con visibilidad, bloqueo y reordenación | `test_vector.cpp :: testLayers` |
| Pluma con Bézier reales, cierre, y edición con selección directa | `test_vector.cpp :: testPen` |
| Rectángulo, elipse, línea, polígono y estrella con relleno/trazo/opacidad | `test_vector.cpp :: testShapes` |
| Transformaciones afines (mover, escalar, girar, reflejar, inclinar, cada uno) | `test_vector.cpp :: testTransforms` |
| Selección, prueba de impacto, alineación, distribución y orden Z | `test_vector.cpp :: testSelection` |
| Deshacer/rehacer **60 pasos**, comparando el documento paso a paso | `test_vector.cpp :: testUndo` |
| Rasterizado: área exacta, antialias, recorte respetado, regla de no-cero | `test_vector.cpp :: testRaster` |
| Cuadrícula y ajuste | `test_vector.cpp :: testSnap` |
| SVG bien formado y fiel (geometría, relleno, trazo, opacidad, matriz, texto escapado) | `test_vector.cpp :: testSVG` |
| Guardar y reabrir; archivos truncados o con un bit cambiado | `test_vector.cpp :: testSerialize` |
| **Límites**: 256 objetos, 256 nodos/trazado, pool global, 8 capas, texto, gradientes y diario lleno — todos alcanzados de verdad y todos fallando con su motivo | `test_vector.cpp :: testLimits` |
| Gradientes: tabla, parámetro, orden de paradas y exportación | `test_vector.cpp :: testGradients` |
| Pathfinder: áreas de unión, intersección, resta y exclusión; deshacer | `test_vector.cpp :: testPathfinder` |
| Sin memoria del anfitrión: se dice y el sistema sigue vivo | `test_vector.cpp :: testNoMemory` |
| QR: ida y vuelta real en los 4 niveles de corrección y hasta la versión 10 | `test_qr.cpp` |
| HTTP: peticiones a medias, rutas peligrosas, testigo de sesión, escapado y ruido | `test_httpshare.cpp` |
| El sketch entero compila, se cablea y respeta el presupuesto de pila | `make ino` (`check_protos`, `check_wiring`, `check_stack`) |
| Ninguna app existente se rompe | la batería completa, en los tres perfiles de placa |

### Lo que solo se puede comprobar en la placa

Estas pruebas no pueden correr en el PC y quedan como comprobación manual
antes de dar la fase por buena en hardware:

1. **Gestos reales sobre el GT911**: pinza, encuadre a dos dedos, arrastre de
   anclas y de tiradores. El multitáctil viene del chip por I2C.
2. **Fluidez medida**: con `FLEX_DIAG` a 1 (`FlexOS_Ultra_Core.h`), el sistema
   vuelca por serie los tiempos por cuadro y cuántos pasan de 16,67 ms. Es la
   forma de comprobar el objetivo de 60 FPS con números, no de vista.
3. **Descarga de extremo a extremo**: exportar, escanear el QR con un móvil de
   la misma red y abrir el SVG. Verifica de una vez el enlace P4↔C6, el
   servidor y el codificador de QR.
4. **Corte de corriente al guardar**: la escritura es atómica
   (`flexFsWriteBinAtomic`), así que debe quedar el documento anterior entero.

---

## 10. Verificación de la Fase 2

La Fase 2 se abordó **después** de que la Fase 1 pasara sus criterios, y no
tocó ninguna de sus funciones: las 300 comprobaciones de la Fase 1 siguen
pasando sin cambios, que es el primer criterio de aceptación de esta fase.

| Criterio de aceptación de la Fase 2 | Dónde se verifica |
|---|---|
| Pathfinder ampliado: **Dividir** (tres regiones) y **Recortar** (conserva las figuras de encima) | `test_vector.cpp :: testDivideTrimClip` |
| Gradientes lineal y radial con edición de paradas y opacidad por parada | `test_vector.cpp :: testGradients` (Fase 1) + panel de Apariencia |
| **Máscara de recorte de forma arbitraria**, y el objeto sin solape desaparece | `test_vector.cpp :: testDivideTrimClip` |
| **Apariencia ampliada**: dos rellenos y dos trazos, con su orden de pintado y su exportación | `test_vector.cpp :: testAppearance2` |
| **Pincel**: estampas a lo largo de un trazado, todas en un objeto, con el espaciado que se abre solo si no caben | `test_vector.cpp :: testBlendRepeatBrush` |
| **Motivos**: los seis tipos, con celda, desplazamiento, coordenadas negativas y `<pattern>` en el SVG | `test_vector.cpp :: testPatterns` |
| **Símbolos**: la instancia no gasta nodos, sigue al maestro al editarlo, y borrar el maestro se lleva las instancias en un paso | `test_vector.cpp :: testSymbols` |
| **Texto de área**: corte por palabras, tres alineaciones, la caja manda, saltos explícitos, y los mismos `<tspan>` en el SVG | `test_vector.cpp :: testAreaText` |
| **Transformar cada uno**, reflejar e inclinar | `test_vector.cpp :: testTransforms` |
| **Fusión**: pasos acotados, forma y color interpolados | `test_vector.cpp :: testBlendRepeatBrush` |
| **Repetición**: rejilla y espejo, instancias acotadas, un solo paso de deshacer | `test_vector.cpp :: testBlendRepeatBrush` |
| **SVG compacto** y exportación de selección | `test_vector.cpp :: testSVG` |
| **Exportación PNG**: se descomprime con zlib y los píxeles vuelven **exactamente** iguales | `test_vector.cpp :: testPng` |
| **Presupuesto de render**: el relleno emite tramos largos, alejar cuesta menos, y un repintado completo cabe en el presupuesto | `test_vector.cpp :: testRenderBudget` |
| Nada de lo marcado **Omitido** o **Futuro** se implementó a medias | revisión de §3.3 y §3.4 contra la API de `FlexOS_Vector.h` |

### 10.1 Rendimiento: qué se mide y qué no

`testRenderBudget` **no mide milisegundos**, y es deliberado: un PC no dice
nada sobre lo que tarda un ESP32-P4. Mide **trabajo** —cuántos tramos y
cuántos píxeles emite el rasterizador—, que sí se traslada, porque el coste en
la placa es proporcional a eso. Los números de hoy:

| Caso | Tramos | Píxeles | Píxeles por tramo |
|---|---:|---:|---:|
| Círculo de 470 px de diámetro | 1966 | 174 108 | **89** |
| 120 círculos con trazo (repintado completo) | 51 120 | 139 200 | 2,7 |
| Objeto de 40×30: banda sucia | — | — | **35 filas de 800** |

Lo que la prueba fija no es el número absoluto sino la **relación**: un relleno
macizo tiene que emitir tramos largos (≈90 píxeles cada uno), no un tramo por
píxel. Si esa cifra cayera a ~1, sería que el rasterizador ha vuelto a emitir
píxel a píxel, y eso multiplica por cien las llamadas al motor gráfico. Esa es
la regresión que la prueba existe para cazar.

El FPS real se mide **en la placa**, poniendo `FLEX_DIAG` a 1 en
`FlexOS_Ultra_Core.h`: el sistema vuelca por serie el tiempo por cuadro, el
peor cuadro y cuántos pasaron de 16,67 ms.

### 10.2 Las cuatro optimizaciones que sostienen la fluidez

1. **La caché de render.** El documento se rasteriza una vez a un lienzo
   RGB565 propio. Un cuadro normal copia de ahí sólo la banda que cambió.
2. **El objeto en movimiento sale de la caché.** Mientras se arrastra, se
   compone encima en cada cuadro; el resto viene ya hecho. Arrastrar cuesta una
   figura, no un documento.
3. **Ordenación por cuenta y lista de aristas activas.** Cada fila mira sólo
   las aristas que la cruzan. Sin eso, una figura de 500 aristas se recorrería
   entera 800 × 4 veces: 1,6 millones de pruebas para una sola forma.
4. **Lo caro se hornea.** Fusión, repetición y pincel producen geometría una
   sola vez. No queda un grafo que re-evaluar en cada repintado — que es lo que
   un editor de escritorio puede permitirse con una GPU detrás y un MCU no.

### 10.3 Lo que sigue necesitando la placa

Lo de §9 sigue vigente, y la Fase 2 añade dos comprobaciones manuales:

5. **PNG en un visor externo**: exportar y abrir el `.png` en un ordenador. La
   prueba de host ya descomprime el flujo con zlib y compara los píxeles, así
   que esto sólo confirma el camino completo hasta la NOR Flash.
6. **Motivos y gradientes a distintos zooms**: los dos se evalúan en
   coordenadas de documento, así que ampliar debe agrandar la trama, no
   revelar píxeles.

## 11. Correcciones sobre la placa

Lo que sigue no son funciones nuevas: son cinco fallos reales vistos en el
dispositivo, con su causa y lo que se hizo. Se documentan aquí porque tres de
ellos no se deducen del código de la app —salen de cómo se comporta el panel
MIPI‑DSI y la caché del P4— y quien toque esto después necesita saberlo.

### 11.1 Los botones de la cabecera se perdían

`T.tap` sólo se enciende si el dedo se movió **menos de 16 px y el pulso duró
menos de 550 ms** (`tDoRelease`, `FlexOS_Ultra_Touch.h`). En una capacitiva real
un pulgar deriva 2‑3 px sin querer, y si el usuario duda el pulso pasa del medio
segundo: deshacer, rehacer y el menú «no respondían».

`vecUiTapAt()` acepta el **levantar el dedo dentro de 10 px, sin tope de
tiempo**, y devuelve la coordenada de la **pulsación**, que es donde el usuario
creyó tocar. Lo usan sólo la cabecera, la tira de herramientas, el menú y los
paneles. **El lienzo no pasa por ahí a propósito**: ahí un arrastre de 3 px es un
arrastre de verdad. No se tocó el táctil del sistema: ninguna otra app cambia.

Las cajas táctiles de los tres botones son de **49 × 49 px** (`VEC_HBTN_HIT`)
aunque la pastilla que se ve mida 40; y la tira de herramientas resuelve por
**celda más cercana**, así que no queda un píxel muerto ni en el hueco de la
divisoria entre grupos.

### 11.2 Orientación horizontal

Se maqueta contra `gAppW`/`gAppH` —el lienzo lógico que da el framework— y no
contra `SCR_W`/`SCR_H`. Es el mismo patrón de `WIN_TOP`/`WIN_BOT`, y el relayout
llega por la vía de siempre: `enter()` se re‑ejecuta con `gRelayout` puesto. Con
el lienzo apaisado la tira de herramientas se pone **de pie a la izquierda** y el
panel se **acopla a la derecha**, que es donde Illustrator tiene el suyo.

**El detalle que no se ve en el código de la app:** en horizontal `Gfx.h` gira 90°
en `putPhys()`. La **fila física es la columna lógica**, y ahí `gClipX0/gClipX1`
ni se miran —el único recorte vivo es `gClipY0/gClipY1`, que acota la **X
lógica**—. Por eso todo lo que copia o recorta pasa por `vecPhysRect()`,
`vecClipRect()` y `vecCopyRect()`. Y por eso **en horizontal no hay repintado
parcial**: sin recorte en Y lógica un tirador de selección se quedaría pegado
sobre la cabecera, y además ahí la app corre siempre hospedada en una ventana de
Modo PC, donde `flxFlush()` no vuelca nada y DeX recompone la ventana entera —
repintar por bandas no ahorraría ni un byte.

El barrido del rasterizador se dimensiona al **lado largo** del panel
(`VEC_RAS_W`), porque en horizontal el lienzo lógico llega a 800 px de ancho y
`fvScanFill` acota el tramo a `covW`: con 480 toda figura más ancha saldría
cortada por la derecha.

### 11.3 Interfaz más cerca de Illustrator

Sólo reorganización, ninguna función de edición nueva:

* la **cabecera es un panel de control** en dos franjas —barra de documento
  arriba, control debajo— con las muestras de relleno y trazo superpuestas, la
  herramienta activa y el grosor. Tocar las muestras abre Apariencia, que es el
  panel que ya existía;
* la **tira de herramientas** va en dos grupos —selección y formas— con
  divisoria, pegados y centrados, para que se lea como un panel y no como ocho
  botones sueltos;
* el **panel acoplado** lleva divisoria bajo el título y, en horizontal, carril
  de acento en el borde por el que se acopla. Conserva su ancho de diseño
  (456 px): los cuatro paneles maquetan su contenido contra él.

### 11.4 Cuadros perdidos

* `vecRenderAll()` **respeta la caché**. Antes rasterizaba el documento entero en
  cada llamada, y la llama cada toque de cabecera, herramientas, menú y paneles:
  abrir un panel volvía a rasterizar los objetos sin que ninguno cambiara.
* El **conteo de objetos de la galería se lee una vez** por archivo en
  `vecReload()`, no dentro del bucle de dibujo.
* La galería **sólo repinta si la lista se movió de verdad**: contra el tope, el
  dedo seguía mandando cuadros y cada uno costaba una pantalla entera.
* La banda sucia se acota **también en X**, salvo con la herramienta de nodos o
  la pluma abiertas, donde un tirador de Bézier se sale de la caja de la curva.

No hay ni una reserva de memoria en el bucle de dibujo ni en el de toque: todo
sale de `vecArenaInit()`.

### 11.5 El destello cian a pantalla completa

Tres causas, las tres reales, y ninguna se tapó con un segundo volcado:

1. **La raíz.** Escribir o leer la flash SPI obliga al IDF a **desactivar la
   caché en los dos núcleos**; mientras está apagada, la DMA del presentador
   MIPI‑DSI no puede alimentar su FIFO desde la PSRAM y el panel pinta un cuadro
   de basura (lo mismo que documenta `FlexOS_OTA.h` para el OTA). La galería
   hacía **una lectura de flash por ficha dentro del bucle de dibujo**, con un
   `flxFlushAll()` justo detrás. Ahora la lectura vive en `vecReload()`.
2. **La papelera, invertida.** `fkTrashTick()` devuelve `true` *mientras la
   papelera sigue abierta*, y en ese caso el llamante no debe hacer nada más.
   Aquí estaba al revés: con la papelera abierta se llamaba a `vecReload()`
   (lecturas de flash) y a `vecRenderGallery()` (pantalla completa +
   `flxFlushAll`) **en cada cuadro**, encima del volcado que la propia papelera
   acababa de hacer. Dos volcados de 768 KB por cuadro con la caché apagándose
   entre medias. Ahora sigue el mismo contrato que Archivos, Galería, Notas y
   Paint — y al salir la lista se recarga, que antes tampoco pasaba.
3. **PSRAM sin inicializar.** `flxGfxInit()` sólo ponía a cero `fb`; `bbuf`,
   `lockBuf` y `homeBuf` salían con lo que hubiera en la PSRAM, y los tres
   acaban en el panel. La caché de Vector, igual. Cuatro `memset` en el arranque
   y uno por reserva de caché (`vecCacheAlloc()`) quitan el modo de fallo.

### 11.6 Qué comprueba la prueba de host

`tests/host/ino_compile.cpp :: testVectorPro()` fija el toque con deriva (3 px y
900 ms siguen siendo un toque; 20 px ya no), las cajas de 49 × 49 sin solaparse,
las cuatro esquinas del botón de menú, la tira sin zonas muertas en las **dos**
orientaciones, que panel y menú caben enteros, que el origen del documento cae en
la esquina del **lienzo** y no de la pantalla, y la traducción `vecPhysRect()` /
`vecClipRect()` en horizontal. El destello sólo se confirma **en la placa**: es
un fallo de temporización entre la caché y la DMA del panel, y ningún doble de
host lo reproduce.
