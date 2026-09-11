# Flex Compass · Flex OS Ultra · ESP32-P4

Caja de aplicaciones → **Flex Compass**

Brujula nativa de Flex OS Ultra sobre el **GY-BNO085 (9-DOF AHRS)**, el MISMO
modulo y el MISMO driver que ya usa la deteccion de caidas de Flex Device Care.
Este documento es la referencia de la funcion: como esta hecha, que comparte con
Device Care, y — sin adornos — **que ha quedado verificado y que no**.

Solo aplica a **Flex OS Ultra (ESP32-P4)**. `FlexOS_Ultra_S3.ino` y
`FlexOS_Pro.ino` no se han tocado.

---

## 1. Arquitectura: un sensor, un driver, dos consumidores

```
        GY-BNO085  (I2C 0x4A / 0x4B, bus compartido SDA=7 SCL=8)
                       |
              FlexOS_BNO085.cpp          <- EL driver (ya existia)
                       |
              Flex IMU Service           <- FlexOS_Ultra_IMU.h (nuevo)
                       |
         +-------------+--------------------+
         |                                  |
  Flex Device Care                    Flex Compass
  (deteccion de caidas)               (brujula)
```

**No hay un segundo driver.** Flex Compass no habla con el bus: todo lo que sabe
del sensor se lo da el servicio. `tests/host/check_wiring.py` lo vigila: la app
tiene PROHIBIDO llamar a `Wire.`, a `flexBnoBegin()` y a `flexBnoStop()`.

### Por que hizo falta una capa nueva

`flexBnoBegin()` / `flexBnoStop()` son **absolutos**: el ultimo que llama gana.
Con un solo consumidor bastaba. Con dos, **salir de la brujula apagaria el
BNO085 por debajo de la deteccion de caidas** — que es lo que no puede pasar en
una funcion de seguridad.

El Flex IMU Service es una capa fina que anade exactamente dos cosas, y ninguna
de ellas es tocar el sensor de otra manera:

1. **Conteo de consumidores.**
   `imuAcquire()` 0→1 hace `flexBnoBegin()`; `imuRelease()` 1→0 hace
   `flexBnoStop()`; entre medias no se toca nada. `imuServiceTick()` llama a
   `flexBnoTick()` desde `loop()` mientras quede alguien escuchando.
2. **Conversiones de orientacion.** El driver entrega el cuaternion crudo; el
   rumbo de brujula, el cabeceo, el alabeo y la rosa de 16 direcciones son
   conversiones — no medidas nuevas — y viven en un solo sitio para que
   cualquier consumidor futuro obtenga el mismo numero.

### Lo que cambio en Device Care (y lo que no)

`dcSensorStart()` llama ahora a `imuAcquire()` en vez de a `flexBnoBegin()`, y
`dcSensorStop()` a `imuRelease()`. `dcSensorTick()` ya no llama a
`flexBnoTick()` porque lo hace el servicio al principio de la vuelta. **Nada
mas.** Device Care enciende y apaga el sensor exactamente en los mismos sitios
que antes, y la deteccion de caidas consume la misma muestra.

---

## 2. Que ve el usuario

| Pantalla | Que hay |
|---|---|
| Sin IMU | "Requiere modulo IMU", el modulo BNO085 **dibujado por codigo** y el estado real del sondeo |
| Detectando | "Buscando modulo IMU" · "Comprobando el bus I2C" |
| Brujula | Rosa de los vientos girando, rumbo con un decimal, rumbo de 16 puntos (corto y largo) y la precision que declara el sensor |
| Al deslizar hacia arriba | Tarjeta Liquid Glass: `BNO085 ● Conectado` / `AHRS ● Activo` |
| Mas abajo | Seccion **Sensor IMU**: el modulo en 2.5D moviendose con la orientacion real, y acelerometro / giroscopio / magnetometro / AHRS con su estado |
| Mas abajo | **Datos tecnicos**: heading, pitch, roll, estado de cada fuente y precision |
| Menu `⋮` | **Informacion del sensor** |

La tarjeta de estado **no se ve en el primer cuadro**: el hero de la brujula
ocupa el viewport entero y la tarjeta nace justo debajo.

---

## 3. Estados

`FIMU_IDLE` · `FIMU_DETECTING` · `FIMU_NO_IMU` · `FIMU_CONNECTED` ·
`FIMU_AHRS_ACTIVE` · `FIMU_ERROR` · `FIMU_DISCONNECTED`

Son la traduccion de los estados del driver a lo unico que la interfaz necesita
para decidir que pintar. El driver ya exige, para declarar un BNO085, que
alguien conteste en 0x4A/0x4B **y** que responda al Product ID: un ACK suelto no
basta, porque ahi puede haber cualquier otro chip.

Si el modulo se desenchufa con la app abierta, **el ultimo dato no se conserva
como si fuera actual**: `imuHeading()` devuelve false, la app funde a la
pantalla de requisito y, al volver a enchufarlo, lo recupera sin salir.

El driver **no** reintenta solo desde ABSENT/LOST, y hace bien: un cable suelto
no puede dejar el bucle del sistema sondeando I2C para siempre. Quien tiene una
pantalla delante del usuario si puede pedirlo, asi que Flex Compass llama a
`imuRetry(2500)` mientras esta a la vista — y esa llamada no hace nada si el
sensor esta listo o configurandose.

---

## 4. De donde sale el rumbo

El vector de rotacion del BNO085 publica el cuaternion del marco
**Este-Norte-Arriba (ENU)**. Con `q = (i, j, k, real) = (x, y, z, w)`, el eje
**+X** del sensor expresado en el mundo es la primera columna de la matriz de
rotacion:

```
Este  = 1 - 2*(y*y + z*z)
Norte = 2*(x*y + w*z)
rumbo = atan2(Este, Norte)        // horario desde el Norte magnetico
```

Comprobacion: con el cuaternion identidad el eje +X apunta al Este y la formula
da 90 grados. La bateria de host recorre 24 giros alrededor del eje vertical y
comprueba que el rumbo sale `90 - angulo` en todos.

El **"delante"** del equipo es el eje +X del modulo (el que llevan serigrafiado
los GY-BNO085). Si tu montaje apunta con otro eje, lo unico que hay que cambiar
es `imuHeadingFromQuat()`.

Rosa de 16 puntos con sectores de 22,5 grados **centrados** en cada rumbo:
`0 -> N`, `45 -> NE`, `347,2 -> NNO`.

---

## 5. Animacion: dos rumbos a proposito

* el del **sensor** se publica en cuanto llega un informe y no espera a ninguna
  animacion;
* el **visual** (`cmpHeadVis`) lo persigue por el camino angular **mas corto**,
  con constante de tiempo de ~110 ms.

Por eso `359 -> 0` recorre **un grado** y no trescientos cincuenta y nueve al
reves. El numero grande ensena el visual, que es el que acompana a la rosa: si
ensenara el crudo, el texto y la aguja se contradirian.

Nada de esto bloquea: toda la fisica (desplazamiento, inercia, rebote y
suavizado del rumbo) se integra con el `dt` real de `millis()`, sin un solo
`delay()` ni bucle de espera.

Ademas, la app toma **una sola lectura del servicio por vuelta** (`cmpSample()`)
y todo el cuadro usa esa: el numero y la aguja no pueden salir de informes
distintos.

---

## 6. El modulo BNO085 esta DIBUJADO, no es una imagen

`cmpDrawModule()` no carga ningun PNG/JPG/SVG: es una caja en 3D (placa mas
grosor) cuyos ocho vertices se rotan con la orientacion real del sensor y se
proyectan con una axonometria sencilla.

* cara superior, cantos y dorso con `fillQuad`;
* **prueba de cara visible** por el area con signo del cuadrilatero proyectado:
  boca abajo se dibuja el dorso y la serigrafia no se pinta "a traves" de la
  placa;
* borde serigrafiado, encapsulado con su marca de pin 1, tira de seis pines y
  cuatro taladros;
* etiquetas `BNO085` y `9-DOF AHRS` con el tamano que **cabe en el ancho
  realmente proyectado**; de canto no se dibujan en vez de encajarlas a la
  fuerza.

Con los tres angulos a cero sale la vista limpia de la pantalla de requisito.
Escala a cualquier tamano porque es geometria. (Device Care tiene su propio
dibujo del modulo para su pantalla de requisito; son dos vistas distintas del
mismo hardware y ninguna es una imagen.)

---

## 7. Rendimiento

* **Dibujo por bandas.** Cada vuelta marca solo lo que cambia (la rosa, el
  modulo, los datos tecnicos, o el viewport entero si se esta arrastrando) y
  publica **una** banda. Con la brujula desplazada fuera de pantalla su
  animacion no cuesta ni un pixel.
* Rosa a ~30 fps, modulo a ~20 fps, datos tecnicos a 4 fps, y **solo** si el
  valor dibujado cambio de verdad. Quieto sobre la mesa, la app no repinta.
* El disco de la rosa se rellena **solido** tambien con Liquid Glass activo: el
  vidrio se reserva para las tarjetas, que es donde se ve.
* Sin reservas dinamicas: todo el estado de la app es estatico.
  `APP_WEIGHT[IC_BRUJULA] = FLEXMEM_W_LIGHT`.
* Con el servicio sin consumidores el coste es **cero**: `imuServiceTick()` sale
  en su primera linea.

---

## 8. Ciclo de vida

```
abrir      -> imuAcquire()  -> el servicio arranca el sensor si no estaba
suspender  -> imuRelease()  -> la app en segundo plano no consume sensor
reanudar   -> imuAcquire()
cerrar     -> imuRelease()  -> el sensor se apaga SOLO si nadie mas lo tiene
```

**Red de seguridad.** El hosting de Modo PC cierra la ventana de una app
liberando su lienzo *sin* llamar a su `close()` — es asi para todas las apps, no
solo para esta — y con la pantalla en exclusiva de otro subsistema (OTA,
Optimizar) el tick tampoco corre. Por eso `compassIdleGuard()`, despachada desde
`loop()`, suelta el enganche si el tick de la app lleva 3 s sin ejecutarse; el
primer tick siguiente lo vuelve a adquirir.

---

## 9. Lo que se comprueba (tests/host)

`testFlexCompass()` en `tests/host/ino_compile.cpp` ejercita el **codigo real**
que va a la placa:

| Bloque | Que fija |
|---|---|
| Rosa de 16 rumbos | los 8 principales, los 8 intermedios y las fronteras (11,2 -> N, 11,3 -> NNE) |
| Cuaternion -> rumbo | 24 giros alrededor del vertical; pitch y roll de referencia |
| Diferencia angular | 359->0 = +1, 1->359 = -2, media vuelta exacta |
| Suavizado | el visual **nunca** se sale del arco corto al cruzar el Norte, y converge |
| **Reparto del sensor** | abrir la brujula con la deteccion de caidas encendida da 2 consumidores; **cerrarla NO apaga el sensor de Device Care**, y al reves tampoco |
| Ciclo de vida | adquirir/soltar cuadra; cerrar desde suspendida no descuadra el conteo; la red de seguridad suelta y repone |
| Sin BNO085 | no hay rumbo, ni cabeceo, ni sensores disponibles, y la app no se queda con el ultimo valor |
| Vistas | sin orientacion -> requisito; con orientacion -> brujula; al perderla -> requisito |
| Desplazamiento | la tarjeta nace fuera del viewport, rebote a los dos limites, la inercia se para |
| Gestos | un arrastre nacido en la cabecera no desplaza la lista; soltar fuera no deja el gesto enganchado |
| Recorte | el contenido desplazable **nunca** pisa la cabecera ni la barra de navegacion |
| Modulo dibujado | pinta geometria de verdad y no se sale de su caja en ninguna orientacion |
| Simbolo de grado | se dibuja (no es el interrogante de "caracter desconocido") y el formateo del rumbo redondea bien |

Ademas, `testIconosEnSuCaja()` mide los **21** iconos de app (el nuevo incluido)
y `check_wiring.py` verifica los ganchos nuevos de `loop()` y del ciclo de vida,
y **prohibe** que la app toque `Wire.`, `flexBnoBegin()` o `flexBnoStop()`.

---

## 10. Limitaciones reales (lo que NO esta verificado ni existe)

1. **No se ha probado contra un GY-BNO085 fisico.** El doble de I2C de
   `tests/host/inostub/` no simula el sensor, asi que en el PC el driver corre
   AUSENTE. Lo que si esta comprobado ahi es el camino sin sensor (la app no
   inventa nada) y **todas** las conversiones, que son aritmetica pura.
2. **El marco de referencia ENU es una premisa documentada**, no medida. Si en
   tu unidad el vector de rotacion viniera referido de otra forma, el efecto
   seria un desplazamiento constante del rumbo y se corrige en una sola
   expresion (`imuHeadingFromQuat`).
3. **No hay pantalla de calibracion.** El driver compartido implementa
   identificacion, activacion de informes y lectura; **no** implementa los
   comandos SH-2 de calibracion (ME Calibration / Save DCD). Anadirlos tocaria
   un modulo del que depende una funcion de seguridad, asi que se ha preferido
   NO exponer una opcion que no podria hacer su trabajo. Lo que si se ensena es
   el **nivel de precision real** que el propio sensor declara en cada vector de
   rotacion (0..3 -> Sin calibrar / Baja / Media / Alta).
4. **No se muestra la estimacion de error en grados.** El informe del vector de
   rotacion la trae (Q12, en radianes) pero el driver no la publica, y aqui no
   se inventa un "±X grados" que nadie ha medido.
5. **La app no esta localizada.** Sus textos son castellano literal, igual que
   Archivos, Notas, Paint, Device Care y las pantallas de Flex Phone. Pasarla al
   sistema `t(S_*)` es anadir sus cadenas a `CH[][5]` en
   `FlexOS_Ultra_Session.h`.
6. **El signo "mas-menos" (U+00B1) no esta en la fuente del sistema.** Donde
   haria falta se usa el punto medio, que si esta.
