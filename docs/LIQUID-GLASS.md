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

## 2. El campo interior: por qué ahora es una lente y no un bisel

La primera versión de este material sólo deformaba la **banda de borde**. El
resultado medido era demoledor: con el perfil por defecto, el **87 % de los
píxeles de un panel tenían desplazamiento exactamente cero**, y el
desplazamiento moría en la columna 10. Todo lo que hubiera detrás del centro
—una imagen, un texto, un icono— salía intacto. Era un bisel, no una lente.

Ahora hay dos términos que se suman:

```
desplazamiento = CAMPO INTERIOR  +  TÉRMINO DE BORDE (SDF)
```

El **campo interior** es radial desde el centro y crece con el cuadrado de la
distancia normalizada: casi nada en el centro, visible a media altura, y su
máximo justo donde arranca la banda. Por eso los dos términos empalman sin
costura. Perfil medido en un panel de 440×200 (1/16 px):

| Desde el centro | 0 % | 20 % | 40 % | 60 % | 80 % | borde | canto |
|---|---|---|---|---|---|---|---|
| desplazamiento | 0 | 1 | 6 | 14 | 25 | 38 | 90 |

Y los píxeles con desplazamiento cero pasan del **87 % al 0,7 %** (la cruz
exacta del centro, donde por definición no hay dirección).

### El desenfoque tuvo que bajar

Con un box-blur de radio 6 —13 px de ventana— el detalle fino desaparece
*antes* de poder doblarlo, y entonces la refracción no se ve porque no queda
nada que refractar. El radio es ahora parte del perfil de calidad y vale 3
con material avanzado. El perfil BAJA conserva el 6 de siempre.

La diferencia se ve de un vistazo en `make lab`: con el material clásico el
texto de detrás es una mancha; con el nuevo se lee **a través** del vidrio, y
se le nota la curvatura.

## 3. El SDF, y por qué la parte cara cuesta el perímetro

La geometría sigue saliendo de un campo de distancia con signo:

```
qx = |px - cx| - (W/2 - r)          por columna
qy = |py - cy| - (H/2 - r)          por FILA, una vez
d  = (qx>0 && qy>0) ? hypot(qx,qy) - r      esquina
                    : max(qx,qy)   - r      lado recto
```

Los píxeles se miden por su **centro** (`px = i + 0,5`). Con eso el panel
conserva exactamente la huella que tenía con `glInset`, y lo único que cambia
son las esquinas: donde había un escalón binario ahora hay una rampa de
cobertura de un píxel.

El reparto de coste, medido sobre una tarjeta de 440×160 con radio 22:

| Perfil | Píxeles por la ruta CARA (SDF, Fresnel, aberración) |
|---|---|
| ULTRA | 25,5 % |
| ALTA (por defecto) | 20,8 % |
| MEDIA | 16,0 % |

El resto **también refracta**, pero por la vía barata: la columna sale de una
tabla (`glIntSx4`, 960 B), las dos filas de origen se mezclan una vez para
toda la fila (`glIntRowBuf`, 960 B), y no se toca ni el SDF, ni la normal, ni
el Fresnel, ni la aberración, ni la cobertura.

"Centro" ya no significa "sin efecto". Significa "sin la parte cara".

## 4. La normal, la refracción y la luz

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

## 5. Interacción: una máquina de estados, no tres variables

```
IDLE -> DOWN -> ACTIVE -> MOVING -> RELEASE -> DECAY -> IDLE
```

El manejador táctil (`flexPollTouch`, última línea, después de todos los
filtros) escribe **tres enteros** y nada más. Ahí no se captura pantalla, no
se desenfoca, no se calcula ningún SDF y no hay hilo nuevo ni bucle de
animación por elemento: eso metería el coste del efecto dentro de la ruta del
tacto, que es donde no puede estar. Quien interpola es el compositor, en el
cuadro que ya iba a dibujar.

Cuatro cosas deforman, y las cuatro salen de ese estado:

**1. Presión.** Hundimiento radial bajo el dedo mientras está apoyado. Sube
en 90 ms, se mantiene, y al soltar decae en 260 ms con curva cuadrática.

**2. Onda.** Cada impacto lanza un frente que **se expande**: el lóbulo está
a `waveSpd × edad` píxeles del epicentro y avanza con el tiempo, así que un
píxel fijo ve pasar la compresión, luego la rarefacción, y vuelve a quedarse
quieto. Es un ciclo único —no un tren de senos, que se leería como agua— y
modifica el **muestreo**, no el brillo. Medido: el alcance crece 262 → 374 →
486 px, y a media vida el desplazamiento en el epicentro es 0 mientras en el
frente es 18/16 px. Es decir, un anillo que viaja.

**3. Velocidad.** El arrastre estira el material en contra del movimiento.
Como es un campo **uniforme**, va horneado en la tabla de columna y en el
desplazamiento vertical de la fila: cuesta cero por píxel y —lo que importa
más— no obliga a tratar el panel entero por la ruta cara durante un scroll.
Acotada en la entrada a 64 px por muestra: un salto de 400 px produce 1,56 px
de estiramiento, no 7.

**4. Estela.** El recorrido siembra impactos pequeños, por **distancia
recorrida** y no por tiempo ni por muestra, así que un arrastre lento no
siembra nada y uno rápido siembra a intervalos regulares en el espacio.

Los impactos viven en un array **fijo** de 3. No hay lista dinámica, no hay
`malloc` por toque, y un gesto rápido no puede hacer crecer nada: el hueco
más viejo se recicla. Medido: 200 muestras de arrastre dejan 3 impactos, el
tope exacto.

### Recuperación

Todo vuelve a cero sin residuo. En el laboratorio óptico, el cuadro en reposo
después de un ciclo completo (toque + onda + arrastre + soltar + esperar) es
**bit a bit idéntico** al de antes de tocar: 0 píxeles de diferencia. Lo
mismo para un botón pulsado y para un dedo que cruza cuatro superficies.

### Qué se anima solo, y qué no

El **Panel Rápido** tiene la maquinaria para animarse sin que nada más
cambie: `qsTick()` pide recomponer la banda que la deformación ensucia este
cuadro unida con la del anterior —para que la cola de la onda se borre de
donde estuvo— y publica sólo esa banda. No es un repintado de 480×800: un
impacto recién nacido son unas decenas de filas.

En el **resto de superficies** la deformación se ve en los cuadros que el
sistema ya iba a repintar, que durante una interacción son precisamente los
de la superficie que el dedo está tocando: el destello de un control, la fila
resaltada de una lista, el botón pulsado, el icono del escritorio, y todo el
contenido mientras se hace scroll. Forzar un repintado global por cada toque
sería exactamente el *full-screen redraw por cada touch* que hay que evitar.

### La tarjeta bajo el dedo sale de la caché

`drawGlassCardFlat` cachea la tarjeta porque sobre un fondo plano el
resultado no depende de dónde se dibuje. Eso deja de ser cierto en cuanto hay
una interacción: el hundimiento y la onda sí dependen de dónde está el dedo
respecto a esa tarjeta. Así que la tarjeta que la interacción alcanza se
compone en vivo —en una lista es una, como mucho dos— y las demás siguen
siendo un `memcpy` por fila.

Dentro de la caché el toque sigue vetado (`gGlassNoTouch`): un hundimiento
grabado ahí se quedaría pegado para siempre y en el sitio equivocado.

### Contenido propio frente a contenido de detrás

El texto y los iconos **de un botón** se dibujan después de su superficie de
vidrio: son contenido del propio componente y conservan su legibilidad, que
es lo que se espera. Lo que se deforma es lo que está **detrás** del vidrio,
que es de donde el compositor toma su fondo.

## 6. Perfil de calidad y calidad adaptativa

Ni un número mágico del material fuera de `kGlassQ[]`, en `Glass.h`:

```
        band blur int refr chr fres pow  hi irid tAmp  tR wAmp wSpd vAmp trail
ULTRA     14   3   48   64  12   52   4   46  14   72  60   40    6   26    22
ALTA      11   3   40   56   8   46   4   42  10   62  54   32    6   20    18
MEDIA      8   4   28   44   0   38   3   34   0   46  46   20    5   14     0
BAJA       0   6    0    0   0    0   0    0   0    0   0    0    0    0     0
```

`band`, `blur` y `tR` en píxeles; `int`(erior), `refr`(acción), `chr`(oma),
`tAmp`, `wAmp` y `vAmp` en 1/16 de píxel; `wSpd` en píxeles por 16 ms; el
resto son alphas 0‑255.

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
iridiscencia → aberración cromática → estela → ancho de banda → amplitud de
la onda → campo interior. BAJA vuelve al material clásico completo. Lo que **nunca** se toca: la respuesta al tacto, la corrección
de la interfaz y la estabilidad del panel.

El **modo visual eficiente** (`gEffMode`, lo enciende “Optimizar Flex OS”
cuando la presión de memoria sigue alta) pone techo MEDIA.

---

## 7. Memoria

El material avanzado **no reserva ni un byte de PSRAM** y no hace ni una
asignación dinámica. Todo lo suyo:

| Qué | Tamaño | Dónde | Vida |
|---|---:|---|---|
| `glRing[8][480]` — anillo de filas | 7.680 B | RAM interna | estático |
| `glIntSx4[480]` — campo interior por columna | 960 B | RAM interna | estático |
| `glIntRowBuf[480]` — fila de origen pre‑mezclada | 960 B | RAM interna | estático |
| `kGlFall` + `kGlRecip` + `kGlWave` + `kGlassQ` | 254 B | flash (`.rodata`) | constante |
| `GlassEdge` | 264 B | pila | por panel |
| `GlassPx` | 24 B | pila | por píxel |
| `glImp[3]` — impactos | 36 B | RAM interna | estático |

Total: **9,6 KB de RAM interna**, 254 B de flash, cero PSRAM, cero
asignaciones dinámicas.

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

## 8. Precisión numérica

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

## 9. Coste medido

`tests/host/ino_compile.cpp` → `testVidrioAvanzado()` cronometra el
compositor **completo** (copia + desenfoque + material) sobre la misma
geometría y el mismo fondo, mejor de 3 × 30 repeticiones:

```
panel 440x160 r22
  ULTRA   5.686 us   +87 % sobre el material clasico
  ALTA    5.414 us   +78 %
  MEDIA   4.954 us   +63 %
  BAJA    3.035 us     ---   (= el material que habia antes)
```

Ese sobrecoste es el precio de que **todos** los píxeles resamplen en vez de
sólo el 21 %: un píxel de interior cuesta unas dos mezclas más que el de la
versión anterior, y ahora son el 79 % del panel. A cambio, lo que hay detrás
del panel se dobla de verdad.

La cifra que se traslada a la placa es el **porcentaje**, no el absoluto: las
dos rutas hacen exactamente el mismo desenfoque y se diferencian sólo en la
pasada de composición. En el P4 ese porcentaje debería ser **menor** que en
el PC, porque allí el desenfoque —cuatro pasadas sobre PSRAM— pesa
proporcionalmente más que la única pasada de composición. No está medido en
hardware: es la razón de que exista la calidad adaptativa, que corta por el
coste real y no por una estimación.

---

## 10. Depuración

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

## 11. Qué se comprueba, y dónde

`tests/host/ino_compile.cpp` → `testVidrioAvanzado()`, contra el código de
verdad y con AddressSanitizer + UndefinedBehaviorSanitizer:

**El material**
* el **centro** del panel refracta (38 % de sus píxeles cambian frente al
  material clásico) — la comprobación es la contraria que en la primera
  versión, donde el centro tenía que salir idéntico;
* la **huella** no cambia: ni un píxel fuera del rectángulo, en los tres
  compositores;
* es **determinista** sobre un fondo limpio;
* las **esquinas** ganan cobertura parcial (el antialias que sustituye al
  escalón);
* el estilo **Plano** no cambia ni un píxel y el perfil **BAJA** reproduce el
  material de siempre;
* la **banda pre‑desenfocada** coincide con el panel normal (fondo de franjas
  horizontales a propósito: es lo que hace que la prueba distinga un paso de
  fila equivocado, que no se sale de ningún buffer y ningún sanitizer vería).

**La interacción**
* la **onda se propaga**: alcance 262 → 374 → 486 px;
* y es un **anillo**, no un disco: a media vida el desplazamiento vale 0 en
  el epicentro y 18/16 px en el frente;
* los **impactos están acotados**: 200 muestras de arrastre dejan 3, el tope;
* la **velocidad está acotada**: un salto de 400 px/muestra da 1,56 px;
* el **ciclo de vida termina** siempre, sin presión, sin impactos y con la
  caja interactiva vacía;
* `glassIaBox` es una **consulta pura** (20 llamadas dan lo mismo) — importa
  porque la llama cada tarjeta cacheada de la pantalla;
* la **región sucia** incluye la del cuadro anterior y cabe en la pantalla;
* el **toque no se graba** en la tarjeta cacheada, y un cambio de perfil la
  recompone.

**Robustez**
* **400 paneles al azar** (geometría, radio, recorte, perfil y un guion
  aleatorio de down/move/up/reset) sin un solo acceso fuera de rango;
* el **presupuesto de memoria**, atado con `sizeof`.

### Y verlo, que es lo que de verdad decide

```bash
cd tests/host && make lab      # -> build/lab/*.png
```

Veinte cuadros del compositor real, pensados para mirarlos en orden:

| Cuadros | Qué demuestran |
|---|---|
| `00`–`02` | fondo desnudo, material clásico y material refractando |
| `03`–`06` | toque y la onda propagándose en tres instantes |
| `07` | arrastre: seguimiento del dedo y estela |
| `08`–`10` | soltar, recuperando y reposo (**idéntico a `02`**) |
| `11`–`13` | botones: reposo, pulsado y recuperado (**idéntico al reposo**) |
| `14`–`16` | cuatro superficies y un dedo cruzándolas en diagonal |
| `17`–`19` | scroll: quieto, arrastrando y soltado |

Las diferencias medidas entre esos cuadros, que es la prueba de que cada
cosa hace algo y de que todo vuelve a cero:

```
toque                    3,7 %      botón pulsado        8,3 %
onda (t1)                7,6 %      botón recuperado     0,0 %
onda (t3)               10,8 %      4 superficies       48,0 %
arrastre                52,8 %      4 sup. recuperado    0,0 %
recuperando              7,1 %      scroll arrastrando  80,2 %
reposo                   0,0 %      scroll: sólo óptica 19,2 %
```

Esa última cifra es la que responde al requisito de *scroll + deformación*:
con el contenido en **la misma** posición desplazada, el cuadro arrastrando
difiere del soltado en un 19,2 % puramente óptico. El scroll sigue su curso
y el vidrio se deforma encima, sin que uno sustituya al otro.
