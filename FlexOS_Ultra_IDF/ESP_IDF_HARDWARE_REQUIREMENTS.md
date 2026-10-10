# Flex OS Ultra — ESP-IDF · Requisitos de hardware y pruebas en la placa

> Todo lo que aparece aquí está **NO PROBADO EN HARDWARE REAL** salvo que una fila diga lo contrario
> con su evidencia.

## Placa

| Pieza | Dato | Fuente | Confirmado en la placa |
|---|---|---|---|
| Placa | GUITION JC4880P443C_I_W (JC4880P443 V1.0) | `FlexOS_Ultra.ino` | Sí (es la que usa hoy la versión Arduino) |
| SoC | ESP32-P4, 2 núcleos HP RISC-V | `soc_caps.h` | **Revisión exacta desconocida** (bloqueante para grabar) |
| Flash | 16 MB QIO 80 MHz | `FlexOS_Ultra.ino` | Pendiente (log de arranque) |
| PSRAM | 32 MB HEX 200 MHz | `FlexOS_Ultra.ino` | Pendiente |
| Pantalla | 480×800 ST7701, MIPI-DSI 2 lanes 500 Mbps, DPI 34 MHz RGB565, LDO 3 a 2,5 V, RST GPIO5, BL GPIO23 | `flex_board.h` (de `FlexOS_Ultra_HAL.h`) | Pendiente |
| Táctil | GT911 I2C 0x5D/0x14, SDA 7, SCL 8, RST 3, INT desconocido (sin él el apagado completo despierta por temporizador cada 400 ms: consumo alto; con el INT en un GPIO 0..15 se usa ext1) | `flex_board.h` | Pendiente |
| Audio | ES8311 I2C 0x18 + I2S (MCLK 13, BCLK 12, WS 10, DOUT 9, DIN 48) | `FlexOS_Audio.*` | Pendiente |
| IMU | BNO085 (SHTP) I2C 0x4A/0x4B | `FlexOS_BNO085.*` | Pendiente |
| Wi-Fi/BLE | ESP32-C6 por SDIO (CLK 18, CMD 19, D0 14, D1 15, D2 16, D3 17, RST 54), firmware esp-hosted | `FlexOS_Ultra_Network.h` | Pendiente; versión del firmware del C6 desconocida |
| Cámara | MIPI-CSI; **sensor no identificado** | `FlexOS_Ultra_AppCamera.h` | Pendiente |
| Batería | **Sin medida conocida** | búsqueda en el código | — |

## Antes de grabar nada

1. `python3 tools/flash_guard.py --port <PUERTO> --solo-comprobar` → revisión real del chip.
2. Compilar la variante que corresponda (`tools/build.sh` para < v3, `--rev v3` para v3.x).
3. Grabar solo con `flash_guard.py` (comprueba revisión, tabla y que NVS/LittleFS no se tocan).
4. **Copia de seguridad recomendada** de NVS (0x9000, 0x5000) y LittleFS (0x510000, 0xAE0000) con
   `esptool read_flash` antes del primer arranque de esta versión. No se hace automáticamente.

## Pruebas en la placa por fase

| Fase | Qué comprobar | Cómo | Evidencia |
|---|---|---|---|
| 0 | Arranca; la revisión que imprime coincide con la del binario | log serie | — |
| 1 | Imagen sin tearing ni destellos; esquinas 0,0 · 479,0 · 0,799 · 479,799 donde se tocan; 5 dedos; brillo; Hz del panel ~60; latencia táctil | pantalla de prueba + log de métricas | — |
| 2 | NVS de Arduino leída ("NVS lista: N claves"); brillo guardado aplicado; LittleFS monta con los archivos de Arduino; el brillo cambiado se conserva al reiniciar y en la versión Arduino; carga por núcleo, pilas y temperatura en el log; fallo guardado detectado al arrancar | log serie | — |
| 3 (Modo seguro) | Con 3 reinicios anormales seguidos (p. ej. un comando de diagnóstico que provoque un fallo) el arranque entra en Modo seguro (log `flex.safe ... modo_seguro=SI`); con clave puesta, "Ir al escritorio", Ajustes y Explorador la piden; "Reiniciar normalmente" arranca normal; 60 s estables ponen el contador a 0 (log) | log serie (`flex.safe`) | — |
| 3 (apagado completo) | "Apagar" del panel rápido → deslizar → la pantalla se funde, "Flex OS", retroiluminación a 0 y la placa queda dormida; **la retroiluminación (GPIO23, no es un pin LP: no se puede retener) sigue apagada durante el deep sleep**; mantener el dedo 3 s la enciende y un toque corto no (la pantalla no llega a encenderse); cortar la alimentación arranca SIEMPRE normal; consumo dormido con el despertar por temporizador (400 ms) medido; el GT911 responde sin pulso de reset al despertar (RST retenido en alto, P4 rev ≥ 3.0) | log serie (`flex.poweroff`) + amperímetro | — |
