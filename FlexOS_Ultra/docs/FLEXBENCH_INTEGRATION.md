# Cómo migrará *Antutu Benchmark for Flex OS* a `flex-app-v1`

**Estado: el benchmark NO está integrado y no se declara listo.**
Su código sigue en su repositorio independiente `FlexBench-for-Flex-OS`;
aquí no se ha copiado nada suyo, no se ha usado su clave de firma y ese
repositorio no se ha tocado.

Lo que este trabajo aporta son **los contratos** que el benchmark
necesitará. Este documento dice cuáles son, qué garantiza cada uno y qué
sigue faltando.

---

## 1. Por qué `flex-ui-1` no puede medir nada

El runtime declarativo lee pantallas JSON: `text`, `button`, `navigate` y
`notify`. No ejecuta lógica, no tiene reloj, no tiene bucle y no puede
guardar un resultado. Una app `flex-ui-1` puede **enseñar** una puntuación,
pero no puede **calcularla**. Por eso la migración no es opcional.

---

## 2. Contratos disponibles hoy

Todos existen, están implementados y tienen pruebas de host con ASan/UBSan.
Ninguno se ha medido en placa (ver §5).

| Lo que necesita el benchmark | Contrato `flex-app-v1` | Permiso |
|---|---|---|
| Reloj monotónico de verdad | `time.now_us_lo` / `time.now_us_hi` → µs en 64 bits desde `micros()`; `time.now_ms` → ms desde `onStart` | — |
| Cesión cooperativa | opcode `yield` + presupuesto por tick; el estado se conserva y el tick siguiente continúa donde iba | — |
| Métricas del sistema | `perf.metric(campo)`: vueltas de `loop()`/s, MHz de CPU, heap interno libre, uptime, µs del último cuadro de la app | `system.performance.metrics` |
| Memoria PSRAM | `mem.psram_free` / `mem.psram_total` / `mem.psram_largest` (KB) | `system.psram.measure` |
| Reserva acotada de PSRAM | `mem.reserve(kb)` → handle > 0, o **0 limpio** si no cabe; `mem.poke` / `mem.peek` para tocarla de verdad; `mem.release` | `system.psram.measure` |
| Orientación horizontal | `display.landscape(1)` — y **la restaura el sistema**, salga la app por donde salga | `display.landscape` |
| Pantalla exclusiva | `display.exclusive(1)`, restaurable igual | `display.exclusive` |
| Render 2D | `gfx.*`: rectángulos, líneas, redondeados, texto, píxeles, sprites RGB565 con escalado y rotación de 90°, recorte y `gfx.present` | — |
| Almacenamiento privado | `store.write` / `read` / `size` / `delete` / `used` / `quota` en `/FlexApps/<id>/data` | `storage.app` |
| Táctil | eventos 1..4 en `onEvent`, y `input.points` / `point_x` / `point_y` / `point_id` para multitáctil | — |
| Carga de CPU | `cpu.stress_begin(tipo, rodajas)` + `cpu.stress_slice()` + `cpu.stress_lo/hi` + `cpu.stress_stop` | `system.cpu.stress` |
| Sesión de medición | `bench.begin(id)` / `bench.end()` | `benchmark.run` |
| Permiso comprobado **por llamada** | cada syscall privilegiada consulta la máscara que salió de verificar el grant al arrancar esa sesión | — |

### Cómo se ejecuta la carga sin colgar el sistema

`cpu.stress_slice()` ejecuta **una** rodaja acotada por tiempo (techo duro de
4 ms) y vuelve. Entre rodaja y rodaja manda el gestor, así que:

* la interfaz sigue respondiendo;
* la carga se cancela en cualquier momento (`cpu.stress_stop`, cerrar la app,
  pasar a segundo plano);
* no hay bucles infinitos y el watchdog lo sigue alimentando `loop()`.

El acumulador (`cpu.stress_lo/hi`) cuenta **trabajo real hecho**, no un número
decorativo: el bucle escribe y relee memoria a través de una variable
`volatile` para que el compilador no pueda borrarlo.

---

## 3. Pasos de la migración

1. **Manifest.** Cambiar `runtime` a `"flex-app-v1"`, apuntar `entry` al
   `.flxb` y declarar en `systemPermissions` exactamente lo que el código
   use. Declarar de más hace que un revisor no se fíe; declarar de menos hace
   que el SDK se niegue a construir.

   ```json
   {
     "runtime": "flex-app-v1",
     "entry": "main.flxb",
     "limits": { "memoryKB": 512, "storageKB": 64,
                 "instrPerTick": 200000, "usPerTick": 8000, "drawPerFrame": 1024 },
     "systemPermissions": [
       "benchmark.run", "system.cpu.stress", "system.psram.measure",
       "system.performance.metrics", "display.landscape", "display.exclusive",
       "storage.app"
     ]
   }
   ```

2. **Estructura del programa.** Reescribir las pantallas declarativas como
   `onStart` / `onEvent` / `onTick` / `onStop`. La regla de oro: **cada
   `onTick` es un cuadro**, y una prueba larga se parte en rodajas que
   avanzan un poco por tick. Un `while` que no acaba nunca lo detiene el
   sistema, con motivo.

3. **Puntuaciones.** El bytecode trabaja con enteros de 32 bits. Las medias y
   los índices se calculan en enteros (o en milésimas) y se guardan en la
   carpeta privada.

4. **Grant.** Construir y subir a Flex Developer Studio el paquete **sin
   grant**. Tras aprobar la versión y sus permisos, Developer Studio expone un
   `FLXG v1` binario firmado. Flex Store descarga por separado el paquete
   inmutable y ese grant, y los instala en una sola transacción. Sin grant la
   app **se instala y funciona**, pero no puede medir el sistema.

   ```bash
   node sdk/bin/flexpkg.js build  bench -k dev.pem -o bench.flexpkg
   # ... subir bench.flexpkg y aprobarlo en Flex Developer Studio ...
   # Flex Store consume automáticamente package URL + permissionGrantUrl.
   # Sólo para instalación manual por cable:
   node sdk/bin/flexpkg.js build  bench -k dev.pem --grant bench.flexgrant -o bench.flexpkg
   ```

5. **Probar sin placa.** `tests/host/build/flexapprun bench.flxb --json`
   ejecuta el bytecode con el **mismo gestor** que corre en el P4 y dice qué
   pasó: si arrancó, cuántos cuadros publicó, qué permisos se le concedieron y
   qué guardó.

---

## 4. Lo que hay que tener claro

* **Un grant vale para UN contenido firmado.** Ata `packageId`, `versionName`,
  `versionCode`, SHA-256 de `manifest || index || payload` y la huella del
  desarrollador. Developer Studio ata además la aprobación al SHA-256 completo
  del archivo subido. Cada versión nueva necesita un grant nuevo: es lo que
  impide reutilizar permisos privilegiados en otro binario.
* **`benchmark.run` no abre nada por sí solo.** Marca la sesión de medición;
  cada servicio sigue exigiendo *su* permiso.
* **Temperatura: hoy devuelve "no disponible".** Este firmware **no** tiene
  todavía una lectura verificada del sensor interno del ESP32-P4. La rama que
  usaría `driver/temperature_sensor.h` está escrita pero desactivada
  (`FLEXOS_TEMP_SENSOR`), y sólo debe encenderse después de contrastar la
  lectura en placa contra una medida externa. Hasta entonces, el benchmark
  tiene que enseñar **"No disponible"** y no estimar nada.
* **No hay FPS del sistema.** Flex OS no tiene un único punto de
  presentación: cada pantalla publica su banda cuando le toca. Lo que el
  benchmark puede medir de verdad es **su propio** ritmo de cuadros
  (`gfx.present` + `time.now_us_*`) y las vueltas de `loop()` por segundo
  (`perf.metric`). Un "FPS del sistema" sería inventado.
* **La reserva de PSRAM puede fallar y hay que tratarlo.** `mem.reserve`
  devuelve 0 cuando no cabe con holgura; nunca se intenta reservar toda la
  PSRAM, y hay un colchón reservado para el sistema.

---

## 5. Lo que NO está hecho

* El benchmark **no** está portado: aquí sólo hay contratos.
* **Nada se ha ejecutado en un ESP32-P4.** En este entorno no hay
  `arduino-cli` ni toolchain RISC-V, así que no se ha compilado el firmware
  para la placa ni se ha grabado. Todas las cifras de este trabajo salen de
  pruebas de host.
* No hay número de referencia, ni tabla de puntuaciones, ni comparativa. Los
  primeros resultados reales tendrán que medirse en placa, y hasta entonces
  no existen.
