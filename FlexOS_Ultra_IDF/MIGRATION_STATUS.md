# Flex OS Ultra → ESP-IDF · Estado para reanudar

Documento corto y vivo: qué está hecho DE VERDAD, qué falta y por dónde seguir. El
detalle de cada decisión está en `ESP_IDF_MIGRATION_REPORT.md` (diferencias con
Arduino, tamaños) y `ESP_IDF_HARDWARE_REQUIREMENTS.md` (qué probar en la placa).

> **Nada está probado en la placa.** "Hecho" = compilado para `esp32p4` + pruebas de
> host + escena del simulador que falla al revertir la corrección (mutación).

Rama: `flexos-esp-idf` (la rama Arduino `claude/flexos-ota-system-blq9g9-2csacx` no se toca).

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

## En curso / pendiente (orden de trabajo)

1. Revisiones 4 y 5 cerradas (todas las dimensiones ejecutadas al menos una vez). La
   siguiente revisión, al cerrar el marco de apps y las primeras apps.
2. Fase 3, shell restante: Personalizar inicio (01a §10: páginas, fondo, mis imágenes,
   temas, widgets, ajustes de inicio, pellizco), sesiones de app (§14), memoria y
   "Optimizar" (§17), arranque (splash/banda forense).
3. Fase 3, apps (las 19 abren hoy "Pendiente de migrar"): marco común de apps → Ajustes,
   Almacenamiento/Explorador, Reloj, Calculadora, Calendario, Notas, Paint, Clima,
   Galería (+editores, visor), Multimedia, Música, Cámara, Device Care, Flex Compass,
   Navegador, Flex Store, Flex Phone, Juegos, Modo PC. Faltan las especificaciones 04–07.
4. Fase 4 (bandas del vidrio, 3 niveles, PPA medido) y Fase 5 (veto al teclear, umbrales).
5. Fases 6–8 (Wi-Fi C6 esp-hosted, red, servidor web, Account, Cloud), 9–13 (multimedia,
   cámara, audio, Device Care, Compass), 14–17 (OTA, particiones, seguridad, optimización).

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

Última actualización: Modo edición del escritorio (Fase 3).
