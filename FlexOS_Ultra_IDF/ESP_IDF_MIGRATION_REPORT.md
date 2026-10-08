# Flex OS Ultra — ESP-IDF · Informe de migración

Rama: `flexos-esp-idf` · Carpeta: `FlexOS_Ultra_IDF/` · Referencia funcional (no se modifica):
`FlexOS_Ultra/` en la rama `claude/flexos-ota-system-blq9g9-2csacx`.

> **NADA ESTÁ PROBADO EN HARDWARE REAL.** Mientras una funcionalidad no tenga evidencia física
> (log, foto o vídeo de la placa), su columna "Hardware probado" dice **NO**. "Implementado" significa
> compilado para `esp32p4` y verificado en el PC hasta donde se puede (pruebas de host, simulador,
> lectura del código fuente de ESP-IDF).

## Estado por fase

| Fase | Contenido | Estado | Informe |
|---|---|---|---|
| 0 | Infraestructura: proyecto, sdkconfig por revisión, particiones provisionales, herramientas | Hecha | `docs/FASE_0_1_INFORME.md` |
| 1 | Pantalla ST7701 por MIPI-DSI, 2 framebuffers, LVGL DIRECT, GT911, pantalla de prueba | Hecha | `docs/FASE_0_1_INFORME.md` |
| 2 | Arquitectura base: bus de eventos, buzón de UI, almacenamiento (NVS + LittleFS compatibles), ajustes, monitor del sistema, lógica portable | Hecha | `docs/FASE_2_INFORME.md` |
| 3 | Interfaz completa en LVGL | Pendiente | |
| 4 | Liquid Glass (3 niveles) | Pendiente | |
| 5 | Táctil y gestos | Pendiente | |
| 6 | Wi-Fi por ESP32-C6 (esp-hosted) | Pendiente | |
| 7 | Flex Account | Pendiente | |
| 8 | Flex Cloud | Pendiente | |
| 9 | Multimedia | Pendiente | |
| 10 | Cámara | Pendiente | |
| 11 | Audio ES8311 | Pendiente | |
| 12 | Device Care | Pendiente (backend de métricas hecho en la Fase 2) | |
| 13 | Flex Compass | Pendiente | |
| 14 | OTA con identificadores propios | Pendiente | |
| 15 | Particiones A/B con el tamaño medido | Pendiente | |
| 16 | Revisión de seguridad y estabilidad | Pendiente | |
| 17 | Optimización medida | Pendiente | |

## Matriz de funcionalidades

| Funcionalidad | Arduino | ESP-IDF | Estado | Hardware probado |
|---|---|---|---|---|
| Home | Escritorio por páginas, widgets, edición | — | Pendiente (Fase 3) | NO |
| Interfaz LVGL | Motor gráfico propio (no se reutiliza) | LVGL 9.6 DIRECT, 2 FB en PSRAM, solo LVGL dibuja (`check_lvgl_only.py`) | Base hecha (Fase 1); pantallas pendientes | NO |
| Liquid Glass | Desenfoque propio con caché | — | Pendiente (Fase 4) | NO |
| Táctil | GT911 en el bucle | GT911 en tarea propia, 5 dedos, recuperación del bus | Hecho (Fase 1); gestos pendientes | NO |
| Wi-Fi | esp-hosted (C6) | — | Pendiente (Fase 6) | NO |
| Flex Account | ES256, claves fijadas | Lógica portable copiada (`FlexAuth`) | Pendiente (Fase 7) | NO |
| Flex Cloud | Control + streaming | Lógica portable copiada (`CloudCore`) | Pendiente (Fase 8) | NO |
| OTA | Manifiesto + MD5 | — | Pendiente (Fase 14) | NO |
| Cámara | Sin sensor identificado ("SIN SEÑAL") | — | Pendiente (Fase 10) | NO |
| Audio | ES8311 + I2S | — | Pendiente (Fase 11) | NO |
| Vídeo | AVI MJPEG | Lógica portable copiada (`Media`, `VidEdit`) | Pendiente (Fase 9) | NO |
| Galería | Rejilla, visor, editor | Lógica portable copiada (`MediaLib`, `MediaStore`, `MediaThumb`, `ImgEdit`, `JPEG`, `JPEGEnc`) | Pendiente (Fase 9) | NO |
| Música | Biblioteca + reproductor | — | Pendiente (Fases 9/11) | NO |
| Device Care | Estado, diagnóstico, caídas, salud… | Monitor del sistema (heap, PSRAM, temperatura, CPU por núcleo, tareas, reinicio, coredump) | Backend hecho (Fase 2); UI pendiente (Fase 12) | NO |
| Compass | BNO085 + AHRS | Lógica portable copiada (`FallDetect`, `Theft`) | Pendiente (Fase 13) | NO |
| Almacenamiento | NVS (Preferences) + LittleFS | NVS compatible (mismos tipos y claves) en caché + LittleFS compatible (probado v2.8 ↔ v2.11), escritor único, sin borrados automáticos | Hecho (Fase 2) | NO |
| Seguridad | Passcode, grants, firmas | Lógica portable copiada (`AppGrant`, `FlexAuth`, `StorageCore`); reparación de datos solo con ficha de confirmación | Parcial; revisión completa en la Fase 16 | NO |

## Tamaño medido (por fase)

| Fase | Variante | Firmware | % ranura A (2,5 MB) | % ranura B (4 MB) |
|---|---|---|---|---|
| 1 | `lt_v3` dev | ver `docs/FASE_0_1_INFORME.md` | | |
| 2 | `lt_v3` dev | 918 864 B | 35,1 % | 21,9 % |
| 2 | `v3` dev | 921 056 B | 35,1 % | 22,0 % |

La tabla de particiones sigue **provisional (A)** hasta medir el firmware con la interfaz completa
(Fase 15). No se ha grabado nada.

## Diferencias de implementación respecto a Arduino (documentadas)

| Área | Arduino | ESP-IDF | Motivo |
|---|---|---|---|
| Ajustes | `Preferences.put*` escribe al momento | Caché en RAM + escritura diferida 300 ms por un único escritor | La UI nunca espera a la flash |
| LittleFS sin montar | `LittleFS.begin(true)` **formatea** si no monta | Nunca formatea; avisa y pide confirmación | No perder datos del usuario |
| Gráficos | Motor propio sobre framebuffer | Solo LVGL | Requisito del proyecto |
