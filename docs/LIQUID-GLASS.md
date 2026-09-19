# Liquid Glass de Flex OS Ultra

Cómo está hecho el material de vidrio del sistema: qué lo dibuja, de dónde
sale el desenfoque, qué añade el campo de distancia (SDF) y qué cuesta cada
cosa en un ESP32‑P4 con pantalla de 480×800.

El código vive en `FlexOS_Ultra/FlexOS_Ultra_Glass.h` (la física del
material) y en `FlexOS_Ultra_Theme.h` (la política: qué superficie usa qué
tinte). La separación es deliberada: el color se decide una vez, en el tema;
la matemática por píxel no depende de ninguna paleta.

---

## 1. Quién dibuja vidrio, y por qué son tres y no uno

No hay un compositor único, y eso no es un descuido: los tres leen su fondo
de un sitio distinto y esa diferencia es justo lo que los hace baratos.

| Compositor | Dónde vive | De dónde saca el fondo desenfocado | Cuándo se usa |
|---|---|---|---|
| `drawLiquidGlassPanelEx` | `Theme.h` | lo desenfoca él, sobre `glassBuf` | el panel general: ~78 llamadas por todo el sistema |
| `uiGlassPanelCached` | `Theme.h` | `uiGlBand`, una banda ya desenfocada | overlays que se **animan** sobre un fondo quieto |
| `qpGlassSurface` | `QuickPanelGlass.h` | el propio buffer del panel (`qsBuf`) | todas las superficies del Panel Rápido |

Los tres aplican **el mismo material**, por las mismas funciones
(`glEdgePx`, `glRefract`, `glLight`). Si divergieran, el vidrio de una
animación se vería distinto del vidrio quieto que sustituye.

### El backdrop compartido ya existía

Una regla de este trabajo fue no meter una cuarta caché: el sistema ya
comparte el fondo desenfocado en los tres sitios donde se puede hacer de
forma **exacta**, y una caché heurística encima sólo añadiría riesgo de
mostrar un desenfoque viejo.

* **Fondo plano** → `glcCard` (`drawGlassCardFlat`). Sobre un color uniforme
  el box‑blur devuelve ese mismo color, así que la tarjeta no depende de
  dónde se dibuje: se compone una vez y cada uso es un `memcpy` por fila.
* **Fondo quieto durante una animación** → `uiGlBand`. Se desenfoca la banda
  **una vez** al empezar y todos los cuadros muestrean de ahí.
* **Panel Rápido** → `qsGlassSm`, 120×200 (¼ de escala) + expansión bilineal.
  Un backdrop para todo el panel, recalculado sólo cuando cambia el fondo.

Ese ¼ de escala es también la respuesta a “¿conviene submuestrear?”: sobre un
fondo ya desenfocado, reducir 4×4 y volver a ampliar es indistinguible de
desenfocar a resolución completa, y son 24.000 píxeles en vez de 384.000.

---

## 2. El SDF, y por qué el efecto cuesta el perímetro y no el área

Todo —cobertura, refracción, Fresnel, especular y aberración— sale de **un
número por píxel**: la distancia con signo al borde del rectángulo
redondeado, negativa dentro.

```
qx = |px - cx| - (W/2 - r)          por columna
qy = |py - cy| - (H/2 - r)          por FILA, una vez
d  = (qx>0 && qy>0) ? hypot(qx,qy) - r      esquina
                    : max(qx,qy)   - r      lado recto
```

Los píxeles se miden por su **centro** (`px = i + 0,5`). Con eso el píxel más
exterior de un lado recto da `d = −0,5` (cobertura completa) y el panel
conserva **exactamente** la huella que tenía antes. Lo único que cambia son
las esquinas, y cambian para bien: donde había un escalón binario
(`glInset`) ahora hay una rampa de cobertura de un píxel.

De `d` sale `edgeFactor`: 0 en el centro, 255 en el canto mismo.

**Y de ahí el ahorro.** Donde `edgeFactor` vale 0 no hay nada que hacer, así
que el compositor parte cada fila en tres tramos y el central toma la mezcla
clásica de siempre, sin SDF, sin muestreo y sin luz. La banda es una tira de
grosor constante que sigue el contorno: en el canto recto entra `band`
píxeles, no `radio + band`. Medido sobre una tarjeta de 440×160 con radio 22:

| Perfil | Píxeles por la ruta lenta |
|---|---|
| ULTRA | 25,5 % |
| ALTA (por defecto) | 20,8 % |
| MEDIA | 16,0 % |

El 79 % restante es, bit a bit, el material que había antes.

---

## 3. La normal, la refracción y la luz

**Normal.** Es el gradiente del SDF. En la esquina es exacto (el radio
unitario, una división por píxel de esquina). En el lado recto el gradiente
del `max()` es un escalón en la diagonal, y un escalón ahí se ve como un
pliegue de 45° saliendo de cada esquina —el “halo cuadrado” que había que
evitar—, así que el peso se reparte entre los dos ejes en una franja de 8 px
alrededor de la diagonal. El resultado no queda unitario, y da igual: la
magnitud la pone `edgeFactor`, no la normal.

**Refracción.** El muestreo se desplaza **hacia dentro** del panel:

```
offset = -normal * refractionStrength * edgeFactor²
```

Hacia dentro y no hacia fuera porque así el canto *estira* lo que hay detrás,
que es como se comporta el borde de una lente real; hacia fuera se lee como
un glitch. El `edgeFactor²` concentra la curvatura en el último tercio: el
centro queda quieto, el medio se mueve poco y el canto hasta 4 px. El
muestreo es bilineal en 1/16 de píxel, porque 4 px de recorrido repartidos en
una banda de 11 saldrían escalonados si se redondeara a píxel entero.

**Fresnel.** No hay cámara, pero sí una superficie que se inclina según se
acerca al canto, y “cuánto se aparta la normal de la vista” es exactamente
`edgeFactor`. Así que `pow(1 − dot(N,V), p)` se resuelve como `pow(ef, p)`
por cuadrados, con `p = 4`. Cae con la cuarta potencia: a 3 px de
profundidad ya no llega al 2 %, y por eso no deja un contorno blanco.

**Realce de borde.** No es una línea: es
`fresnel × highlight × max(0, N·L)` con la luz arriba‑izquierda, más la
sombra opuesta a mitad de fuerza. Eso es lo que da **grosor** al canto sin
pintar ningún borde. Sustituye al borde de 2 px que dibujaba el material
clásico.

**Aberración cromática.** Tres muestras a lo largo de la normal (R la más
desplazada, B la menos), y **sólo** en el tercio exterior de la banda:
si se aplicase en toda, se leería como “RGB” en vez de como cristal. El
máximo son 0,75 px por lado, o sea 1,5 px de separación R‑B total.

**Iridiscencia.** El realce se tiñe de frío o cálido según hacia dónde mire
la normal. Al 5,5 %, y resuelta con una tabla de 17 entradas construida una
vez por panel, porque sólo depende de `nx`.

Todo esto ocurre **en una sola pasada** sobre el píxel. No hay una capa de
brillo encima de otra de color encima de otra de borde.

---

## 4. Deformación por toque

```
evento táctil  →  actualiza estado  →  el compositor interpola  →  cuadro
```

El manejador táctil (`flexPollTouch`, última línea, después de todos los
filtros) escribe **tres enteros** y nada más. Ahí no se captura pantalla, no
se desenfoca y no se calcula ningún SDF: eso metería el coste del efecto
dentro de la ruta del tacto, que es donde no puede estar. Tampoco hay ningún
hilo nuevo ni un bucle de animación por elemento.

El hundimiento es un empuje radial desde el punto de contacto que se apaga
con la distancia según una exponencial tabulada, y se **suma** al
desplazamiento que la refracción ya iba a aplicar: no es una capa nueva ni
una pasada nueva. Dura 190 ms —tope mientras el dedo está apoyado, retorno
suave al soltar— y en reposo el estado devuelve 0, así que todo el bloque se
salta con una comparación.

Dos guardas que importan:

* Un panel que el dedo no toca no paga nada (se descarta en `glEdgeBegin`,
  antes de mirar un solo píxel), y dentro de un panel tocado sólo pagan la
  ruta lenta las columnas al alcance del dedo.
* La tarjeta cacheada (`drawGlassCardFlat`) se compone **fuera de pantalla**
  y se reutiliza en cualquier posición, así que el toque está vetado ahí
  (`gGlassNoTouch`): un hundimiento grabado en la caché se quedaría pegado
  para siempre y en el sitio equivocado.

**Limitación, y es deliberada:** esto *no* fuerza a repintar ningún panel. La
deformación se ve en las superficies que el sistema ya estaba repintando
durante esos 190 ms —que son justo las que el dedo está tocando: el destello
del control del Panel Rápido, la fila resaltada de una lista, el botón
pulsado, el icono del escritorio—. Forzar un repintado global por cada toque
sería exactamente el *full‑screen redraw por cada touch* que no se puede
hacer.

---

## 5. Perfil de calidad y calidad adaptativa

Ni un número mágico del material fuera de `kGlassQ[]`, en `Glass.h`:

```
             band  refract  chroma  fresnel  fresPow  highlight  irid  touchAmp  touchR
ULTRA         14     64       12      52       4        46        14     56        44
ALTA          11     56        8      46       4        42        10     48        40
MEDIA          8     44        0      38       3        34         0     38        34
BAJA           0      0        0       0       0         0         0      0         0
```

`band` y `touchR` en píxeles; `refract`, `chroma` y `touchAmp` en 1/16 de
píxel; el resto son alphas 0‑255.

**BAJA es el material clásico exacto**, no una versión degradada: `glEdgeBegin`
devuelve `false` y el compositor toma la ruta de siempre, línea por línea.
Esa es la red de seguridad de todo el diseño.

`glassQualityTick()` mide, en una ventana de **un segundo**, los
microsegundos que el sistema gasta componiendo vidrio —no un “FPS global”,
que este sistema no tiene, porque cada pantalla publica su banda cuando le
toca—. Por encima del 22 % del tiempo baja un escalón; por debajo del 10 %
durante **tres** ventanas seguidas sube uno. La histéresis es a propósito:
un perfil oscilando entre dos niveles se vería como un parpadeo del
material, que es peor que quedarse abajo.

El orden en que se pierde cosas es el de coste dividido por lo que se nota:
iridiscencia → aberración cromática → deformación → ancho de banda →
refracción. Lo que **nunca** se toca: la respuesta al tacto, la corrección
de la interfaz y la estabilidad del panel.

El **modo visual eficiente** (`gEffMode`, lo enciende “Optimizar Flex OS”
cuando la presión de memoria sigue alta) pone techo MEDIA.

---

## 6. Memoria

El material avanzado **no reserva ni un byte de PSRAM** y no hace ni una
asignación dinámica. Todo lo suyo:

| Qué | Tamaño | Dónde | Vida |
|---|---:|---|---|
| `glRing[8][480]` — anillo de filas | 7.680 B | RAM interna | estático |
| `kGlFall` + `kGlRecip` + `kGlassQ` | 230 B | flash (`.rodata`) | constante |
| `GlassEdge` | 148 B | pila | por panel |
| `GlassPx` | 24 B | pila | por píxel |

El anillo existe por un motivo concreto: el Panel Rápido compone **en sitio**
(lee y escribe `qsBuf`), y un muestreo que se desplaza hacia arriba leería
filas que esa misma superficie acaba de escribir —el tinte se aplicaría dos
veces y quedaría una banda más saturada en el canto inferior—. Es el mismo
patrón que el box‑blur del sistema ya usaba (`glbRing`). Los otros dos
compositores leen de un buffer aparte y no pasan por ahí.

Ocho filas cubren el desplazamiento vertical de ULTRA (4,0 px de refracción
+ 0,75 de aberración). Sólo se queda corto si el hundimiento del dedo —hasta
3,5 px— apunta además en la misma dirección: 8,2 px en el peor caso. Ahí se
reutiliza la fila más vieja del anillo, un error de 0,2 px sobre un fondo ya
desenfocado, y nunca una lectura fuera de rango. Doblar el anillo costaría
7,5 KB más de RAM interna para una diferencia que no se ve.

Para contexto, los buffers grandes que ya existían y **no** se han tocado:
`fb`, `bbuf`, `lockBuf`, `homeBuf` (768 KB cada uno), `glassBuf` (768 KB),
`qsGlassSm` (48 KB), `qsGlassFull` (768 KB), `uiGlBand` (300 KB).

---

## 7. Precisión numérica

Ni un `double`. El P4 tiene FPU de simple precisión, pero en un bucle que ya
trabaja con RGB565 empaquetado la conversión float↔entero cuesta más que la
aritmética. Así que:

* **Q4** (1/16 px) para geometría y desplazamientos. Medio orden de magnitud
  más fino que el paso visible sobre un fondo borroso, y todos los productos
  intermedios caben de sobra en `int32`.
* **Q8** para normales, alphas y factores: es el dominio en el que `mix565`
  ya trabaja, así que no hay conversión.
* `isqrt32` —que ya existía en el motor gráfico— para la única raíz real, y
  sólo en las esquinas.
* Tablas para `exp()` y para `1/n`; recíprocos de 20 bits para las dos
  divisiones que quedaban, igual que ya hacía el box‑blur con `glbRecip`.
* `pow()` por cuadrados, porque el exponente es entero y pequeño.

No se sustituyó ninguna función “a ciegas”: las que se tabularon son las que
se ejecutan por píxel.

---

## 8. Coste medido

`tests/host/ino_compile.cpp` → `testVidrioAvanzado()` cronometra el
compositor **completo** (copia + desenfoque + material) sobre la misma
geometría y el mismo fondo, mejor de 3 × 30 repeticiones:

```
panel 440x160 r22
  ULTRA   4.509 us   +49 % sobre el material clasico
  ALTA    4.226 us   +40 %
  MEDIA   3.906 us   +29 %
  BAJA    3.029 us     ---
```

La cifra que se traslada a la placa es el **porcentaje**, no el absoluto: las
dos rutas hacen exactamente el mismo desenfoque y se diferencian sólo en la
pasada de composición. En el P4 ese porcentaje debería ser **menor** que en
el PC, porque allí el desenfoque —cuatro pasadas sobre PSRAM— pesa
proporcionalmente más que la única pasada de composición. No está medido en
hardware: es la razón de que exista la calidad adaptativa, que corta por el
coste real y no por una estimación.

---

## 9. Depuración

Modos **de compilación**, nunca activos en release. Sin la macro, el
compilador no genera ni un byte:

```
-DFLEXOS_GLASS_DEBUG=1   SDF / cobertura
                     2   normales
                     3   magnitud de la refraccion
                     4   Fresnel
                     5   cobertura sobre el fondo (magenta)
```

El Panel Rápido, al cerrarse, añade a su informe de siempre (`QP_PROF`, una
vez por sesión de cortina, no por cuadro) la calidad efectiva, la pedida y
los microsegundos de vidrio de la ventana.

---

## 10. Qué se comprueba, y dónde

`tests/host/ino_compile.cpp` → `testVidrioAvanzado()`, contra el código de
verdad y con AddressSanitizer + UndefinedBehaviorSanitizer:

* el **centro** del panel sale bit a bit igual que con el material clásico
  —o sea que el efecto es de borde y no ha cambiado el interior de las ~78
  superficies de vidrio repartidas por Flex OS—;
* la **huella** es la misma: ni un píxel fuera del rectángulo, en los tres
  compositores;
* es **determinista** sobre un fondo limpio (si no, cada repintado parcial
  dejaría costuras);
* las **esquinas** ganan cobertura parcial (el antialias que sustituye al
  escalón);
* el estilo **Plano** no cambia ni un píxel;
* el perfil **BAJA** produce exactamente el material de siempre;
* el **toque** deforma, caduca solo y no se graba en la tarjeta cacheada;
* la **banda pre‑desenfocada** coincide con el panel normal (esta prueba usa
  un fondo de franjas horizontales a propósito: es lo que la hace capaz de
  distinguir un paso de fila equivocado, que no se sale de ningún buffer y
  por tanto ningún sanitizer vería);
* la **calidad adaptativa** baja con presión sostenida y se recupera;
* **400 paneles al azar** (geometría, radio, recorte, perfil y posición del
  dedo) sin un solo acceso fuera de rango;
* y el **presupuesto de memoria**, atado con `sizeof`.
