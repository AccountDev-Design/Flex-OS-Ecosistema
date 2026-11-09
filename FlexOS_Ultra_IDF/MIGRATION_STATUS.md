# Flex OS Ultra → ESP-IDF · Estado para reanudar

Documento corto y vivo: qué está hecho DE VERDAD, qué falta y por dónde seguir. El
detalle de cada decisión está en `ESP_IDF_MIGRATION_REPORT.md` (diferencias con
Arduino, tamaños) y `ESP_IDF_HARDWARE_REQUIREMENTS.md` (qué probar en la placa).

> **Nada está probado en la placa.** "Hecho" = compilado para `esp32p4` + pruebas de
> host + escena del simulador que falla al revertir la corrección (mutación).

Rama: `flexos-esp-idf` (la rama Arduino `claude/flexos-ota-system-blq9g9-2csacx` no se toca).
Último bloque cerrado: **Device Care** (A17: inicio con tarjeta y anillo animado, estado, salud, optimización, historial persistente y diagnóstico completo —sistema, pantalla en ocho pasadas, táctil, resultado— sobre los servicios reales, con las pantallas como listas de operaciones de dibujo comparadas primitiva a primitiva con Arduino; el inventario separa el IMU y las caídas como A17h); antes **Juegos** (A10: Jumper de Arduino sin cambios sobre el motor de dibujo de Arduino extraido, en un lienzo vertical que gira el propio motor, a pantalla completa y sin gestos del sistema); antes **Almacenamiento + Archivos** (A04 local: pantalla principal con medidas reales asíncronas, detalles de memoria y sistema, explorador modal con filas por ventana, borrar / renombrar / papelera / selección múltiple sobre las operaciones reales; el inventario separa lo que depende de servicios sin migrar como A04c: teléfono emparejado, Flex Cloud y visores de medios); antes **Reloj** (A01: pestañas Hora / Cronómetro / Temporizador, cronómetro con esfera, tabla de vueltas y dos botones, cápsula en la barra de estado, tarjeta expandida modal, mosaico del panel rápido y widget del escritorio con el mismo servicio; el inventario separa Temporizador y alarmas como A01t); antes **Ajustes del teclado** (A11k: 8 páginas navegables, atajos con el teclado del sistema, ajustes que guardan su clave; modal sobre la app que la abre: Ajustes o el engranaje del teclado de Notas); antes **Ajustes** (A11: lista de 12 categorías, filas con valores reales y su acción, transición de 270 ms con paralaje, sesión; el inventario se parte en A11 / A11k teclado / A11n servicios sin migrar, el total de 2000 no cambia); antes **Paint** (A09: galería con miniaturas reales, lienzo con trazo en vivo, deshacer/limpiar, guardado atómico asíncrono, sesión; rasterizador de Arduino portado píxel a píxel); antes **Notas** (A06: lista, editor, teclado con barra y chips, portapapeles, sesión) que integra el **kit de archivos** (S5); antes **Calendario** (A19), el **kit de componentes**, **App Calculadora** (A12) y la **revisión 6**; antes, **Recientes con memoria y sesiones** (S4e), **Arranque** (S2), **Memoria y Optimizar**, **Sesiones de app** y **Personalizar inicio**. Entorno de
compilación en un contenedor nuevo: `bash tools/setup_idf_cloud.sh` (ver "Entorno" abajo).

### Estado para reanudar (tras Device Care, A17)

* **Rama:** `flexos-esp-idf`. **Último commit:** el de Device Care ("Device Care (A17): ..."), subido a `origin/flexos-esp-idf`; sin
  procesos, agentes ni tareas programadas en segundo plano.
* **Progreso** (`python3 tools/progress.py`): **29,7 %** completado · 70,3 % pendiente · placa 0,0 % de 15 % posible. Inventario de 2000
  puntos intacto (A17 se partió en A17 28 + A17h 11: el IMU y las caídas esperan a A08 / N6).
* **Último módulo completado:** Device Care (A17). Firmware `v3`: 1 736 608 B (66,2 % de la ranura A provisional, 41,4 % de la B),
  0 avisos del compilador (+46 KB). Antes: Juegos (A10). Batería `tools/run_tests.sh`: TODO OK (3 104 815 comprobaciones de host con
  ASan/UBSan y TSan, de ellas 913 031 de `test_dc` frente al código EXTRAÍDO de Arduino; 36 escenas del simulador, incluidas `devcare` y
  `dcui`). Mutantes de Device Care: 243 (83 de la app y del pintor con escena, 70 + 90 de los modelos con la batería de host), 211 muertos,
  6 que no compilan y 26 equivalentes o defensivos (tamaños de buffer, centinelas -1 / -2, topes que el dato nunca alcanza, guardas
  redundantes: cerrar tras suspender, `NULL` frente a longitud 0, la barra de estado que ya tapa la capa de pantalla completa).
* **Hecho en A17 que no hay que repetir:** `tools/gen_devcare_text.py` (tabla de textos ES / EN / FR / PT / IT generada de Arduino y
  comprobada en `run_tests.sh`), `apps/flex_dc_model.*` (métricas, puntuación, historial y motor de diagnóstico en C portable),
  `flex_dc_screens.*` (las nueve pantallas como listas de operaciones de dibujo con las coordenadas de Arduino + la pantalla de caídas sin
  controlador y el inicio con el anillo a medio barrer), `flex_dc_ui.*` (el pintor LVGL: superficies del sistema, vectores en un objeto de
  dibujo, textos como etiquetas, actualización en su sitio de solo vectores o solo cifras), `app_devcare.c` (`flex_app_devcare_ops`:
  cabecera propia, reloj de las animaciones, motor de etapas de 620 ms, test de pantalla a pantalla completa, test táctil, historial
  asíncrono de una escritura a la vez, Optimizar), `flex_system_abnormal_boots()` (NVS `flexcare` / `badboot`, contado en
  `flex_system_start`), `flex_clock_utc()`, simulador (`sim_i2c_recoveries`, `sim_task_wdt`, `sim_system_abnormal`, `sim_read_hold`).
* **Pendiente de A17h (11 pts, 0):** el GY-BNO085 y su controlador (A08 / N6): sondeo, comprobaciones, prueba en vivo, detector, aviso global
  y revisión tras impacto. La pantalla de Caídas dice "No disponible"; el diagnóstico anota "IMU no disponible". Alojar la app en una
  ventana de Modo PC (A05) exige darle la caja de la ventana (`flex_dc_ctx_t` ya está parametrizado) y quitar la cabecera propia.
  Toda la validación en la placa (`ESP_IDF_HARDWARE_REQUIREMENTS.md`, fila "Device Care").
* **Hecho en A04 que no hay que repetir:** `flex_fs_scan.h` + `flex_fsx_list/scan` + `flex_fs_explore_async` / `flex_fs_scan_async`
  (tarea de almacenamiento), `flex_stor_model.*`, `flex_files_model.*` (modelos puros), `flex_stor_ui.*` (pintor de operaciones),
  `app_storage.c` (`flex_app_almacen_ops`), `flex_files.c` (modal: `flex_files_open/active/back/close_now`, enganchado a Atrás / Inicio /
  Recientes / bloqueo en `flex_shell.c`), `flex_system_fw_size()` / `flex_system_reset_reason()`, `flex_metrics.ui_loops`, `FLEX_GLYPH_CLOUD`,
  simulador (`sim_fs_mounted`, `sim_net_online`, `sim_explore_hold/release`, `sim_system_set`, `sim_metrics_set_loops`).
* **Hecho en A10 que no hay que repetir:** `components/flex_games/` (`flex_jumper.cpp`, `jumper_prelude.h`, `jumper_engine.inc` y
  `jumper_games.inc` generados por `tools/gen_jumper_engine.py` y comprobados en `run_tests.sh`, copias idénticas de `FlexOS_Jumper*.h` que
  vigila `check_portable.py`), `apps/app_games.c` (`flex_app_juegos_ops`), `flex_shell_land()` (sin barra de navegación ni gestos del sistema
  en una app `FLEX_APP_LAND`, área completa), excepción acotada `GAME_CANVAS_ALLOWED` en `tools/check_lvgl_only.py` (el motor de Arduino solo
  puede vivir en `flex_games`), `stub_cfg.c` con `flex_cfg_get/set_u32`, simulador (`sim_fb()` ya existía; la escena compara el panel con el lienzo).
* **Pendiente de Juegos:** solo la validación en la placa (60 cuadros/s del lienzo de 768 KB con LVGL; ver `ESP_IDF_HARDWARE_REQUIREMENTS.md`).
* **Pendiente de A04 (A04c, 14 pts, 0):** tarjeta y pantalla del teléfono emparejado (Flex Storage; 03 §9.5 y el cuadro de aprobación §10),
  pestaña Flex Cloud y "Subir a Flex Cloud" (N4) y abrir medios desde el explorador (A02 / A03: hoy avisa). Todo dice "No disponible" en vez
  de fingir. Y toda la validación en la placa (`ESP_IDF_HARDWARE_REQUIREMENTS.md`, fila "Almacenamiento y Archivos").
* **Pendiente de Reloj:** A01t (Temporizador y alarmas: sin referencia en Arduino, necesita sonido de la Fase 11 y avisos).
  `flex_crono_svc_hide_bar(bool)` está listo para Galería / Multimedia (visor a pantalla completa).
* **Reglas vigentes:** no grabar firmware ni tocar particiones sin autorización expresa (identificar antes la revisión del P4,
  `docs/REVISION_CHIP.md`); no tocar la rama Arduino; tras cada módulo `tools/run_tests.sh` + `tools/build.sh --rev v3`, actualizar este
  archivo, commit y push, y mostrar el bloque de progreso. **No editar fuentes mientras corre un script de mutación** (restaura todos
  los archivos que snapshotea al empezar y pisa lo editado).

### Punto de reanudación (siguiente tarea exacta)

1. **Siguiente tarea**, en este orden por valor y viabilidad sin hardware ni red: **Modo PC** (A05, 40; ventanas que alojan otras apps con
   `FLEX_APP_FLEX`: las apps migradas deben maquetarse contra la caja de su ventana —Device Care ya parametriza `flex_dc_ctx_t`; Reloj,
   Calendario, Calculadora y Almacenamiento hay que revisarlas—), después los servicios que se prueban en host sin hardware: **N5** (OTA con
   manifiesto, MD5 y reversión), **N3** (Flex Account, ES256), **N1** (estados de Wi-Fi y NTP como servicio). Las demás (Galería A02, Navegador
   A07, Flex Phone A16, Flex Store A15, Clima A14, Compass A08, Cámara A13, Música A18, Multimedia A03) dependen de M1 o de los servicios N1–N6 /
   del hardware.
   Las filas de **A11n** (hora por red, actualizaciones, Flex Account, Wi-Fi/BLE, robo, audio) se acreditan cuando exista cada servicio: son
   símbolos débiles `flex_*` que `app_settings.c` ya consulta (hoy ausentes: la fila dice "No disponible" y tocarla avisa "Proximamente").
   Cada app implementa `save_session`/`load_session`/`shed` y su escena con mutantes. Ejemplo completo de app con ficheros: `apps/app_notes.c`.
2. Deuda: las miniaturas de páginas fuera de pantalla con vidrio por icono salen con el fondo desplazado (cosmético); S5b (barra de
   medios) espera a Música/Galería. Device Care no tiene descriptor de sesión a propósito: en Arduino `dcSaveSess` solo guarda el historial,
   y aquí el historial se guarda por su cuenta (al anotar, al suspender y al cerrar).
3. Notas: los mutantes "el autoguardado no llega" y "suspender no guarda" **sobreviven por ser equivalentes**: la sesión (a los
   1,2 s y al suspender) también guarda la nota, como en Arduino (`noteSaveSess`); el temporizador de 2 s cubre el caso en que la
   sesión no esté activa.

## Cómo validar antes de cada commit

```
cd FlexOS_Ultra_IDF
bash tools/run_tests.sh                      # host + LittleFS + escenas + reglas
source ~/esp/env.sh && tools/build.sh --rev v3   # firmware (no graba nada)
```

## Hecho (código + pruebas; placa: NO)

| Bloque | Dónde | Pruebas |
|---|---|---|
| Fases 0–1: proyecto, sdkconfig, pantalla ST7701 DSI, 2 FB, LVGL DIRECT, GT911 | `components/flex_display`, `flex_touch`, `main` | compilación, simulador |
| Fase 2: bus, buzón de UI, NVS/LittleFS compatibles con Arduino, monitor del sistema | `flex_core`, `flex_storage`, `flex_system` | host (kv, fs, LittleFS v2.8↔v2.11) |
| Clave del sistema (PBKDF2, diario, migración) | `flex_security` | host (802 comprobaciones) |
| Shell: bloqueo, clave PIN/contraseña, escritorio, caja de apps, Recientes, teclado | `flex_ui/src/shell`, `widgets` | escenas `shell clave caja recientes teclado` |
| Energía: suspensión, bloqueo por inactividad | `flex_power.c` (UI) | escena `energia` |
| Panel rápido completo (cortina, controles reales, editor, catálogo) | `flex_qs*.c` | host + escena `panel` |
| Avisos: banner, Centro, No molestar | `flex_notif*.c` | host + escena `avisos` |
| Apagado completo: confirmación, apagado seguro, animación, deep sleep, filtro de 3 s | `flex_poweroff_ui.c`, `components/flex_power` | host + escena `apagado` |
| Modo seguro: arranque, pantalla, clave antes del acceso, escritorio limitado, limpiar cachés | `flex_safe_ui.c`, `flex_system/flex_safeboot*` | host + escena `seguro` |
| Correcciones de la revisión 4 (tacto, Centro, bloqueo, candados, umbrales, refresco) | varios | escena `regresiones` |
| Restablecimiento de fábrica: marcador transaccional, etapas en exclusiva en el escritor, asistente de 5 vistas, reanudación tras corte | `flex_system/flex_reset*`, `flex_storage` (exclusivo), `flex_factory_ui.c` | host (15 cortes) + escena `fabrica` |
| Primera configuración (OOBE): idioma en vivo, nombre del equipo con su teclado, fin en el bloqueo; borra el marcador de un restablecimiento terminado. Falta el paso de Flex Account (Fases 6–7) | `flex_oobe_*.c` | host (20 000 teclas frente a Arduino) + escena `oobe` |
| Avisos: no salen encima del apagado, el restablecimiento ni la primera configuración (como `fpbScreenAllows`); esperan en la cola | `flex_notif.c` | escenas `fabrica` y `oobe` (mutación) |
| Revisión 5 (memoria, fidelidad, arranque; las tres dimensiones que nunca se habían ejecutado): memoria sin hallazgos; 6 hallazgos verificados y corregidos: contador del Modo seguro grabado antes de seguir (alta), "Reiniciar normalmente" sin reinicio a ciegas, desliz del bloqueo solo vertical, caja no se abre desde la barra de estado, fila "Añadir a inicio" coherente, widgets como Arduino | `flex_safeboot.c`, `flex_lock.c`, `flex_home*.c`, `flex_drawer.c` | host `boot_fw` (código de arranque real sobre NVS falsa) + escenas `regresiones`, `seguro`, `widgets` (mutación de cada uno) |
| Menú contextual del escritorio (candado de app con la clave, kiosco; edición y pantalla completa atenuadas hasta sus bloques) y Modo kiosco completo (zona excluida, candado, vetos de 01c §12.4, salida con la clave, persistencia, arranque directo) | `flex_home_ctx.c`, `flex_kiosk*.c`, `flex_touch_feed.c` (gancho) | host (carga, zona y salida frente a Arduino) + escena `kiosco` (mutaciones) |
| Modo edición del escritorio: temblor y resorte, reordenar con 400 ms, llevar iconos y widgets a la página vecina (700 ms en el borde), seleccionar / mover / redimensionar / quitar widgets, diseño bloqueado, salida guardando | `flex_home_edit.c`, `flex_home_model.c` | host (24 000 operaciones frente a Arduino) + escena `edicion` (mutaciones) |
| Widgets del escritorio como `wgDrawCell`: esfera con agujas, rótulos, barra de almacenamiento, antena, Clima de una fila con su material; datos en su sitio cada 2 s; solo Cámara, Clima y Calendario se tocan | `flex_home_widgets.c` | escena `widgets` |
| Personalizar inicio (01a §10): carrusel de páginas con arrastre elástico, "+", casita, papelera con modal, reordenar con 650 ms, Fondo (8 fondos, Inicio/Bloqueo/Ambos, Restaurar, paleta), Temas (aplicar coherente y restaurar), Widgets (vista previa real, añadir con avisos), Ajustes de inicio (rejilla, tamaño, nombres, bloqueo, indicadores, principal, pellizco, animaciones, encuadre, restablecer), animación de 180 ms con captura del escritorio, pellizco de dos dedos como consumidor previo a LVGL, cierre al bloquear/suspender. Falta "Mis imágenes" como fondo (JPEG por hardware) | `flex_homecfg.c`, `flex_home_model.c` (operaciones y clasificador), `flex_storage` (`flex_fs_list_async`), `flex_home_widgets.c` (vista previa) | host (5000 escritorios, 3000 carpetas, 7500 guiones de pellizco frente a Arduino; 3 mutantes) + escena `personalizar` (14 pasos, 4 mutantes) + firmware `esp32p4` |
| Sesiones de app (01c §14): formato de archivo de Arduino (cabecera de 16 B + CRC-32), guardado diferido (1200 ms / tope 30 s / nunca con el dedo apoyado ni con un restablecimiento en curso), un único volcado pendiente, no reescribir una sesión idéntica, restauración perezosa antes de crear la app, volcado en el acto al cerrar (si no se pudo guardar la app NO se cierra salvo el restablecimiento), re-armado al suspender; ganchos `save_session`/`load_session` en `flex_app_ops_t` | `flex_session_model.c` (portable), `flex_session.c`, `flex_shell.c` (ciclo de vida), `flex_storage` (lectura/escritura asíncrona ya existentes) | host (6000 guiones y 20 000 archivos frente a Arduino; 4 mutantes) + escena `sesiones` (13 comprobaciones, 4 mutantes) + firmware `esp32p4` |
| Memoria (01c §17): nivel sostenido con histéresis y avisos (núcleo `FlexOS_Mem.cpp` de Arduino, con su `test_mem.cpp` portado), soltar en orden LRU solo apps suspendidas y sin trabajo real, admisión al abrir una app cerrada (`"%s no se abre ahora"`), alivio automático con un solo aviso al empeorar, desalojo por presupuesto en cada suspensión (cierra la menos reciente solo si su sesión se guardó), huella medida por app, registro de cachés reconstruibles (fondos, miniaturas de Recientes) y **Optimizar Flex OS** (5 etapas de 420 ms, modal, borra `/System/Cache` por la tarea de almacenamiento, modo visual eficiente temporal que se apaga solo) | `flex_mem_model.c` (portable), `flex_mem.c`, `flex_optimize.c`, `flex_shell.c` | host (12 000 mundos × 8 operaciones frente a `Core.h` de Arduino: 1 660 cierres, 13 246 avisos; 4 mutantes) + escena `memoria` (6 mutantes y el del guardián modal) + firmware `esp32p4` |
| Arranque (01a §2.3-2.5): banda forense solo tras un reinicio anormal (cualquier motivo salvo POWERON, SW o DEEPSLEEP; 2200 ms), splash negro con "FlexOS Ultra" / "ESP32-P4" y la curva de opacidad de Arduino (600 ms sube, 1400 fijo, 600 baja) con tres puntos que cambian cada 320 ms, toques tragados mientras dura, destino al terminar (Modo seguro tras la banda sin splash, primera configuración, kiosco o bloqueo); nada bloquea la tarea de UI (`lv_timer`) | `flex_boot_model.c` (portable), `flex_boot_ui.c`, `flex_shell.c` (destino) | host (motivos 0..20 y curva 0..3200 ms frente a `resetReasonStr` y `splashTick`) + escena `arranque` (4 casos, 3 mutantes) + firmware `esp32p4` |
| Recientes con la memoria y las sesiones (01a §12.3, §12.5): estado de cada tarjeta "Activa" / "Pausada" / "Estado guardado" (`flex_mem_app_shed`), "Consumo estimado" con la PSRAM medida o "No disponible" (nunca un número inventado), punto de cambios sin guardar (`dirty()` o sesión de disco desfasada), cerrar una tarjeta guarda la sesión en el acto y, si no se puede, la app se queda abierta con "Sin espacio para guardar" (también en "Cerrar todas") | `flex_recents.c` | escenas `memoria` y `sesiones` (3 mutantes) + firmware `esp32p4` |
| App Calculadora (03 §3): lógica pura probada tecla a tecla frente a `AppsBasic.h` (30 000 secuencias, 1 047 errores, 246 714 operaciones a medias; sesión `calc.bin` byte a byte), línea de operación ("25 + 10"), "Error", reparto adaptativo del lienzo con la rejilla 5×4 prioritaria, teclas naranja de marca, la primera apertura tras el arranque recupera la sesión y las demás empiezan en "0"; primera app real sobre el marco (guardado diferido, restauración, Recientes) | `flex_calc_model.c`, `app_calc.c` | host + escena `calculadora` (24 pasos, 4 mutantes) + firmware `esp32p4` |
| Kit de componentes y de maquetación (02 §7, §8.4): botones, interruptor, fila-tarjeta, línea informativa, barra, deslizador, pestañas, diálogo, aviso local (uno por padre, sin estado global) y `uiBox/uiPad/uiGap/uiFontH/uiLineH/uiFontFit` | `widgets/flex_kit.*`, `flex_layout.*` | host (`test_layout` frente a Arduino) + escena `kit` (mutantes) |
| App Calendario (03 §4): rejilla del mes, círculo de hoy, panel lateral "Hoy", estadísticas; modelo probado frente a `AppsBasic.h` | `apps/app_calendar.c`, `flex_cal_model.*` | host + escena `calendario` |
| Capa de operaciones de archivo (flexFs*): borrar recursivo, renombrar conservando la extensión, papelera con la ruta en el nombre, restaurar sin pisar, vaciar, fichero nuevo con el primer nombre libre, estadística de carpeta; todo en la tarea de almacenamiento | `flex_storage/src/flex_fs_names.*`, `flex_fs_ops.*`, `flex_storage.c` | host (frente a `FlexOS_FS.cpp` + disco real; 10 mutantes) |
| Kit de archivos (03 §5): menú contextual, diálogo de nombre con el teclado del sistema, confirmación, papelera (lista con ventana de filas), "sin almacenamiento", texto en caja. **S5 se acredita al integrarlo en Notas** | `widgets/flex_filekit.*`, `flex_fk_model.*`, glifos del menú en `flex_icons.c`; disco real en el simulador (`sim_disk_*`) | host (`test_fk` frente a `FileKit.h`) + escena `kit-archivos` (11 mutantes) |
| App Notas (03 §6): lista de tarjetas (vista previa real, selección, menú, renombrar, papelera, borrar), editor (texto con cursor, selección y manijas, Cortar/Copiar/Pegar/Todo, pulsación larga, scroll, acentos, corrector), teclado con barra de 5 botones y chips, portapapeles de 12 ranuras con panel, menú "más", autoguardado, sesión `notas.bin` v3. Lógica pura probada frente a Arduino (3 000 dibujos con cursor, selección, ortografía y menú) | `apps/app_notes.c`, `flex_ned.*`, `flex_note_model.*`, `widgets/flex_clip.*`, `flex_kb.c` (extras), glifos en `flex_icons.c` | host (`test_ned`, `test_clip`, `test_notes`) + escena `notas` (15 mutantes detectados, 2 equivalentes) |
| App Paint (03 §7): galería (tarjetas con la miniatura real: los mismos trazos reproducidos a escala; botón flotante; selección; menú; renombrar; papelera; borrar), lienzo (dibujo con su proporción, trazo en vivo con bordes suaves, 6 colores, 3 grosores, deshacer y limpiar reales), guardado atómico asíncrono tras cada trazo (una escritura en vuelo, la siguiente lleva lo último), reintento tras un fallo de disco, sesión `paint.bin` v1, `shed` de miniaturas y del lienzo. Formato `.fxp`, geometría, captura del trazo y rasterizador probados frente a Arduino (12 000 operaciones, 475 995 fotogramas de dedo, 5 000 figuras píxel a píxel) | `apps/app_paint.c`, `flex_paint_model.*`, `widgets/flex_raster.*` | host (`test_paint`, `test_raster`; 12 mutantes del modelo) + escena `paint` (23 mutantes detectados) |
| App Ajustes (03b §11): lista de 12 categorías y su pantalla con scroll nativo sin inercia, transición de 270 ms con paralaje del 28 %, volver por chevron / borde izquierdo / botón atrás, 12 pantallas con sus filas y valores REALES (idioma, formato de hora, brillo con el ciclo de 25 en 25, estilo, apariencia, barra de navegación, widgets del bloqueo, iconos, transiciones, bloqueo de inactividad, apagado seguro, restablecer, almacenamiento con la papelera medida, "Dispositivo"), cada fila escribe solo su clave, sesión `ajustes.bin` v1. Contenido, acciones y sesión probados frente a Arduino (18 000 pantallas, 9 000 acciones); 8 glifos de fila y 12 iconos de categoría en `flex_icons.c` | `apps/app_settings.c`, `flex_set_model.*`, `widgets/flex_icons.c` | host (`test_set`; 16 mutantes del modelo) + escena `ajustes` (16 mutantes de la app) |
| Ajustes del teclado (A11k): 8 páginas (principal, idiomas, diseño y tamaño, deslizar y tocar, símbolos, atajos, editor de atajos, sobre teclado), 12 ajustes con su clave en la NVS (`kbsize`, `kbfast`, `kbtool`, `kbpred`, `kbspell`, `kbemoji`, `kbhicon`, `kbopa`, `kbstyle`, `kbfont`, `kblp`, `kbfx`, `kbsyms`, `kbscabr`, `kbscexp`), filas apagadas por hardware, restablecer, editor con el teclado del sistema, transición de 90 ms según el estilo de animación, modal que Atrás / Inicio / Recientes / el bloqueo cierran; Notas relee sus ajustes al volver. Páginas, acciones, volver y toques probados frente a Arduino (16 000 páginas, 16 000 acciones, 20 000 toques) | `apps/app_kbsettings.c`, `flex_kbs_model.*`, `flex_set_ui.*` (compartido con Ajustes), `flex_kb_layout.c` (`flex_kb_prefs_save`) | host (`test_kbs`) + escena `teclado-ajustes` (incluye el engranaje de Notas) |
| App Reloj (A01; 03 §1-2): contenedor con tres pestañas (Hora: reloj grande con la fuente del bloqueo escalada a la altura de cifras de Arduino, fecha larga, tarjetas Fecha / Formato, pie; Cronómetro: esfera de 60 marcas con dos agujas y cubo, tabla de vueltas con la mejor y la peor, dos botones circulares con zona de r + 6, 30 fps solo mientras corre; Temporizador: "Próximamente", como Arduino), servicio `flex_crono` con `now` inyectado y un único estado (la app, la cápsula, la tarjeta, el mosaico del panel rápido y el widget del escritorio lo leen; sigue contando con la app cerrada y con el bloqueo), cápsula opaca en la barra de estado del escritorio y del marco estándar (ancho por clase MM:SS / H:MM:SS, la hora normal cede si no cabe todo; nunca bajo la cortina), tarjeta expandida modal en `lv_layer_top` con el morfeo de 190 ms de Arduino y contenido desde p ≥ 0,55, botones, gesto arriba / tocar fuera para contraer, tiempo e icono abren la app, pulsación larga > 700 ms, bloqueo y otras pantallas la sueltan; cabecera estándar y tarjetas de apps recoloreadas al cambiar el tema (corrección del marco). Lógica, formato, glifo, cápsula, `cronoBarSurface`, `cronoCapsuleTick`, maqueta, esfera, tabla, botones, tarjeta, tacto y pestaña Hora probados frente al código EXTRAÍDO de Arduino (649 000 comprobaciones; 2048 combinaciones de superficie; 60 000 cuadros de tacto) | `apps/app_reloj.c`, `flex_chrono_model.*`, `shell/flex_crono.*`, `flex_frame.c` (cápsula), `flex_icons.c` (`FLEX_GLYPH_STOPWATCH`), `flex_kit.c` (`flex_kit_segmented_at`), `flex_qs.c` (mosaico), `flex_shell.c` (cabecera y tema) | host (`test_chrono`; 62 mutantes del modelo muertos) + escena `reloj` (51 mutantes de la interfaz: 45 muertos, 6 equivalentes documentados) + escena `widgets` |
| App Almacenamiento + Archivos (A04 local; 03 §8-§9): pantalla principal (memoria interna y PSRAM con barras, tarjeta "Detalles de memoria y sistema" con el estado de salud y el boton Optimizar, categorias Documentos / Sistema / Aplicaciones / Papelera medidas de verdad en la tarea de almacenamiento —asincrono: "..." y "Calculando..." hasta que llega, "--" y "No se pudo leer" si falla, nunca una cifra inventada—, los archivos mayores sin los de la biblioteca y la fila "Todos los archivos / Ver..."), detalles (5 secciones con scroll nativo: Optimizar, PSRAM, SRAM, flash con el tamano del firmware leido de la cabecera de la imagen en ejecucion y las categorias, estado del sistema con el ritmo real de la tarea de UI en vueltas/s; refresco por firma una vez por segundo con el dedo fuera), explorador modal sobre Almacenamiento (carpetas primero, "..", biblioteca /System/Media oculta, hasta 256 entradas con FILAS POR VENTANA, pulsacion larga = menu, Eliminar con confirmacion, Renombrar con el teclado del sistema, Papelera, seleccion multiple con su barra, "Flex Cloud no disponible", Atras cierra capas / vuelve de la nube / sale), todo sobre las operaciones reales de la tarea de almacenamiento (`flex_fs_explore_async`, `flex_fs_scan_async`, borrar, renombrar, papelera). Modelos puros comparados con el codigo EXTRAIDO de Arduino: listado y medidas (`flexFsList/CatSize/Largest`, 160 arboles aleatorios), las pantallas (`almRenderMain`, `almDetDrawSection`: 4 000 variantes de caja, telefono y medidas; detalle con 3 000 estados de sistema × 5 secciones) y el explorador (`filesTick`, `filesReload`, `filesOpenEntry`, `filesMenuAction`: toques, pulsacion larga, dialogos, filas). Diferencias deliberadas y probadas: "Ver..." tiene prioridad sobre los archivos grandes (Arduino dejaba el explorador inalcanzable), una entrada cuya ruta no cabe en 160 B no se lista (Arduino la recortaba y la accion tocaba OTRO archivo), hasta 256 entradas ordenadas (Arduino: las 24 primeras) | `apps/app_storage.c`, `apps/flex_files.c`, `apps/flex_stor_ui.c`, `apps/flex_stor_model.*`, `apps/flex_files_model.*`, `flex_storage` (`flex_fs_scan.h`, `flex_fsx_list/scan`), `flex_system` (`flex_system_fw_size`, `flex_system_reset_reason`), `flex_metrics` (`ui_loops`), `flex_icons.c` (`FLEX_GLYPH_CLOUD`), `flex_shell.c` (Atras / Inicio / Recientes / bloqueo cierran el explorador) | host (`test_fsscan`, `test_stor`, `test_files`; mutantes de los modelos y de la capa de datos) + escena `almacenamiento` (52 mutantes de la interfaz: 51 muertos, 1 solo visual)
| App Juegos: Jumper (A10; 03b §14): el juego de Arduino (`FlexOS_Jumper.h` y `FlexOS_Jumper_Level.h`, copias IDENTICAS que comprueba `check_portable.py`: fisica de paso fijo a 120 Hz, nivel de 900 bloques a 10,4 bloques/s, cubo y nave, portales, monedas, particulas, intro, selector con records, modo practica con puntos de control, pausa, final) corriendo SIN CAMBIOS sobre el motor de dibujo de Arduino extraido tal cual (`tools/gen_jumper_engine.py` genera `jumper_engine.inc`: primitivas de `Gfx.h`, fuente Outfit antialiasada y 5x7; `run_tests.sh` comprueba que coincide con los originales). El adaptador (`flex_games/src/flex_jumper.cpp` + `jumper_prelude.h`) da lo que daba el sketch: el lienzo FISICO de 480 x 800 (el motor gira 90 grados al dibujar el lienzo logico de 800 x 480, como en Arduino), el tactil fisico con el flanco de apoyo de cada cuadro, el reloj en microsegundos, los records en las MISMAS claves del NVS (`jmp_bn`, `jmp_bp`, `jmp_coin`, `jmp_cmp`, `jmp_att`) y la salida. La app (`app_games.c`) muestra el lienzo como una imagen de LVGL a pantalla completa y da vueltas al juego cada 4 ms (el juego presenta a 60 Hz exactos por su cuenta). Es `FLEX_APP_LAND`: el marco la deja sin barra de navegacion ni gestos del sistema (cortina del panel rapido, Centro de avisos) y sin miniatura en Recientes (`flex_shell_land`); suspender la deja en pausa (`gamesSuspend`), cerrarla guarda lo pendiente | `components/flex_games/` (`flex_jumper.cpp`, `jumper_prelude.h`, `jumper_engine.inc`, `jumper_games.inc`, copias de Jumper), `apps/app_games.c`, `flex_shell.c` (`flex_shell_land`, area completa), `flex_qs.c` / `flex_notif.c` (sin gestos en horizontal), `tools/gen_jumper_engine.py`, `tools/check_lvgl_only.py` (excepcion acotada) | host (`test_jumper`: intro, selector, partida, muerte y reaparicion, salto, pausa, records y su guardado al cerrar tras morir, salida, suspender / reanudar, determinacion, sin escrituras fuera del lienzo; 13 mutantes del adaptador muertos) + escena `juegos` (16 mutantes de la app y del marco: 10 muertos y 6 equivalentes o no observables en el simulador: reanudar repinta una pausa que el siguiente cuadro ya repinta, `G.susp` redundante con borrar el temporizador, cerrar tras suspender no tiene nada que guardar, las coordenadas del flanco las corrige el PRESSING del mismo ciclo, el lienzo se repinta entero, y la fuga del buffer solo la ve un analizador de memoria)
| App Device Care (A17; 03b §15): metricas reales (memoria interna y PSRAM de `flex_mem`, motivo de reinicio y reinicios inesperados acumulados de `flex_system`, vigilante de ESP-IDF, ritmo de la UI, servicios, errores con las recuperaciones del bus de `flex_i2c`, particion de datos), puntuacion 0..100 con su palabra, historial de 16 registros en `/System/devcare.bin` (lectura y escritura asincronas, una a la vez, sin perder el registro que llega mientras tanto, otro formato u otra version se ignora) y diagnostico completo: etapas de 620 ms, test de pantalla a pantalla completa en ocho pasadas con los pixeles de verdad, pregunta, test tactil de 6 x 8 celdas con la division entera de Arduino, resultado con la media de lo que SE PUDO medir. Las nueve pantallas son listas de operaciones de dibujo con las coordenadas exactas de Arduino (`flex_dc_screens.c`, comparadas primitiva a primitiva con las funciones originales extraidas) que pinta `flex_dc_ui.c` con LVGL; las animaciones cambian solo vectores y cifras en su sitio. Optimizar abre el panel del sistema y anota la pasada. Sin controlador del IMU (A17h) la pantalla de Caidas dice "No disponible", nombra el modulo que espera y no finge un estado | `apps/app_devcare.c`, `flex_dc_model.*`, `flex_dc_screens.*`, `flex_dc_ui.*`, `flex_dc_text.inc` (generado por `tools/gen_devcare_text.py`), `flex_system_abnormal_boots()`, `flex_clock_utc()` | host (`test_dc`: 913 031 comprobaciones frente a Arduino extraido: textos de los 5 idiomas, 8 metricas, nota, historial, motor de diagnostico, resultado, 9 pantallas en cajas de 300 a 480 x 330 a 800; 90 + 70 mutantes automaticos, 46 + 87 muertos y el resto equivalentes) + escenas `devcare` y `dcui` (83 mutantes de la app y del pintor: 78 muertos y 5 equivalentes: cerrar tras suspender, `NULL` frente a longitud 0, reanudar el test de pantalla que ya esta ahi, la barra de estado que tapa la capa de pantalla completa, el barrido cero que LVGL ya no pinta) y `dcui` (los pixeles de cada tipo de operacion y de cada rol de la paleta, las reglas de actualizacion)

## Progreso global (método congelado v1)

Cálculo reproducible: `python3 tools/progress.py` (lee la tabla de abajo; no hay otro origen).

* **Pesos** (suma 2000): esfuerzo ≈ líneas del firmware Arduino de referencia que cada ítem
  reemplaza (`WebUI.h` ×0,3 por ser HTML en cadenas; motor `Gfx/Font/Types/HAL` = 0, no se
  migra), más pesos fijos para lo que Arduino no tiene (base IDF, C6, particiones, revisión
  y optimización). 1 punto ≈ 100 líneas Arduino equivalentes. **No se tocan entre sesiones**:
  solo si cambia el alcance, y entonces se anota aquí la causa y se recalcula.
* **Crédito**: `Impl=1` (85 % del peso) solo si el ítem está implementado, integrado, verificado
  en host o simulador y sin regresiones. `Hw=1` (15 %) solo con evidencia en la placa real. Sin
  créditos parciales: código a medias, planes, documentación y escenas decorativas = 0.
* La lógica portable ya copiada (≈20 000 líneas en `flex_portable`) no cuenta hasta que el
  ítem que la usa esté integrado y probado.
* Sin la placa, el máximo alcanzable es el 85 %.

| id | Ítem | Peso | Impl | Hw |
|---|---|---:|:-:|:-:|
| B1 | Base: proyecto ESP-IDF 5.5.5, sdkconfig por revision, particiones provisionales, herramientas | 20 | 1 | 0 |
| B2 | Base: pantalla ST7701 DSI, 2 framebuffers, LVGL DIRECT, GT911 | 60 | 1 | 0 |
| B3 | Base: bus de eventos, buzon de UI, NVS/LittleFS compatibles, monitor del sistema, clave PBKDF2 | 60 | 1 | 0 |
| S1 | Shell migrado: bloqueo, clave, kiosco, escritorio, edicion, widgets, caja, Recientes, panel rapido, avisos, energia, apagado, Modo seguro, restablecimiento, OOBE (sin Account), teclado | 258 | 1 | 0 |
| S2 | Arranque: banda forense de reinicio anormal, splash con su curva y destino (01a 2.3-2.5) | 8 | 1 | 0 |
| S2b | Paso Flex Account del OOBE (necesita las Fases 6-7) | 1 | 0 | 0 |
| S3a | Personalizar inicio: operaciones del modelo (paginas, principal, rejilla, reset) + host | 6 | 1 | 0 |
| S3b | Personalizar inicio: vistas Paginas y Ajustes de inicio + modales (LVGL) | 11 | 1 | 0 |
| S3c | Personalizar inicio: Fondo, Temas y Widgets (LVGL) | 6 | 1 | 0 |
| S3e | Personalizar inicio: Mis imagenes, imagen propia como fondo y encuadre (necesita el JPEG por hardware, M1) | 2 | 0 | 0 |
| S3d | Personalizar inicio: pellizco de dos dedos (clasificador + consumidor de toque) | 3 | 1 | 0 |
| S4c | Marco de apps: sesiones (formato, guardado diferido, restauracion perezosa, cierre seguro) y ganchos save/load_session | 8 | 1 | 0 |
| S4d | Marco de apps: desalojo por presupuesto, apps con segundo plano, huella medida por app y admision al abrir (01c 17.3, 17.6, 17.9) | 15 | 1 | 0 |
| S4e | Marco de apps: Recientes con huella, Pausada/Estado guardado y aviso de cambios sin guardar; regreso | 8 | 1 | 0 |
| S4b | Memoria y funcion Optimizar (01c 17), umbrales y avisos | 8 | 1 | 0 |
| S5 | Infraestructura compartida de apps en LVGL: kit de componentes, kit de archivos (menu, nombre, confirmacion, papelera) y su capa de operaciones; integrada en Notas | 9 | 1 | 0 |
| S5b | Infraestructura compartida: barra de medios (alcance separado de S5 al cerrarse el kit; el total de 2000 no cambia) | 3 | 0 | 0 |
| A01 | App Reloj: pestanas Hora y Cronometro (esfera, vueltas, botones), capsula de la barra de estado, tarjeta expandida, mosaico del panel rapido y widget con el mismo servicio | 14 | 1 | 0 |
| A01t | Reloj: Temporizador y alarmas (el inventario los separa de A01 al cerrarse: Arduino no los tiene, la pestana dice "Proximamente"; alarmas y fin del temporizador necesitan el sonido de la Fase 11 y avisos; el total de 2000 no cambia) | 3 | 0 | 0 |
| A02 | App Galeria + visor + editor de fotos + editor de video | 145 | 0 | 0 |
| A03 | App Multimedia | 16 | 0 | 0 |
| A04 | App Almacenamiento + Archivos (parte local: medidores, detalles de memoria y sistema con Optimizar, categorias y archivos grandes medidos en la tarea de almacenamiento, explorador modal con borrar / renombrar / papelera / seleccion multiple / lista de 256 con filas por ventana) | 62 | 1 | 0 |
| A04c | Almacenamiento y Archivos: lo que depende de servicios sin migrar (tarjeta y pantalla "Flex Cloud en tu telefono" = servicio Flex Storage del telefono emparejado, cuota, pausa y olvido, 03 §9.5; cuadro de aprobacion del emparejamiento, §10; pestana "Flex Cloud" del explorador y "Subir a Flex Cloud" = N4; abrir fotos / video / audio desde el explorador en su visor = A02 / A03). Hoy la tarjeta dice "No disponible todavia", la pestana "Flex Cloud no disponible" y los medios avisan que no hay visor: nada se finge. El total de 2000 no cambia | 14 | 0 | 0 |
| A05 | App Modo PC (DeX) | 40 | 0 | 0 |
| A06 | App Notas | 8 | 1 | 0 |
| A07 | App Navegador | 95 | 0 | 0 |
| A08 | App Flex Compass (brujula + AHRS) y sensores BNO085 | 40 | 0 | 0 |
| A09 | App Paint | 9 | 1 | 0 |
| A10 | App Juegos: Jumper (el unico juego de Arduino: nivel de 900 bloques, cubo y nave, particulas, intro, selector, practica, pausa, records) sobre el motor de Arduino extraido; pantalla completa horizontal sin gestos del sistema | 40 | 1 | 0 |
| A11 | App Ajustes: lista de categorias, las 12 pantallas con sus filas reales, transicion, sesion y acciones locales | 14 | 1 | 0 |
| A11k | Ajustes del teclado (8 paginas: filas, idiomas, tamano y diseno, atajos con editor, simbolos; `flex_kbsettings_open` que Notas ya espera) | 8 | 1 | 0 |
| A11n | Ajustes: filas que dependen de servicios sin migrar (hora por red, actualizaciones, Flex Account, Conectividad/Wi-Fi/BLE, proteccion contra robo, audio); hoy dicen "No disponible" y no abren nada | 3 | 0 | 0 |
| A12 | App Calculadora (cuatro operaciones, linea de operacion, sesion calc.bin) | 3 | 1 | 0 |
| A19 | App Calendario | 3 | 1 | 0 |
| A13 | App Camara (necesita el modelo del sensor) | 3 | 0 | 0 |
| A14 | App Clima + servicio del tiempo | 57 | 0 | 0 |
| A15 | App Flex Store + paquetes + maquina virtual de apps + permisos | 101 | 0 | 0 |
| A16 | App Flex Phone (enlace con el telefono) | 127 | 0 | 0 |
| A17 | App Device Care: inicio con tarjeta y anillo animado, estado, salud, optimizacion, historial persistente y diagnostico completo (sistema, pantalla en ocho pasadas, tactil, resultado) sobre los servicios reales | 28 | 1 | 0 |
| A17h | Device Care: lo que depende del IMU GY-BNO085 y de la deteccion de caidas (sondeo del modulo, estados ausente / sondeando / listo, cinco comprobaciones, prueba con lecturas en vivo, detector en segundo plano y aviso global de caida, revision tras impacto, eventos de caida en el historial). Causa: el controlador del IMU y la fusion de sensores son A08 / N6. Hoy la pantalla de Caidas dice "No disponible", nombra el modulo que espera y no finge un estado; el diagnostico anota "IMU no disponible". El total de 2000 no cambia | 11 | 0 | 0 |
| A18 | App Musica + audio ES8311 | 21 | 0 | 0 |
| M1 | Multimedia nucleo: JPEG HW, miniaturas, biblioteca, AVI MJPEG, escritores WAV/AVI | 82 | 0 | 0 |
| N1 | Red: Wi-Fi (estados, reconexion) y NTP en LVGL/servicio | 17 | 0 | 0 |
| N2 | Servidor web + compartir por HTTP + Media web (la web embebida cuenta x0.3) | 78 | 0 | 0 |
| N3 | Flex Account (ES256, claves fijadas, OOBE) | 31 | 0 | 0 |
| N4 | Flex Cloud (control, streaming, TLS) | 87 | 0 | 0 |
| N5 | OTA con identificadores propios (manifiesto, MD5, rollback) | 33 | 0 | 0 |
| N6 | Antirrobo y deteccion de caidas | 51 | 0 | 0 |
| X1a | Liquid Glass: superficies de vidrio sobre fondo y color liso | 16 | 1 | 0 |
| X1b | Liquid Glass: 3 niveles y bandas visibles | 18 | 0 | 0 |
| X1c | Liquid Glass: PPA/aceleracion medidos | 16 | 0 | 0 |
| X2a | Tactil: arbitraje antes de LVGL, bordes del sistema, suspension con dos dedos | 14 | 1 | 0 |
| X2b | Tactil: veto al teclear y umbrales de toque de Arduino | 16 | 0 | 0 |
| X3 | Wi-Fi por el ESP32-C6 (esp-hosted) + integracion P4-C6 + firmware del C6 | 70 | 0 | 0 |
| X4 | Particiones A/B con el tamano medido | 30 | 0 | 0 |
| X5 | Revision de seguridad y estabilidad globales | 50 | 0 | 0 |
| X6 | Optimizacion medida (memoria, rendimiento, consumo) | 40 | 0 | 0 |

**Contraste con la estimación anterior** (≈258 h realistas restantes, sin desglose conservado):
con este inventario lo hecho equivale al 18 % de la obra medida en líneas de referencia, así que
esa cifra de horas era optimista para el reparto de las 19 apps. Se recalibrará con la velocidad
medida en las primeras apps (el shell se cerró a ≈120 líneas Arduino/h, con lógica portable a
favor en las apps).

## En curso / pendiente (orden de trabajo)

1. Revisiones 4 y 5 cerradas (todas las dimensiones ejecutadas al menos una vez). La
   siguiente revisión, al cerrar el marco de apps y las primeras apps. Personalizar inicio aún
   no ha pasado por revisión adversarial (candidata: junto con el marco de apps).
2. Fase 3, shell restante: sesiones de app (01c §14), memoria y "Optimizar" (01c §17), arranque
   (splash/banda forense, 01a §2). Personalizar inicio está hecho salvo "Mis imágenes" como fondo
   (S3e, necesita el JPEG por hardware de la Fase 9).
3. Fase 3, apps (las 19 abren hoy "Pendiente de migrar"): marco común de apps → Ajustes,
   Almacenamiento/Explorador, Reloj, Calculadora, Calendario, Notas, Paint, Clima,
   Galería (+editores, visor), Multimedia, Música, Cámara, Device Care, Flex Compass,
   Navegador, Flex Store, Flex Phone, Juegos, Modo PC. Faltan las especificaciones 04–07.
4. Fase 4 (bandas del vidrio, 3 niveles, PPA medido) y Fase 5 (veto al teclear, umbrales).
5. Fases 6–8 (Wi-Fi C6 esp-hosted, red, servidor web, Account, Cloud), 9–13 (multimedia,
   cámara, audio, Device Care, Compass), 14–17 (OTA, particiones, seguridad, optimización).

## Resultados de la última validación (Notas, kit de componentes, Calendario, kit de archivos; antes: Personalizar inicio, sesiones, memoria, arranque y Recientes)

| Comprobación | Resultado |
|---|---|
| `tests/host` (ASan+UBSan y TSan), incl. `test_homecfg`, `test_session`, `test_mem_model`, `test_boot`, `test_calc`, `test_layout`, `test_fsnames` (20 013 operaciones de papelera/restaurar/renombrar/nombre nuevo frente a `FlexOS_FS.cpp`), `test_fsops` (operaciones sobre un disco real, con salida de la raíz intentada) `test_fk` (menú, confirmación, papelera, texto en caja y nombre frente a `FileKit.h`), `test_ned` (editor de Notas: 3 000 dibujos con cursor, selección, ortografía y menú frente a `Keyboard.h`), `test_clip` (portapapeles de 12 ranuras y NVS frente a Arduino) y `test_notes` (lista y sesión `notas.bin` v3) | OK: 984 688 comprobaciones, 0 fallos |
| Mutantes de las operaciones de archivo (colisión que pisa, `@` sin escapar, recorte del nombre, sufijos agotados, restaurar sin validar la ruta, renombrar que pisa, tirar la papelera a sí misma, renombrar sin conservar la extensión, fichero nuevo que pisa, borrar sin recursión) | 10/10 detectados |
| Mutantes de la interfaz del kit de archivos (barra sin rehacer, menú que no se cierra, nombre inicial con `/`, volver de "sin almacenamiento", todas las filas creadas, ocupado sin liberar, borrar y vaciar sin confirmar, cancelar que confirma, `close_all` que no cierra) | 11/11 detectados (2 sobrevivían por una escena floja: se reforzó con `flex_fk_layers`) |
| Mutantes de los modelos (pellizco, principal al borrar, redondeo del reordenar; tope de 30 s, cierre forzado, CRC al leer, otra app pendiente; LRU con empates, cerrar sin guardar, admitir dos veces, alivio sin empeorar) | 11/11 detectados |
| `tools/run_tests.sh` completo (host, LittleFS, 22 escenas, reglas) | TODO OK |
| Escena `calculadora` | 24 pasos OK (teclas con el dedo, encadenado, Error, sesión y restauración); 4 mutantes detectados |
| Escena `arranque` | 4 casos OK (normal, tras un PANIC, Modo seguro, primera configuración); 3 mutantes detectados |
| Escena `memoria` | OK; 7 mutantes de la interfaz detectados (admisión, presupuesto al suspender, soltar en Optimizar, modo eficiente, cierre con un toque, guardián modal de Inicio) |
| Escena `sesiones` | 13 comprobaciones OK; 4 mutantes de la interfaz detectados (el último, tras añadir el paso de re-armado) |
| Escena `personalizar` | 14 pasos OK; mutantes de la interfaz (nueva página, cierre al bloquear, confirmar borrado, casita) detectados |
| Escena `notas` (disco real, dedo y teclado: crear, escribir, chips, acentos, menú de selección, portapapeles, "más", autoguardado, renombrar, papelera, borrar, límites, sesión, suspensión, fallo de guardado) | OK; 16 mutantes: 14 detectados y 2 equivalentes (la sesión también guarda la nota); el escenario encontró y corrigió un fallo grave: al abrir una nota existente se vaciaba el texto (y se habría guardado vacío) |
| Escenas `kit`, `calendario` y `kit-archivos` | OK (la última con disco real: menú, confirmación, nombre con el teclado, papelera con restaurar/borrar/vaciar, lista de 60 con ventana de filas, "sin almacenamiento") |
| Firmware `tools/build.sh --rev v3` | OK sin avisos: 1 472 384 B, 56,2 % de la ranura A (2,5 MB), 35,1 % de la B (4 MB) |
| Capturas revisadas a ojo | Páginas, Fondo, Widgets, Ajustes y modal fieles a 01a §10 |
| **Placa real** | NO probado (tacto del carrusel, fluidez de la animación y del pellizco con el GT911) |

## Entorno de compilación (contenedor de la nube)

* El proxy solo permite GitHub y PyPI: `dl.espressif.com` y `components.espressif.com` están
  bloqueados. `bash tools/setup_idf_cloud.sh` instala ESP-IDF v5.5.5 y la cadena RISC-V en `~/esp`
  (las herramientas salen de las releases de GitHub; lvgl y esp_littlefs ya se piden por git).
  Después: `source ~/esp/env.sh && tools/build.sh --rev v3`. La compilación completa tarda ≈4 min.
* Decisión: compilar y probar en cada punto de control; no repetir `build.sh --clean` salvo que cambie
  el `sdkconfig`.

## Revisión 6 (adversarial, 2 revisores en paralelo, solo lectura)

Alcance: Personalizar inicio, arranque, sesiones, memoria/Optimizar y Recientes. **21 hallazgos, 17 corregidos** con su
prueba de regresión (cada reversión se comprobó contra las escenas): `write_done` llamaba a un puntero nulo si otra app
tenía cambios pendientes (alta); el desalojo por presupuesto elegía siempre la misma víctima porque `terminate_cb` no
dejaba la app cerrada (la maqueta de la prueba sí lo hacía y lo escondía); la app que se abre podía ser la víctima al
suspender la de delante; cerrar la app de delante bajo presión ejecutaba `on_close` dos veces; `drag_moved` no se
reiniciaba y se perdía el primer toque en la barra inferior tras arrastrar el carrusel (alta); cerrar por bloqueo no
reconstruía el escritorio; un fallo de disco tras cerrar la app reintentaba su `save_session`; la guardia de las
miniaturas de Recientes; la app del kiosco se podía negar por memoria; las etiquetas "Página N" no seguían a su
tarjeta; la miniatura del widget no recogía el toque; "Usar"/"Añadir" 16 px a la izquierda; "Restablecer" iba a Páginas;
la cadena de listados de "Mis imágenes" sin comprobar; y la huella no se re-medía al reanudar.
**Baja prioridad, verificada y cerrada (sesión del kit):** `load_once` ya no marca "cargada" si la lectura
asíncrona no llegó (`flex_session.c`, con su regresión); la huella mide PSRAM **y** SRAM interna (`free_alloc()` en
`flex_mem.c`), así que una app ligera ya no enseña "0 B". **Pendientes, documentados:** Device Care guarda su historial
por su cuenta (sin descriptor; se resuelve con esa app); las miniaturas de páginas fuera de pantalla con vidrio por icono
salen con el fondo desplazado (cosmético).

## Bloqueos y datos que faltan

* **Revisión del chip P4** (bloqueante para grabar), firmware del C6, sensor de la cámara,
  batería, INT del GT911: ver `ESP_IDF_MIGRATION_PLAN.md` §9.
* **Ninguna prueba en la placa**: la primera prueba de humo de las Fases 0–2 (con
  autorización expresa del usuario para grabar) es lo que más riesgo quita.
* Revisiones multiagente: los flujos grandes mueren por el límite de uso (revisión 3:
  23/24 agentes; revisión 4: 5/7). La revisión 5 (3 agentes) terminó entera: seguir
  con flujos de 2–3 agentes.

## Estimación del trabajo restante (actualizar al cerrar cada bloque)

Implementación (sin validación en la placa): optimista ≈172 h, realista ≈258 h,
conservador ≈387 h de trabajo efectivo (desde la auditoría del 10-10 se han cerrado
la primera configuración, la revisión 5, los widgets, el menú contextual, el kiosco y
el Modo edición: ≈22 h del escenario realista). Validación en la placa aparte (≈20–40 h
conjuntas, depende de grabar con autorización). Base: tamaño del código Arduino que
queda frente al ritmo medido en las Fases 0–3 (ver el informe de auditoría del 10-10).

Última actualización: Calculadora y revisión 6. Quedan de realista ≈228 h (S3e depende del JPEG de la Fase 9).
