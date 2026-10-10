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

## En curso / pendiente (orden de trabajo)

1. Revisión 4: sus 10 hallazgos verificados y corregidos (escena `regresiones`, una
   comprobación por hallazgo con su mutación). Dimensiones nunca ejecutadas:
   memoria, fidelidad a Arduino, arranque/pruebas.
2. Fase 3, shell restante: sesiones de app (§14),
   memoria y "Optimizar" (§17), OOBE, edición/personalización del escritorio y menús,
   kiosco, arranque (splash/banda forense).
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
  23/24 agentes; revisión 4: 5/7). Las dimensiones "memoria", "fidelidad" y
  "arranque/pruebas" no se han ejecutado nunca: lanzarlas pequeñas (2–3 agentes).

## Estimación del trabajo restante (actualizar al cerrar cada bloque)

Implementación (sin validación en la placa): optimista ≈190 h, realista ≈280 h,
conservador ≈420 h de trabajo efectivo. Validación en la placa aparte (≈20–40 h
conjuntas, depende de grabar con autorización). Base: tamaño del código Arduino que
queda frente al ritmo medido en las Fases 0–3 (ver el informe de auditoría del 10-10).

Última actualización: restablecimiento de fábrica (Fase 3).
