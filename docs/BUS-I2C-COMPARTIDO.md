# El bus I²C compartido · por qué retirar el IMU congelaba Flex OS Ultra

Solo aplica a **Flex OS Ultra (ESP32-P4)**. `FlexOS_Ultra_S3.ino` y
`FlexOS_Pro.ino` no llevan BNO085 y no se han tocado.

---

## 1. El síntoma

Con Flex OS Ultra funcionando y el **GY-BNO085** conectado, retirar físicamente
el módulo congelaba el sistema entero: el táctil dejaba de responder, la
interfaz dejaba de reaccionar y el aparato quedaba bloqueado indefinidamente.

No era un fallo de la brujula ni de Device Care. Era del **bus**.

---

## 2. Un solo bus para tres chips

```
  GPIO7 (SDA) / GPIO8 (SCL)  ·  400 kHz
        |            |            |
    GT911         ES8311      GY-BNO085
   (táctil)       (audio)       (IMU)
```

El táctil, el códec y el IMU **comparten el mismo bus**, y todos se hablan desde
el **mismo hilo** (el del `loop()`). Esa regla es deliberada y sigue en pie: dos
tareas hablando por un I²C sin protección es lo que corrompe el bus.

La consecuencia es la que importa aquí: **lo que le pase al bus por culpa del
IMU se lo pasa al táctil**.

---

## 3. Las tres causas raíz

### 3.1 El driver seguía leyendo un bus muerto, para siempre

`flexBnoTick()` salía pronto solo en `FLEXBNO_ST_ABSENT`. El bloque de **lectura**
corría para cualquier otro estado, `FLEXBNO_ST_LOST` incluido — y el
`case FLEXBNO_ST_LOST` de la máquina de estados, que no hace nada, está *debajo*
de ese bloque. O sea: dado el sensor por perdido, el sistema seguía intentando
leerlo **en cada vuelta del bucle, indefinidamente**. Cada intento pagaba el
plazo de espera del driver de I²C, desde el mismo hilo que sondea el táctil.

**Arreglo:** desde `LOST` no se toca el bus. Quien decide reintentar es el Flex
IMU Service, con su propio enfriamiento.

### 3.2 Una cabecera SHTP inventada podía parar el sistema de golpe

La cabecera SHTP trae la longitud en 15 bits: hasta **32 767 bytes**. Un bus
sucio —y el de un módulo recién retirado lo es— entrega cabeceras que se leen
como "vienen 32 763 bytes". `bnoGetData()` los vaciaba en trozos de 32, sin
ningún techo de tiempo: **más de mil transacciones I²C seguidas dentro de una
sola llamada**, en el hilo del bucle. La prueba lo mide contra el driver
anterior: **3513 transacciones en UNA vuelta**.

**Arreglo:** tope de longitud (`BNO_MAX_PKT`, 512 B — el paquete legítimo más
grande del sensor es su anuncio inicial, del orden de 300) y techo de tiempo
*dentro* del vaciado (`BNO_DRAIN_BUDGET_MS`).

### 3.3 Nadie recuperaba el bus, así que el táctil no volvía

Un esclavo que pierde la alimentación **en mitad de una transacción** se queda
tirando de SDA a masa. Desde ese instante el maestro no puede ni generar un
START: fallan **todas** las transacciones, también las del GT911. Sin
recuperación del bus, el táctil no vuelve nunca. Esto es lo que se veía como
"pierdo el táctil y ya no vuelve".

**Arreglo:** recuperación estándar de I²C en `FlexOS_Ultra_HAL.h`
(`i2cBusRecover`), disparada por el propio sondeo del táctil:

1. se suelta el periférico;
2. se mueve **SCL a mano** hasta nueve pulsos —un byte y su ACK— hasta que SDA
   vuelva a subir;
3. se da una condición de **STOP** a mano;
4. se vuelve a montar el bus y se comprueba el GT911; si sigue sin contestar
   *y SDA ya está libre*, se le da además su pulso de reset.

Se dispara tras `GT_FAIL_RECOVER_N` (8) lecturas del táctil fallidas seguidas,
con un enfriamiento de 1 s, y **nunca apaga `gtOk`**: rendirse dejaría el táctil
muerto para siempre, que es justo lo que este bloque existe para evitar.

### 3.4 Y una cuarta, de coste: el plazo por defecto

Arduino espera **50 ms** por transacción. El táctil hace dos o tres por vuelta;
contra un bus trabado eso es el bucle entero. `i2cBusBegin()` lo baja a
`I2C_BUS_TIMEOUT_MS` (8 ms), que le sobra a cualquier transacción sana —a
400 kHz no llega ni al milisegundo— y recorta el peor caso de golpe.

---

## 4. Lo que se quitó, y por qué ayuda

La **detección genérica de módulos I²C** (barrido de 0x08 a 0x77 cada 3 s,
listado de lo que contestara y aviso por la isla) se ha retirado entera. Además
de no dar utilidad suficiente —una dirección que devuelve ACK no prueba nada—,
eran **112 direcciones por barrido** sobre el mismo bus del táctil, cada una
pagando el plazo de espera cuando el bus está a medio conectar.

Lo que **no** se ha tocado: el I²C del BNO085. Nunca pasó por ahí. Vive en
`FlexOS_BNO085.cpp` y lo reparte el Flex IMU Service.

---

## 5. Comportamiento garantizado al retirar el módulo

| | |
|---|---|
| Táctil | sigue funcionando; si el bus se traba, se recupera y vuelve |
| UI y navegación | siguen funcionando: el driver deja de consumir vueltas |
| Apps sin IMU | intactas |
| Device Care | la detección de caídas se desarma sola y lo dice |
| Protección contra robo | avisa de la desconexión y **no** clasifica un arrebato |
| Flex Compass | funde a la pantalla de requisito; se puede seguir navegando y salir |
| Estado | `FIMU_DISCONNECTED` — "lo hubo y se ha ido", no "aquí no hay IMU" |

Y al volver a conectarlo: el servicio re-sondea solo cada 4 s
(`IMU_AUTOPROBE_MS`), pasando por `flexBnoBegin()` y su freno progresivo de 1,5 s
a 8 s. La recuperación vale para **todos** los consumidores, también los que
trabajan sin ninguna pantalla delante.

---

## 6. Cómo se comprueba

```bash
make -C tests/host
```

| Prueba | Qué fija |
|---|---|
| `tests/host/test_imu.cpp` | compila el driver **real** contra un bus simulado (sano / vacío / trabado / basura). Fija: con el sensor perdido, **cero transacciones**; ninguna vuelta por encima del presupuesto; la cabecera imposible no dispara el vaciado; `flexBnoStop()` sobre un sensor perdido no escribe; y la reconexión vuelve a `READY` |
| `testBusI2cCompartido()` (`ino_compile.cpp`) | con SDA a masa: el táctil dispara la recuperación, los pulsos liberan la línea y **el táctil vuelve a entregar contactos**, sin darse por perdido |

Las dos fallan contra el código anterior: 5 y 4 comprobaciones respectivamente.

---

## 7. Lo que sigue sin estar verificado

* **Nada de esto se ha probado tirando de un cable de verdad.** El bus simulado
  habla SHTP y modela SDA a masa, pero un cable arrancado tiene rebotes,
  transitorios de alimentación y estados que no se modelan. La comprobación
  final es en placa.
* Si SDA queda a masa **por hardware** —un chip que sigue alimentado a medias y
  no suelta la línea con nueve pulsos— la recuperación no puede hacer más. El
  sistema no se cuelga (reintenta cada segundo y sigue dibujando), pero el
  táctil no volverá hasta que la línea se libere.
