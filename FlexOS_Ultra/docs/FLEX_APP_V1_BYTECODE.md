# `flex-app-v1` — formato de bytecode y contrato de ejecución

Documento normativo del runtime aislado de Flex OS Ultra.
Implementación de referencia: `FlexOS_AppVM.h/.cpp` (máquina) y
`FlexOS_AppHost.h/.cpp` (gestor y syscalls). Escritor: `sdk/lib/asm.js`.

Todo entero es **little-endian**. Todo valor de la máquina es **int32 con
signo**; el desbordamiento **da la vuelta** (aritmética de 32 bits sin signo
reinterpretada), nunca es comportamiento indefinido.

---

## 1. Contenedor `FLXB` v1

```
off  tam  campo
  0    4  "FLXB"
  4    2  formatVersion = 1
  6    2  flags = 0
  8    4  codeLen        bytes de la sección de código
 12    4  constLen       bytes del pool de constantes
 16    4  memBytes       memoria lineal (múltiplo de 4)
 20    2  globalCount
 22    2  stackSlots     profundidad máxima de la pila de operandos
 24    2  frameSlots     reserva TOTAL de variables locales
 26    2  callDepth      marcos de llamada simultáneos
 28    2  funcCount
 30    2  reservado = 0
 32    2  onStart   (índice de función, 0xFFFF = no existe)
 34    2  onEvent
 36    2  onTick
 38    2  onStop
 40    4  reservado = 0
 44    4  reservado = 0
 48       tabla de funciones: funcCount × 8 bytes
          [ codeOffset(u32) | argCount(u8) | localCount(u8) | reservado(u16)=0 ]
          pool de constantes: constLen bytes
          código: codeLen bytes
```

`tamaño del archivo == 48 + funcCount*8 + constLen + codeLen`, exactamente.

### Techos del formato

| Concepto | Máximo |
|---|---|
| `codeLen` | 128 KB |
| `constLen` | 64 KB |
| `memBytes` | 1 MB (y nunca más que `limits.memoryKB` del manifest) |
| `globalCount` | 256 |
| `stackSlots` | 1024 |
| `frameSlots` | 1024 |
| `callDepth` | 64 |
| `funcCount` | 512 |
| argumentos por función y por syscall | 8 |

---

## 2. Validación antes de ejecutar

`flexVmValidate()` rechaza la imagen entera si algo de esto no se cumple. No
hay "reparación", ni ejecución parcial, ni aviso: o entra completa o no entra.

1. Magia, versión, flags y reservados.
2. Todos los techos de arriba, y el que imponga el manifest.
3. Tamaño declarado == tamaño real (aritmética de 64 bits).
4. **El código se decodifica linealmente de principio a fin.** Cada opcode
   tiene que existir, cada instrucción tiene que caber entera y no puede
   quedar ni un byte sin cubrir. El código es un flujo puro de
   instrucciones: los datos van en el pool de constantes.
5. Se anota **dónde empieza cada instrucción**. Todo destino de `jmp`, `jz`
   y `jnz` tiene que caer dentro del código **y en un inicio de instrucción**.
   Saltar a mitad de otra instrucción es el truco clásico para fabricar
   código que el validador no vio: aquí no existe.
6. Índices comprobables sin ejecutar: global `< globalCount`, función
   `< funcCount`, syscall **conocida por ESTE firmware** (una app que llame a
   un servicio que no existe no llega a cargarse).
7. Tabla de funciones: `codeOffset` dentro del código y en un inicio de
   instrucción; `argCount + localCount <= frameSlots`.
8. Los manejadores declarados existen y tienen la aridad exacta
   (`onStart` 0, `onEvent` 3, `onTick` 1, `onStop` 1). Una imagen sin
   ningún manejador se rechaza.

---

## 3. Modelo de ejecución

* **Pila de operandos** de `stackSlots` int32.
* **Memoria lineal** de `memBytes`, direccionada por bytes desde 0. Todo
  acceso se comprueba en aritmética de 64 bits sin signo: una dirección
  negativa o enorme no puede dar la vuelta.
* **Globals**: `globalCount` int32, a cero en cada lanzamiento.
* **Constantes**: `constLen` bytes de **sólo lectura**.
* **Marcos**: `callDepth` marcos, con las locales sacadas de una reserva
  común de `frameSlots` slots.

**Toda función devuelve exactamente un valor.** `ret` empuja 0; `retv`
empuja el valor de la cima. Así el llamante siempre encuentra un resultado y
la pila no se puede descuadrar; para tirarlo se usa `drop`.

### Presupuesto y cesión

`flexVmRun(vm, presupuesto)` ejecuta como mucho ese número de instrucciones y
vuelve. Agotarlo **no es un error**: devuelve `YIELD` con el PC y la pila
guardados, y el tick siguiente continúa exactamente donde iba. El opcode
`yield` hace lo mismo de forma voluntaria.

El **tiempo** lo controla el gestor: llama a la máquina en tandas cortas y
mira el reloj entre tanda y tanda. Por eso la máquina no depende de ningún
reloj y se puede verificar entera en el PC.

Una app que no termina se detiene, con motivo, cuando pasa de
`FLEXAPP_MAX_OVERRUN_TICKS` ticks seguidos sin acabar un manejador, o de
`FLEXAPP_MAX_INSTR_CALL` instrucciones en una sola invocación. **Flex OS no
se reinicia**: la app pasa a `Detenida por seguridad`.

### Trampas

Cualquiera de estas aborta la app de forma limpia (pila, marcos y locales a
cero) y deja un motivo legible: opcode desconocido, pila desbordada o vacía,
demasiadas llamadas anidadas, acceso fuera de la memoria lineal o de las
constantes, global o local inválida, división o módulo por cero, salto
inválido, función inválida, syscall no permitida, `trap` de la propia app, o
fallo declarado por el anfitrión.

---

## 4. Instrucciones

`i8`/`i32` = inmediato con signo · `u8`/`u16` = sin signo · `rel16` =
desplazamiento con signo **relativo a la instrucción siguiente**.

| Op | Mnemónico | Tam | Efecto |
|---|---|---|---|
| 0x00 | `nop` | 1 | — |
| 0x01 | `push.i8 i8` | 2 | apila el inmediato |
| 0x02 | `push.i32 i32` | 5 | apila el inmediato |
| 0x03 | `dup` | 1 | duplica la cima |
| 0x04 | `drop` | 1 | descarta la cima |
| 0x05 | `swap` | 1 | intercambia las dos primeras |
| 0x06 | `over` | 1 | copia la segunda encima |
| 0x10 | `ldlocal u8` | 2 | apila la local |
| 0x11 | `stlocal u8` | 2 | guarda en la local |
| 0x12 | `ldglobal u16` | 3 | apila la global |
| 0x13 | `stglobal u16` | 3 | guarda en la global |
| 0x18 | `ld8` | 1 | `(dir) -> byte con signo` |
| 0x19 | `ld8u` | 1 | `(dir) -> byte sin signo` |
| 0x1A | `ld16` | 1 | 16 bits con signo |
| 0x1B | `ld16u` | 1 | 16 bits sin signo |
| 0x1C | `ld32` | 1 | 32 bits |
| 0x1D | `st8` | 1 | `(dir, valor) ->` |
| 0x1E | `st16` | 1 | idem, 16 bits |
| 0x1F | `st32` | 1 | idem, 32 bits |
| 0x20 | `kld8u` | 1 | lee del pool de constantes |
| 0x21 | `kld16u` | 1 | idem |
| 0x22 | `kld32` | 1 | idem |
| 0x30..0x3F | `add sub mul div mod neg and or xor not shl shr ushr min max abs` | 1 | aritmética |
| 0x40..0x45 | `eq ne lt le gt ge` | 1 | comparación → 1 / 0 |
| 0x50 | `jmp rel16` | 3 | salto |
| 0x51 | `jz rel16` | 3 | salta si la cima es 0 (la consume) |
| 0x52 | `jnz rel16` | 3 | salta si la cima no es 0 (la consume) |
| 0x53 | `call u16` | 3 | llama a la función; siempre deja UN valor |
| 0x54 | `ret` | 1 | vuelve con 0 |
| 0x55 | `retv` | 1 | vuelve con la cima |
| 0x56 | `halt` | 1 | termina el manejador entero |
| 0x60 | `sys u16` | 3 | llama al sistema; siempre deja UN valor |
| 0x70 | `yield` | 1 | cede el resto del tick |
| 0x7F | `trap u8` | 2 | aborta la app con un código |

Reglas de aritmética que el formato **fija** (no dependen del compilador):

* `add`, `sub`, `mul`, `neg`: dan la vuelta en 32 bits.
* `div`, `mod` por cero: **trampa** `DIVZERO`.
* `INT32_MIN / -1` = `INT32_MIN`; `INT32_MIN % -1` = `0`.
* `shl`, `shr`, `ushr`: la cuenta se toma **módulo 32**; `shr` conserva el
  signo, `ushr` rellena con ceros.

---

## 5. Manejadores

```
onStart()                 al abrir la app
onEvent(tipo, a, b)       un evento; ver la tabla de abajo
onTick(msDesdeStart)      un cuadro, si no había eventos pendientes
onStop(motivo)            al cerrar, con presupuesto propio y acotado
```

Se ejecuta **como mucho un manejador por tick**: si hay eventos en cola se
atiende uno; si no, `onTick`. La cola es de 16 eventos y **no crece**: si se
llena se descarta el más antiguo y se cuenta.

| Tipo | Evento | `a` | `b` |
|---|---|---|---|
| 1 | dedo abajo | x | y |
| 2 | dedo moviéndose | x | y |
| 3 | dedo arriba | x | y |
| 4 | toque | x | y |
| 5 | botón atrás | 0 | 0 |
| 6 | pausa (segundo plano) | 0 | 0 |
| 7 | reanudación | 0 | 0 |
| 8 | temporizador | id | 0 |
| 9 | cierre inminente | 0 | 0 |

`motivo` de `onStop`: 0 usuario · 1 sistema · 2 presupuesto · 3 error ·
4 no arrancó · 5 cancelada · 6 desinstalación.

---

## 6. Llamadas al sistema

Los identificadores son **estables**. Cada una consume su número exacto de
argumentos (en el orden en que se apilaron) y **empuja exactamente un
valor**. La tabla completa vive en `FlexOS_AppHost.h` (firmware) y
`sdk/lib/isa.js` (SDK); hay vectores dorados en las dos baterías de pruebas
para que no puedan separarse.

Cuando un servicio **no existe en este equipo** (sin PSRAM, sin sensor de
temperatura, el panel no reporta multitáctil) o **el permiso no está
concedido**, la syscall devuelve `FLEXAPP_NO_VALUE` (`0x80000000`). Nunca se
devuelve un número inventado.

| Familia | Qué hace | Permiso |
|---|---|---|
| `gfx.*` | render 2D en el lienzo de la app: rectángulos, líneas, redondeados, texto, píxeles, sprites RGB565 con escalado y rotación de 90°, recorte, `present` | ninguno |
| `ui.*` | componentes con el tema del sistema: tarjeta, título, etiqueta, pie, botón, fila de lista, título de cabecera | ninguno |
| `time.*` | reloj monotónico (ms desde el arranque de la app y µs en 64 bits), 8 temporizadores, presupuesto restante | ninguno |
| `input.*` | multitáctil **si el panel lo reporta de verdad** | ninguno |
| `sys.*` | tamaño de pantalla, versión de Flex OS, modelo, capacidades, registro, notificación, salir | `notifications` (manifest) para notificar |
| `store.*` | carpeta privada: escribir, leer, tamaño, borrar, cuota | `storage.app` |
| `display.*` | horizontal, pantalla exclusiva, orientación actual | `display.landscape`, `display.exclusive` |
| `perf.*` | métricas reales del sistema | `system.performance.metrics` |
| `mem.*` | PSRAM: libre, total, mayor bloque, reserva acotada y devolución | `system.psram.measure` |
| `temp.*` | temperatura en m°C, o "no disponible" | `system.temperature.read` |
| `cpu.stress_*` | carga por rodajas cooperativas, cancelable | `system.cpu.stress` |
| `bench.*` | abre y cierra una sesión de medición | `benchmark.run` |

### Lo que NO existe en esta versión

No hay coma flotante en el modelo de datos (los valores son int32), ni hilos,
ni red, ni cámara, ni Bluetooth, ni Wi‑Fi, ni acceso a archivos fuera de la
carpeta privada, ni forma alguna de obtener un puntero. Tampoco se puede
crear una tarea de FreeRTOS ni tocar el watchdog.

---

## 7. Límites por app

Salen del manifest y el gestor los **recorta** a lo que permite el sistema.

| Límite | Por defecto | Máximo |
|---|---|---|
| `instrPerTick` | 120 000 | 400 000 |
| `usPerTick` | 6 000 µs | 12 000 µs |
| `drawPerFrame` | 1 024 comandos | 4 096 |
| memoria lineal | la del bytecode | `limits.memoryKB` |
| cuota de la carpeta privada | `limits.storageKB` | 8 MB |
| reserva de PSRAM | — | 8 MB por app, con colchón para el sistema |

Pasarse del presupuesto de **dibujo** no mata la app: los comandos de más se
descartan y se cuentan. Pasarse del de **tiempo o instrucciones** sí la
detiene, porque es la diferencia entre una app pesada y una app colgada.

---

## 8. Limitaciones conocidas de esta versión

* **Recorte en horizontal.** En vertical, `gfx.clip` acota filas y columnas
  con exactitud. En horizontal las primitivas van rotadas y la banda de
  recorte del motor acota la *x* lógica: el recorte por *y* se aplica
  acotando las coordenadas de cada primitiva (rectángulos, líneas, píxeles y
  sprites), pero **el texto no se recorta verticalmente** en ese modo.
* **Sin coma flotante** en el bytecode. Las puntuaciones y medias se calculan
  con enteros (o en el servicio privilegiado, que sí usa float por dentro).
* **Un manejador por tick.** Una app que encole eventos más rápido de lo que
  los procesa perderá los más antiguos, y se lo puede consultar al gestor.
