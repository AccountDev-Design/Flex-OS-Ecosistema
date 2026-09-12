# Proteccion contra robo · Flex OS Ultra · ESP32-P4

Ajustes → **Seguridad y privacidad** → **Proteccion contra robo**

Detecta un **patron de movimiento compatible con un posible arrebato** usando el
**TENSTAR / GY-BNO085 (9-DOF AHRS)** — el MISMO modulo y el MISMO driver que ya
usan la deteccion de caidas de Flex Device Care y Flex Compass — y, cuando lo
encuentra, **bloquea el aparato** con el bloqueo de seguridad del sistema.

Este documento es la referencia de la funcion: como esta hecha, en que se
diferencia de la deteccion de caidas, y — sin adornos — **que ha quedado
verificado y que no**.

Solo aplica a **Flex OS Ultra (ESP32-P4)**. `FlexOS_Ultra_S3.ino` y
`FlexOS_Pro.ino` no se han tocado.

---

## 0. Lo primero: que puede y que no puede afirmar

Un IMU mide **aceleracion y giro**. No sabe quien sujeta el aparato ni con que
intencion. Por eso en toda la funcion — interfaz, historial, aviso de bloqueo,
registro del puerto serie y codigo — se dice siempre:

> **Posible arrebato**

y nunca "robo". Lo que se detecta es un patron de movimiento **compatible** con
que alguien arranque el aparato de la mano, y la cifra que lo acompana es una
**confianza (0..100)**, no una prueba.

El bloqueo es una medida preventiva: si el patron era un falso positivo, el
dueno desbloquea y sigue. Ese es el peor caso, y es deliberadamente el barato.

---

## 1. Arquitectura: un sensor, un driver, un motor, dos clasificadores

```
        TENSTAR / GY-BNO085   (I2C 0x4A / 0x4B, bus compartido SDA=7 SCL=8)
                       |
              FlexOS_BNO085.cpp          <- EL driver (ya existia)
                       |
              Flex IMU Service           <- FlexOS_Ultra_IMU.h
                       |                    (conteo de consumidores + orientacion)
              Flex Motion Engine         <- FlexOS_Ultra_IMU.h  (NUEVO)
                       |                    UNA muestra por informe, con nº de secuencia
         +-------------+---------------------------+
         |                                         |
  Flex Device Care                        Proteccion contra robo
  FlexOS_FallDetect.cpp                   FlexOS_Theft.cpp
  "¿hubo una caida?"                      "¿hubo un patron compatible
         |                                 con un posible arrebato?"
         |                                         |
         +--------------> Event Manager <----------+
                        (FlexOS_Ultra_Theft.h)
```

**No hay un segundo driver, ni un segundo bus, ni una segunda inicializacion del
BNO085.** La funcion adquiere el servicio (`imuAcquire`/`imuRelease`, con su
conteo de consumidores) y consume la muestra que publica el motor.
`tests/host/check_wiring.py` lo vigila: la funcion tiene PROHIBIDO llamar a
`Wire.`, a `flexBnoBegin()`, a `flexBnoStop()` y a `flexBnoAccel()`.

### Por que hizo falta el Flex Motion Engine

Con **un** consumidor bastaba con que Device Care llamase a
`flexBnoAccel/Gyro/Quat` y se marcase su propio ritmo. Con **dos**, cada uno
tendria su propia cadencia, su propio "¿ha llegado ya un informe nuevo?" y su
propia idea de que muestra acaba de medirse. **Dos verdades sobre el mismo
instante** es exactamente lo que no puede haber cuando uno de los dos decide si
bloquear el aparato.

El motor publica una muestra por periodo del sensor (50 Hz) con un numero de
secuencia. Quien consume pregunta "¿hay una nueva desde la que yo vi?" y recibe
**exactamente la misma** que el otro.

### Lo que cambio en Device Care (y lo que no)

`dcSensorTick()` ya no llama a `flexBnoAccel/Gyro/Quat` ni lleva su propio
reloj: toma la muestra con `motionTake(&dcMotionSeq)`. **Nada mas.** Misma
cadencia, mismos datos, misma politica de "nunca la misma lectura dos veces",
mismo reinicio del detector al perderse el sensor. La logica de caidas
(`FlexOS_FallDetect.cpp`) **no se ha tocado ni una linea**, y su bateria
(`tests/host/test_fall`) sigue pasando igual.

Se anadio ademas UNA llamada, `tpNoteFall(&ev)`, justo despues de que Device
Care registre una caida. No cambia ninguna decision de Device Care —la caida ya
esta evaluada, registrada y avisada— y sirve solo para correlacionar incidentes
(apartado 5).

---

## 2. Por que no es el detector de caidas

Son dos preguntas distintas sobre la misma senal, y se apoyan en rasgos
**opuestos**:

|                  | caida                      | posible arrebato                |
|------------------|----------------------------|---------------------------------|
| antes            | da igual                   | **en la mano** (micromovimiento)|
| caida libre      | **SI**, es la firma        | **NO** (penaliza: es una caida) |
| tiron            | no hace falta              | **SI**, direccional y sostenido |
| direccion        | irrelevante                | **COHERENTE** (no oscila)       |
| despues          | **se queda quieto**        | **sigue moviendose** (huida)    |

De ahi que una caida sola no pueda producir un arrebato ni al reves: **lo que
suma en uno resta en el otro**. Son dos clasificadores independientes, con su
propio estado, sus propios parametros y sus propias pruebas; lo unico que
comparten es la fuente de datos.

`check_wiring.py` tambien lo vigila: `tpSensorTick()` no puede llamar a
`flexFallFeed(` ni tocar `dcDet`, y `dcSensorTick()` no puede llamar a
`flexTheftFeed(`.

---

## 3. El clasificador (`FlexOS_Theft.cpp`)

Logica pura: entra una muestra medida, sale un veredicto. Sin Arduino, sin
hardware y sin dibujo — por eso se compila y se ejercita **entera en el PC**
(`tests/host/test_theft`).

### 3.1 La maquina de estados

```
  REST / CARRY   normal (quieto sobre una superficie / en movimiento)
        |            un empujon con derivada alta abre el caso
        v
  SUSPECT        movimiento sospechoso: se mide el tiron
        |            el tiron cierra y pasa la puerta de intensidad y duracion
        v
  CONFIRM        ventana de 2,5 s: ¿sigue moviendose? ¿hubo impacto?
        |
        v
  EVAL           confianza -> veredicto -> BLOQUEO si supera el umbral
```

Abrir un caso es barato y **no genera evento por si solo**. Lo que decide es el
cierre del tiron: sin la intensidad o con una duracion que no es la de un tiron
humano, el caso se cierra en silencio y no ensucia el historial.

### 3.2 Que se mide

El driver entrega aceleracion **con gravedad**, giro y cuaternion. La funcion
deriva lo que necesita:

* **gravedad** = eje "arriba" del mundo en el sistema del sensor, sacado del
  cuaternion (`flexTheftUpFromQuat`). Sin sensor fusion se estima con un filtro
  de primer orden sobre la propia aceleracion — peor, pero honesto y
  documentado, no un dato inventado;
* **aceleracion lineal** = medida − gravedad. Es la que importa: un aparato
  quieto en la mano mide 1 g y no se esta moviendo;
* **derivada** de esa aceleracion lineal (g/s): un tiron no solo es fuerte,
  **arranca de golpe**;
* **coherencia de direccion** = |Σ direcciones| / Σ magnitudes ∈ [0,1]. Un tiron
  empuja siempre hacia el mismo lado (0,85–0,98); agitar o correr invierten la
  direccion varias veces por segundo (0,2–0,5).

### 3.3 La ventana temporal

128 ranuras de 16 bytes = **2 KB**, o sea **2,56 s a 50 Hz**, en enteros de 16
bits (milesimas de g, centesimas de m/s², milesimas de rad/s). Estatica, dentro
del propio detector: **ni un `malloc` durante el monitoreo**. Y **no se guarda
en almacenamiento permanente**: lo que sobrevive es el evento, no el flujo del
sensor.

Sirve para mirar **atras** cuando algo pasa, y responde:

* ¿estaba en la mano o apoyado en una superficie? (media del micromovimiento)
* ¿venia de una **caida libre**? (racha de |a| por los suelos)
* ¿la direccion ya se estaba invirtiendo? (agitar / correr)
* ¿habia cadencia de pasos?

Un detalle que costo encontrar: en una caida real **el vuelo no termina antes
del pico, termina EN el pico**. Si la racha de caida libre solo se cerrase al
ver una muestra pesada, esa caida entraria sin la penalizacion — justo la
secuencia que hay que distinguir. La racha se cierra **contra el instante del
tiron**. Lo cubre `testCaidaConGolpeLargo`.

### 3.4 La confianza

| suma                                   | peso |
|----------------------------------------|-----:|
| estaba en la mano antes                 | +10 |
| intensidad del tiron                    | +22 (+10 si es muy violento) |
| arranque brusco (derivada)              | +16 (+8 si es instantaneo)   |
| direccion coherente                     | hasta +24 |
| giro acoplado al tiron                  | +12 |
| separacion posterior (sigue moviendose) | hasta +22 |
| impacto posterior                       | **0 — es CONTEXTO, no puntua** |

| resta                                                   | peso |
|---------------------------------------------------------|-----:|
| la direccion se invirtio (agitar, correr)                | −30 |
| se quedo quieto **sin haberse movido antes**             | −30 |
| hubo **caida libre** antes del pico → es una caida       | −28 |
| cadencia de pasos justo antes                            | −16 |
| giro grande sin traslacion (ensenarselo a alguien)       | −18 |
| estaba apoyado en una superficie, no en la mano          | −12 |

Y **dos condiciones**, que no son puntos:

1. **Direccion coherente.** Un arrebato *es* un tiron en una direccion. Sin eso
   no hay arrebato: hay una oscilacion. Esta puerta existe por un falso positivo
   real encontrado durante el desarrollo — la primera zancada fuerte de una
   carrera sumaba intensidad + arranque + "estaba en la mano" + "sigue
   moviendose" y llegaba al umbral con una coherencia de **0,27**, o sea
   empujando a un lado y al otro. Lo cubre `testCorrer`.
2. **Separacion posterior**, solo en sensibilidad **Baja**.

Nota sobre "se quedo quieto": la penalizacion se aplica **solo si no hubo
separacion antes**. Quedarse quieto tras haberse movido es exactamente lo que
pasa cuando se lo llevan y se les cae — y eso no puede restar.

### 3.5 Sensibilidad

No es un umbral con tres valores: cada nivel mueve el clasificador **entero**.

|                        | Baja | **Normal** | Alta |
|------------------------|-----:|-----------:|-----:|
| intensidad del tiron   | 1,95 g | **1,45 g** | 1,10 g |
| arranque (derivada)    | 92 g/s | **65 g/s** | 48 g/s |
| duracion aceptada      | 90–320 ms | **70–400 ms** | 60–520 ms |
| coherencia exigida     | 0,86 | **0,78** | 0,70 |
| separacion exigida     | 600 ms | **450 ms** | 320 ms |
| confianza minima       | 78 | **68** | 58 |
| separacion obligatoria | **si** | no | no |

El valor recomendado y de fabrica es **Normal**.

---

## 4. El bloqueo

Cuando la confianza supera el umbral:

1. el evento se registra en el historial (con su hora exacta);
2. se escribe **una** linea en el puerto serie;
3. se **arma el bloqueo** y se persiste en NVS;
4. cae la pantalla de bloqueo **del sistema** (`autoLockNow()`).

**No se inventa una pantalla de bloqueo nueva.** Se usa la del sistema, la misma
del bloqueo por inactividad, y encima se dibuja un aviso propio con el candado,
el motivo y **la fecha y hora exactas** del evento. Asi el desbloqueo sigue
siendo exactamente el de Flex OS — PIN, contrasena o gesto —, con su contador de
intentos fallidos y sus esperas, **sin una segunda puerta que auditar**.

El aviso forma parte de `renderLock()`, no es un overlay con temporizador: asi
sobrevive al cambio de minuto, al ir y volver de la pantalla de clave y a
cualquier repintado, sin nada que lo pueda dejar a medias. Ocupa la franja
84..188, que el bloqueo deja libre entre la barra de estado y el reloj gigante.

### Una vez activado, no se cancela solo

Ni porque el aparato deje de moverse, ni porque vuelva a moverse, ni porque
alguien lo recoja, ni porque despues se caiga, **ni reiniciando** — el bloqueo
va en NVS precisamente para que quitar la bateria no sea la via de escape de la
propia proteccion.

La unica salida son los **dos** caminos de desbloqueo explicito que ya tiene el
sistema, y los dos llaman a `tpLockCleared()`:

* `lockOnSuccess()` — PIN o contrasena correctos;
* `lockTick()` — gesto de deslizar, cuando no hay clave configurada.

No hay un tercero, y por eso no hace falta vigilar nada mas.

### Si el bloqueo no cabe AHORA

Con una descarga OTA a pantalla completa, un restablecimiento en curso, la
cortina abierta, el modo seguro, la pantalla suspendida o el arranque, el
bloqueo **no se pierde**: queda pendiente y cae en cuanto la pantalla se
despeja (`tpLockPendingTick()`, en `loop()`). Y **no caduca**: a diferencia de
un aviso, esto es una medida de seguridad.

---

## 5. Event Manager: arrebato + caida es UN incidente

El caso que lo justifica:

```
  14:32:16   patron compatible con posible arrebato  ->  BLOQUEO
  14:32:17   durante la huida el aparato cae al suelo ->  IMPACTO
```

Son **dos detecciones de dos clasificadores distintos, pero un solo incidente**.
Sin correlacion el historial diria "posible arrebato" y, debajo, "posible
caida", como si no tuvieran nada que ver.

La regla es de un solo sentido:

* un arrebato reciente + una caida dentro de **8 s** → el registro que YA existe
  **asciende** a *"posible arrebato + caida"*, conservando su hora, su confianza
  y sus motivos, y anotando el pico del impacto;
* una **caida sola nunca crea un registro aqui**: esa pregunta es de Flex Device
  Care y alli se sigue registrando como siempre;
* un arrebato **jamas** se borra, se cancela ni se degrada por lo que pase
  despues. El impacto se **anade** al evento, no lo sustituye.

Si el propio clasificador ve el impacto dentro de su ventana de 2,5 s, el
incidente nace ya correlacionado y el gancho de Device Care no tiene nada que
ascender: **no se duplica** por ninguna de las dos vias.

---

## 6. Estados de la funcion

| estado | que significa |
|---|---|
| `TP_ST_OFF`      | DESACTIVADA · no monitorea, el sensor esta suelto |
| `TP_ST_ACTIVE`   | ACTIVA · monitoreando |
| `TP_ST_PAUSED`   | PAUSADA · el IMU no esta disponible |
| `TP_ST_DETECTED` | ARREBATO_DETECTADO · el bloqueo esta cayendo |
| `TP_ST_LOCKED`   | BLOQUEADA · hasta el desbloqueo explicito |

Sin estados ambiguos: son excluyentes y se calculan de una sola fuente
(`tpStatus()`), que es la que alimentan la fila de Ajustes, la pantalla y el
historial.

---

## 7. Modulo ausente, desconectado y reconectado

**Al entrar** se comprueba el estado REAL del BNO085. Si no esta, no se ensena
una pantalla vacia: hay una pantalla dedicada que dice **que** modulo hace
falta, lo **ensena** — el mismo grafico del TENSTAR GY-BNO085 que dibuja Flex
Device Care, **por codigo, sin un byte de imagen en flash**, con su placa
morada, sus taladros, su chip, su columna de pads y la rosa de ejes — y da un
boton **Volver a comprobar**.

Mientras no exista un BNO085 funcional: **no se puede activar la proteccion, el
clasificador no corre, no se finge que hay monitorizacion y no se genera ni un
evento.**

**Si se desconecta** con la proteccion activa: notificacion *"Modulo IMU
desconectado · Proteccion pausada"*, el clasificador se reinicia, el estado pasa
a PAUSADA y el sistema sigue funcionando. Sin crash, sin reinicio, sin bloqueo
accidental, sin pantalla congelada y sin bucle infinito.

**Cuando vuelve**: notificacion *"BNO085 reconectado · Proteccion contra robo
restaurada"* y vuelta al monitoreo normal.

El re-sondeo es **a peticion** (`imuRetry`), nunca en bucle: un cable suelto no
puede convertir el bucle del sistema en un sondeo I²C permanente.

---

## 8. La interfaz (480 × 800)

```
  0..62    cabecera: volver + "Proteccion contra robo"
  72..148  Proteccion                                  [ interruptor ]
 158..246  BNO085 ·  TENSTAR / GY-BNO085                    Conectado
           AHRS · 9DOF · I²C
 256..486  [ ANIMACION ]
 496..590  Proteccion activa / Monitoreando movimiento...
           ⚠ Posible arrebato   12/09/2026 · 14:32:18   Bloqueo activado
 600..658  Historial de seguridad                                    >
 666..724  Sensibilidad                                         Normal
 736       "El IMU solo puede ver un patron de movimiento, no una intencion."
```

Las coordenadas viven en un solo sitio (las constantes `TP_*`); el dibujo y el
tactil leen de ahi, asi que **el sitio donde se ve y el sitio donde responde no
pueden separarse**. Los textos pasan por `uiFontFit` o `drawTextClip`, el cuerpo
largo de la pantalla de requisito se parte por palabras respetando UTF-8 y
termina en puntos suspensivos si no cabe, y la lista del historial tiene recorte
propio — que es lo que impide que una tarjeta a medio salir se dibuje encima de
la cabecera.

`test_ino` comprueba que **ningun bloque se solapa con otro ni se sale de los
800 px**, y que el aviso del bloqueo no pisa ni la barra de estado ni el reloj
gigante.

Material: `uiSurface()` — o sea el mismo punto de decision Liquid Glass / Plano
que usa todo el sistema. No hay una segunda paleta ni un segundo material.

### Historial de seguridad

Agrupado por dia con **HOY** y **AYER** en claro (calculados contra el dia local
de *ahora*, no contra un texto guardado). Cada tarjeta abre su **detalle**:
fecha, hora exacta, tipo, confianza, intensidad del tiron, duracion, separacion,
impacto posterior si lo hubo, la **secuencia** de rasgos que vio el clasificador
y el estado del bloqueo. Sin esa secuencia, una confianza de 82 no se puede
revisar.

Son 16 registros de 16 bytes (264 bytes con cabecera) en
`/System/theft.bin`, con la misma escritura recuperable que usa el resto del
sistema (`flexFsWriteBinAtomic`).

---

## 9. La animacion

Un **bucle perfecto de 7,2 s** dibujado por codigo — ni un byte de imagen, ni un
GIF, ni un video — que explica de un vistazo para que sirve la funcion:

```
  0,00  una mano sostiene el aparato con calma
  0,16  otra mano se acerca por la derecha
  0,34  ARREBATO: el aparato sale disparado, la mano se abre
  0,46  huida: el aparato y la mano salen de escena
  0,60  el aparato ha quedado separado; aviso
  0,70  aparece el estado de seguridad
  0,76  el candado se cierra
  0,84  pulso de confirmacion
  0,90  transicion suave al primer estado
  1,00  = 0,00
```

Todo sale de la interpolacion de un unico parametro `u ∈ [0,1)` sobre
rectangulos redondeados, circulos y segmentos suavizados. Que sea funcion de `u`
y no de "un paso por cuadro" es lo que hace que **la velocidad no dependa de los
fps** y que perder un cuadro no desincronice nada.

**La receta del bucle perfecto no es un corte, es una coincidencia**: cada
magnitud animada vale en `u = 1` exactamente lo mismo que en `u = 0`.
`test_ino` lo comprueba dibujando los dos cuadros y comparandolos **pixel a
pixel** — y se verifico que la prueba tiene dientes rompiendo el bucle a
proposito y viendo cómo falla.

### Rendimiento

* se repinta **solo el rectangulo interior** de su tarjeta (420 × 190), no la
  pantalla. El marco se dibuja una vez y la banda se siembra en `bbuf` desde
  `fb`;
* se compone en `bbuf` y se publica con **un solo `present()`** por cuadro: el
  mismo camino anti-parpadeo de la tarjeta del cronometro y del aviso de caida;
* **cero reservas por cuadro** y **cero `delay()`**: `check_wiring.py` prohibe
  `delay(` dentro de la animacion;
* el escenario se repinta entero cada cuadro, asi que no quedan restos del
  anterior (comprobado);
* nada escribe fuera del rectangulo (comprobado a lo largo de todo el bucle).

Medido en el PC, un cuadro cuesta **0,25 ms** frente a **7,5 ms** de un panel de
vidrio comparable del sistema — unas **30 veces menos**. El numero absoluto no
vale para el P4, pero la comparacion si: es mucho mas barato que algo que ya se
dibuja con soltura en la placa.

---

## 10. Memoria y CPU

| que | cuanto |
|---|---|
| detector completo (con la ventana temporal) | **2 360 bytes**, estatico |
| ventana temporal | 2 048 bytes (2,56 s a 50 Hz) |
| historial en RAM y en disco | 264 bytes |
| reservas dinamicas durante el monitoreo | **ninguna** |
| coste por vuelta sin la funcion activada | sale en la primera linea |
| assets en flash | **ninguno** (todo dibujado) |

---

## 11. Que se ha verificado

Todo lo de aqui se ha **ejecutado**, no razonado.

### `tests/host/test_theft` — 85 comprobaciones, clasificador puro

Con sanitizers (ASan + UBSan). Cada escenario sintetiza una senal con la forma
del gesto real y comprueba el **veredicto**:

| escenario | resultado exigido |
|---|---|
| arrebato tipico (en la mano → tiron → se lo llevan) | **posible arrebato**, confianza ≥ 80 |
| arrebato + caida posterior | **posible arrebato**, con impacto anotado, y la caida NO lo cancela |
| arrebato y se lo llevan andando | **posible arrebato** |
| caida sola | normal |
| **caida con golpe largo y direccional** | normal (se reconoce la caida libre previa) |
| caminar | normal |
| correr | normal |
| dejarlo en la mesa / recogerlo | normal |
| agitarlo (3 Hz, hasta 2,2 g) | normal |
| golpecito seco de 30 ms | normal — ni llega a evaluarse |
| ensenarselo a alguien (giro puro) | normal |
| sensibilidad Baja / Normal / Alta | cambian el veredicto del mismo gesto |
| tirones encadenados | un solo incidente (periodo refractario) |
| NaN, infinitos, saturacion, punteros nulos | se descartan sin reventar |
| sin sensor fusion | funciona con la gravedad estimada, sin inventar arrebatos |
| hueco de 3 s en las muestras | no deja un caso a medias |

### `tests/host/test_ino` — integracion dentro del sketch

Con sanitizers. Comprueba lo que solo existe montado dentro de Flex OS:

* **el reparto del sensor**: activar la proteccion adquiere el MISMO servicio;
  apagar la deteccion de caidas no deja a la proteccion sin sensor y al reves;
  activar o desactivar dos veces no descuadra la cuenta;
* **con su pantalla delante** el enganche se conserva aunque se apague la
  funcion (si no, apagar el interruptor dejaba la pantalla diciendo "requiere
  modulo IMU" con el modulo puesto — **fallo real encontrado y corregido**);
* **la red de seguridad**: si la pantalla se pierde sin pasar por la salida, el
  enganche de "solo mirar" se suelta solo — y nunca el de una proteccion activa;
* **los cinco estados** de la funcion;
* **el Event Manager**: la caida no crea una segunda entrada, el registro
  asciende conservando confianza y hora, se anota el impacto, una caida repetida
  no duplica, una caida fuera de la ventana no se cuela, y una caida sola no
  entra;
* **el bloqueo**: cuando se puede y cuando no, que queda armado y pendiente si
  la pantalla esta ocupada, que se persiste en NVS, que **nada** lo cancela
  (moverse, una caida posterior, reintentarlo) y que el desbloqueo explicito SI
  lo levanta y tambien se persiste;
* **sin modulo**: 200 vueltas del tick no evaluan ni un evento, no se inventa
  ningun bloqueo y el estado que se ensena es PAUSADA;
* **la maqueta**: ningun bloque se solapa ni se sale de los 800 px, las tarjetas
  respetan los margenes y el aviso del bloqueo no pisa ni la barra de estado ni
  el reloj;
* **la animacion**: bucle perfecto pixel a pixel, se mueve de verdad por el
  camino, no escribe fuera de su rectangulo en todo el recorrido, repinta el
  escenario entero cada cuadro y cuesta menos que un panel de vidrio;
* **la fila de Ajustes** dice el estado real;
* **la sensibilidad** mueve intensidad, coherencia, separacion y confianza.

### `check_wiring.py` — 60 ganchos verificados

Vigila que el motor publique, que los dos clasificadores consuman de el, que la
proteccion adquiera y suelte el servicio, que el bloqueo se persista y se
aplique, que el aviso se dibuje en el bloqueo, que los dos caminos de desbloqueo
lo levanten y que Device Care correlacione. Y prohibe lo contrario: tocar el
bus, encender o apagar el sensor por la cara, mezclar los dos clasificadores y
meter un `delay(` en la animacion.

### Bateria completa

`make` en `tests/host`: **todas las pruebas de host pasan**, incluidas las 111
de la deteccion de caidas, que siguen verdes despues de pasar Device Care por el
Flex Motion Engine.

---

## 12. Limitaciones reales

Hay que decirlas.

1. **No se ha probado en la placa.** Todo lo anterior corre en el PC contra
   dobles de Arduino y senales sintetizadas. El sketch **compila** entero, pero
   nadie ha arrancado esto en un ESP32-P4 con un GY-BNO085 conectado.
2. **Los umbrales son un punto de partida documentado, no valores calibrados.**
   Salen de la fisica del gesto y de lo que el BNO085 puede medir, y estan todos
   centralizados en `flexTheftDefaults()` precisamente para poder ajustarlos
   contra el modulo real sin tocar la logica. **La tasa de falsos positivos y de
   fallos con movimiento humano real esta sin medir.**
3. **Los escenarios de prueba son sinteticos.** Tienen la forma del gesto real,
   pero una senal real trae ruido, deriva del giroscopio, saturacion y errores
   de calibracion que un generador no reproduce.
4. **`autoLockNow()` bloquea el bucle ~200 ms** mientras corre la animacion del
   bloqueo. Es el comportamiento que ya tenia el bloqueo por inactividad del
   sistema — se reutiliza tal cual en vez de escribir un segundo mecanismo de
   bloqueo — pero conviene saber que ese coste existe.
5. **La animacion no se ha visto en movimiento.** Su fluidez esta respaldada por
   una medida de coste y por la comprobacion de que el bucle cierra, no por
   haberla mirado en la pantalla.
6. **Solo se soporta el TENSTAR / GY-BNO085.** Otros IMU no estan activados como
   compatibles, a proposito.
7. **Sin sensor fusion** (cuaternion) la gravedad se estima con un filtro y la
   deteccion es menos fiable. Funciona y no inventa eventos, pero es un modo
   degradado.
8. **El texto del detalle se muestra en espanol o en ingles.** Los nombres de la
   secuencia de rasgos no tienen todavia columna propia para frances, portugues
   e italiano; esos idiomas caen en ingles, igual que hace el chino en el resto
   de tablas del sistema.
