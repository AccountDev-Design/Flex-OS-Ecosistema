# Tabla de particiones: decisión PENDIENTE

La tabla definitiva **no está elegida**. Se decide con el tamaño real del firmware medido al final de las
Fases 1-3 (con LVGL, esp-hosted y TLS), antes de dar el sistema por cerrado.

## Candidatas (16 MB)

| | A (compatible con los datos Arduino) | B (más margen de firmware) |
|---|---|---|
| nvs | 0x9000 · 20 KB (igual que Arduino) | igual |
| otadata | 0xE000 · 8 KB | igual |
| ota_0 / ota_1 | 2,5 MB cada una (ocupan el antiguo `app0`) | 4 MB cada una |
| LittleFS (`spiffs`) | 0x510000 · 10,9 MB (**igual que Arduino**) | 0x810000 · 7,9 MB (**se mueve**) |
| coredump | 0xFF0000 · 64 KB | igual |

Ambas se validan con `gen_esp32part.py --flash-size 16MB`.

* **A**: pasar de Arduino a ESP-IDF (y volver) no toca NVS ni LittleFS. Riesgo: 2,5 MB por ranura.
* **B**: obliga a copiar los archivos del usuario y reformatear LittleFS.

## Mediciones (ESP-IDF 5.5.5, `COMPILER_OPTIMIZATION_PERF`)

| Fase | Variante | Firmware | Ranura A ocupada | Ranura B ocupada |
|---|---|---|---|---|
| 1 | lt_v3 | 832 544 B (0,79 MiB) | 31,8 % | 19,8 % |
| 1 | v3 | 834 720 B (0,80 MiB) | 31,8 % | 19,9 % |

`tools/size_report.py <build>` repite la medición (lo ejecuta `tools/build.sh`).

Regla del plan: si al final de las Fases 1-3 el firmware queda por debajo de ~2,0 MB → A; si no → B.

## Bootloader (hallazgo de la Fase 0)

En el P4 el bootloader va en 0x2000 y debe caber antes de la tabla (0x8000): 24 576 B.

| Log del bootloader | Tamaño | Libre |
|---|---|---|
| INFO | 24 160 B | **416 B (2 %)** |
| WARN (elegido) | 21 408 B | 3 168 B (13 %) |
| WARN, variante v3 | 22 144 B | 2 432 B |

Si algún día no cupiera, habría que mover la tabla de particiones y con ella la NVS, perdiendo la
compatibilidad con los datos Arduino. Por eso el bootloader va en WARN.

## Grabar mientras la decisión está abierta

Para probar las Fases 1-3 en la placa se graba con la **candidata A** porque no mueve NVS ni LittleFS:
los datos de la versión Arduino quedan intactos (las Fases 0-1 tampoco montan NVS ni LittleFS).
`tools/flash_guard.py` exige `--acepto-tabla-provisional` y se niega a grabar una tabla que mueva
LittleFS sin `--acepto-perder-littlefs`.
