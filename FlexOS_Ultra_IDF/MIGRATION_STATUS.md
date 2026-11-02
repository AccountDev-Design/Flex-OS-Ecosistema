# Flex OS Ultra → ESP-IDF · Estado para reanudar

Documento corto y vivo: qué está hecho DE VERDAD, qué falta y por dónde seguir. El
detalle de cada decisión está en `ESP_IDF_MIGRATION_REPORT.md` (diferencias con
Arduino, tamaños) y `ESP_IDF_HARDWARE_REQUIREMENTS.md` (qué probar en la placa).

> **Nada está probado en la placa.** "Hecho" = compilado para `esp32p4` + pruebas de
> host + escena del simulador que falla al revertir la corrección (mutación).

Rama: `flexos-esp-idf` (la rama Arduino `claude/flexos-ota-system-blq9g9-2csacx` no se toca).
Último bloque cerrado: **Personalizar inicio** (ver el commit más reciente de la rama). Entorno de
compilación en un contenedor nuevo: `bash tools/setup_idf_cloud.sh` (ver "Entorno" abajo).

### Punto de reanudación (siguiente tarea exacta)

1. **Sesiones de app** (`docs/spec/01c` §14, ítem S4a): guardar y restaurar el estado de UI. `flex_frame.c` y
   `flex_app_registry.c` ya tienen el marco mínimo; falta el ciclo completo (crear/suspender/reanudar/
   destruir por app, regreso, memoria por app, Recientes↔sesiones). Leer `01c` §14 y
   `FlexOS_Ultra_Session.h` + `FlexOS_Ultra_AppFramework.h` (Arduino); extender `flex_app.h`. Pruebas: host de la política de
   sesiones frente a Arduino + escena de ciclo de vida.
2. Memoria y "Optimizar Flex OS" (`01c` §17, S4b), luego arranque (splash/banda forense, S2), luego S5 y las apps.

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
| S2 | Arranque: splash, banda forense de reinicio anormal, paso Flex Account del OOBE | 9 | 0 | 0 |
| S3a | Personalizar inicio: operaciones del modelo (paginas, principal, rejilla, reset) + host | 6 | 1 | 0 |
| S3b | Personalizar inicio: vistas Paginas y Ajustes de inicio + modales (LVGL) | 11 | 1 | 0 |
| S3c | Personalizar inicio: Fondo, Temas y Widgets (LVGL) | 6 | 1 | 0 |
| S3e | Personalizar inicio: Mis imagenes, imagen propia como fondo y encuadre (necesita el JPEG por hardware, M1) | 2 | 0 | 0 |
| S3d | Personalizar inicio: pellizco de dos dedos (clasificador + consumidor de toque) | 3 | 1 | 0 |
| S4a | Marco de apps: sesiones y ciclo de vida (01c 14), Recientes<->sesiones, regreso, memoria por app | 31 | 0 | 0 |
| S4b | Memoria y funcion Optimizar (01c 17), umbrales y avisos | 8 | 0 | 0 |
| S5 | Infraestructura compartida de apps en LVGL (cabecera, listas, dialogos, selector de archivos, barra de medios) | 12 | 0 | 0 |
| A01 | App Reloj (reloj, alarmas, cronometro, temporizador) | 17 | 0 | 0 |
| A02 | App Galeria + visor + editor de fotos + editor de video | 145 | 0 | 0 |
| A03 | App Multimedia | 16 | 0 | 0 |
| A04 | App Almacenamiento + Archivos/Explorador | 76 | 0 | 0 |
| A05 | App Modo PC (DeX) | 40 | 0 | 0 |
| A06 | App Notas | 8 | 0 | 0 |
| A07 | App Navegador | 95 | 0 | 0 |
| A08 | App Flex Compass (brujula + AHRS) y sensores BNO085 | 40 | 0 | 0 |
| A09 | App Paint | 9 | 0 | 0 |
| A10 | App Juegos (Jumper y demas) | 40 | 0 | 0 |
| A11 | App Ajustes (+ ajustes de teclado y conectividad) | 25 | 0 | 0 |
| A12 | Apps Calculadora y Calendario | 6 | 0 | 0 |
| A13 | App Camara (necesita el modelo del sensor) | 3 | 0 | 0 |
| A14 | App Clima + servicio del tiempo | 57 | 0 | 0 |
| A15 | App Flex Store + paquetes + maquina virtual de apps + permisos | 101 | 0 | 0 |
| A16 | App Flex Phone (enlace con el telefono) | 127 | 0 | 0 |
| A17 | App Device Care (estado, diagnostico, pruebas) | 39 | 0 | 0 |
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

## Resultados de la última validación (Personalizar inicio)

| Comprobación | Resultado |
|---|---|
| `tests/host` (ASan+UBSan y TSan), incl. `test_homecfg` | OK: 983 827 comprobaciones, 0 fallos |
| Mutantes del modelo (pellizco 52→60, principal al borrar, redondeo del reordenar) | 3/3 detectados |
| `tools/run_tests.sh` completo (host, LittleFS, 22 escenas, reglas) | TODO OK |
| Escena `personalizar` | 14 pasos OK; mutantes de la interfaz (nueva página, cierre al bloquear, confirmar borrado, casita) detectados |
| Firmware `tools/build.sh --rev v3` | OK: 1 418 448 B, 54,1 % de la ranura A (2,5 MB), 33,8 % de la B (4 MB) |
| Capturas revisadas a ojo | Páginas, Fondo, Widgets, Ajustes y modal fieles a 01a §10 |
| **Placa real** | NO probado (tacto del carrusel, fluidez de la animación y del pellizco con el GT911) |

## Entorno de compilación (contenedor de la nube)

* El proxy solo permite GitHub y PyPI: `dl.espressif.com` y `components.espressif.com` están
  bloqueados. `bash tools/setup_idf_cloud.sh` instala ESP-IDF v5.5.5 y la cadena RISC-V en `~/esp`
  (las herramientas salen de las releases de GitHub; lvgl y esp_littlefs ya se piden por git).
  Después: `source ~/esp/env.sh && tools/build.sh --rev v3`. La compilación completa tarda ≈4 min.
* Decisión: compilar y probar en cada punto de control; no repetir `build.sh --clean` salvo que cambie
  el `sdkconfig`.

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

Última actualización: Personalizar inicio (Fase 3): ≈11 h de trabajo de este bloque; quedan de realista
≈247 h (S3e depende del JPEG de la Fase 9).
