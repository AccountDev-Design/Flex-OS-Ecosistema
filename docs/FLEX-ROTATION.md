# Flex Rotation — auto-rotación del sistema

Función **nativa** de Flex OS Ultra. No es una aplicación: no tiene icono, no
aparece en la Caja de aplicaciones, no abre ventana y no tiene pantalla de
ajustes propia. Se enciende y se apaga desde el control **Auto-rotación** del
Panel Rápido.

---

## 1. Arquitectura

```
FlexOS_BNO085.cpp          driver SHTP / SH-2 sobre el Wire del táctil
        │
FlexOS_Ultra_IMU.h         Flex IMU Service — reparto del sensor (refcount)
        │                  + conversiones puras de orientación
        ├──────────────────────────────┐
        ▼                              ▼
FLEX ROTATION                    FALL DETECTION
FlexOS_Ultra_Rotation.h          FlexOS_FallDetect.cpp
FlexOS_RotationCore.h                   │
        │                               ▼
        ▼                        Flex Device Care
orientación de la UI             (aviso de caída, Post-Impact Check)
```

Los dos motores son **independientes** y comparten el módulo a través del
conteo de consumidores del servicio (`imuAcquire` / `imuRelease`):

* encender o apagar Auto-rotación **no** arranca ni para el BNO085 si Device
  Care lo está usando;
* Flex Rotation **no llama nunca** a `flexBnoBegin()`, `flexBnoStop()` ni
  `flexBnoRescan()`, y no cambia ni un parámetro del sensor;
* `rotEngineTick()` corre en `loop()` **después** de `dcSensorTick()`: la
  detección de caídas se queda siempre con la muestra primero.

Girar el aparato **no** es caerse, y el motor de rotación no alimenta ni
consulta al detector de caídas. Lo único que hace con el movimiento brusco es
**dejar de girar la pantalla** mientras dura.

---

## 2. Qué superficies giran, y por qué no todas

El compositor rasteriza sobre un panel **vertical de 480×800** y ya sabe girar
90° (`gLand`): es lo que usan Modo PC y Juegos desde siempre. Lo que no existe
es una geometría dinámica — hay más de dos mil usos literales de `SCR_W` /
`SCR_H` repartidos por el sistema, y el escritorio, el bloqueo, la cortina y la
Caja de aplicaciones maquetan contra ellos.

Flex Rotation gira **solo lo que el sistema ya sabe maquetar contra un lienzo
arbitrario**: las apps `APP_FLEX`, que son las que Modo PC dibuja 1:1 dentro de
ventanas de cualquier tamaño todos los días (su tamaño por defecto, ~430×268,
es **más pequeño** que los 800×320 del área de ventana en horizontal).

| Superficie | Gira | Motivo |
|---|---|---|
| Reloj, Galería, Almacenamiento, Educación, Navegador, Code IDE, Bienestar, Calculadora, Calendario, Flex Phone, **Flex Device Care** | **sí** | `APP_FLEX`: maquetan contra `gAppW` / `uiW()` / `uiH()` |
| Inicio, bloqueo, Panel Rápido, Caja de aplicaciones, Recientes, Ajustes, Wi-Fi, Archivos, Flex Vault | no | maquetan contra `SCR_W` / `SCR_H` |
| Multimedia, Notas, Paint, Cámara, Clima, Flex Compass | no | no son `APP_FLEX` |
| **Juegos** | no (excluido) | `APP_LAND`: conservan su propia orientación |
| **Modo PC / DeX** | no (excluido) | tiene su propia gestión de ventanas |
| Flex Store | no (excluido) | puede hospedar una app `flex-app-v1`, que controla `gLand` por su cuenta |

Forzar el resto daría exactamente lo que esta función tiene prohibido
producir: texto cortado, botones encima de otros y táctil desplazado.

---

## 3. Orientaciones soportadas

Dos, no cuatro. La rotación del motor es **una** transformación fija:

```
putPhys(lx,ly) → x = (SCR_W-1) - ly ,  y = lx
```

y esa misma expresión está replicada, literal, en el escalador y el táctil de
Modo PC, en el rasterizador de Jumper (Juegos), en el volcado girado del
reproductor, en el aviso de caída de Device Care y en el puente de las apps
`flex-app-v1`. Añadir la transformación espejo obligaría a tocar los seis
sitios — incluidos Juegos, Modo PC y una pantalla de **seguridad** — para ganar
una sola dirección de giro.

El núcleo de decisión **distingue las cuatro posturas físicas** (vertical,
horizontal A, horizontal B y vertical invertida). El motor aplica dos:

* `FROT_ORI_PORTRAIT` — vertical
* `FROT_ORI_LAND` — horizontal con el borde **derecho** del panel arriba

Las otras dos se reconocen y **no se aplican**: la interfaz se queda como está
en vez de pintarse del revés.

---

## 4. Cómo se decide la orientación

`FlexOS_RotationCore.h` es lógica pura (ni hardware ni dibujo) y se ejercita
entera en el PC: `tests/host/test_rotation`. Tres filtros, en este orden:

1. **Planitud.** Boca arriba o boca abajo sobre una mesa, la gravedad casi no
   se proyecta sobre el plano de la pantalla: el ángulo que sale de ahí es
   ruido. No se clasifica nada — se **conserva** la postura que ya había.
2. **Movimiento.** Si `||a|−1 g| > 0,35 g` o el giroscopio pasa de 2,2 rad/s
   hay un evento de movimiento activo: durante el evento la interfaz **no se
   mueve**, y al terminar hacen falta 400 ms de calma antes de admitir un
   cambio.
3. **Permanencia con histéresis.** Entrar en una postura exige estar a menos de
   32° de su referencia; salir de la actual exige pasar de 58°. Entre las dos
   hay 26° de **zona muerta** en la que no pasa nada. Y la candidata tiene que
   mantenerse 650 ms seguidos.

El montaje del módulo respecto de la pantalla está en **un solo sitio**
(`IMU_MOUNT_*` en `FlexOS_Ultra_IMU.h`, junto al mismo "delante" que usa la
brújula). Si el GY-BNO085 va pegado con otra orientación, eso es lo único que
hay que cambiar.

---

## 5. La transición

Atómica, validada y reversible:

```
postura estable  →  ¿la superficie puede girar?  →  bloqueo (gRotBusy)
                                                          ↓
                                     superficie LIMPIA (un solo fillRect)
                                                          ↓
                                     cabecera + enter() con gRelayout
                                                          ↓
                                                   validación
                                                    ↙        ↘
                                                  sí          no
                                                  ↓            ↓
                                              un flush     ROLLBACK
```

* **Un solo repintado.** Se compone en `fb` y se publica con **un**
  `flxFlushAll()` al final: no hay borrado-y-repintado en cadena ni frames
  negros intermedios.
* **Nada sobrevive.** El `fillRect` inicial cubre el panel entero en las dos
  orientaciones, así que ni un texto, ni un botón, ni una tarjeta de la
  orientación anterior puede quedar debajo del layout nuevo.
* **`gRelayout`**, no re-inicialización: la app re-dibuja con la geometría
  nueva conservando su estado lógico (la nota, el lienzo, el scroll).
* **Validación** (`rotValidate`): el alcance rotado se cerró y quedó
  equilibrado, el lienzo global volvió a 480×800, los framebuffers siguen
  vivos, la app no navegó fuera y la transición no pasó de
  `FROT_TIMEOUT_MS`. Si algo falla → se restaura la orientación anterior y se
  recompone con ella.
* **Watchdog** (`rotWatchdogTick`): si `gRotBusy` sobreviviera a la llamada, se
  suelta y el motor queda coherente. *Límite honesto*: si un `enter()` se
  colgara dentro de la transición, esto no lo puede interrumpir — corre en el
  mismo hilo. De eso se encarga el TWDT, como con cualquier otro bucle que no
  vuelve.

---

## 6. Táctil

Dentro del "alcance rotado" (mismo patrón que `dexHostRun`) se desvía el estado
global — rotación, lienzo lógico y táctil — y se restaura pase lo que pase. El
mapeo es el mismo que usan Modo PC, el reproductor y el aviso de caída:

```
lx = T.y            ly = (SCR_W-1) - T.x
```

Los deslizamientos también giran (un barrido físico hacia abajo es, en el
lienzo horizontal, un barrido hacia la derecha), y al cerrar el alcance se
devuelven al táctil físico los **consumos** que hizo la app (poner `tap` a
`false` es como una capa dice "este toque es mío").

La barra de navegación del sistema se dibuja **con la maqueta** y sus tres
botones responden en su sitio. Las salidas (atrás / inicio / recientes) se
**aparcan** y se ejecutan ya fuera del alcance: el escritorio, Recientes y las
pantallas del sistema se componen siempre en vertical.

---

## 6 bis. Recortes y bandas: `gClipLogical`

Con la superficie girada, quien dibuja maneja coordenadas **lógicas**
(lx 0..799, ly 0..479) que ya no coinciden con las filas físicas del panel.
Mientras el alcance rotado está abierto —y sólo entonces— cambian dos cosas en
el motor gráfico:

* **Los recortes.** `gClipY0`/`gClipY1` son, en la ruta landscape de siempre,
  una banda de **filas físicas**: así los usan Modo PC y Juegos para acotar lo
  sucio, y eso no se toca. Pero una app que escribe *"recorta de y=96 a y=416"*
  habla de **su** maqueta, así que ahí ese par recorta el eje **Y lógico**. Sin
  esto, el viewport de una lista con scroll acotaría el eje equivocado y las
  filas se dibujarían **encima de la cabecera** — exactamente el apilado que
  esta función no puede producir.
* **Las bandas que se publican.** Una franja horizontal del lienzo girado ocupa
  **todas** las filas físicas del panel (es una columna, no una fila). El
  volcado al panel es por filas contiguas, así que `flxFlush` publica el panel
  entero; y `fbCopyBand` / `fbSeedBand` copian el **rango de columnas** que de
  verdad corresponde, en vez de ensanchar a filas enteras — ensanchar sacaría a
  pantalla el contenido viejo del buffer de composición.

**Límite conocido:** el recorte **horizontal** (`gClipX0`/`gClipX1`) no se
aplica en esta ruta. En el sistema ese par se usa como *"ancho completo"*
escribiendo `SCR_W-1`, que en la maqueta girada serían 480 de 800 y cortaría un
tercio de la pantalla. Entre acotar de menos y cortar contenido, se acota de
menos: el recorte que protege la cabecera es el vertical, y ése sí se aplica.

Los paneles Liquid Glass caen a su **variante plana tintada** en horizontal,
que es el comportamiento que `drawLiquidGlassPanelEx` ya tenía para Modo PC
(la versión con desenfoque indexa el buffer en vertical y no respeta la
rotación). No se ha cambiado el diseño Liquid Glass: se reutiliza su propio
camino landscape.

---

## 7. Sin IMU

Si el usuario pulsa Auto-rotación y no hay módulo compatible:

* **no** se activa, **no** se simula orientación y **no** se usan valores
  falsos;
* el control se queda en **OFF**;
* aparece el aviso *"Auto-rotación requiere un módulo IMU — Conecta un módulo
  TENSTAR BNO085 para utilizar esta función."*

Mientras el driver está identificando el módulo el estado es `PROBING`
("Detectando IMU…"), que **no** es ON: nunca se enciende el interruptor sin
hardware detrás.

**Si el BNO085 desaparece con la función activa**: se deja de girar, se
**conserva** la orientación actual, el control pasa a OFF / "Requiere módulo
IMU" y se informa. No se reinicia nada, no se bloquea la pantalla, no se cierra
ninguna app y Flex Device Care sigue funcionando exactamente igual.

El único módulo soportado es el **TENSTAR / GY-BNO085**.

---

## 8. Estados

| Estado | Significado |
|---|---|
| `FROT_ST_OFF` | apagada por el usuario |
| `FROT_ST_PROBING` | se pidió encenderla: sondeando si hay IMU |
| `FROT_ST_ON` | encendida y con sensor detrás |
| `FROT_ST_NO_IMU` | se intentó encender (o se perdió el sensor) y no hay IMU |

`ON` + `NO_IMU` **no puede existir**: son un solo campo excluyente.

Aparte, dos variables de orientación en vez de una:

* `gRotOri` — la que está **pintada**
* `gRotWant` — la que el sistema **quiere**

Separarlas es lo que permite que una petición sobreviva a no poder atenderse en
el momento: apagar la auto-rotación con la cortina abierta, o girar el aparato
con un juego delante, deja una intención pendiente que `rotConverge()` aplica
sola en cuanto hay una superficie que pueda recibirla.

---

## 9. Cómo se comprueba

```bash
cd tests/host
make ino            # compila el sketch entero + check_wiring (12 ganchos de Flex Rotation)
make                # además ejecuta test_rotation y el resto de la batería
```

`test_rotation` cubre, con señales sintéticas: giro lento franco, giro pequeño,
vaivén en el límite, sacudida continuada, **caída con giro de 180°**, giro
normal sin caída, aparato plano sobre la mesa, horizontal opuesta, hueco largo
sin muestras y reinicio del estado.

---

## 10. Limitaciones reales

1. **Una sola postura horizontal** (§3). La opuesta se detecta y no se aplica.
2. **Inicio, bloqueo, Panel Rápido, Caja de aplicaciones, Ajustes y las apps no
   `APP_FLEX` no giran** (§2). No es un olvido: es la única forma de cumplir
   "no romper la UI existente" sin reescribir la geometría de todo el sistema.
3. **El gesto de la cortina y el de Inicio (modo gestos) siguen viviendo en el
   borde físico del panel**, que con la maqueta girada es un lateral. Se hace a
   propósito — el gesto pertenece a la placa, no a la maqueta — y el indicador
   de gestos se dibuja donde de verdad está el gesto.
4. **Nada de esto se ha probado en hardware.** Todo lo verificado es
   compilación del sketch completo, cableado y la batería de host; no hay
   ninguna placa con un GY-BNO085 en este entorno.
