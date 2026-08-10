# Dobles del entorno Arduino + ESP-IDF para compilar el sketch en el PC

Estos ficheros NO son parte del firmware. Existen para que
`make ino` pueda compilar `FlexOS_Ultra.ino` entero con `g++`, sin
`arduino-cli` y sin el core de ESP32.

Cada cabecera reproduce **solo la superficie que usa el sketch**, con las
mismas firmas que arduino-esp32 v3.2.0 / ESP-IDF v5.4:

| Fichero | Que cubre |
|---|---|
| `Arduino.h` | `millis`, `delay`, GPIO, `Serial`, `String`, `IPAddress`, `ledc*`, `map`, macros `min`/`max` |
| `Wire.h`, `Preferences.h` | I2C del GT911 y NVS |
| `WiFi.h`, `WiFiClientSecure.h`, `HTTPClient.h` | pila de red (STA, escaneo, cliente HTTP/TLS) |
| `esp_lcd_*.h`, `esp_ldo_regulator.h` | bring-up MIPI-DSI del ST7701 |
| `freertos/*` | tareas, semaforos, colas, secciones criticas |
| `esp_heap_caps.h`, `esp_system.h`, `esp_task_wdt.h`, `esp_sleep.h`, `driver/gpio.h`, `soc/soc_caps.h` | PSRAM, motivo de reinicio, TWDT, deep sleep |

## Que garantiza y que no

**Si garantiza:** que el sketch compila -- tipos, nombres, firmas,
`#if` de configuracion y aritmetica de punteros. Es lo que detecta un
error de edicion antes de llegar a la placa.

**No garantiza:** comportamiento. Los dobles devuelven valores fijos y no
hablan con ningun hardware. Una prueba en la placa sigue siendo
obligatoria antes de dar nada por bueno.

## Uso

    cd tests/host && make ino
