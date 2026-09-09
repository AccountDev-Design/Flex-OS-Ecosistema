# Flex Device Care (v1.0)

Aplicación de estado, diagnóstico y detección de caídas de **Flex OS Ultra /
ESP32‑P4**. Se integra en la arquitectura que ya existía: no crea un segundo
motor gráfico, ni un segundo gestor de notificaciones, ni un segundo
optimizador.

**Estado de verificación, sin adornos:** todo lo que hay aquí compila para los
tres perfiles de placa y pasa las pruebas de host (con AddressSanitizer y
UndefinedBehaviorSanitizer donde corresponde). **El módulo GY‑BNO085 no se ha
podido probar físicamente**; por eso el driver no anuncia nada como disponible
hasta que el sensor contesta, y la interfaz no dibuja un `✓` que no tenga
detrás un informe real de ese sensor.

---

## 1. Qué se añadió y dónde

### Unidades de traducción propias (código portable o de hardware)

| Archivo | Qué es |
|---|---|
| `FlexOS_Ultra/FlexOS_BNO085.h/.cpp` | Driver del IMU: SHTP (transporte) y lo justo de SH‑2 (control) para identificar el sensor, activar cuatro informes y leerlos. Comparte el `Wire` del táctil, como el códec de audio. |
| `FlexOS_Ultra/FlexOS_FallDetect.h/.cpp` | Lógica de detección de caídas. **Sin Arduino y sin hardware**: entra una muestra medida y sale un veredicto, así que se ejercita entera en el PC. |

### Módulos del sketch (cadena de cabeceras)

| Archivo | Qué es |
|---|---|
| `FlexOS_Ultra_DeviceCare.h` | La app: seis secciones, tarjeta de estado, salud de Flex OS, historial persistente y el **gráfico del GY‑BNO085 dibujado por código**. |
| `FlexOS_Ultra_DeviceTests.h` | Motor de diagnóstico y las cuatro pruebas (sistema, IMU, pantalla, táctil), más Optimización y la pantalla de resultado. |
| `FlexOS_Ultra_FallAlert.h` | Aviso global de posible caída, con maqueta vertical y maqueta horizontal propias. |

La cadena queda `… → Vault → DeviceCare → DeviceTests → FallAlert → Recovery`,
que es lo que comprueba `tests/host/check_wiring.py`.

### Cambios en código existente (mínimos)

* `FlexOS_Ultra_Icons.h` — `IC_DEVCARE` **al final** del enum (`APP_N` 20) y su
  icono vectorial. Ningún índice anterior se mueve, así que el escritorio
  guardado, las favoritas y el candado por app de una placa que actualiza
  siguen apuntando a lo mismo.
* `FlexOS_Ultra_AppFramework.h` — prototipos, `H_DEVCARE` y la fila de
  `APP_REG`. Nace con `APP_DEF_DOCK`: **el escritorio de fábrica no se
  reordena**; la app se añade desde la Caja de aplicaciones.
* `FlexOS_Ultra_Session.h` / `FlexOS_Ultra_Core.h` — nombre de la app y su clase
  de peso de memoria.
* `FlexOS_Ultra_Types.h` — `MOD_BNO085` **al final** de `ModuleType`.
* `FlexOS_Ultra_System.h` — el barrido I²C reconoce `0x4A`/`0x4B`, y el panel
  «Optimizar Flex OS» acepta un aviso de cierre (`optStartCb`) para volver a
  quien lo abrió. `optStart()` conserva su firma exacta.
* `FlexOS_Ultra_Conn.h` — icono del módulo en la isla de notificaciones.
* `FlexOS_Ultra.ino` — tres `#include`, `dcBegin()` y `dcApplyFallPref()` en
  `setup()`, y `dcSensorTick()` + `faPendingTick()` + el bloque del aviso en
  `loop()`.

Nada más se ha tocado. OTA, App Store, launcher, Modo PC, táctil, cámara,
almacenamiento, temas, notificaciones, arranque y conectividad quedan como
estaban.

---

## 2. El hardware

```
GY-BNO085        ESP32-P4 (cabecera de expansión)
  VCC   ------->  3V3
  GND   ------->  GND
  SCL   ------->  I2C_SCL   (GPIO8, PIN_TP_SCL)
  SDA   ------->  I2C_SDA   (GPIO7, PIN_TP_SDA)
```

Es **el mismo bus** del GT911 táctil y del códec ES8311. Por eso el driver:

* no llama a `Wire.begin()` ni cambia la velocidad del bus (eso lo
  reconfiguraría por debajo del panel táctil);
* corre en el **mismo hilo** que `flexPollTouch()` y que el barrido I²C
  incremental. No hay una segunda tarea tocando `Wire`, así que no hacen falta
  mutex y no puede haber transacciones solapadas.

Los pads `PS1`/`PS0` del módulo eligen el protocolo: los dos a masa (valor de
fábrica) es I²C, el único modo que soporta este driver. La dirección es `0x4A`
con `AD0` a masa y `0x4B` con `AD0` a 3V3; se prueban las dos, en ese orden.

### Por qué no hay librería

El proyecto no llevaba ninguna de BNO08x y las disponibles arrastran su propia
capa de transporte y su propio bus — justo lo contrario de lo que hace falta
aquí. Se implementa el mínimo de SHTP/SH‑2 necesario, con el mismo criterio que
el códec ES8311: registros justificados, nada copiado a ciegas. **No se añadió
ninguna dependencia externa.**

### Disponibilidad honesta

`flexBnoAvailable()` devuelve `true` sólo si (1) alguien contesta en `0x4A` o
`0x4B`, (2) el sensor responde al *Product ID* (informe `0xF1`) y (3) está
entregando informes ahora mismo. Y cada sensor por separado —acelerómetro,
giroscopio, magnetómetro y *sensor fusion*— sólo se marca como comprobado
cuando ha llegado un informe **suyo**.

---

## 3. Detección de caídas

No es «aceleración > X». Es una evaluación **temporal**:

```
movimiento normal → caída libre → impacto → giro brusco
      → cambio de postura → estabilización → evaluación
      → Confidence Score → ¿supera el umbral?
```

Estados: `IDLE → MOTION → FREE_FALL → IMPACT → POST_IMPACT → EVALUATION`.

Reparto de puntos (todos los pesos y umbrales están juntos en
`flexFallDefaults`, para poder calibrarlos contra el módulo real sin tocar la
lógica):

| Prueba | Puntos |
|---|---|
| Caída libre (proporcional a su duración) | hasta +28 |
| Impacto | +24 |
| Impacto fuerte | +14 |
| Giro brusco | +14 |
| Cambio de postura (necesita *sensor fusion*) | hasta +16 |
| Estabilización posterior | +12 / −18 si no se estabiliza |
| Cadencia de pasos justo antes | −22 |
| Impacto sin vuelo **y** sin cambio de postura | −20 |

Umbral de fábrica: **62**. Con eso, un impacto solo —por grande que sea— no
llega: hacen falta dos pruebas fuertes, o una fuerte y dos débiles.

**Es una detección experimental de un evento físico. No es un sistema médico,
no avisa a nadie y no sustituye a nada.**

### Anti‑falsos positivos, comprobado en el PC

`tests/host/test_fall.cpp` sintetiza señales con la forma del gesto real y
comprueba el **veredicto**:

| Escenario | Esperado |
|---|---|
| Caída completa (vuelo + golpe + vuelco + reposo) | **CAÍDA** |
| Golpe seco en la mesa (sin vuelo, sin vuelco) | normal |
| Caminar, incluido un paso más fuerte al final | normal |
| Dejarlo en la mesa | normal |
| Agitarlo en la mano | normal |
| Vuelo largo sin impacto | ni siquiera se evalúa |
| Caída sin *sensor fusion* | caída, pero sin puntuar la orientación |

Existe porque comprobar a mano que caminar no dispara una caída obliga a
caminar con el aparato en la mano, y comprobar que una caída sí la dispara
obliga a tirarlo al suelo.

---

## 4. El gráfico del GY‑BNO085

**No es una imagen.** No hay ni un PNG, ni un JPG, ni un byte de bitmap en
flash por culpa de esta ilustración: es la placa morada, sus taladros de
montaje, el chip central con la marca del pin 1, la columna de pads con sus
etiquetas reales (`VCC GND SCL SDA AD0 CS INT RST PS1 PS0`), la serigrafía y la
rosa de ejes X/Y/Z, todo con las mismas primitivas que usa el resto de Flex OS.

Lo único que se mueve es el **enlace I²C punteado**, y avanza con `millis()`
—es función del tiempo, no un paso por cuadro—, así que su velocidad no depende
de los fps. Se repinta **sólo su banda**, no la pantalla.

**Sólo aparece aquí:**

```
Flex Device Care → Detección de caídas → BNO085 NO detectado
```

`tests/host/ino_compile.cpp` lo verifica contando los píxeles del morado de la
placa en cada pantalla de la app: inicio, estado, historial, salud,
diagnóstico, optimización, la prueba del módulo y la pantalla con el módulo ya
conectado dan **cero**. Si algún día el gráfico se cuela en otra pantalla, la
prueba falla antes de que se vea en el aparato.

Cuando el módulo se conecta con esa pantalla abierta, **cambia sola**: el
`tick` compara el estado del driver con el que hay dibujado.

---

## 5. El aviso global

Sigue el patrón del OTA y de la tarjeta del cronómetro: captura la banda real
de `fb`, se dibuja encima y, al cerrarse, la devuelve píxel a píxel. **No se
creó un segundo gestor de notificaciones**: los avisos normales siguen saliendo
por `notifPush`.

* **Vertical** — tarjeta centrada, los dos botones apilados donde cae el pulgar
  en 480×800.
* **Horizontal** — icono a la izquierda, texto en el centro y los dos botones
  **en fila**. No es la vertical girada: cambia el reparto.
* **DeX / Modo PC** — no se dibuja **nunca**, ni ahora ni luego. El evento se
  registra igual y queda en `Device Care → Historial`.
* **Pantalla ocupada** (cortina abierta, OTA descargando, bloqueo, suspendida)
  — el aviso **no se pierde**: espera y sale en cuanto la pantalla vuelve a ser
  normal, con una ventana de 60 s.
* **Memoria** — la banda se pide al abrir y **se suelta al cerrar**. En reposo
  esta función no retiene nada. Si la reserva dejara la PSRAM por debajo del
  suelo de protección, no se saca: se avisa por la vía normal del sistema.
* **Válvula de seguridad** — el aviso es modal para la app de debajo; si nadie
  lo toca en 20 s se retira solo.
* **La barra de navegación del sistema sigue viva.** Esos 64 px son del sistema,
  no del aviso: si el usuario navega, el aviso se retira sin restaurar (la
  pantalla nueva se pinta entera por su cuenta) y queda **en espera** para
  volver a salir allí. El usuario nunca queda atrapado.

### El invariante que impide que congele la interfaz

El aviso es dueño exclusivo de la pantalla mientras está a la vista: `loop()`
devuelve antes de `switch(gState)`, así que las apps no hacen `tick`. Eso lo
hace modal — y también significa que **si el aviso se queda la pantalla sin
poder dibujar, la interfaz se congela**. Pasó: `faRaise` calculaba la banda con
`faBand()` y acto seguido llamaba a `faFreeBand()` para redimensionar el
buffer… y `faFreeBand` invalidaba la geometría recién calculada. `faCompose` y
`faRestore` salían sin hacer nada, para siempre. Como `faBakCap` arranca en 0,
ocurría en **el primer aviso**: interfaz congelada, apps sin responder,
notificación que no aparecía nunca, y sólo el panel rápido vivo — porque
`loop()` lo despacha *antes* del bloque del aviso.

Tres cosas lo hacen imposible ahora:

1. **Buffer y geometría son cosas distintas.** `faFreeBand()` sólo libera
   memoria; invalidar la banda se dice aparte (`faInvalidateBand()`).
2. **`faBandReady()` es el invariante**, y `faTick()` lo comprueba en cada
   vuelta: si el aviso no puede dibujar, suelta la pantalla **en esa misma
   vuelta**. Ningún fallo futuro en esta zona puede congelar el sistema más de
   un cuadro.
3. **La detección no paga el trabajo pesado.** `faRaise()` la llama
   `dcSensorTick()`, en la parte temprana de `loop()` junto al táctil y al
   barrido I²C: ahí no puede haber una reserva de medio megabyte, dos `memcpy`
   de ese tamaño y una composición entera. Ahora `faRaise` sólo **arma**
   (`FA_ARMED`, coste O(1)) y todo eso ocurre en `faTick()`, en la fase de
   dibujo, donde el aviso ya es dueño de la pantalla. Si no se puede preparar,
   no se la queda: se retira y el aviso sale por la vía normal del sistema.

`[Revisar dispositivo]` abre la app y **arranca el Post‑Impact Check solo**: el
usuario no tiene que buscar el diagnóstico.

---

## 6. Diagnóstico

Un **único motor** para los dos caminos —«Diagnóstico completo» y «Post‑Impact
Check»—: las mismas cuatro etapas y el mismo código; lo único que cambia es
quién lo arranca. Una etapa por vuelta de `loop()`, separadas por tiempo: cero
`delay()`, cero bucles de espera.

1. **Sistema** — CPU, RAM, PSRAM, Flash, almacenamiento, servicios, watchdog,
   errores recientes y reinicios inesperados. Todo medido
   (`heap_caps_*`, LittleFS, `esp_task_wdt_status`, `esp_reset_reason`,
   `getCpuFrequencyMhz`). Lo que no se puede medir dice **«No disponible»**.
2. **IMU** — lo que el driver ha comprobado de verdad, no lo que «debería».
3. **Pantalla** — negro, blanco, rojo, verde, azul, gris, degradado con rampas
   RGB y patrón de rejilla + tablero. Después, la pregunta.
4. **Táctil** — rejilla 6×8 interactiva, con cobertura y la última coordenada
   leída. Reutiliza el táctil del sistema.

La puntuación final es la media de **lo que se pudo medir**: una prueba que no
se hizo no puntúa ni a favor ni en contra.

**No hay prueba de sonido en v1.0** (fuera de alcance a propósito), ni cámara,
ni batería, ni temperatura interna.

---

## 7. Salud de Flex OS y Optimización

Ocho métricas, ocho medidas: memoria, PSRAM, estabilidad, watchdog, servicios,
errores, reinicios y almacenamiento. **La puntuación sale de esas lecturas**: si
una métrica empeora, la puntuación baja de verdad — lo comprueba una prueba de
host que empeora la medida y verifica que el número cae. Una métrica que no se
puede medir no puntúa y su fila lo dice.

La Optimización **no reimplementa nada**: es el «Optimizar Flex OS» que ya
existía, con sus cinco etapas reales y su regla de oro (sólo se suelta lo que el
sistema sabe reconstruir). Lo único que añade Device Care es abrirlo desde su
pantalla, volver a ella al terminar y anotar la pasada en el historial. No se
tocan notas, dibujos, ajustes, archivos, apps, OTA ni firmware.

---

## 8. Historial

Un archivo pequeño en la partición de datos (`/System/devcare.bin`): cabecera
más 16 registros de 8 bytes, menos de 200 bytes en total, escrito con la misma
escritura recuperable que usa el resto del sistema (`flexFsWriteBinAtomic`). No
hay ninguna base de datos.

---

## 9. Rendimiento

* Sin `String`, sin `delay()`, sin bucles bloqueantes.
* Nada se reserva dentro del `loop`: los textos se componen con `snprintf` en
  buffers de pila del propio dibujo.
* Toda animación es **función del tiempo**, no un paso por cuadro: perder un
  cuadro no cambia la velocidad de nada.
* Repintado **parcial**: el anillo de puntuación, el enlace del gráfico, el
  latido del diagnóstico y cada celda del test táctil publican sólo su banda.
* El driver lee como mucho tres paquetes por vuelta y el detector es aritmética
  sobre una muestra: la detección corre en segundo plano sin congelar la
  interfaz.
* Ninguna función nueva se acerca al techo de pila (`check_stack.py`).

---

## 9 bis. La regla del Liquid Glass (apilado de capas de blur)

`drawLiquidGlassPanel` **lee** la región del buffer, la desenfoca y la
**escribe encima**. No es idempotente sobre su propia salida: volver a
dibujarlo sobre lo que publicó el cuadro anterior desenfoca lo ya
desenfocado y vuelve a aplicar tinte, especular y borde. En una animación eso
se apila cuadro a cuadro hasta que el texto deja de leerse.

**Regla: el vidrio se compone SIEMPRE sobre un fondo limpio, y un cuadro de
animación tiene que ser idempotente** — ejecutado dos veces con el mismo estado
lógico debe dar exactamente los mismos píxeles. Hay tres formas válidas de
conseguirlo, todas ya en uso en el sistema:

1. **Restaurar desde una captura** tomada antes de dibujar nada (isla de
   notificaciones desde `homeBuf`, tarjeta del cronómetro desde
   `gCronoCardBak`, aviso de caída desde `faBak`, y ahora el panel de
   optimización desde `optBak`).
2. **Rehacer el fondo procedimentalmente** antes del vidrio (la tarjeta de
   estado de Device Care rellena la tarjeta entera con el fondo de página en
   cada cuadro).
3. **Usar el material plano** cuando la superficie se estampa sobre sí misma y
   el color no cambia entre los dos materiales (la cápsula del cronómetro).

Un corolario que costó un cuadro mal publicado: `present()` copia **filas
enteras**, así que un cuadro parcial tiene que dejar correctos también los
márgenes de la banda a los lados de lo que dibuja, no solo su propio
rectángulo.

Y otro: el tinte adaptativo del vidrio muestrea la luminancia del panel
**completo**, no solo de la banda visible. Por eso el fondo limpio se rellena
sin recorte y el recorte se aplica después: si no, el tinte cambiaría de un
cuadro a otro y la banda se vería como una costura de otro color.

`testLiquidGlassSinApilar()` en `tests/host/ino_compile.cpp` lo comprueba por
píxeles: documenta el contrato del primitivo, repite 20 cuadros del anillo y 12
del panel de optimización exigiendo cero deriva, verifica que el último cuadro
del barrido es idéntico al repintado completo (lo que además garantiza que la
animación no borra los textos de la tarjeta) y estampa la cápsula del
cronómetro ocho veces exigiendo cero movimiento.

---

## 10. Fallo seguro

Si el BNO085 se desconecta, deja de responder o devuelve datos inválidos:

* el detector se reinicia y la detección queda sin efecto;
* la interfaz lo dice (la pantalla vuelve sola al requisito de hardware);
* **el sistema sigue funcionando.** Nada se bloquea y nadie se queda esperando.

Con la detección desactivada el módulo se detiene del todo y el bus I²C vuelve a
ser exclusivamente del táctil.

---

## 11. Cómo se comprueba

```bash
cd tests/host
make              # todas las baterías (perfil Ultra/P4)
make all-boards   # el código de dispositivo en los tres perfiles
```

* `test_fall` — la lógica de caídas, con ASan/UBSan (los siete escenarios de §3).
* `test_ino` — Device Care dentro del sketch enlazado: la regla del gráfico,
  la navegación interna, la excepción DeX, el aviso en espera, las dos
  maquetas, el historial, la puntuación derivada de métricas reales, el fallo
  seguro del sensor y **el flujo completo del criterio de aceptación** (aviso →
  Revisar dispositivo → Post‑Impact Check → diagnóstico automático → resultado →
  historial).
* `make ino` — compila el sketch entero y también `FlexOS_BNO085.cpp`, que es
  una unidad de traducción aparte que ninguna otra batería tocaba.

---

## 12. Fuera de alcance en v1.0

Test de sonido, cámara, batería, temperatura interna, otros IMU, APIs externas,
IA (local o en la nube) y cualquier pretensión de sistema médico.
