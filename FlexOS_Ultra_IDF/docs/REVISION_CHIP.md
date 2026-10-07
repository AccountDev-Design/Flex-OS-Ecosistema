# Revisión del ESP32-P4 (dato bloqueante para grabar)

ESP-IDF 5.5.5 trata los P4 **anteriores a v3** y los **v3.x** como chips incompatibles: un binario para
uno **no arranca** en el otro (el bootloader lo rechaza). Por eso hay dos variantes de build:

| Variante | Fragmento | Acepta | CPU |
|---|---|---|---|
| `lt_v3` (por defecto) | `sdkconfig.defaults.rev_lt_v3` | v0.1 … v1.99 | 360 MHz |
| `v3` | `sdkconfig.defaults.rev_v3` | v3.0 … v3.99 | 400 MHz |

`lt_v3` es la opción por defecto porque la placa funciona hoy con el core Arduino 3.2.1 (ESP-IDF 5.4),
lo que apunta a silicio anterior a v3. **Está sin confirmar.**

Notas:

* Se usa `REV_MIN_1` (v0.1) y no `REV_MIN_0`: con `REV_MIN_0`, ESP-IDF 5.5.5 elimina la opción de flash
  a 80 MHz. Si esptool dijera v0.0, hay que cambiar esa línea por `CONFIG_ESP32P4_REV_MIN_0=y`.
* En v3 se usa `REV_MIN_300` (acepta v3.0); con v3.1 o posterior se puede subir a `REV_MIN_301`.

## Cómo confirmarla (sin escribir nada en la placa)

```
. $IDF_PATH/export.sh          # ESP-IDF 5.5.5
python3 tools/flash_guard.py --port /dev/ttyUSB0 --solo-comprobar
```

Lee la revisión con `esptool chip-id` (solo lectura) y dice qué variante compilar. Alternativas: la línea
`Chip rev` del log de arranque del firmware Arduino, o `python -m esptool --chip esp32p4 -p PUERTO chip-id`.

## Protección

* `tools/flash_guard.py` no graba si la revisión del chip queda fuera de la ventana del binario.
* esptool, además, rechaza escribir una imagen fuera de rango (salvo `--force`, que la guardia nunca usa).
* La aplicación imprime al arrancar la revisión real y la ventana del binario, y la pantalla de prueba
  las muestra en la fila "Chip / binario".
