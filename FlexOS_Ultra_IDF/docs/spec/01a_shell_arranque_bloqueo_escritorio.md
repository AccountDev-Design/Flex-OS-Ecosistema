# 01a · Shell del sistema: arranque, OOBE, bloqueo y clave, escritorio, widgets, edición, personalización, caja de aplicaciones, menús contextuales, Recientes, motor táctil y registro de apps

> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical, MIPI-DSI, GT911)
> todo el **shell** de la versión Arduino `FlexOS_Ultra/`. El motor de dibujo propio (framebuffers, bandas, `present()`,
> `homeBuf`, `hpBuf`, `lockBuf`, `blurBg`…) **no se reutiliza**: aquí se describe *qué se ve*, *dónde* (coordenadas exactas
> del código), *qué textos* y *cómo responde*, para rehacerlo con objetos, estilos, eventos y animaciones LVGL sin perder
> ninguna función.

**Fuentes leídas por completo:** `FlexOS_Ultra.ino`, `FlexOS_Ultra_Types.h`, `FlexOS_Ultra_Core.h`, `FlexOS_Ultra_Shell.h`,
`FlexOS_Ultra_Home.h`, `FlexOS_Ultra_HomeCfg.h`, `FlexOS_Ultra_AppDrawer.h`, `FlexOS_Ultra_AppSwitcher.h`,
`FlexOS_Ultra_Lock.h`, `FlexOS_Passcode.h`, `FlexOS_Passcode.cpp`, `FlexOS_Ultra_Widgets.h`, `FlexOS_Ultra_Clock.h`,
`FlexOS_Ultra_Touch.h`, `FlexOS_Ultra_Session.h`, `FlexOS_Ultra_Prefs.h`, `FlexOS_Account_Bridge.h`.
**Leídas en la parte que toca a esta área:** `FlexOS_Ultra_Power.h` (verificación de clave `lsu*`, suspensión al
despertar, filtro de encendido), `FlexOS_Ultra_AppFramework.h` (`APP_REG`, banderas, `getIconRect`, `touchDropAll`,
inmersivo), `FlexOS_Ultra_Icons.h` (enum `IC_*`), `FlexOS_Ultra_HAL.h` (`gtPoll`, `gtPollMulti`),
`FlexOS_Ultra_PkgApps.h` (apps descargadas en Inicio/caja), `FlexOS_Ultra_Wallpaper.h` (catálogo de fondos y temas),
`FlexOS_Ultra_AppWeather.h` (widget de clima de Inicio y del bloqueo), `FlexOS_Ultra_AppChrono.h` (`cronoBarClock`),
`FlexOS_Ultra_Notif.h`, `FlexOS_Ultra_Network.h` (`wifiOobeEnter`), `FlexOS_Ultra_Recovery.h` (lista blanca de Modo
seguro), `FlexOS_Ultra_TheftUI.h` (aviso en el bloqueo), `FlexOS_Ultra_DeX.h` (`dexMatch`), `FlexOS_Ultra_AppSettings.h`
(filas de Seguridad/Bloqueo), `FlexOS_Ultra_NTP.h` (claves del reloj).

Rutas `archivo:línea` relativas a `FlexOS_Ultra/` salvo que se diga otra cosa. Este documento **complementa** a
`02_diseno_visual_liquid_glass.md` (tokens de color, Liquid Glass, tipografía, iconos, framework de apps, transiciones,
barra de navegación y barra de gestos): cuando algo ya está especificado allí se cita la sección en vez de repetirlo.

---

## 0. Convenciones de este documento

* **Coordenadas**: píxeles de la pantalla física vertical 480×800, origen arriba-izquierda, `(x, y, w, h)` tal como están en
  el código. "Centrado" = `x = 240`.
* **Texto**: "tamaño N" es el `size` del motor (1 = mapa de bits 5×7; 2..6 = Outfit escalada). Equivalencias LVGL y el
  desfase vertical (la `y` del código es el **tope de las mayúsculas**, no la caja de línea) en **02 §5.1**.
  `drawTextC` = centrado en x; `drawTextR` = alineado a la derecha en x; `drawTextClip` = corta sin puntos al llegar al
  borde derecho; `uiFontFit(t, maxW, maxSize)` = mayor tamaño ≤ maxSize que cabe; `uiLabelFit` = corta con `"..."` (02 §5.3).
* **Colores**: tokens semánticos `TH_*` (hex oscuro/claro en **02 §2.2**), colores "sobre wallpaper" `TH_ONWALL`
  (`#FFFFFF`), `TH_ONWALL2` (`#D7DEEE`), `TH_WALLSURF` (`#2C365C`), `TH_WALLSURF2` (`#303C6E`), `TH_WALLPANEL`
  (`#24283A`) (02 §2.3). Alias de Ajustes: `PAGE_BG = TH_PAGE`, `SET_CARD_BG = TH_SURF`, `SET_CARD_GLASS = TH_GLASS`,
  `SET_TXT_HI = TH_TXT`, `SET_TXT_LO = TH_TXT2`, `SET_TXT_MUTE = TH_MUTE` (`AppSettings.h:68-73`). `wallAccent()` /
  `wallAccent2()` = acento del tema o paleta del fondo (02 §2.5). Los colores literales se dan en hex `#RRGGBB` a partir de
  los valores `rgb565(r,g,b)` de 8 bits del código; "α" = alfa 0..255.
* **Vidrio** = `drawLiquidGlassPanel(x,y,w,h,rad,tinte)` (Liquid Glass, 02 §4). Casi todas las superficies tienen dos
  variantes según la preferencia `uiGlass` (material): **Vidrio** (panel Liquid Glass con el tinte indicado) o **Plano**
  (relleno sólido o translúcido indicado). Ambas se documentan.
* **gLand** (horizontal): ninguna pantalla de este documento se dibuja en horizontal. Todas fuerzan `gLand = false` al
  entrar (`activarMultitarea` `AppSwitcher.h:472`, `lsuStartVerify` `Power.h:590`, `suspWakeLockScreen` `Power.h:651`,
  `appClose` `Core.h:657`, `kioskExitNow` `Lock.h:473`, `ensureBlurBg` `Theme.h:1060`). En LVGL: estas pantallas son
  siempre verticales; solo las apps `APP_LAND`/inmersivas giran (02 §8.10-8.11).
* **Fondo velado reutilizable `blurBg`** (02 §3.5): wallpaper del Inicio + velo `#080A12` α70 a pantalla completa (no es un
  desenfoque real). Lo usan: verificación de clave, Recientes, Caja de aplicaciones (con velo extra), Personalizar inicio.

### 0.1 Mapa de pantallas de esta área

| Estado / capa | Pantalla | Sección |
|---|---|---|
| `ST_SPLASH` (0) | Banda forense + splash | §2 |
| `ST_OOBE_LANG` (1) | OOBE: idioma | §3.1 |
| `ST_OOBE_NAME` (2) | OOBE: nombre del equipo | §3.2 |
| `ST_OOBE_ACCOUNT` (18) | OOBE: Flex Account (también desde Flex Store y Ajustes) | §3.3 |
| `ST_LOCK` (3) | Pantalla de bloqueo | §4 |
| `ST_LOCKSETUP` (7) | Crear / verificar PIN o contraseña (+ revelado del escritorio) | §5 |
| `ST_KIOSKSET` (10) | Definir área excluida del Modo kiosco | §5.14 |
| `ST_HOME` (4) | Escritorio por páginas + widgets | §6, §7 |
| `ST_HOME` + `editMode` | Modo edición del escritorio | §8 |
| `ST_CTX` (9) | Menú contextual de pulsación larga sobre un icono | §9 |
| `ST_HOMECFG` (17) | Personalizar inicio (páginas, fondo, temas, widgets, ajustes) | §10 |
| `ST_DRAWER` (16) | Caja de aplicaciones (+ menú contextual, ficha, desinstalar) | §11 |
| `ST_SWITCHER` (6) | Recientes (+ ficha y confirmación) | §12 |
| (capa) | Motor táctil, gestos y suspensión de pantalla | §14 |
| (datos) | `APP_REG`: registro de apps | §15 |
| (datos) | Claves NVS de todo lo anterior | §16 |

Fuera de esta área (otros documentos): panel rápido, centro de notificaciones e isla, OTA, Ajustes (salvo las filas de
Seguridad que abren estas pantallas), Wi-Fi, apagado completo, Modo seguro y restablecimiento (aquí solo su papel en el
arranque), protección contra robo (aquí solo su aviso dentro del bloqueo), teclado del sistema.

---

## 1. Máquina de estados global y bucle principal

### 1.1 Enum `gState` (`Shell.h:199-233`)

Los valores numéricos **no se mueven nunca** (los nuevos se añaden al final). En IDF no hace falta conservar los números
(no viajan a NVS), pero sí la lista completa:

`ST_SPLASH=0, ST_OOBE_LANG=1, ST_OOBE_NAME=2, ST_LOCK=3, ST_HOME=4, ST_APP=5, ST_SWITCHER=6, ST_LOCKSETUP=7, ST_WIFI=8,
ST_CTX=9, ST_KIOSKSET=10, ST_POWEROFF_CONFIRM=11, ST_POWEROFF_ANIM=12, ST_KBSET=13, ST_CONN=14, ST_FILES=15, ST_DRAWER=16,
ST_HOMECFG=17, ST_OOBE_ACCOUNT=18, ST_FACTORY=19, ST_SAFE=20, ST_THEFT=21`.

Variables globales de shell (`Shell.h:235-240`): `gState=ST_SPLASH`, `splashStart`, `lockOff=0` (desplazamiento del
bloqueo en px), `lastLockOff=-1`, `oobeSel=0`, `gAppId=0` (app lógica en primer plano), `editMode=false`.

### 1.2 Orden de arbitraje de `loop()` (`FlexOS_Ultra.ino:670-1017`)

El orden **decide quién se queda cada toque**. Reproducirlo en IDF como una cadena de "consumidores" previa a la entrega del
evento a la pantalla LVGL activa:

1. `flexFeedWdt()`, `loopRateTick()` (vueltas/s), **`flexPollTouch()`** (lee GT911 y clasifica; dentro: filtro de kiosco,
   tragado de episodio heredado, detector de doble toque de suspensión, pellizco) — §14.
2. `uiGlassBandGuard()`: la banda pre-desenfocada solo vive mientras su dueño (ST_CTX, tarjeta del cronómetro, menú de
   medios) manda (`.ino:666`).
3. Restablecimiento en curso (`gFrPending` o `ST_FACTORY`): solo reloj + `frTick()`; nadie más dibuja.
4. Ticks sin pantalla: modo seguro estable, autoguardado de sesiones, memoria, avisos de memoria, **fundido de
   suspensión**, **`autoLockTick()`** (lee `T` sin filtrar), toque de la cápsula del cronómetro, banner de Flex Phone,
   **isla de notificaciones** (`notifHandleTouch`), puente táctil OTA, IMU, caídas, robo, Flex Storage, medios, servidor web,
   música, Wi-Fi diferido, NTP, persistencia de hora, `clkUpdate()` → `gMinChanged`.
5. Pantallas exclusivas: OTA (cierra Personalizar inicio guardando, `.ino:798`), "Optimizar Flex OS".
6. Clima y Flex Phone (ticks de datos).
7. **Centro de notificaciones** (borde izquierdo), luego **panel rápido** (borde superior/derecho): si se quedan el gesto,
   la pantalla de debajo no lo ve.
8. Overlays modales: tarjeta del cronómetro, aviso de caída, aprobación de teléfono.
9. **`switch(gState)`**: un tick por estado (§1.1). En `ST_LOCK` el cambio de minuto recompone el bloqueo; en `ST_HOME`
   lo recompone salvo gesto de página/transición/cortina/edición (se aplaza con `gHomeDirty`).
10. `kioskTick()` (gesto de salida), `wgDataTick()` (datos de widgets).
11. Transición de app: si tiene la pantalla, solo ella dibuja (02 §8.7); se aborta si el estado ya no es Inicio/App.
12. Repintado parcial de widgets (`wgRepaint`) si `wgDirty` y el escritorio está quieto.
13. `uiTick()` (jiggle de edición, destello de icono, cortina), `notifTick()`, banner, cápsula del cronómetro, OTA.
14. `delay(loopPaceMs())`.

**Ritmo del bucle** (`loopPaceMs`, `.ino:647`): 1 ms si hay dedo, evento táctil, gesto terminado hace < 400 ms, transición,
cortina, destello o "UI ocupada"; si no, 5 ms. **Cadencia de `uiTick`** (`.ino:584`): 16 ms (≈60 fps) con cortina visible o
con destello de icono en Inicio; 38 ms (≈26 fps) en el resto (jiggle de edición).

### 1.3 Transiciones entre pantallas (resumen)

| Desde | Disparador | Hacia | Ref |
|---|---|---|---|
| Arranque | fin de splash, `gSafeMode` | `ST_SAFE` | `Home.h:305` |
| Arranque | fin de splash, `!cfgOobeDone` | `ST_OOBE_LANG` | `Home.h:306` |
| Arranque | fin de splash, kiosco guardado | `ST_APP` (app clavada) | `Home.h:310` |
| Arranque | fin de splash, resto | `ST_LOCK` | `Home.h:315` |
| `ST_OOBE_LANG` | "Continuar" | `ST_OOBE_NAME` | `Home.h:365` |
| `ST_OOBE_NAME` | "OK" | `ST_OOBE_ACCOUNT` | `Home.h:428-436` |
| `ST_OOBE_ACCOUNT` | "Continuar"/"Omitir por ahora" (OOBE) | `ST_LOCK` | `Account_Bridge.h:236` |
| `ST_OOBE_ACCOUNT` | sin Wi-Fi + botón | `ST_WIFI` (vuelve a la cuenta) | `Network.h:726` |
| `ST_LOCK` | deslizar arriba sin clave | `ST_HOME` | `Home.h:1489` |
| `ST_LOCK` | deslizar arriba con clave | `ST_LOCKSETUP` (verificar) | `Home.h:1480` |
| `ST_LOCKSETUP` | clave correcta (desbloqueo) | revelado → `ST_HOME` (o la app donde estaba al suspender) | `Power.h:149-201` |
| `ST_LOCKSETUP` | cancelar (salió del bloqueo) | `ST_LOCK` | `Power.h:89` |
| `ST_HOME` | toque en icono | `ST_APP` (o verificación si tiene candado) | `HomeCfg.h:1294-1314` |
| `ST_HOME` | pulsación larga 1000 ms en icono | `ST_CTX` | `HomeCfg.h:1258` |
| `ST_HOME` | pulsación larga 650 ms en hueco / pellizco | `ST_HOMECFG` | `HomeCfg.h:1285`, `:1198` |
| `ST_HOME` | deslizar arriba (inicio y > 96) | `ST_DRAWER` | `HomeCfg.h:1290` |
| `ST_HOME` | toque abajo-derecha / gesto mantenido | `ST_SWITCHER` | `HomeCfg.h:1292`, `Core.h:1106` |
| `ST_CTX` | "Modo edición" | `ST_HOME` + `editMode` | `AppDrawer.h:250` |
| `ST_CTX` | "Modo kiosko" | `ST_KIOSKSET` | `AppDrawer.h:254` |
| `ST_CTX` | "Bloquear/Desbloquear app" | `ST_LOCKSETUP` | `AppDrawer.h:249` |
| `ST_APP`/`ST_HOME`/`ST_DRAWER`/`ST_HOMECFG` | inactividad ≥ `gAutoLockMs` | `ST_LOCK` | `Lock.h:343` |
| cualquiera (con clave) | despertar de suspensión | `ST_LOCK` | `Power.h:633` |
| `ST_SWITCHER` | toque fuera / abajo | `ST_HOME` | `AppSwitcher.h:461` |
| `ST_SWITCHER` | toque en tarjeta central | `ST_APP` (reanuda) | `AppSwitcher.h:464` |

---

## 2. Arranque

### 2.1 Secuencia exacta de `setup()` (`FlexOS_Ultra.ino:349-579`)

1. Serie a 115200; `"\n=== FlexOS Ultra (ESP32-P4) arrancando ==="`.
2. **`poffWakeGate()`** (§2.2) — antes de encender el panel.
3. `flexPanelInit()` con un reintento tras 150 ms. Si falla: `"[FATAL] el panel DSI no responde (revisa cableado)"` y
   **parpadeo SOS del backlight** (GPIO 23, 150 ms on / 150 ms off, para siempre).
4. `flxGfxInit()` (framebuffers en PSRAM). Si falla: `"[FATAL] sin PSRAM (activa 'PSRAM: Enabled' en el IDE)"` y espera
   infinita.
5. `flexTouchInit()` (GT911; fallo suave: se sigue sin táctil).
6. `safeBootEval()` (decide Modo seguro, §2.7) y `frLoadState()` (marcador de restablecimiento).
7. `wifiConfigureHostedTransport()` (solo pines del enlace P4-C6; no levanta la radio).
8. Si no hay restablecimiento pendiente ni Modo seguro: `bootInitRadioSafe()`, `flexBrowserBegin()`, `flexOtaBegin()`.
9. Si no hay restablecimiento pendiente: **`flexLockMigrate()`** (§5.10) — antes de `cfgLoad()` para que `"locktype"` ya sea
   el definitivo. Log `"[SEG] clave del bloqueo migrada a hash con sal"` o `"[SEG] no se pudo migrar la clave: se conserva la anterior"`.
10. **`cfgLoad()`** (todas las preferencias de §16).
11. `flexFsBegin()` (LittleFS). Si monta: crea `/System/Sessions` y `/System/Cache`; `flexPkgBegin()` y `mlBegin()` si no
    hay Modo seguro ni restablecimiento.
12. `flexAudioBegin()`, `dcBegin()` (Device Care), `tpBegin()` (robo).
13. **Si `gFrPending`**: aplica brillo y `frResumeAfterBoot()`; **fin** (no hay splash).
14. Una sola vez por aparato: purga de la carpeta segura retirada (NVS `"fxvpurge"` = 1).
15. Si no Modo seguro: Flex Store, Flex Account, Flex Storage, Flex Cloud, modo avión, clima, Flex Phone (solo cargan
    estado y crean tareas; **nada de red en `setup()`**).
16. `setBacklight(gBright)`; caídas y robo si estaban activados.
17. **`homeOrderLoad()`** (escritorio, §6.10) y **`homeCfgLoad()`** (fondo/tema, §10.8).
18. Teclado: tamaño, multitáctil, portapapeles fijado.
19. **Reloj**: `clkBootMs = millis()`, semilla de fábrica **sábado 4 de julio de 2026, 13:23 local** (`Clock.h:57-61`),
    `clkLoadNvs()` (última hora guardada en NVS `flexos_time`, pisa la semilla), `clkUpdate()`.
20. Motivo del reinicio: si es despertar de deep sleep, lee y **borra** `"cleanoff"` (apagado limpio). Reinicio
    **anormal** = cualquiera que no sea `POWERON`, `SW` o `DEEPSLEEP` → **banda forense** (§2.3).
21. Si `gSafeMode` → `safeEnter()`; fin.
22. Pantalla **negra absoluta**, `splashStart = millis()`, `gState = ST_SPLASH`.

En IDF: los pasos de hardware ya existen en `components/flex_display`, `flex_touch`, `flex_storage`; el orden lógico
(migración de clave → carga de preferencias → FS → escritorio → reloj → decisión) debe mantenerse. **Ninguna operación de red
ni escritura de flash larga en el arranque**.

### 2.2 Filtro de encendido desde apagado completo (`poffWakeGate`, `Power.h:1056`)

Solo si el motivo es `ESP_RST_DEEPSLEEP` y `POWEROFF_ON`. Con el panel **todavía apagado**: suelta el *hold* del reset del
GT911, inicia el táctil y durante **`POFF_WAKE_GATE_MS = 4200` ms** sondea cada 10 ms; si hay ≥ 1 dedo sostenido
**`POFF_WAKE_HOLD_MS = 3000` ms** seguidos (un contacto caduca a los 120 ms sin frame) → arranque completo; si no → vuelve a
deep sleep. Sin táctil (`!gtOk`) arranca siempre. Constantes en `Types.h:352-356` (`POFF_WAKE_GPIO = -1`: sin INT cableado,
despertar por temporizador cada `POFF_WAKE_POLL_MS = 400` ms). La pantalla de apagado en sí es de otro documento.

### 2.3 Banda forense de reinicio anormal (`showBootBanner`, `Home.h:262-275`)

* Fondo negro `#000000` a pantalla completa.
* `"FlexOS Ultra"` tamaño 3, centrado, y = 376, `#EBEEF5`.
* `"ultimo reinicio: %s"` tamaño 1, centrado, y = 422, `#F0B95A`, con `%s` = `resetReasonStr()` (`Home.h:246`):
  `"POWERON"`, `"SW"`, `"PANIC (crash)"`, `"INT_WDT"`, `"TASK_WDT"`, `"WDT"`, `"BROWNOUT (voltaje)"`, `"DEEPSLEEP"`, `"OTRO"`.
* `"P4 480x800 - modo offline"` tamaño 1, centrado, y = 442, `#8C96AA`.
* Se mantiene **2200 ms** (bloqueante, `delay`). En IDF: pantalla LVGL estática + `lv_timer` de 2200 ms antes del splash
  (no bloquear la tarea UI).

### 2.4 Splash (`splashFrame`/`splashTick`, `Home.h:278-320`)

* Fondo negro absoluto (también en tema claro: evita fogonazo con el backlight subiendo).
* `"FlexOS Ultra"` tamaño 6, centrado, y = 360 (`SCR_H/2 − 40`), blanco con opacidad `a`.
* `"ESP32-P4"` tamaño 2, centrado, y = 422, `#AAB6C8` con opacidad `a·7/10`.
* **Puntos de carga**: 3 círculos AA r = 4 en (224, 672), (240, 672), (256, 672); el activo blanco, los otros `#464A52`;
  activo = `(millis()/320) % 3` (cambia cada **320 ms**, en bucle).
* Curva de opacidad `a` (lineal) con `e = millis() − splashStart`: **0-600 ms** sube 0→255; **600-2000 ms** 255;
  **2000-2600 ms** baja 255→0; a los **2600 ms** termina.
* Un cuadro cada ~16 ms (`delay(16)`, bloqueante).
* LVGL: pantalla con dos `lv_label` + 3 `lv_obj` círculo; `lv_anim` de `opa` (in 600 ms lineal, espera 1400, out 600) y
  `lv_timer` de 320 ms para los puntos; al terminar, `ready_cb` decide destino (§2.5).

### 2.5 Destino al terminar el splash (`Home.h:302-316`)

1. `gSafeMode` → `safeEnter()` (pantalla de Modo seguro; ni bloqueo ni escritorio).
2. `!cfgOobeDone` → `enterOobeLang()` (§3.1).
3. `KIOSK_ON && kioskOn && kioskApp >= 0` → compone el escritorio fuera de pantalla, **abre directamente la app clavada**
   (`enterApp(kioskApp)`) y estampa el candado de kiosco (§5.14). La única salida es el gesto del candado + clave.
4. Resto → compone escritorio y bloqueo, muestra el bloqueo, `gState = ST_LOCK`, `lockOff = 0`.

### 2.6 Reloj del sistema (`Clock.h`)

* Fuente única de verdad: época UTC en segundos anclada a `millis()` (`clkEpochRef`, `clkRefMs`); re-ancla cada hora
  (`3 600 000` ms) para no acercarse al desbordamiento. Época mínima válida `1767225600` (1 ene 2026 UTC).
* Zona fija **America/Lima, UTC−5** sin horario de verano (`FLEXOS_TZ_OFFSET_SEC = −18000`).
* `clkUpdate()` devuelve `true` al cambiar de minuto y recalcula `rtcY, rtcMo, rtcD, rtcWd (0 = domingo), rtcH, rtcMin` con
  el algoritmo civil de Howard Hinnant (`clkDaysFromCivil`/`clkCivilFromDays`, lógica pura).
* Formatos: `clkStr12` → `"H:MM"` (12 h sin AM/PM o 24 h según `g24h`; lo usa el **reloj grande** del bloqueo, que solo
  sabe dígitos y `:`); `clkStrBar` → `"13:23"` (24 h) o `"1:23 PM"` (12 h) para barras de estado y widget.
* Fechas localizadas (`Home.h:441-454`): **larga** ES/PT `"%s, %d de %s"` (`"Sábado, 4 de julio"`), FR/IT `"%s %d %s"`,
  EN/ZH `"%s, %s %d"`; **corta** `"%s, %d %s"` con abreviaturas (`"sáb, 4 jul"`). Tablas `WD_FULL`, `WD_SHORT`, `MO_FULL`,
  `MO_SHORT` en `Session.h:369-396` (5 idiomas; chino usa inglés).
* Persistencia de la hora: NVS `flexos_time` (`"epoch"`, `"lastsync"`, `uint64`), guardado como mucho **1 vez/hora**
  (`NTP.h:78-80`, `:255`). NTP es de otro documento.

### 2.7 Modo seguro y restablecimiento en el arranque (`Session.h:115-298`)

* **Modo seguro**: NVS `flexsafe` → `"fails"` (int, reinicios anormales consecutivos), `"cause"` (int, `esp_reset_reason`).
  Anormal = `PANIC`, `INT_WDT`, `TASK_WDT`, `WDT`, `BROWNOUT`. Con **≥ 3** (`SAFE_FAIL_MAX`) seguidos → Modo seguro. Tras
  **60 s** (`SAFE_STABLE_MS`) sin reiniciar el contador vuelve a 0 (una escritura). Textos de causa (`safeCauseText`):
  `"Fallo del sistema (crash)"`, `"Watchdog de tarea (TASK_WDT)"`, `"Watchdog de interrupcion (INT_WDT)"`,
  `"Watchdog del chip"`, `"Caida de tension (brownout)"`, `"Reinicio inesperado"`.
  En Modo seguro solo se abren **Ajustes, Almacenamiento, Reloj y Calculadora** (`Recovery.h:549`); otra app muestra una
  tarjeta `(28, 308, 424×60)` r16 `#181A24` α240 con `"No disponible en Modo seguro"` (tamaño 2, `#F0F4FC`, y = 318) y el
  nombre de la app (tamaño 1, `#AAB2C4`, y = 342) en la banda 300..375. El escritorio muestra la **píldora "Modo seguro"**
  (§6.3) y tocarla (136..344 × 48..104) abre `safeEnter()`. Sin destello de iconos ni animación de Recientes en Modo seguro.
* **Restablecimiento**: NVS `flexreset` → `"pending"` (bool), `"ver"` (int = 1), `"stage"` (int, etapas
  `FR_ST_IDLE..FR_ST_FAIL`), `"err"`. Si al arrancar `pending && ver==1 && stage==DONE` → `gFrConfirmPending`: el marcador
  se borra **al entrar en el OOBE de idioma** (`enterOobeLang`, `Home.h:352`), que es la prueba de arranque limpio.
  Pantallas y motor: otro documento.

### 2.8 Notas de migración del arranque

* **Lógica pura reutilizable tal cual**: `safeBootEval`/`safeStableTick` (contador con tope 250), `frLoadState`
  (validación de versión de formato), `clkDaysFromCivil`/`clkCivilFromDays`/`clkUpdate`, `clkStr12`/`clkStrBar`,
  `buildLongDate`/`buildShortDate`. Pruebas host sugeridas: fechas límite (29-feb, cambio de año, 1970), formatos 12/24 h
  ("12:00 PM", "12:05 AM"), contador de Modo seguro con secuencias de motivos.
* **Depende de Arduino**: `Preferences` → `flex_kv` (`components/flex_storage/include/flex_kv.h`); `delay()` → nada
  bloqueante en la tarea LVGL (usar `lv_timer`/`lv_anim`); `esp_reset_reason()` sigue igual en IDF.
* **Rarezas**: banda forense y splash bloquean la tarea (2200 ms y 16 ms/cuadro); los textos `"ultimo reinicio"` y
  `"P4 480x800 - modo offline"` no llevan tildes ni reflejan el estado de red real.

---

## 3. OOBE (primera configuración)

Se entra si NVS `"oobe"` es `false` (placa virgen o tras restablecer). Orden: **Idioma → Nombre → Flex Account → Bloqueo**.
Las tres pantallas se pintan **sobre el wallpaper** (`drawWallpaper(fb, false)`, sin blobs) y en colores claros fijos
(todavía no hay preferencia de apariencia).

### 3.1 Idioma (`ST_OOBE_LANG`, `Home.h:323-366`)

**Entrada**: `enterOobeLang()` pone `cfgLang = 0`, `oobeSel = 0` y, si había restablecimiento confirmado, borra su marcador.

**Layout**:
* Título `t(S_SELLANG)` tamaño 3, centrado, y = 78, blanco.
* **6 filas** (`NLANG = 6`): x = 44, w = 392, h = 74, separación 14 → y = 158, 246, 334, 422, 510, 598. Rectángulo redondeado
  r18 blanco con α **235** si seleccionada / α **55** si no. Texto tamaño 3 en (72, y+27): `#1C1C26` seleccionada / blanco.
  Etiquetas (`LANG_ENDONYM`, `Session.h:302`): `"Español"`, `"English"`, `"Français"`, `"Português"`, `"Italiano"`, y la
  6.ª se muestra como **`"Chinese"`** (no hay glifos CJK; el endónimo `"中文"` no se pinta). La seleccionada lleva un
  **check** verde `#28A05A` de 2 px: segmentos (380, cy)→(386, cy+8)→(400, cy−10) con cy = y+37.
* **Botón "Continuar"**: (44, 704, 392×60) r30 blanco; texto `t(S_CONTINUE)` tamaño 3 `#2850C8` centrado y = 725.

**Interacción** (solo `T.tap`): fila → `oobeSel = cfgLang = i` y repinta (el idioma cambia **en vivo**: el título y el botón
se traducen al instante). Botón → `enterOobeName()`.

**Textos** (clave → ES/EN/FR/PT/IT; chino usa EN): `S_SELLANG` = "Selecciona tu idioma" / "Select your language" /
"Choisis ta langue" / "Selecione o idioma" / "Seleziona la lingua"; `S_CONTINUE` = "Continuar" / "Continue" / "Continuer" /
"Continuar" / "Continua".

**LVGL**: `lv_list` o columna flex de 6 `lv_button` con estado `LV_STATE_CHECKED` (estilo α235 + texto oscuro + icono
check); botón primario abajo. Grupo de radio (un solo marcado).

### 3.2 Nombre del equipo (`ST_OOBE_NAME`, `Home.h:369-438`)

**Entrada**: `enterOobeName()` vacía `cfgName`.

**Layout**:
* Título `t(S_YOURNAME)` tamaño 3, centrado, y = 70, blanco.
* **Campo**: (44, 150, 392×64) r16 blanco. Vacío: pista `t(S_NAMEHINT)` tamaño 2 `#96969E` en (64, 174). Con texto: nombre
  tamaño 3 `#18181E` en (64, 172) y **cursor** fijo (no parpadea) de 3×32 `#3778F0` en (fin_texto + 2, 166).
* **Teclado QWERTY propio** (no es el teclado del sistema), teclas `#FAFAFC` r8, leyenda tamaño 2 `#1C1C26`:
  * Fila 1 `QWERTYUIOP`: 10 teclas 40×52, paso 46, x0 = 13, y = 544.
  * Fila 2 `ASDFGHJKL`: 9 teclas, x0 = 36, y = 604.
  * Fila 3 `ZXCVBNM`: 7 teclas, x0 = 36, y = 664; **retroceso** `"<-"` en (358, 664, 86×52).
  * Fila 4 (y = 724): **espacio** (48, 724, 298×52) con una barrita 60×4 r2 `#5A5A64` centrada; **`"OK"`** (352, 724, 120×52).

**Interacción** (solo `T.tap`, zona = rectángulo de la tecla inclusive):
* Letra (solo **mayúsculas A-Z**): añade si longitud < **20**.
* `"<-"`: borra el último carácter.
* Espacio: solo si 0 < longitud < 20 (no se puede empezar por espacio).
* `"OK"`: si vacío → `"FlexOS Ultra"`. **Escribe en NVS** `"lang"` (int) y `"name"` (string) y pasa a Flex Account
  (`accountOobeEnter`). La marca `"oobe"` todavía **no** se escribe.

**Textos**: `S_YOURNAME` = "¿Cómo se llama el equipo?" / "Name your device" / "Nomme ton appareil" / "Nomeie o dispositivo" /
"Nomina il dispositivo"; `S_NAMEHINT` = "Toca para escribir" / "Tap to type" / "Touche pour écrire" / "Toque para escrever" /
"Tocca per scrivere".

**Uso del nombre**: `cfgName` (búfer de 24 bytes) se envía como nombre del dispositivo al pedir el código de Flex Account
(`flexAccountRequestCode(cfgName)`).

**LVGL**: `lv_textarea` (una línea, `max_length = 20`, cursor visible) + `lv_buttonmatrix` con el mapa de 4 filas (o
`lv_keyboard` con mapa propio en mayúsculas). Mantener el comportamiento "vacío → FlexOS Ultra".

### 3.3 Flex Account (`ST_OOBE_ACCOUNT`, `FlexOS_Account_Bridge.h`)

**Propósito**: vincular (o no) la identidad `usuario@flex` mediante **código de dispositivo**. Tres vías de entrada con
destino de salida explícito `AccountReturn`: `ACC_RET_OOBE` (primer arranque), `ACC_RET_STORE` (botón Cuenta de Flex Store),
`ACC_RET_SETTINGS` (Ajustes → General → Flex Account). Volver del configurador de Wi-Fi **conserva** el destino
(`accountResumeEnter`, `:228`). Al entrar con cuenta guardada y Wi-Fi se pide validarla en segundo plano
(`flexAccountRequestValidation`, no bloquea).

**Layout común** (fondo = wallpaper):
* Halo de marca: círculo AA (240, 112) r54 `#6648E6`; círculo (221, 102) r12 `#53E2DC`; círculo (257, 119) r19 blanco.
* `"Flex Account"` tamaño 4, centrado, y = 187, blanco.
* Flecha "atrás" (solo si **no** es primer arranque): trazos 3 px blancos (38,31)→(24,43)→(38,55); zona x < 64, y < 78.
* Tarjeta (28, 238, 424×386) r30 `#12162A` α218.
* Pie: `"Omitir por ahora"` (primer arranque) / `"Volver sin cambios"` tamaño 2 blanco y = 704; nota
  `"@flex es una identidad publica, no un buzon de correo."` tamaño 1 `#D2D6E5` y = 750. **Zona táctil del pie: y ≥ 670**.
* Botones (`accountButton`): cápsula r = h/2; primario `#6F52EE` α245; secundario blanco α62; texto tamaño 2 blanco.

**Estados** (instantánea `FlexAccountSnapshot`, re-pintado solo si cambian estado, progreso, conectividad, vínculo, etapa o
error; sondeo cada **80 ms**):

| Estado | Contenido (dentro de la tarjeta) | Zonas táctiles |
|---|---|---|
| Sin vincular / error / expirado | Título tamaño 2 y=278: `"No se pudo vincular"` (ERROR) / `"El codigo expiro"` (EXPIRED) / `"No se pudo leer la cuenta"` (link ERROR) / `"Una cuenta para todo FlexOS"`. Subtítulo tamaño 1 y=318: el error (`#FF9AA6`) o `"Publica apps, comenta, recibe soporte"` + y=340 `"y usa tu identidad usuario@flex."` (`#CDD2E4`). Sin Wi-Fi: banda (52,378,376×48) r18 `#FFB53D` α52 con `"Necesitas conectar Wi-Fi primero"` tamaño 1 `#FFDFA1` y=394. Primario (52,452,376×58): `"Conectar Wi-Fi"` (sin red) / `"Reintentar"` (error o expirado) / `"Iniciar sesion"`. Secundario (52,526,376×54): `"Crear una cuenta"` / `"Configurar red"` (sin red). | 444..518 y 520..588: sin Wi-Fi → `wifiOobeEnter()`; con Wi-Fi → `flexAccountRequestCode(cfgName)` (**los dos botones hacen lo mismo**) |
| `REQUESTING` | `"Creando enlace seguro"` tamaño 3 y=300; barra (60,368,360×16) r8 pista `#343A52`, relleno `#5CDCD7` (mín. 8 px) al `progress` %; etapa tamaño 1 `#CDD2E4` y=412; `"FlexOS no guarda tu contrasena."` tamaño 1 `#AEB5CD` y=462; `"Cancelar"` secundario (110,540,260×54) | 520..622 → `flexAccountCancel()` |
| `CODE_READY` | `"Abre en tu celular"` tamaño 2 `#CDD2E4` y=274; URL en dos líneas tamaño 1 `#73E7E2`: `"flex-developer-studio"` y=308 y `".ralvarezsantos980.chatgpt.site/activate"` y=330; `"y escribe este codigo"` tamaño 1 `#AEB5CD` y=375; caja (90,405,300×86) r24 blanca α245 con el **código** tamaño 5 `#2F2268` y=430; nota y=514: error (ámbar `#FFB53D`) o `"El codigo vence en 10 minutos"`; `"Cancelar"` (110,552,260×54) | 520..622 → cancelar |
| `LINKED` | Círculo (240,300) r34 verde `#2CBE85` con **check** blanco 4 px, o del color del vínculo con **"!"** si hay que re-vincular; título tamaño 3 y=350: `"Cuenta vinculada"` / `"Sesion caducada"` (token caducado) / `"Vuelve a iniciar sesion"`; dirección `@flex` tamaño 2 `#73E7E2` y=392; nombre visible tamaño 1 `#CDD2E4` y=422; **píldora de estado** centrada (y=446, h=28, r14, color del vínculo α60, punto r4, texto `flexAccountLinkLabel(link)` tamaño 1); error `#FF9AA6` o detalle `#AEB5CD` y=486; `"Tu correo de recuperacion nunca se muestra."` `#969EB8` y=512; primario (58,548,364×58): re-vincular → `"Volver a vincular"`/`"Conectar Wi-Fi"`; si no → `"Continuar"` (OOBE) / `"Volver a Ajustes"` / `"Volver a Flex Store"`; enlace (120,632,240×34) r17 `#12162A` α215 `"Desvincular cuenta"` tamaño 1 `#FF9AA6` | 628..668 con \|x−240\| ≤ 124 → confirmación; 530..622 → re-vincular (Wi-Fi o código) o terminar |

Colores del vínculo (`accountLinkColor`): vinculada `#2CBE85`; vinculada sin conexión `#7892C4`; servicio no disponible
`#E8AA42`; requiere autenticación / token caducado `#F0805C`; error `#EC606E`; otro `#969CB0`.

**Confirmación de desvincular** (modal; cualquier toque es suyo): velo negro α150; tarjeta (32,236,416×318) r26 `#181C34`
α252; `"¿Desvincular esta cuenta?"` tamaño 2 blanco y=260; tamaño 1 `#CDD2E4`: `"Se borra la credencial guardada en este P4."`
(y=306), `"Flex Cloud dejara de funcionar hasta que"` (y=328), `"vuelvas a vincular una cuenta."` (y=348);
`"Tus archivos en la nube NO se borran."` `#73E7E2` (y=376); `#AEB5CD`: `"El aparato sigue en tu lista de la web"` (y=406),
`"hasta que lo quites alli."` (y=426). Botones 178×52 en y=482: `"Cancelar"` (52) blanco α62, `"Desvincular"` (250)
`#DC3C54` α250. Desvincular → borra la credencial local, avisa a Flex Cloud y notifica `sysNotify("Flex Account",
"Cuenta desvinculada de este P4")`; cualquier otro toque cancela.

**Salida** (`accountFinish`, `:228`): cancela un enlace en curso; OOBE → **`cfgSaveOobe()`** (NVS `"oobe"=true`, `"lang"`,
`"name"`) y va al **bloqueo** (`ST_LOCK`, aunque no haya clave); Ajustes → vuelve a Ajustes; Store → `storeEnter()`.

**Wi-Fi desde la cuenta**: `wifiOobeEnter()` (`Network.h:726`) abre `ST_WIFI` con `wifiReturnState = ST_OOBE_ACCOUNT`; al
salir vuelve con `accountResumeEnter()` conservando el destino.

**Servicios**: `flexAccount*` (tarea propia de red con su freno; ninguna llamada de esta pantalla bloquea), `flexCloud*`.

**LVGL**: una pantalla con tarjeta (`lv_obj` r30) cuyo contenido se reconstruye por estado (cuatro sub-contenedores
ocultables); suscribirse al evento de cambio de estado de Flex Account del bus de eventos en vez de sondear cada 80 ms.

### 3.4 Casos límite y notas del OOBE

* Si se reinicia en la pantalla de Nombre o de Cuenta, el OOBE **vuelve a empezar por el idioma** y `enterOobeLang` pone
  `cfgLang = 0` aunque `"lang"` ya estuviera guardado (rareza: conviene preseleccionar el idioma guardado).
* El fin del OOBE lleva al **bloqueo** (deslizar para entrar), no al escritorio.
* **Rarezas a no copiar**: `accountOobeTick` consulta `WiFi.status()` cada 80 ms (en el P4 esa llamada puede despertar
  esp-hosted; usar el estado publicado por `flex_wifi`); la URL de activación está **escrita en el código** (hacerla
  configurable); los dos botones de la pantalla sin vincular hacen lo mismo; los textos de esta pantalla no llevan tildes
  (`"contrasena"`, `"codigo"`, `"Sesion"`, `"publica"`, `"buzon"`, `"alli"`, `"dejara"`) — decisión de producto si se
  corrigen; el teclado del nombre no admite minúsculas, dígitos ni `Ñ`.
* **Lógica pura**: reglas de edición del nombre (máx. 20, sin espacio inicial, vacío → `"FlexOS Ultra"`); selección de
  textos por estado (`accountPrimaryLabel`, `accountCodeNote`, `accountNeedsRelink`). Prueba host existente:
  `testFlexAccount` (`tests/host/ino_compile.cpp`).

---

## 4. Pantalla de bloqueo (`ST_LOCK`)

### 4.1 Cómo se llega

* Fin del splash (sin OOBE pendiente ni kiosco) — §2.5.
* Fin del OOBE (`accountFinish`).
* **Bloqueo por inactividad** (`autoLockNow`, §5.11) — el bloqueo **baja** animado.
* **Despertar de una suspensión** con clave configurada (`suspWakeLockScreen`, §5.12) — se compone a oscuras.
* Cancelar la verificación de clave que salió del bloqueo (`lsuExit`, §5.9).

### 4.2 Layout (`renderLock`, `Home.h:502-545`)

Fondo: **wallpaper del bloqueo** `gWallLock` (independiente del de Inicio, §10.3). Todo el *chrome* usa `TH_ONWALL`.

| Elemento | Posición / tamaño | Estilo | Condición |
|---|---|---|---|
| Icono Wi-Fi | `drawWifi(414, 40, 12)` | `TH_ONWALL` | siempre (sin hora en la barra) |
| Batería | `drawBattery(434, 31, 30×15, 82 %)` | `TH_ONWALL` (el 82 % es un valor fijo) | siempre |
| Aviso de robo | (24, 84, 432×104) r24 superficie sobre wallpaper + borde `TH_DANGER` | ver documento de robo (`TheftUI.h:1112`) | protección disparada |
| Panel de vidrio tras el reloj | (28, 198, 424×252) r28 tinte `TH_GLASS2` | solo con material **Vidrio** | `LW_CLOCK` |
| Reloj grande | `drawBigClock(clkStr12, cx=240, y=242, capH=140, thick=18)` (02 §5.4) | `TH_ONWALL` | `LW_CLOCK` |
| Fecha larga | tamaño 3, centrada, y = 418 (`242+140+36`) | `TH_ONWALL` | `LW_CLOCK` |
| Tarjetas de widgets | x = 28, w = 424, h = 50, r16; primera en y = **462** (con reloj) o **200** (sin reloj); paso **60** | ver §4.3 | según `gLockWidgets` |
| Asa de deslizar | (170, 650, 140×10) r5 | `TH_ONWALL` sólido | siempre |
| Texto de ayuda | `t(S_SWIPE)` tamaño 2, centrado, y = 682 | `TH_ONWALL` | siempre |

Orden de pila de tarjetas: Clima → Calendario → Notificaciones (las activas, sin huecos). Máximo con reloj: y = 462, 522, 582.

### 4.3 Widgets del bloqueo (`gLockWidgets`, NVS `"lockwidgets"`, defecto `LW_CLOCK` = 1)

Bits: `LW_CLOCK 0x01` (reloj + fecha), `LW_WEATHER 0x02`, `LW_CAL 0x04`, `LW_NOTIF 0x08` (`Prefs.h:138-142`). Se cambian en
Ajustes → Personalización → sección "Bloqueo": filas `"Reloj grande"`, `"Clima"`, `"Calendario"`, `"Notificaciones"` con
valor `"Activado"`/`"Desactivado"` (`AppSettings.h:323-327`, `:654-657`).

* **Tarjeta genérica** (`lockWidgetCard`, `Home.h:490`): Vidrio tinte `TH_GLASS2` / Plano `TH_SURF` α215; glifo de 18 px en
  (58, y+25) del color de acento; título tamaño 2 `TH_TXT` en (82, y+9); valor tamaño 1 `TH_TXT2` en (82, y+30).
  Glifos (`lockGlyph`): 0 nube, 1 calendario (cuadro con banda y dos puntos), 2 campana.
  * **Calendario**: título `appName(IC_CALEND)` ("Calendario"), valor `t(S_NOEVENTS)` = "(Sin eventos)" (**maqueta**: no hay
    eventos reales), acento `#EB6E5A`.
  * **Notificaciones**: título `t(S_NOTIFS)` = "Notificaciones", valor = título del último aviso de la isla
    (`gNotifs[gNotifCount−1].mod.name`) o `t(S_NONOTIFS)` = "(Sin notificaciones)", acento `#E6B45A`.
* **Clima** (`wxLockCard`, `AppWeather.h:1365`): Vidrio tinte `#28325A` / Plano `TH_ONWALL` α45, r16. Sin datos: icono nublado
  r12 en (58, y+25), `"Clima"` tamaño 2 en (86, y+9), `"Sin datos meteorológicos"` tamaño 1 `TH_ONWALL2` en (86, y+30). Con
  datos: icono del tiempo (día/noche) r12; temperatura tamaño 3 en (84, y+12) (número + símbolo de grado); a su derecha (+8)
  nombre de la ubicación tamaño 2 (recortado para caber) en y+8 y condición tamaño 1 `TH_ONWALL2` en y+30. **Cero red** al
  componer: lee el `WeatherState` publicado.

### 4.4 Interacción (`lockTick`, `Home.h:1477-1500`)

**Con clave configurada** (`gLockType > 0`): el escritorio **nunca** se revela.
* Mientras el dedo está apoyado y `startY − y > 60` px → `lockStartVerify()` (§5).
* Al soltar con `swipeUp` (> 55 px vertical, §14.3) → `lockStartVerify()`.
* Si el bloqueo vino de una suspensión con una app abierta, la verificación se pide con destino "abrir esa app"
  (`LSU_AFTER_OPENAPP`) para volver exactamente donde estaba (`Home.h:1464-1476`); `gLockVerifyLocked = true` marca que
  la verificación salió del bloqueo.

**Sin clave** (desbloqueo físico):
* Arrastre: `off = clamp(startY − y, 0, 800)`; cada cambio compone el cuadro: el bloqueo desplazado **hacia arriba** `off`
  px y, en las últimas `off` filas, el **escritorio quieto** debajo (`composeUnlockFrom`, `Home.h:1415`). No hay velo ni
  escala: es un "telón" que sube.
* Soltar con `off > 266` (`SCR_H/3`) **o** `swipeUp` → `tpLockCleared()` (levanta la protección contra robo) → animación
  hasta 800 → `enterHome()`.
* Soltar por debajo → animación de vuelta a 0 y repinta el bloqueo.
* Animación `animateTo` (`Home.h:1442`): **200 ms**, ease-out cuadrática `1 − (1−p)²`, por tiempo (bloqueante en el
  original: `for(;;)` sin ceder; en IDF debe ser un `lv_anim` no bloqueante).

**Refresco**: al cambiar el minuto se recompone; solo se vuelca si `lockOff == 0` (no durante el arrastre).

### 4.5 LVGL

Pantalla `lock` con imagen de fondo (wallpaper del bloqueo), contenedor de vidrio, etiqueta del reloj con fuente dedicada de
dígitos (02 §5.4), etiqueta de fecha, columna de tarjetas y asa. Desbloqueo físico: el contenedor completo de la pantalla
de bloqueo se mueve con `lv_obj_set_y(−off)` en `LV_EVENT_PRESSING` sobre una capa superior, con la pantalla de Inicio ya
cargada debajo (cargar Inicio con `lv_screen_load` y poner el bloqueo en `lv_layer_top`, o usar dos capas); al soltar,
`lv_anim` de 200 ms con `lv_anim_path_ease_out`. Con clave: en `LV_EVENT_PRESSING` medir el recorrido y lanzar la
verificación al pasar 60 px (una vez).

### 4.6 Referencias y notas

`renderLock` `Home.h:502`; `lockTick` `Home.h:1477`; `lockStartVerify` `Home.h:1464`; `composeUnlockFrom` `Home.h:1415`;
`animateTo` `Home.h:1442`; `drawHomeIndicator` `Home.h:459`; tarjetas `Home.h:472-500`; clima `AppWeather.h:1365`.
Prueba host: `testDesbloqueoFluido` (`ino_compile.cpp:5716`).
Rarezas: batería fija 82 %; el calendario del bloqueo es una maqueta; el reloj grande no muestra AM/PM en formato 12 h (es
intencionado); la verificación arranca con 60 px de recorrido aunque el gesto empiece en cualquier punto.

---

## 5. Clave del sistema: crear, verificar, bloqueo reforzado, candados y kiosco (`ST_LOCKSETUP`)

### 5.1 Una sola ruta de verificación para todo (`lsuAfter`, `Prefs.h:128-137`)

`lsuStartVerify()` es la **única** pantalla de verificación; `lsuStartVerifyFor(what, id)` la reutiliza con un destino:

| `lsuAfter` | Valor | Quién la pide | Al acertar (`lsuFinishAfter`, `Power.h:36`) | Al cancelar (`lsuExit`) |
|---|---|---|---|---|
| `LSU_AFTER_UNLOCK` | 0 | Bloqueo | revelado del escritorio (§5.8) | vuelve al bloqueo |
| `LSU_AFTER_OPENAPP` | 1 | toque en app con candado (Inicio/caja), despertar con app abierta, pantalla completa con candado | `ST_HOME` + abre la app | vuelve al bloqueo si salió de él; si no, al Inicio |
| `LSU_AFTER_LOCKAPP` | 2 | menú contextual "Bloquear app" | `appLockSet(id, true)` y escritorio | Inicio |
| `LSU_AFTER_UNLOCKAPP` | 3 | menú contextual "Desbloquear app" | `appLockSet(id, false)` y escritorio | Inicio |
| `LSU_AFTER_KIOSKOUT` | 4 | gesto de salida del kiosco | `kioskExitNow()` | **vuelve a la app clavada** (nunca al Inicio) |
| `LSU_AFTER_POWEROFF` | 5 | apagado seguro (`"poffpin"`) | animación de apagado | pantalla de confirmación de apagado |
| `LSU_AFTER_FACTORY` | 6 | restablecimiento | último paso del asistente | Ajustes, sin tocar datos |
| `LSU_AFTER_MEDIA` | 7 | medios protegidos (Galería, Multimedia, Música) | `mediaAfterVerify(true)` | `mediaAfterVerify(false)` |

`lsuStartVerifyFor` no hace nada si `gLockType == 0`.

### 5.2 Crear la clave: entrada y selector (`lsuEnter`, `Power.h:415`; `lsuRenderSel`, `Power.h:283`)

**Entrada**: Ajustes → Seguridad → fila `"Bloqueo"` (valor `"PIN configurado"` / `"Contraseña configurada"` /
`"Deslizar"`, `AppSettings.h:356`). Vetado en kiosco. **No pide la clave actual** (ver §5.15).

**Selector** (fondo sólido `PAGE_BG`; colores de la paleta activa):
* Flecha atrás: dos trazos AA 2.4 px (30,26)→(18,18)→(30,10) color `lsuTxtHi()`; **zona x < 48, y < 48** (igual en todas
  las pantallas de clave).
* `"Bloqueo de pantalla"` tamaño 3 centrado y=74 (`SET_TXT_HI`); `"Elige un metodo"` tamaño 2 y=118 (`SET_TXT_LO`).
* Dos botones (40, 220, 400×120) y (40, 370, 400×120) r22: Plano `TH_PRIM`; Vidrio panel con tinte `mix(TH_PRIM, TH_SURF, 60)`.
  Texto tamaño 4 `TH_ONACC`: `"PIN"` y `"Contraseña"` (centrados, y = top+42).
* Toque: PIN → pantalla PIN (crear); Contraseña → pantalla de contraseña con teclado deslizándose.

### 5.3 Pantalla PIN (`Power.h:300-355`, `:460-509`)

**Colores según modo** (`Lock.h:64-72`): al **crear** se usa la paleta (`PAGE_BG`, `SET_CARD_BG`, `SET_CARD_GLASS`,
`SET_TXT_HI/LO`); al **verificar** el fondo es `blurBg` y se usan colores sobre wallpaper (`TH_WALLSURF`, `TH_WALLSURF2`,
`TH_ONWALL`, `TH_ONWALL2`).

**Layout**:
* Título tamaño 3 `"Introduce el PIN"` (verificar) o tamaño **4** `"Crear PIN"`, centrado y = 60.
* **8 posiciones de punto**: centros x = 142 + 28·i (i = 0..7), y = 150, r = 8; llenos `TH_PRIM` (o `TH_ERR` durante
  **500 ms** tras un fallo), vacíos con contorno `lsuTxtLo()`.
* **Teclado 3×4**: teclas 132×82, separación 12, x0 = 30, y0 = 300 → columnas x = 30, 174, 318; filas y = 300, 394, 488,
  582. Vidrio (r16, tinte `lsuGlassCol`) o Plano (`lsuCardCol`). Leyendas tamaño 3: `"1"…"9"`, `"<"` (borrar, `TH_WARN`),
  `"0"`, `"OK"` (`TH_OK`); resto `lsuTxtHi()`.
* Destello de pulsación: sobre la tecla, `lsuTxtHi()` con α `(1−p)·90` durante **200 ms**.
* Animación de puntos/destello a ~33 fps (cada 30 ms) en la banda 120..700.

**Interacción** (toques):
* Dígito: añade si hay < **8**. **Autoconfirmación** al verificar: cuando el número de dígitos iguala la longitud guardada
  (`"locklen"`), se pinta el último punto **y luego** arranca la verificación (§5.6).
* `"<"`: borra uno. `"OK"`: verificar → arranca la verificación; crear → guarda si hay **≥ 4** dígitos (`lsuSavePin`).
* Mientras verifica, el teclado no acepta toques (sí sigue animando).

### 5.4 Pantalla contraseña (`Power.h:357-413`, `:511-577`)

* Título tamaño 3 `"Introduce contraseña"` / `"Crear contraseña"` centrado y = 50.
* Puntos: uno por **carácter UTF-8**, r7 `TH_PRIM` en x = 30 + 24·i, y = 120, máx. 18 visibles.
* **Teclado del sistema** (módulo de teclado, otro documento) sin barra superior ni sugerencias, distribución ES por
  defecto; panel Vidrio (tinte `lsuKbGlass`) o Plano (`lsuKbBgCol`) desde `KB_Y − 4` hasta abajo; fila de función:
  `"shift"`, etiqueta de capa (letras → `LAYOUT_NUM` → `LAYOUT_EMOJI` → letras), `"ES"`/`"EN"`, `"espacio"`, `"<-"`
  (borra un carácter UTF-8 completo), `"OK"`.
* El teclado **entra deslizándose desde abajo en 300 ms** (lineal).
* Escritura rápida multitáctil activa si `"kbfast"` (las teclas de función solo con toque deliberado).
* `"OK"`: verificar → arranca verificación; crear → guarda si `strlen ≥ 4` **bytes** (`lsuSavePass`). Máx. 63 bytes.

### 5.5 Transición de seguridad (`authFade*`, `Lock.h:99-187`)

Al pedir verificación: **fundido de salida 190 ms** de la pantalla actual hacia `blurBg` y **fundido de entrada 230 ms** de
`blurBg` a la pantalla de clave; curva *smoothstep* `p²(3−2p)`; por tiempo, un cuadro por vuelta. Si no hay PSRAM/`blurBg`,
o en horizontal/hospedada, no hay fundido (corte directo). Con contraseña, el destino del fundido es la pantalla con el
teclado aún fuera; al terminar el fundido arranca el deslizamiento del teclado.
LVGL: `lv_screen_load_anim(..., LV_SCR_LOAD_ANIM_FADE_OUT/IN)` o dos `lv_anim` de `opa` (190 + 230 ms).

### 5.6 Verificación a plazos (`Power.h:204-260`, `FlexOS_Passcode.cpp:217-284`)

* `flexLockVerifyBegin(secreto)` lee sal, hash e iteraciones de NVS y calcula U1; cada vuelta `flexLockVerifyStep(1500)`
  hace **1500 iteraciones HMAC-SHA256** hasta completar `lockitr` (12 000) → `FLEXLOCK_OK`/`FLEXLOCK_FAIL`; comparación en
  **tiempo constante**. El secreto se copia al módulo y se **borra** en todos los caminos (`flexLockWipe` volátil).
* Si no hay sal/hash válidos o el secreto ≥ 64 bytes → fallo inmediato.
* Cancelar (salir, bloquear, apagar) → `flexLockVerifyCancel()`.
* **IDF**: ejecutar la derivación en una **tarea aparte** (o en tandas desde un `lv_timer`) y publicar el resultado por el
  bus de eventos; jamás bloquear la tarea LVGL. Hay además `flexLockVerifyAlone()` (variables locales) para verificar desde
  otra tarea (servidor web) sin interferir.

### 5.7 Fallos: sacudida, contador y espera progresiva (`Lock.h:189-336`)

* **Sacudida** (`LOCK_SHAKE_ON`): **180 ms**, amplitud 12 px, 3 ciclos, decaimiento lineal:
  `off = 12·(1−p)·sin(p·3·2π)`. En PIN se desplazan juntos puntos y teclado; en contraseña solo las teclas. Durante la
  sacudida no se aceptan pulsaciones. El campo se vacía.
* **Contador persistente** `lockFails` (NVS `"lockfails"`, int, tope 9999), guardado **antes** de aplicar la espera:
  * 1-3 fallos: sin espera.
  * 4-5 (`LOCK_FAILS_SOFT = 4`): **30 s** (`LOCK_WAIT_SOFT_MS`).
  * ≥ 6 (`LOCK_FAILS_HARD = 6`): **5 min** (`LOCK_WAIT_HARD_MS = 300000`) en cada fallo.
  * Acierto → contador a 0 (y se escribe).
  * Tras reiniciar, si el contador ya estaba alto, la espera se **vuelve a cobrar una vez** al abrir la verificación
    (`lockArmPendingPenalty`): reiniciar no es vía de escape.
* **Pantalla de espera** (el teclado queda inerte; la flecha atrás sí funciona y **no** perdona la espera): banda 180..276;
  `"Demasiados intentos fallidos"` (`uiFontFit` ≤ 2, `TH_ERR`) y = 186; si ≥ 6: `"Bloqueo temporal de 5 minutos"` y = 206
  (`lsuTxtLo`); cuenta atrás tamaño 4 `lsuTxtHi` y = 232: `"M:SS"` si ≥ 60 s, si no `"%d s"` (redondeo hacia arriba; se
  repinta solo al cambiar el segundo). Al cumplirse se borra con el siguiente repintado.

### 5.8 Revelado del escritorio tras acertar (`lsuRevealTick`, `Power.h:147-190`)

**400 ms**: mezcla lineal de `blurBg` → escritorio con un **temblor horizontal que decae**
`sh = (1−p)·6·sin(e·0.05)` px. `gState` sigue en `ST_LOCKSETUP` hasta el último cuadro; luego `ST_HOME`, escritorio exacto y
`touchDropAll()` (el dedo del último dígito no abre un icono). Antes: `lockOnSuccess()` (contador a 0, levanta protección
contra robo), `gLockVerifyLocked = false`.
Si el destino no es `UNLOCK` no hay revelado: se ejecuta `lsuFinishAfter` directamente.
LVGL: cargar la pantalla de Inicio con `lv_anim` de `opa` 0→255 (400 ms) y un `lv_anim` de `translate_x` con amplitud
decreciente.

### 5.9 Salir / cancelar (`lsuExit`, `Power.h:74-124`)

Corta verificación y fundidos y, en este orden:
1. Verificación que **salió del bloqueo** (`gLockVerifyLocked`) → vuelve a `ST_LOCK` (nunca al escritorio).
2. Verificación con destino ≠ UNLOCK → según la tabla §5.1 (medios, restablecimiento, apagado, kiosco, o Inicio).
3. Verificación de pantalla → `ST_LOCK`.
4. Creación (no verificación) → vuelve a Ajustes (`ST_APP` + `settingsRender()`).

Guardar (`lsuSavePin`/`lsuSavePass`): `flexLockSet(secreto, tipo)`; si OK `gLockType = 1|2`; borra el búfer y `lsuExit()`.

### 5.10 Almacenamiento seguro de la clave (`FlexOS_Passcode.h/.cpp`)

* **Formato** (NVS `flexos`): `"lockslt"` sal 16 B aleatoria (`esp_fill_random`); `"lockhsh"` PBKDF2-HMAC-SHA256 32 B;
  `"lockitr"` uint (= **12 000**, `FLEXLOCK_ITERS`; se rechaza 0 o > 1 000 000); `"locklen"` int (longitud del PIN, para
  autoconfirmar; 0 si contraseña); `"locktype"` int 0 ninguno / 1 PIN / 2 contraseña. Claves legadas en texto claro
  `"lockpin"`/`"lockpass"` (string) **se borran**.
* PBKDF2 implementado sobre HMAC (un solo bloque, dkLen ≤ 32): `U1 = HMAC(clave, sal‖00000001)`, `Ui = HMAC(clave, Ui−1)`,
  XOR acumulado. Backend mbedTLS en placa, OpenSSL en host.
* `flexLockSet`: sal nueva + hash + escribe todo + borra texto claro. `flexLockClear` (quitar clave) existe pero **ninguna
  pantalla la llama** (§5.15).
* **Migración** (`flexLockMigrate`, en cada arranque): si ya hay hash, limpia restos en claro; si hay clave en claro:
  (1) escribe hash sin borrar la vieja, (2) verifica que el hash valida esa clave, (3) solo entonces borra la vieja; si falla
  deja todo como estaba (borra sal/hash/itr) y devuelve −1.
* **Reutilizable tal cual** en `components/flex_portable` (cambiando `Preferences` por `flex_kv`); prueba host existente
  `tests/host/test_passcode.cpp` (vectores PBKDF2, a plazos == de una sentada, migración en tres pasos).

### 5.11 Bloqueo automático por inactividad (`autoLockTick`, `Lock.h:356-396`)

* Ventana `gAutoLockMs` (NVS `"autolockms"`, int, defecto **60000**). Opciones (Ajustes → Seguridad → `"Bloqueo de
  inactividad"`, cicla): `30000 "30 segundos"`, `60000 "1 minuto"`, `300000 "5 minutos"`, `600000 "10 minutos"`,
  `1800000 "30 minutos"`, `0 "Nunca"`. Un valor guardado que no esté en la lista se normaliza a 1 minuto.
* Cualquier contacto en cualquier pantalla rearma el temporizador. No actúa: en kiosco, suspendido, durante el apagado, con
  OTA en curso u OTA a pantalla completa (rearma), en horizontal/hospedada, con la cortina abierta, ni fuera de
  `ST_HOME`/`ST_APP`/`ST_DRAWER`/`ST_HOMECFG`.
* **Se aplica aunque no haya clave** (cae en el bloqueo de deslizar).
* `autoLockNow`: cierra Personalizar inicio guardando, sale del Modo edición, cierra (suspende) la app, compone escritorio y
  bloqueo, `ST_LOCK`, el bloqueo **baja** desde arriba (`animateTo(800, 0)`, 200 ms ease-out).

### 5.12 Suspensión de pantalla y bloqueo al despertar (`Touch.h:102-304`, `Power.h:633`)

* **Suspender**: doble toque con **dos dedos** en cualquier sitio (§14.5). Cierra la cortina y funde el backlight a 0
  (pasos de 6 % cada 10 ms ≈ 170 ms) y después `DISPOFF`. El sistema **sigue corriendo**; `gState` no cambia.
* **Despertar**: doble toque con **un dedo**. Con clave (`SUSPEND_LOCK_ON` y `gLockType > 0`) y si no estaba ya en el
  bloqueo/clave, **antes** de encender: anota `gSuspRetState`/`gSuspRetApp`, sale del Modo edición, cierra la cortina, fuerza
  vertical y compone el bloqueo. Luego `DISPON` y fundido del backlight al brillo del usuario. Sin clave: vuelve donde
  estaba. La app **no** se cierra: tras acertar la clave se vuelve a ella con `enterApp`.
* Vetos: kiosco, tecleando con dos pulgares, pellizco de una app.

### 5.13 Candado por app (`APPLOCK_ON`, `Prefs.h:91-96`, `:322-334`)

* Bitmask `gAppLock` (bit i = app i), NVS `"applockm"` int, defecto 0. Poner y quitar **exigen la clave**.
* Al abrir una app con candado (Inicio, caja, pantalla completa) y `gLockType > 0` → verificación `LSU_AFTER_OPENAPP`.
  Si no hay clave, los candados se ignoran.
* En la caja se marcan con un cuadrito rojo (§11.4) y en su ficha "Bloqueada: sí".
* **Bug a no copiar**: `cfgLoad` lee `(uint16_t)prefs.getInt("applockm")` (`Prefs.h:276`): con `APP_N = 19` los candados de
  **Flex Phone (16), Device Care (17) y Música (18) se pierden al reiniciar**. En IDF guardar 32 bits.

### 5.14 Modo kiosco (`KIOSK_ON`, `Touch.h:59-100`, `Lock.h:398-622`)

**Propósito**: prestar el teléfono con **una sola app** abierta; opcionalmente con una **zona de la pantalla excluida** del
táctil. Requiere clave (`gLockType > 0`); sin clave la fila del menú está atenuada e inerte.

**Entrada**: menú contextual de un icono → `"Modo kiosko"` → pantalla `ST_KIOSKSET` (`kioskSetEnter`).

**Pantalla "definir área excluida"** (fondo = escritorio + velo `PAGE_BG` α208):
* Tarjeta (24, 40, 432×350) r24: Vidrio tinte `SET_CARD_GLASS` / Plano `SET_CARD_BG` α235.
* `"Modo Kiosco"` tamaño 4 `SET_TXT_HI` centrado y=64; `"Arrastra para excluir una zona del tactil"` (≤ 2, `SET_TXT_LO`)
  y=122; `"Si no arrastras, toda la pantalla queda activa"` (≤ 2, `SET_TXT_MUTE`) y=146; icono de la app 72 px en
  (204, 196); nombre tamaño 3 y=282; `"Para salir: manten pulsado el candado de la"` y=344 y `"esquina y escribe tu clave del sistema"`
  y=366 (≤ 2, `SET_TXT_LO`).
* Botones (y = 704, 180×64, r20): `"Cancelar"` (30) tarjeta del tema; `"Iniciar"` (270) `TH_PRIM` con texto `TH_ONACC`.
* **Arrastre** (solo si empieza por encima de y = 696): dibuja el rectángulo `TH_ERR` α95 con borde r4 `TH_ERR` y el texto
  `"%d x %d"` tamaño 2 centrado. Cuenta como área si **≥ 20×20 px**. Repintado cada 30 ms.
* `"Cancelar"` → Inicio. `"Iniciar"` → `kioskStart(app, área)`: guarda en NVS y abre la app con su animación normal.

**Durante el kiosco**:
* **Candado** estampado sobre la app en cada transferencia (solo en `ST_APP`): pastilla (448, 44, 24×24) r7 `TH_PAGE` α200
  con candado `TH_TXT` (arco r5 en (460,53) con hueco r3, cuerpo (453,55,14×9) r2). Coordenadas físicas.
* **Salida**: mantener **> 1000 ms** (movimiento < 12 px) dentro de la zona del candado ampliada 14 px
  (x 434..486, y 30..82) → verificación `LSU_AFTER_KIOSKOUT`; cancelar vuelve a la app.
* **Zona excluida**: los toques dentro se descartan en el punto más alto del pipeline (se convierten en "sin dato"), **solo
  en `ST_APP`**; la zona del candado siempre gana.
* Vetados: Atrás/Inicio/Recientes, cerrar la app, abrir otra app, gestos de la barra, caja, cortina, crear clave
  (`lsuEnter`), autobloqueo, gesto de suspensión.
* **Persiste al reiniciar** (se vuelve a la app al terminar el splash). NVS: `"kioskon"` bool, `"kioskapp"` int (−1),
  `"kioskx"`, `"kiosky"`, `"kioskw"`, `"kioskh"` int (0). Al cargar: app fuera de rango o `gLockType == 0` → se desactiva.
* Salida (`kioskExitNow`): borra el estado (y NVS), fuerza vertical, Inicio.

### 5.15 Notas de migración de la clave

* **Lógica pura**: `FlexOS_Passcode` completo; `lockPenaltyMs` y la máquina de espera (con `millis` inyectable); sacudida
  (`lsuShakeOff` como función de `e`); reglas de longitud (PIN 4..8 dígitos, contraseña 4..63 bytes); tabla de destinos
  `lsuAfter`; `kioskInExcluded`/`kioskInExit`.
* **Huecos de producto/seguridad a decidir (no copiar a ciegas)**: (1) **no hay forma de volver a "Deslizar"** (quitar la
  clave) salvo restablecer de fábrica — `flexLockClear` no tiene pantalla; (2) **cambiar la clave no pide la actual**;
  (3) no se pide repetir la clave nueva para confirmarla; (4) la longitud mínima de la contraseña se cuenta en bytes.
* **Rarezas**: `"Elige un metodo"`, `"Modo kiosko"` (menú) vs `"Modo Kiosco"` (pantalla), textos sin tildes en kiosco.
* LVGL: PIN con `lv_buttonmatrix` 3×4 + fila de 8 `lv_obj` puntos; sacudida con `lv_anim` sobre `translate_x` del
  contenedor; cuenta atrás con `lv_timer` de 250 ms; contraseña con el teclado del sistema LVGL del proyecto.

---

## 6. Escritorio (`ST_HOME`)

### 6.1 Modelo de datos (`Home.h:45-212`, `Prefs.h:105-120`)

* `homeOrder[100]`: un byte por ranura, **paso fijo de 20 ranuras por página** (`HOME_STRIDE = 5 cols máx × 4 filas máx`) ×
  **5 páginas** (`HOME_PAGES_MAX`). Valores: `0..18` = app nativa (id de `APP_REG`); `128 + n` = app **descargada** anclada
  (n = ranura en la tabla `pkgpref`, §11.6); `0xFF` = hueco (`HOME_EMPTY`). Las ranuras más allá de la rejilla activa se
  mantienen vacías.
* `gHomePageN` (1..5, defecto 3), `gHomeMain` (página principal, la de la "casita"), `gHomePage` (visible; al arrancar =
  principal), `gHomeCols × gHomeRows` ∈ {4×3, 5×3, 4×4, 5×4}, `gHomeIconSz` 0 pequeño / 1 normal / 2 grande.
* Banderas: `gHomeLabels` (nombres bajo iconos, true), `gHomeLocked` (diseño bloqueado, false), `gHomeDots` (indicadores,
  true), `gHomePinch` (pellizco, true), `gHomeReduce` (reducir animaciones, false).
* `gAppFav` (bit i = app i en el escritorio) y `gAppHidden` (bit i = oculta en la caja y en Inicio).
* Widgets por página: `gHomeWg[5][6]` + `gHomeWgN[5]` (§7).

### 6.2 Geometría de la rejilla (`homeGrid`, `Home.h:641-676`)

```
S      = {60, 72, 84}[gHomeIconSz]
cstep  = 480 / cols          (4 → 120, 5 → 96)
rstep  = rows ≥ 4 ? 92 : 112
S      = min(S, cstep − 20, rstep − 26), mínimo 44
gx0    = (cstep − S) / 2      gy0 = 212
icono(slot) = (gx0 + (slot % cols)·cstep, 212 + (slot / cols)·rstep)
dotsY  = 212 + (rows−1)·rstep + S + (rows ≥ 4 ? 24 : 34)
bandBot= min(dotsY + 18, 596)
```

| Rejilla | Pequeño (S, gx0) | Normal | Grande | dotsY (normal) | bandBot (normal) |
|---|---|---|---|---|---|
| 4×3 | 60, 30 | **72, 24** (fábrica) | 84, 18 | **542** | 560 |
| 5×3 | 60, 18 | 72, 12 | 76, 10 | 542 | 560 |
| 4×4 | 60, 30 | 66, 27 | 66, 27 | 578 | 596 |
| 5×4 | 60, 18 | 66, 15 | 66, 15 | 578 | 596 |

Franjas: **página** = y 72 (`HOME_PAGE_TOP`, fila de cabecera de widgets) .. `bandBot`; rejilla de iconos desde y = 206
(`HOME_BAND_TOP`). La isla de notificaciones ocupa como mucho y 26..126 (`Types.h:412-424`).

### 6.3 Elementos fijos (iguales en todas las páginas) (`renderHomeInto`, `Home.h:910-947`)

| Elemento | Geometría | Estilo |
|---|---|---|
| Hora (+ cápsula del cronómetro si corre) | `cronoBarClock(16)`: texto tamaño 2 en (20, 16); con cronómetro activo la cápsula va a la derecha de la hora y, si no cabe, la hora se oculta | `TH_ONWALL` |
| Fecha corta | tamaño 1 en (20, 40) | `TH_ONWALL2` |
| Wi-Fi | `drawWifi(414, 28, 11)` | `TH_ONWALL` |
| Batería | `drawBattery(434, 20, 30×15, 82 %)` | `TH_ONWALL` (valor fijo) |
| **Dock** | (24, 624, 432×96) r28 — Vidrio tinte `TH_GLASS2` / Plano `TH_SURF` α90 | 4 iconos de 64 px en x = 40, 152, 264, 376, y = 640 |
| Indicadores de página | ver §6.4 | `TH_ONWALL` |
| Barra de navegación (modo Botones) | `ny = 748`: atrás = triángulo (70,756)-(88,746)-(88,766); inicio = anillo doble r12/r11 en (240,756); recientes = cuadrado redondeado (389,745,22×22) r4 contorno | `TH_ONWALL` (sobre el wallpaper, sin fondo) |
| Barra de gestos (modo Gestos) | píldora (175, 775, 130×5) r2 | `TH_ONWALL` α220 |
| Píldora Modo seguro | (146, 56, 188×38) r19 `#BA7030`; `"Modo seguro"` tamaño 2 blanco centrado y=66 | solo en Modo seguro |

**Dock (bug heredado a decidir)**: se dibujan los ids **12..15 literales** = Calendario, Cámara, Clima, Flex Store
(`drawHomeDock`, `Home.h:603`; `hitHomeIcon`, `:973`; `getIconRect`, `AppFramework.h:209`). En el registro v1 esos ids eran
Ajustes, Calculadora, Calendario y Cámara (intención original, ver comentario de `APP_DEF_DOCK`, `AppFramework.h:645-649`).
Consecuencias actuales: Flex Store aparece **dos veces** (rejilla de fábrica + dock) y **Ajustes y Calculadora no están en
Inicio de fábrica**. En IDF: el dock debe ser una **lista explícita de `IC_*`**; decidir con producto entre {Ajustes,
Calculadora, Calendario, Cámara} (intención) o el comportamiento actual. El dock no es editable (ni pulsación larga ni
arrastre).

### 6.4 Rejilla, iconos, etiquetas e indicadores

* Icono: `drawAppIcon(id, x, y, S)` (02 §6.1; estilo Plano o Vidrio según `"iconstyle"`) o el icono de la app descargada.
* Etiqueta (si `gHomeLabels`): centrada bajo el icono en `y + S + 6`, color `TH_ONWALL`; tamaño **2** si `cstep ≥ 110`
  (4 columnas) y **1** con 5 columnas; ancho máximo `cstep − 14`; primero se encoge la fuente, luego se corta con `"..."`.
* **Indicadores** (`homeDrawDots`, `Home.h:757`; si `gHomeDots`): centrados en x con paso 18 (`x0 = 240 − (n−1)·9`),
  y = `dotsY`. La **página principal** se dibuja como una **casita** (triángulo (x,y−7)-(x−6,y−1)-(x+6,y−1) + rectángulo
  8×6); las demás, círculos r4 α110. El punto **activo** es un círculo r5 α255 que **se desplaza con el dedo** durante el
  deslizamiento (`pos = from + (to−from)·frac`); estando quieto en la principal no se dibuja (la casita ya lo indica).

### 6.5 Interacción en el escritorio (`homeTick`, `HomeCfg.h:1228-1325`) — orden de prioridad

0. Antes (en `loop`): isla de notificaciones, centro de notificaciones (borde izq. 26 px), **panel rápido** (borde superior
   30 px / derecho 26 px), pellizco (§10.9), suspensión.
1. Modo edición → `edTick()` (§8) y nada más.
2. Si `gHomeDirty` y no hay gesto de página: recomponer (si hay transición en curso, solo fuera de pantalla).
3. Modo seguro: toque en la píldora → pantalla de Modo seguro.
4. **Deslizamiento de páginas** (§6.6): si está en curso, se queda el toque entero.
5. Modo Gestos: barra de gestos (02 §8.9): rápido → refresca Inicio; mantenido ≥ 300 ms → Recientes.
6. **Destello** al apoyar sobre un icono (solo estilo de icono Vidrio, §6.7).
7. **Pulsación larga > 1000 ms** (movimiento < 12 px) sobre una **celda** de la rejilla con app → app nativa: **menú
   contextual** (§9); app descargada: Modo edición agarrando el icono.
8. **Pulsación larga > 650 ms** (≤ 12 px) en un hueco **verdaderamente vacío** (inicio y punto actual: dentro de la franja
   de página, sin icono, sin widget, fuera de la isla visible) → **Personalizar inicio** (§10).
9. **Deslizar hacia arriba** (`swipeUp`, §14.3) que empezó en y > 96, con la caja disponible → **Caja de aplicaciones** (§11).
10. **Toque**:
    * x > 320 **y** y > 728 → **Recientes** (se atiende en los dos modos de navegación).
    * Icono (zona `[ix−6, ix+S+6] × [iy, iy+S+16]`; un widget encima gana) o icono del dock (su cuadrado de 64):
      descargada → anota la petición y abre Flex Store (que lanza el runtime); nativa con candado y clave → verificación;
      resto → `enterApp(id)` (transición desde el icono, 02 §8.7).
    * Widget con acción: **Cámara** → app Cámara, **Clima** → app Clima, **Calendario** → app Calendario. Los demás son
      informativos.
    * Los botones Atrás e Inicio de la barra no hacen nada en el escritorio.

Notas: el chequeo de 650 ms se cumple antes que el de 1000 ms, así que una pulsación larga en el **hueco entre iconos de una
celda ocupada** abre Personalizar inicio; sobre el propio icono (su zona de toque) espera a los 1000 ms y abre el menú.

### 6.6 Deslizamiento entre páginas (`Home.h:981-1400`)

| Parámetro | Valor |
|---|---|
| Inicio del arrastre | `|dx| ≥ 18` px (`HP_DRAG_MIN`) **y** `|dx| ≥ 2·|dy|`; el dedo empezó dentro de la franja de página `[72, bandBot)`; no en la banda de la isla si está pintando; no en Modo edición |
| Bordes | sin página a ese lado → no se arrastra (sin rebote, sin envolver) |
| Durante | la página se mueve con el dedo, **acotada a una pantalla** y solo hacia el lado elegido; la vecina entra pegada; el wallpaper **no se mueve**; el vidrio de los widgets se re-muestrea en su nueva posición (no "arrastra" el fondo) |
| Cadencia | 33 ms (30 fps); no se recompone si el dedo está quieto |
| Soltar | cambia si **flick** (duración < 320 ms y `|dx| > 42` px) **o** `|dx| > 120` px (¼ de pantalla); si no, vuelve |
| Acomodo | **190 ms**, ease-out cúbica `1 − (1−p)³`, por tiempo |
| Franja que se mueve | desde y = 72 si alguna de las dos páginas tiene widget en la cabecera; si no, desde 206 |
| Fijos | barra de estado, dock, barra de navegación y la fila de puntos |
| Isla | si su banda se solapa, se dibuja encima de cada cuadro |

Sin PSRAM para el caché de la página vecina el gesto **no hace nada** (`hpTryStart` devuelve false; el comentario dice que
"salta a la página", pero no es así — no copiar; en IDF siempre debe haber cambio de página).
Cambio de página sin animación (`homeGoPage`) desde Personalizar inicio.

**LVGL**: contenedor horizontal con `LV_SCROLL_SNAP_CENTER` y páginas de 480 px como hijos (o `lv_tileview` en una fila),
`LV_OBJ_FLAG_SCROLL_ONE`, sin elasticidad en los extremos (`LV_OBJ_FLAG_SCROLL_ELASTIC` off), animación de acomodo 190 ms
ease-out; el wallpaper fuera del contenedor (fondo fijo); el gesto vertical (caja) debe cancelarse si el horizontal domina
(regla 2:1). Ajustar `scroll_limit`/`scroll_throw` del indev para reproducir 18 px y el flick 320 ms/42 px.

### 6.7 Destello de icono (`animateIconRipple`, `Widgets.h:593-627`)

Solo con estilo de icono **Vidrio** y fuera de Modo seguro: al **apoyar** el dedo sobre un icono, círculo `TH_ONWALL` centrado
en el punto tocado que crece r = 70·p y se desvanece α = 160·(1−p) durante **500 ms** (lineal), a ~60 fps; banda recortada
a y 64..726. Se cancela al cambiar de pantalla. LVGL: objeto círculo en la capa superior con `lv_anim` de tamaño y opa.

### 6.8 Normalización y colocación automática (`homeOrderNormalize`, `Home.h:1646-1717`)

Se ejecuta al cargar y tras cada operación que cambia el escritorio. Reglas en orden:
0. Las ranuras fuera de la rejilla o de las páginas activas se vacían y sus iconos se **rescatan**.
1. Un icono **debajo de un widget** se rescata.
2. Se vacía la ranura si: id ≥ `APP_N`, repetido, **no favorito** u **oculto**. Descargadas: se vacía si ya no está instalada,
   no está anclada, está oculta o repetida.
3. Rescatados → primer hueco libre (`homeFirstFreeGrow`: recorre páginas en orden; si no hay hueco **crea una página** al
   final, hasta 5). Si de verdad no cabe (5 páginas llenas), la app nativa **deja de ser favorita**.
4. Toda app favorita sin ranura → primer hueco (creando página si hace falta).

Un icono por ranura por construcción: nunca dos en la misma celda. Los widgets se normalizan **antes** (§7.5).

### 6.9 Escritorio de fábrica y migraciones del registro (`Home.h:87-129`, `:1718-1884`)

* **Fábrica** (página 0, 4×3, 3 páginas, principal 0): `Reloj, Galería, Multimedia, Almacenamiento / Modo PC, Notas,
  Flex Store, Navegador / Flex Compass, Device Care, Paint, Juegos` (`HOME_FACTORY`). Widgets: **Clima** (col 0, fila 0,
  2×1) y **Calendario** (col 2, fila 0, 2×1) en la cabecera de la página principal (`homeWgFactory`).
  Favoritas de fábrica = apps con `APP_DEF_FAV` (§15): ids 0-9, 15, 17.
* **Versión del registro** `APPREG_VER = 3`. Al cargar, si `"appver"` < 3 y había datos, se traducen **ranuras, favoritas,
  ocultas y candados**:
  * v1 (22 apps) → actual: `APPREG_MAP_V1 = {0,1,2,3,4,5, 15(Educación→Flex Store), 6, 7(Code IDE→Compass), 17(Bienestar→
    Device Care), 8,9,10,11,12,13,14,15,16,17,7, 0xFF(Carpeta segura retirada)}`.
  * v2 (19 apps) → actual: `APPREG_MAP_V2 = {0,1,2,3,4,5,6, 7(Code IDE→Compass), 8,9,10,11,12,13,14,15,16,17, 7(Compass)}`.
  * El candado de Code IDE **no** se hereda. Tras v1: Flex Store y Device Care forzadas a favoritas y visibles. Siempre:
    Flex Compass favorita y visible.
* `"appn"` (apps que conocía el firmware que guardó): si < `APP_N`, las apps nuevas con `APP_DEF_FAV` se marcan favoritas
  (`drawerRegistryAdopt`).
* Se limpian bits de apps inexistentes; **Ajustes nunca oculto**.
* Escritorio: clave `"hordq"` (100 B). Si falta: migra `"hordp"` (36 B = 3 páginas × 12) o `"hord"` (12 B = página 0); las
  claves viejas **no se reescriben** (permiten volver a una versión anterior). Primer arranque real → fábrica.

### 6.10 Persistencia del escritorio (`homeOrderSave`, `Home.h:1540-1567`)

Una sola apertura de NVS por guardado, **solo al cambiar algo** (soltar en edición, acción de menú, personalización). Claves:
`"hordq"` bytes[100], `"hpgn"` int, `"hpmain"` int, `"hgrid"` int `(cols<<8)|rows`, `"hicon"` int, `"hflag"` int
(bit0 nombres, bit1 bloqueado, bit2 indicadores, bit3 pellizco, bit4 reducir animaciones), `"hwg2"` bytes[157], `"appn"`
int (= 19), `"appver"` int (= 3), `"appfav"` int, `"apphide"` int. Valores por defecto al leer en §16.

### 6.11 Referencias y migración

`homeGrid` `Home.h:641`; `homeDotsY` `:659`; `homeDrawGridWork` `:718`; `homeDrawDots` `:757`; `renderHomeInto` `:910`;
`hitHomeIcon` `:963`; `hpTryStart` `:1283`; `hpTick` `:1332`; `homeOrderNormalize` `:1646`; `homeOrderLoad` `:1718`;
`homeTick` `HomeCfg.h:1228`; `homeEmptySpaceAt` `HomeCfg.h:1212`.
**Lógica pura reutilizable** (portar a C sin dibujo): `homeGrid`, `homeSlotXY`, `homeDotsY`, `homeBandBot`, `homeIdx`,
`homeFirstFree(Grow)`, `homePageAppendQuiet`, `homeOrderNormalize`, `homeOrderLoad` (con mapas de migración y lector de
claves legadas), decisión de cambio de página (flick/umbral), `edMove`. Pruebas host existentes a portar:
`testPaginasHome` (`ino_compile.cpp:2279`), `testDeslizarPaginas` (`:2518`), `testRejillaAutoPaginas` (`:5386`),
`testIconosEnSuCaja` (`:5197`).
**Depende del motor**: cachés `homeBuf`, `hpBuf`, `hpBg`, `hgBd`, máscaras de contenido y anotación de paneles de vidrio
(`homeGlassBegin/Replay`) — en LVGL desaparecen (objetos reales con estilo de vidrio sobre un fondo fijo), pero el
**requisito visual** se mantiene: durante el deslizamiento el vidrio de cada widget muestra el fondo que tiene detrás en su
posición actual (no se "pega" el fondo de su posición original).

---

## 7. Widgets del escritorio (`FlexOS_Ultra_Widgets.h`)

### 7.1 Catálogo (`WG_REG`, `Widgets.h:60-74`) — los ids viajan a NVS

| id | Enum | Nombre (selector) | Categoría | Tamaño al añadir (celdas) | ancho mín..máx | alto mín..máx | alto mín px | Dato |
|---|---|---|---|---|---|---|---|---|
| 0 | `WG_NONE` | — | — | — | — | — | — | nunca se ofrece |
| 1 | `WG_CLOCK` | "Reloj digital" | "Reloj" | 2×1 | 2..4 | 1..2 | 0 | hora (`clkStrBar`) |
| 2 | `WG_CLOCK_A` | "Reloj analógico" | "Reloj" | 2×2 | 1..2 | 1..2 | 70 | hora/minuto |
| 3 | `WG_DATE` | "Fecha" | "Reloj" | 2×1 | 2..4 | 1..1 | 0 | fecha corta |
| 4 | `WG_WIFI` | "Wi-Fi" | "Sistema" | 1×1 | 1..2 | 1..1 | 0 | conectado sí/no |
| 5 | `WG_MEM` | "Memoria" | "Sistema" | 2×1 | 2..4 | 1..1 | 0 | heap libre |
| 6 | `WG_STORAGE` | "Almacenamiento" | "Sistema" | 2×1 | 2..4 | 1..1 | 0 | LittleFS usado/total |
| 7 | `WG_RETIRED_7` | — | — | — | — | — | — | **retirado**: se descarta al normalizar |
| 8 | `WG_CRONO` | "Cronómetro" | "Reloj" | 2×1 | 2..4 | 1..1 | 0 | tiempo del cronómetro |
| 9 | `WG_CAM` | "Cámara" | "Accesos" | 1×1 | 1..2 | 1..1 | 0 | acceso directo |
| 10 | `WG_CLIMA` | "Clima" | "Información" | 2×2 | 2..4 | 1..2 | 116 | `WeatherState` (Open-Meteo) |
| 11 | `WG_CALEND` | "Calendario" | "Información" | 2×1 | 2..4 | 1..3 | 110 | mes actual |

Máximo **6 widgets por página** (`HOME_WG_MAX`); ningún widget pasa de 4 columnas.

### 7.2 Geometría (`wgRect`, `Widgets.h:174`)

Filas en coordenadas de widget: **fila 0 = cabecera** (y 72..192, 120 px, solo widgets), filas 1.. = filas de iconos.
```
x  = col·cstep + 8              w = ancho·cstep − 16
y  = (fila == 0) ? 72 : 212 + (fila−1)·rstep − 6
yFin = (filaFin == 0) ? 192 : 212 + filaFin·rstep − 18      h = yFin − y   (mín. 24×24)
```
Ejemplos 4×3 normal: cabecera 2×1 → (8, 72, 224×120); fila 1 → y 206, h 100; filas 1-2 → h 212; cabecera+fila 1 → (72..306).
Con 4 filas (rstep 92) una fila de iconos da 80 px de alto: por eso Clima (116 px) y Calendario (110 px) no caben en una
sola fila de iconos y sí en la cabecera.

### 7.3 Dibujo por tipo (`wgDrawCell`, `Widgets.h:186`)

* **Superficie** común: Vidrio tinte `TH_GLASS2` r20 / Plano `TH_SURF` α225 r20. Texto principal `fg = onColor(base)`,
  secundario `fg2 = mix(fg, base, 96)`; margen `pad = 12`.
* **Reloj digital**: `"Reloj"` tamaño 1 `fg2` en (x+12, y+10); hora tamaño 4 `fg` en (x+12, y+h/2−10).
* **Reloj analógico**: esfera centrada r = min(w,h)/2 − 14 (mín. 8): dos circunferencias (`fg`, `fg2`), 12 marcas r1 a
  r−6, aguja horaria 2.4 px a 0.52 r, minutero 1.8 px a 0.78 r, eje r3. Sin segundero.
* **Fecha**: `"Fecha"` tamaño 1; fecha corta tamaño 2 recortada.
* **Wi-Fi**: glifo de antena (3 arcos de puntos, solo el punto sin red) en (cx, cy−14) r11; `"Wi-Fi"` / `"Sin red"`
  tamaño 1 centrado en y+h−22.
* **Memoria**: `"Memoria"` tamaño 1; `"%u KB libres"` tamaño 2.
* **Almacenamiento**: `"Almacenamiento"` tamaño 1; `"%s de %s"` (usado de total, `flexFsFmtSize`) tamaño 1; barra
  (x+12, y+h−24, w−24 × 8) r4 pista `TH_TRACK`, relleno `wallAccent2()` (mín. 2 px) al porcentaje.
* **Clima**, alto < 180 px (una fila): tarjeta propia `wxHomeWidget` (`AppWeather.h:1289`): Vidrio tinte `#1E4896` /
  Plano `#1C3A78`, r20; **compacta** (< 400 px de ancho): ubicación tamaño 1 `TH_ONWALL2` (x+16, y+14); temperatura
  tamaño 6 (x+14, y+32); icono r15 en (x+w−44, y+44); condición tamaño 1 (x+16, y+88); máx/mín con flechas tamaño 2 en
  y+102. **Ancha** (≥ 400 px): bloque izquierdo de 200 px (ubicación tamaño 2, temperatura tamaño 6, icono, condición,
  máx/mín) + **4 horas reales** a la derecha (columnas de 46 px: hora tamaño 1 y+16, icono r10 y+44 con día/noche de
  **esa** hora, temperatura tamaño 2 y+72). Sin datos: `"Clima"` tamaño 2, `"Sin datos meteorológicos"` tamaño 1 y la
  pista `"Añadir ubicación"` (sin ubicaciones) o `"Reintentar"`.
  Alto ≥ 180 px: tarjeta genérica con `"Clima"`, icono r16 en (x+w−34, y+40), temperatura tamaño 5 en (x+12, y+34),
  ubicación y condición tamaño 1 abajo; sin datos `"Sin datos meteorológicos"` tamaño 2 centrado.
* **Calendario** (`calWidgetBody`, `Home.h:563`): título `"%s %d"` (mes corto + año) tamaño 2 (x+12, y+9); iniciales de la
  semana **empezando en domingo** tamaño 1 (ES `D L M M J V S`, EN `S M T W T F S`, FR `D L M M J V S`, PT
  `D S T Q Q S S`, IT `D L M M G V S`) en y+33; días del mes en rejilla de 7 columnas desde y+49 con alto de fila
  `max(9, (h−54)/6)`; **hoy** con círculo r8 `TH_PRIM` y número `TH_ONACC`.
* **Cronómetro**: `"Cronómetro en marcha"` (corriendo) / `"Cronómetro"` tamaño 1; tiempo tamaño 3 (formato sin
  centésimas).
* **Cámara**: cuerpo (cx−15, cy−20, 30×22) r6 `fg` con objetivo r7 del color de fondo; `"Cámara"` tamaño 1 en y+h−22.
* Versión **mini** (miniaturas de Personalizar inicio): panel `TH_SURF` α200 r6 + barra de título y una barra (o un círculo
  para el reloj analógico).

### 7.4 Datos y refresco (`wgDataTick`, `Widgets.h:118-156`; `wgRepaint`, `:560`)

* Regla: **el widget no calcula nada al dibujarse**. Cada **2 s** se recalculan cadenas en caché (hora, fecha, Wi-Fi,
  memoria, almacenamiento, cronómetro); si alguna cambia → `wgDirty`.
* Clima: se comprueba **en cada vuelta** la generación del motor (`flexWeatherGen`); al cambiar se repinta.
* Almacenamiento (caro, recorre LittleFS): solo si hay un widget de almacenamiento colocado, en Inicio, sin edición, sin
  dedo, sin gesto ni transición, y como mucho cada **10 s** (`WG_STO_MS`); si no, se conserva lo último medido.
* Wi-Fi: se usa el estado publicado `gNetOnline` (nunca `WiFi.status()`), SSID sin `String`.
* Repintado: solo las **filas** que ocupan los widgets de la página visible, con el escritorio quieto.
* LVGL: cada widget es un `lv_obj` con sus `lv_label`; un `lv_timer` de 2 s actualiza textos con
  `lv_label_set_text` solo si cambian; el clima se suscribe al evento del servicio de clima.

### 7.5 Validación espacial (única puerta: `homeWgPlaceOk`, `Widgets.h:337-350`)

Un widget de tipo T cabe en (c, r, w, h) si: T válido (no 0, no 7, < 12); `minW ≤ w ≤ maxW` y `minH ≤ h ≤ maxH`; su alto en
píxeles (`wgRect`) ≥ `minPxH`; está dentro de la rejilla (`c+w ≤ cols`, `r+h ≤ rows+1`); no pisa iconos (máscara de celdas)
ni otros widgets (en la cabecera, máscara de columnas). Pasa por aquí añadir, mover, redimensionar y cambiar de página.

* **Primer hueco** (`homeWgSpotFor`): recorre filas de arriba abajo y columnas de izquierda a derecha (determinista).
* **Añadir** (`homeWgAdd`): 0 ok / 1 la página ya tiene 6 / 2 sin hueco.
* **Llevar a otra página** (`homeWgToPage`): 1) mismo sitio y tamaño; 2) primer hueco con su tamaño; 3) primer hueco con su
  tamaño **mínimo** (y si el mínimo no llega al alto en px, con una fila más); 4) si no, no se mueve (−1).
* **Normalización** (`homeWgNormalize`): elimina tipos inválidos/retirados y los de páginas inexistentes, acota tamaños, y
  si un widget no cabe (cambio de rejilla, solape) lo **recoloca** en su página (mismo tamaño → mínimo → mínimo+1 fila); si
  no hay sitio, lo retira. Los widgets mandan sobre las celdas: los iconos de debajo se rescatan (§6.8).

### 7.6 Serialización (`Widgets.h:497-539`)

Blob **v2** (NVS `"hwg2"`, **157 B**): `'W'`, `2`, y por cada una de las 5 páginas: `count` + 6 × (`type, col, row, w, h`).
Blob **v1** (NVS `"hwg"`, 82 B, solo lectura para migrar): 3 widgets por página y **sin cabecera** (al migrar cada fila
baja 1). Validación al leer: magic/versión, `count ≤ perPage`, `type < 12`, `w ≤ 5`, `h ≤ 5`, `c+w ≤ 5`, `r+h ≤ 5`; un blob
que no cuadra se descarta entero (escritorio sin widgets). Si no hay `"hwg2"`, se migra (v1 o vacío) **y además** se
colocan Clima y Calendario de fábrica en la cabecera de la principal; la migración se escribe una sola vez.

### 7.7 Referencias y migración

`WG_REG` `Widgets.h:60`; `wgRect` `:174`; `wgDrawCell` `:186`; `homeCellMask` `:276`; `homeWgFits` `:309`;
`homeWgPlaceOk` `:348`; `homeWgAdd` `:383`; `homeWgToPage` `:420`; `homeWgNormalize` `:448`; `homeWgFactory` `:486`;
`homeWgSerialize/Parse` `:500-539`. Tipos `HomeWidget`, `WgDesc` en `Types.h:508-520`.
**Lógica pura** (todo salvo el dibujo): máscaras, fits, spot, add/remove/resize/toPage, normalize, serialize/parse. Prueba
host existente `testWidgetsDePagina` (`ino_compile.cpp:7356`). **Rareza**: `wgWifi` calcula el SSID ("Conectado" /
"Sin conexión") pero el widget muestra solo `"Wi-Fi"`/`"Sin red"`; la memoria muestra el heap total, no PSRAM.

---

## 8. Modo edición del escritorio (`Home.h:1501-2197`)

**Cómo se llega**: menú contextual → `"Modo edición"` (sin agarrar icono); pulsación larga > 1000 ms sobre un icono de app
**descargada** (agarrándolo); con `CTXMENU_ON = 0` la pulsación larga entraría siempre aquí.
**Cómo se sale**: toque en un hueco sin widget seleccionado (incluye la barra de navegación); autobloqueo; suspensión.
Al salir: guarda (`homeOrderSave`) y recompone.

**Layout** (se recompone la franja y = 64 .. 618):
* El escritorio de fondo **sin la página** (barras, dock, wallpaper).
* **Widgets** de la página: superficie normal; el seleccionado con halo `wallAccent()` α120 (x−4, y−4, w+8, h+8) r22.
  Si el diseño no está bloqueado: **insignia de quitar** círculo r11 `TH_DANGER` en (wx+12, wy+12) con aspa 2 px
  `TH_ONACC`; y si el seleccionado admite otros tamaños, **asa de tamaño** círculo r12 `wallAccent()` en (wx+w−12, wy+h−12)
  con flecha diagonal doble `onColor(acento)`.
* **Iconos**: al **89 %** (`S·8/9`, centrados en su celda), con **temblor** `±2 px`: `ox = 2·sin(t·0.02 + i·0.6)`,
  `oy = 2·cos(t·0.017 + i·0.6)` (t en ms) y **muelle** hacia su celda (`pos += (destino − pos)·0.2` por cuadro). Huecos sin
  nada.
* **Icono arrastrado**: tamaño completo bajo el dedo (centrado), sobre un panel (x−6, y−6, S+12) r16 — Vidrio `TH_GLASS2`
  / Plano `TH_SEL` α150. Posición acotada: x ∈ [8, 480−S−8], y ∈ [140, bandBot−S−10].
* **Línea de ayuda** tamaño 1 centrada en y = 602: `"Arrastra iconos y widgets - Inicio para salir"` (`TH_ONWALL2`) o el
  aviso temporal (1600 ms, `TH_ONWALL`).
* Cadencia ≈ 26 fps (con iconos Vidrio, como mucho cada 50 ms).

**Interacción** (`edTick`):
1. Apoyo sobre el **asa** (±22 px) del widget seleccionado → **redimensionar**: la esquina sigue a la celda bajo el dedo;
   tamaño acotado a los límites del tipo; solo se acepta si `homeWgPlaceOk`; si no, aviso `"Ese tamaño no cabe ahí"`.
2. Apoyo sobre un widget: en la **insignia** (±14 px) → lo quita (normaliza y guarda); en el cuerpo → lo selecciona y (si no
   bloqueado) empieza a **moverlo por celdas** agarrado por la celda tocada; solo se acepta una posición válida.
3. Apoyo sobre una celda con icono → lo **agarra** (suelta la selección de widget). Hueco o diseño bloqueado → no agarra.
4. **Reordenar**: si el centro del icono arrastrado permanece **400 ms** sobre otra celda → `edMove` (lo reinserta
   **desplazando** los intermedios).
5. **Llevar a la página vecina**: mantener el icono o widget a ≤ **34 px** de un borde lateral durante **700 ms**: icono → a
   la primera celda libre (sin icono ni widget) de la vecina; si no hay, `"Sin espacio en la página %d"` (reintenta tras
   otros 700 ms). Widget → `homeWgToPage`; mensajes `"Widget movido a la página %d"`, `"La página %d ya tiene %d widgets"`,
   `"Sin espacio en la página %d"`.
6. Soltar → normaliza y guarda (también tras mover un icono, porque desplazar puede meter un icono bajo un widget).
7. Toque en vacío: con widget seleccionado → deselecciona; sin selección → sale del modo.

`gHomeLocked` ("Bloquear diseño del inicio"): sin arrastre de iconos/widgets, sin insignias ni asa; sí permite seleccionar.
En Modo edición el deslizamiento normal de páginas está desactivado (solo el "empujar el borde").

**LVGL**: entrar = añadir a cada icono un `lv_anim` infinito de `translate_x/y` con fase por índice y `transform_scale`
230/256; arrastre con `LV_EVENT_PRESSING` moviendo el objeto en `lv_layer_top`; temporizador de *dwell* 400 ms y de borde
700 ms con `lv_timer`; el muelle con `lv_anim` (ease-out) en vez de factor por cuadro. **Lógica pura** a portar con
pruebas: `edMove`, `edSlotAt`, `homeLayoutCellAt`, `edEdgeCheck` (con tiempo inyectable), redimensionar por celdas.
Rareza a no copiar: el muelle y el temblor dependen de la cadencia del bucle.

---

## 9. Menú contextual del escritorio (`ST_CTX`, `AppDrawer.h:30-291`)

**Cómo se llega**: pulsación larga > 1000 ms sobre un icono **nativo** de la rejilla (no del dock).

**Filas** (`ctxBuildRows`), en este orden:

| Fila | Texto exacto | Glifo (26 px, a la derecha) | Activa si | Acción (al terminar la animación de cierre) |
|---|---|---|---|---|
| Candado | `"Bloquear app"` / `"Desbloquear app"` | candado `TH_DANGER` | hay clave y `APPLOCK_ON` | verificación `LOCKAPP`/`UNLOCKAPP` |
| Edición | `"Modo edición"` | rejilla 2×2 `TH_TXT2` | siempre | entra en Modo edición |
| Kiosco | `"Modo kiosko"` | pantalla con candado `TH_OK` | hay clave y `KIOSK_ON` | pantalla de área excluida |
| Pantalla completa | `"Pantalla completa"` | 4 esquinas (marco alto) `TH_PRIM` | solo apps `APP_IMMERSIVE` (hoy: **Navegador**) | abre a pantalla completa vertical (con verificación si tiene candado) |
| Pantalla completa horizontal | `"Pantalla completa horizontal"` | 4 esquinas (marco ancho) `TH_PRIM` | ídem | abre a pantalla completa horizontal |

**Layout**: panel único `CTX_W = 244` × (`filas × 58`), r20, superficie del sistema `UIS_ELEVATED` (Vidrio desde el primer
cuadro gracias a la banda pre-desenfocada; Plano sólido) con α 238. Texto alineado a la izquierda (margen 18), tamaño
`uiFontFit(texto, 176, 3)`, color `SET_TXT_HI` (activa) / `SET_TXT_MUTE` (inactiva; glifo con α 110/255). Separadores 1 px
`SET_TXT_MUTE` α95 con 14 px de margen. Sin fila "Cancelar".
**Posición** (`ctxOpen`): a la **derecha** del icono (`ix + S + 12`) si cabe entero (margen 8), si no a la izquierda
(`ix − 12 − 244`), si no en el lado con más sitio; `y` alineada con el borde superior del icono; recorte final a
[8, 480−8−244] × [8, 800−8−alto].
**Animación**: **150 ms** apertura y cierre, ease-out cuadrática; escala 0.88 → 1 hacia su centro y fundido α 0 → 255.
**Interacción**: toque en fila activa → cierra y luego ejecuta; fila **inactiva** → no hace nada (ni cierra); toque fuera →
cierra (cancelar). Al cerrar: `ST_HOME` y escritorio limpio.
LVGL: `lv_obj` en `lv_layer_top` con `transform_scale` + `opa` animados (150 ms, `lv_anim_path_ease_out`), filas como
`lv_button` con estado `LV_STATE_DISABLED`; un objeto *scrim* transparente a pantalla completa debajo para capturar el toque
de fuera.

---

## 10. Personalizar inicio (`ST_HOMECFG`, `FlexOS_Ultra_HomeCfg.h`)

### 10.1 Cómo se llega y cómo se sale

* Entrada: pulsación larga 650 ms en hueco vacío del escritorio, o **pellizco** de dos dedos hacia dentro (§10.9). Requiere
  PSRAM para el fondo velado (si no, no entra).
* Animación de entrada (**180 ms**, ease-out cuadrática): el escritorio real **se reduce** desde pantalla completa hasta la
  tarjeta central (216×360 en y = 150) con radio 0 → 26, sobre el fondo velado. Salida: la inversa. Con
  `gHomeReduce` sin animación.
* Salida: botón Inicio (modo Botones) o gesto de la barra (modo Gestos, desde y > 756), **pellizco hacia fuera** en la vista
  de páginas, "Atrás" desde la vista de páginas, OTA, autobloqueo. Al salir: la página visible pasa a ser la centrada (o la
  principal si estaba en la tarjeta "+"), normaliza y **guarda** (`homeOrderSave` + `homeCfgSave`).
* Tras entrar se ignoran toques hasta que el dedo se levanta (`hcIgnore`). Repintado como mucho cada 24 ms y solo si algo
  cambió.

### 10.2 Elementos comunes

* Fondo: `blurBg` (wallpaper del Inicio + velo).
* *Chrome*: hora/cápsula (20, 16), Wi-Fi, batería como el escritorio; **título** tamaño 3 centrado y = 66 `TH_ONWALL`;
  **subtítulo** tamaño 2 y = 98 `TH_ONWALL2`.
* **Chip Atrás** (vistas secundarias): (14, 52, 96×36) r18 `TH_SCRIM` α130, triángulo (34,70)-(46,61)-(46,79) y `"Atrás"`
  tamaño 2 en (52, 62) `TH_ONWALL`; zona 14..110 × 52..88.
* Barra de navegación como el escritorio (§6.3). Botón **Atrás** (x < 160, y > 728): cierra modal → de "Mis imágenes" a
  "Fondo" → de cualquier vista a "Páginas" → sale. Botón **Inicio** (x < 320): sale.
* Botones (`hcBtn`): cápsula r = h/2; primario `wallAccent()`, secundario `TH_SURF2`; texto tamaño 2 `onColor(fondo)`.
* Fila (`hcRow`): panel (16, y, 448×h) r16 `TH_SURF` α235 + borde `TH_BORDER`; título tamaño 2 `TH_TXT` en (32, y+(h−20)/2)
  recortado a 360; tipo 0 = valor tamaño 2 `TH_TXT2` alineado a x = 426 + chevrón (x = 448); tipo 1 = **interruptor**
  46×26 r13 en (402, y+(h−26)/2) `wallAccent()` / `TH_TRACK` con pomo blanco r10 a la derecha/izquierda; tipo 2 = acción.

### 10.3 Vista Páginas (`HCV_PAGES`, inicial)

* Título `"Personalizar inicio"`; subtítulo `"Página %d de %d"`.
* **Carrusel** de tarjetas 216×360 (x central = 132, y = 150), paso 240; se ven la central y las vecinas. Cada tarjeta:
  miniatura del **fondo** (escalado al vecino más cercano) con r26 + **miniatura del modelo real** (dock en esquema blanco
  α60, widgets en versión mini, iconos a escala 216/480 en estilo Plano); borde `wallAccent()` doble (central) o
  `TH_BORDER`; **casita** en (x+190, y+26) sobre círculo r16 `TH_SCRIM` α140: rellena amarilla `#FFD650` si es la
  principal, contorno `TH_ONWALL` si no; **papelera** en (x+26, y+26) (rosa `#FCD6D6`) si hay > 1 página y el diseño no
  está bloqueado; etiqueta `"Página %d"` tamaño 2 bajo la tarjeta (y+366).
* Tarjeta **"+"** al final si hay < 5 páginas y no bloqueado: blanca α34 r26, borde `TH_ONWALL2`, cruz de 56 px,
  `"Nueva página"`.
* **Puntos** en y = 552, paso 26: principal = casita 13 px; resto círculos r5; la centrada en `wallAccent()` (α255), el
  resto `TH_ONWALL` α140.
* **Barra inferior** (12, 590, 456×112) r26 panel α225 con 4 entradas de 114 px: iconos vectoriales en y = 630 y textos
  tamaño 2 en y = 660: `"Fondo"`, `"Temas"`, `"Widgets"`, `"Ajustes"`.

**Interacción**:
* Toque en la barra → abre la vista. Toque en un punto (±13 px, y 534..570) → centra esa página.
* Casita de la central o de una vecina (±20 px) → **página principal** (guarda).
* Papelera de la central (±20 px) → modal **Eliminar** (§10.8).
* Tarjeta "+" → **nueva página** vacía al final (máx. 5; si no, aviso `"Maximo de 5 paginas"`).
* Toque en una vecina → la centra.
* **Arrastre horizontal** (empieza en y 110..550, umbral 14 px): el carrusel sigue al dedo con tope elástico de
  0.42 × 240 ≈ 100 px en los extremos; al soltar cambia si `|slide| > 80` px (1/3 del paso).
* **Reordenar**: pulsación larga **650 ms** (≤ 12 px) sobre la tarjeta central (no bloqueado) → solo esa tarjeta sigue al
  dedo; al soltar se calcula el destino `reorder − round(slide/240)` y se intercambia paso a paso (iconos, widgets y marca
  de principal viajan con la página).

### 10.4 Vista Fondo (`HCV_WALL`)

* Título `"Fondo de pantalla"`; subtítulo `"Integrados de Flex OS e imágenes tuyas"`.
* **8 miniaturas** 100×166 r12 en x = 18 + c·116 (c 0..3), y = 130 + f·186 (f 0..1); nombre tamaño 1 debajo (y+171):
  `"Flex Original"`, `"Aurora"`, `"Nocturno"`, `"Halo"`, `"Onyx"`, `"Oceano"`, `"Violeta"`, `"Naturaleza"`
  (`Wallpaper.h:44`). Seleccionada: borde doble `wallAccent()` + check en círculo r9; el fondo actual del Inicio con punto
  r5 `wallAccent()` abajo-izquierda.
* **Hoja** (12, 506, 456×148) r22: sin selección `"Elige un fondo para ver las opciones"` tamaño 2 `TH_TXT2`; con selección
  `"Aplicar \"%s\" a:"` (o `"tu imagen"`) tamaño 2 y botones primarios 136×42 en y = 544: `"Inicio"` (24), `"Bloqueo"`
  (172), `"Ambos"` (320). Siempre: `"Mis imágenes"` (24, 594, 210×42) y `"Restaurar"` (246, 594, 210×42) secundarios.
* Interruptor `"Aplicar paleta al sistema"` (fila 16, 664, 448×44): calcula acentos del fondo (§02 2.5) o vuelve a los del
  tema.
* Aplicar (`hcApplyWall`): cambia `gWallHome` y/o `gWallLock`, recalcula fondo velado, miniatura y paleta, guarda
  (`homeCfgSave`). `"Restaurar"` = "Flex Original" en Inicio y Bloqueo. Errores en modal: `"Ese fondo no esta disponible"`,
  o el error de imagen.

### 10.5 Vista Mis imágenes (`HCV_PICK`)

* Título `"Mis imágenes"`; subtítulo `"JPEG de /Imagenes, /Camara y /Descargas"`.
* Lista con scroll (área y 128..700) de hasta **24** rutas (`.jpg`, `.JPG`, `.jpeg`, `.JPEG`; hasta 24 entradas leídas por
  carpeta): filas (16, y, 448×52) r14 paso 58 con la ruta tamaño 2 recortada; la seleccionada con borde de acento y botón
  `"Usar"` (354, y+8, 100×36). Tocar una fila la selecciona; tocar `"Usar"` aplica la imagen a **Inicio y Bloqueo** y
  vuelve a "Fondo" si cargó.
* Vacía: `"No hay imagenes JPEG guardadas"` tamaño 2 y = 230 y `"Guarda alguna desde Camara o Archivos"` tamaño 1 y = 256.
* Error de carga bajo la lista (y = 706, tamaño 1, `TH_ERR`): `"Sin imagen elegida"`, `"La imagen ya no esta"`,
  `"Imagen demasiado grande"` (> 512 KB), `"Sin memoria"`, `"No se pudo leer"`, `"No es un JPEG valido"`,
  `"JPEG progresivo no admitido"`, `"Sin PSRAM para el fondo"` (`Wallpaper.h:366-402`).
* Scroll: arrastre vertical que empieza dentro de la lista, sin inercia, acotado.

### 10.6 Vista Temas (`HCV_THEME`)

* Título `"Temas"`; subtítulo `"Temas integrados de Flex OS"`.
* 8 filas (16, 130+i·54, 448×48) r16: muestras (28, y+10, 28×28) r8 acento, (60, y+10, 14×28) r6 acento claro, (78, y+10,
  14×28) r6 fondo `#1E222E` (oscuro) o `#F0F3F9` (claro); nombre tamaño 2 (104, y+15); `"Activo"` tamaño 1 `wallAccent()`
  alineado a x = 448. Nombres: `"Flex Original"`, `"Claro"`, `"Oscuro"`, `"AMOLED"`, `"Oceano"`, `"Violeta"`,
  `"Naturaleza"`, `"Alto contraste"` (definición de cada tema en 02 §2.6).
* Con selección: `"Aplicar"` (24, 570, 210×44) primario y `"Cancelar"` (246, 570) secundario; sin ella:
  `"Elige un tema para verlo y aplicarlo"` tamaño 2. Siempre `"Restaurar tema predeterminado"` (24, 624, 432×42).
* Aplicar (`hcApplyLook`): apariencia, material, estilo de icono, fondo de Inicio **y** Bloqueo, paleta y acentos de una
  vez; si algo no valida, vuelve atrás entero y avisa `"No se pudo aplicar el tema"`. Guarda y propaga (`themeChanged`).

### 10.7 Vista Widgets (`HCV_WIDGETS`)

* Título `"Widgets"`; subtítulo `"Se colocan en la página %d"` (centrada, o la principal si está en "+").
* Lista con scroll agrupada por categoría (cabecera tamaño 2 `TH_ONWALL2` cada vez que cambia: "Reloj", "Sistema", "Reloj",
  "Accesos", "Información" — orden de `WG_REG`), filas 76 px paso 82: **vista previa real** del widget en (26, y+8, 108×60),
  nombre tamaño 2 en (148, y+18), `"%dx%d celdas"` tamaño 1 en (148, y+42); seleccionada → `"Añadir"` (358, y+20, 96×36).
* Añadir → `homeWgAdd`: éxito → normaliza, guarda y vuelve a Páginas; `"Esta pagina ya tiene 6 widgets"`;
  `"Sin espacio: elige otra pagina"`.

### 10.8 Vista Ajustes de inicio (`HCV_SETTINGS`) y modales

Título `"Ajustes de inicio"`. Filas 52 px paso 58 desde y = 132 (con scroll):

| # | Fila | Tipo | Acción |
|---|---|---|---|
| 0 | `"Cuadrícula del inicio"` | valor `"4x3"`… | cicla 4×3 → 5×3 → 4×4 → 5×4 (normaliza sin perder iconos) |
| 1 | `"Tamaño de iconos"` | `"Pequeño"`/`"Normal"`/`"Grande"` | cicla |
| 2 | `"Nombres de aplicaciones"` | interruptor | `gHomeLabels` |
| 3 | `"Bloquear diseño del inicio"` | interruptor | `gHomeLocked` |
| 4 | `"Indicadores de página"` | interruptor | `gHomeDots` |
| 5 | `"Página principal"` | `"Página %d"` | va a Páginas centrada en la principal |
| 6 | `"Gesto de pellizco"` | interruptor | `gHomePinch` |
| 7 | `"Reducir animaciones"` | interruptor | `gHomeReduce` (sin animación de entrada/salida de este modo) |
| 8 | `"Encuadre de la imagen"` | `"Rellenar"`/`"Ajustar"`/`"Centrar"` | cicla `gWallFit` y recarga la imagen si se usa |
| 9 | `"Restablecer diseño del inicio"` | acción | modal Restablecer |

**Modales** (velo `TH_SCRIM` α150; diálogo (32, 300, 416×200) r26 superficie del sistema + borde; título tamaño 3 y = 326;
botones 46 px en y = 438):
* **Eliminar página**: `"Eliminar esta página"`; `"Sus iconos se recolocan solos"` / `"en las paginas que queden"`;
  `"Cancelar"` (48, 438, 184) y `"Eliminar"` (248, 438, 184) primario. Los iconos se recolocan; si se borra la principal, la
  nueva es la de la izquierda. No se puede borrar la última (`"No puedes eliminar la ultima pagina"`).
* **Restablecer**: `"Restablecer el inicio"`; `"Vuelve al reparto de fabrica:"` / `"tres páginas y rejilla 4x3"`;
  `"Cancelar"` / `"Restablecer"` → fábrica (3 páginas, 4×3, icono normal, Clima+Calendario, favoritas de fábrica).
* **Aviso** (`HCM_INFO`): `"Aviso"` + mensaje tamaño 2 en y = 380; botón `"Entendido"` (170, 438, 140×46).

**Persistencia de aspecto** (`homeCfgSave`, `Home.h:1573`): `"wallh"` int, `"walll"` int (0..7 o **200** = imagen),
`"wallfit"` int (0 rellenar, 1 ajustar, 2 centrar), `"wallpal"` bool, `"hlook"` int (0..7), `"wallpb"` bytes[80] (ruta del
JPEG, siempre terminada en NUL). Carga (`homeCfgLoad`, `:1586`) con acotado de valores y fallback al fondo integrado si la
imagen no carga, sin reiniciar.

### 10.9 Gesto de pellizco de dos dedos (`hpzUpdate`, `HomeCfg.h:1109-1206`)

Usa la lectura **multipunto** real del GT911 (ids de seguimiento), solo cuando el chip ya informa ≥ 2 contactos (cuenta
válida 150 ms). Permitido en: escritorio libre (`drawerCanOpen`, sin gesto de página, sin suspensión, sin tarjeta del
cronómetro, sin arrastrar notificación, con `gHomePinch`) para **cerrar**; vista Páginas de Personalizar inicio (sin modal,
animación, arrastre ni reordenación) para **abrir/salir**.

| Parámetro | Valor |
|---|---|
| Separación inicial mínima (cerrar, Inicio) | 140 px |
| Separación inicial mínima (abrir, Personalizar) | 45 px |
| Ventana | 120..700 ms desde el extremo del recorrido (la referencia sigue el máximo al cerrar / el mínimo al abrir) |
| Cerrar → abre Personalizar inicio | distancia² ≤ 52 % de la referencia² (≈ −28 %) **y** ≥ 50 px absolutos |
| Abrir → sale de Personalizar | distancia² ≥ 190 % (≈ +38 %) **y** ≥ 50 px |
| Tercer dedo | cancela y consume |
| Cambio de dedos | se rearma sin disparar |

Con dos contactos válidos el gesto **se traga** todos los eventos (nadie más ve el toque) hasta que se levantan los dos.
LVGL: necesita los frames multipunto de `flex_touch_get_frame()`; implementarlo como "consumidor" previo al indev de LVGL.

### 10.10 Referencias y migración

`hcEnter` `HomeCfg.h:772`; `hcClose` `:792`; `hcBeginExit` `:805`; `hcAnimTick` `:820`; `hcRender` `:742`; vistas
`:510-716`; modales `:718-741`, `:872-888`; toques `:889-1085`; `hcTick` `:1086`; operaciones `homePageAdd/Delete/Swap`,
`homeSetMain/Grid`, `homeResetLayout` `:301-380`; `hcApplyWall` `:383`; `hcApplyLook` `:402`; `hcScanImages` `:431`.
**Lógica pura**: operaciones sobre el modelo (añadir/borrar/intercambiar páginas, principal, rejilla, reset), cálculo del
destino de reordenación, clasificador de pellizco (con distancias y tiempos inyectados), filtro de extensiones JPEG.
Prueba host existente: `testPersonalizarInicio` (`ino_compile.cpp:2957`).
**LVGL**: carrusel = contenedor con scroll horizontal y snap; vistas = sub-contenedores; modales en `lv_layer_top`.
**Rarezas**: textos sin tildes (`"Maximo de 5 paginas"`, `"No puedes eliminar la ultima pagina"`, `"Ese fondo no esta
disponible"`, `"Sus iconos..."`, `"en las paginas que queden"`, `"Vuelve al reparto de fabrica:"`, `"Esta pagina ya tiene
6 widgets"`, `"Sin espacio: elige otra pagina"`, `"No hay imagenes JPEG guardadas"`, `"Oceano"`); el comentario de
`gWallPath` dice NVS `"wallp"` pero la clave real es **`"wallpb"`**.

---

## 11. Caja de aplicaciones (`ST_DRAWER`, `AppDrawer.h:293-1492`)

### 11.1 Cómo se llega y se sale

* **Abrir**: deslizar hacia arriba en el escritorio (inicio en y > 96) cuando `drawerCanOpen()`: vertical, no hospedada, no
  Modo edición, no kiosco, sin OTA, sin cortina, `ST_HOME`. Antes de subir: la isla se oculta conservando su cola y se
  recompone el escritorio si estaba sucio.
* **Animación**: hoja que sube desde abajo **240 ms**, ease-out cúbica, por tiempo, a ~30 fps; la hoja tiene **esquinas
  superiores r28** (se ve el escritorio por los lados). Bajar: la inversa, y la acción pendiente (abrir app / ir a
  Recientes) se ejecuta **al terminar** la bajada.
* **Cerrar**: botones Atrás o Inicio (modo Botones, y > 724) → cierra; Recientes → cierra y abre Recientes; modo Gestos:
  soltar con inicio en y > 756 y recorrido > 30 px → cierra; **deslizar hacia abajo** con la lista arriba del todo
  (scroll ≤ 0.5) o empezando sobre la cabecera (y < 152) → cierra. Con scroll, deslizar abajo desplaza la lista.
* Con OTA dueña de la pantalla la caja ni dibuja ni escucha.

### 11.2 Layout

* **Fondo**: `blurBg` + velo extra `#060810` α96 (compuesto una vez por apariencia). Sin PSRAM: `blurBg` o el escritorio.
* **Cabecera**: hora tamaño 2 en (20, 16), fecha corta tamaño 1 (20, 40) `TH_ONWALL2`, Wi-Fi y batería como Inicio.
* **Buscador** (24, 74, 432×58) r29: Vidrio tinte `TH_GLASS2` (solo en reposo) / Plano `TH_SURF` α150; lupa (círculo r10 +
  mango) en (54, 103) `TH_ONWALL`; consulta tamaño 2 `TH_ONWALL` en (78, 95) o `"Buscar aplicaciones"` `TH_ONWALL2`;
  **ojo** (ver ocultas) en (424, 103), 22 px, `TH_PRIM` si activo / `TH_ONWALL2` tachado si no.
* **Rejilla**: 4 columnas, x = 24 + c·120, filas desde y = 152 con paso 116; iconos **72 px siempre en estilo Plano**;
  etiqueta centrada en y+80, `uiFontFit(nombre, 106, 2)` y corte con `"..."`, `TH_ONWALL` (o `TH_ONWALL2` si atenuada).
  Área visible hasta y = 724 (o 614 con teclado). Lista vacía: `"Sin resultados"` tamaño 3 `TH_ONWALL2` en y = 212.
* **Marcas**: oculta (en modo "ver ocultas") → velo negro α130 sobre el icono y etiqueta atenuada; con **candado** →
  cuadrito (ix+54, iy+54, 16×16) r5 `TH_DANGER` α230; descargada actualizándose → punto r7 en (ix+63, iy+9) `TH_PRIM`;
  descargada con error → punto `TH_DANGER` y atenuada.
* **Barra de navegación**: igual que el escritorio (sin fondo), desplazada con la hoja; oculta con el teclado.
* **Teclado del buscador** (propio, minúsculas): panel negro α120 de y = 622 a 799; teclas 42×46 r10 `TH_SURF` α170 con
  letra tamaño 2 `TH_ONWALL`; filas `qwertyuiop` (x0 = 12, y = 636), `asdfghjkl` (x0 = 35, y = 688), `zxcvbnm` (x0 = 135,
  y = 740); a los lados de la 3.ª fila: **cerrar teclado** (12, 740, 50×46) con chevrón hacia abajo y **borrar** (418, 740,
  50×46) con un guion. Sin espacio ni dígitos. Consulta máx. **14** caracteres.

### 11.3 Contenido, filtro y orden (`drwFilter`, `AppDrawer.h:464`)

1. Apps **nativas** de `APP_REG` (0..18) salvo ocultas (a menos que "ver ocultas").
2. Apps **descargadas** del registro real (`/FlexApps`, máx. **24** = `FLEXPKG_MAX_INSTALLED`) salvo ocultas.
3. Filtro por nombre localizado: **subcadena sin distinguir mayúsculas ASCII** (`dexMatch`, `DeX.h:270`); los acentos no
   se pliegan (`"camara"` no encuentra `"Cámara"`).
4. **Un solo orden por nombre** mezclando nativas y descargadas (`pkgAppNameCmp`: comparación por bytes con mayúsculas ASCII
   plegadas), desempate por tipo y luego índice (estable). Resultado en español: Ajustes, Almacenamiento, Calculadora,
   Calendario, **Clima, Cámara** (la `á` ordena tras todas las ASCII), Device Care, Flex Compass, Flex Phone, Flex Store,
   Galería, Juegos, Modo PC, Multimedia, Música, Navegador, Notas, Paint, Reloj.

### 11.4 Interacción

* Toque en el buscador: zona del ojo (x ≥ 402) → alterna "ver ocultas"; resto → abre/cierra el teclado.
* Teclas: añaden letra, borran o cierran el teclado; cada cambio refiltra y vuelve arriba.
* **Scroll** vertical con inercia: un arrastre que empieza en la rejilla cuenta como movimiento a partir de **16 px**;
  velocidad medida cada ≥ 12 ms; inercia `v ← v·(1 − dt/160)` y parada si |v| < 24 px/s; sin rebote (acotado a
  `[0, filas·116 + 16 − alto_visible]`).
* **Toque** en una celda (zona: 84 px de ancho desde el borde del icono, fila completa de 116 px) → cierra la caja y abre:
  nativa → transición **desde el icono de la caja** (`gIconOvr*`), con candado → verificación; descargada lista → petición a
  Flex Store; descargada no lista → abre su ficha.
* **Pulsación larga 600 ms** (< 12 px, sin scroll) sobre una celda → menú contextual (§11.5).

### 11.5 Menú contextual, ficha y desinstalar

**Menú** (dentro de `ST_DRAWER`): panel 262 × (filas·56) r20 — Vidrio tinte `TH_GLASS` / Plano `TH_SURF` α242; a la
derecha del icono (cx+84) o a la izquierda (cx−274), x acotada a [8, 210], y a [152, 800−8−alto]. Texto
`uiFontFit(..., 186, 3)` en (x+18), glifo 26 px en (x+218); separadores `TH_TXT2` α80.

| Fila | Texto | Activa si | Acción |
|---|---|---|---|
| Abrir | `"Abrir"` | siempre | cierra la caja y abre |
| Inicio | `"Quitar de inicio"` / `"Añadir a inicio"` / `"Inicio completo"` (sin hueco y sin páginas por crear) | está en inicio, o (hay hueco y no oculta) | alterna favorita (nativa) o anclaje (descargada); añadir crea página si hace falta; guarda |
| Ocultar | `"Ocultar"` / `"Mostrar"` | nativa ≠ Ajustes (Ajustes atenuada); descargada siempre | ocultar también la quita de Inicio; guarda |
| Información | `"Información"` | siempre | abre la ficha |
| Desinstalar | `"Desinstalar"` (rojo `TH_DANGER`) | solo descargadas | confirmación |

Toque fuera → cierra; fila inactiva → nada; deslizar arriba/abajo → cierra el menú (no la caja).

**Ficha de información** (344×244 centrada en (68, 278), r24; Vidrio `TH_GLASS` / Plano `TH_SURF` α245; cualquier toque o
deslizamiento la cierra; `"Toca para cerrar"` tamaño 1 abajo):
* Nativa: icono 56 px (+20,+20); nombre (`uiFontFit` ≤ 3) en (+88,+28); categoría tamaño 2 `TH_TXT2` (+88,+54); desde
  y+96 cada 26 px, tamaño 2: `"Id de registro: %d"`, `"En inicio: sí|no"`, `"Visible: sí|no"`, `"Bloqueada: sí|no"`.
* Descargada: `"Descargada"`, `"Version: %s (%u)"`, `"Runtime: flex-app-v1"`/`"Runtime: flex-ui-1"`,
  `"Estado: lista"` o `"Estado: Actualizando"`/`"Estado: No disponible"` (rojo), id del paquete tamaño 1.
* Error de desinstalación: `"No se pudo completar"` tamaño 3 `TH_DANGER` y el motivo del gestor de paquetes.

**Confirmar desinstalación** (348×208 en (66, 296), r24): `"¿Desinstalar?"` tamaño 3; nombre tamaño 2; `"Se borrara la
aplicacion"` / `"y todos sus datos."` tamaño 1 `TH_TXT2`; botones 150×46 en y = 432: `"Cancelar"` (82) `TH_TXT2` α60 y
`"Desinstalar"` (248) `TH_DANGER` texto blanco. Cualquier toque fuera de "Desinstalar" cancela. Desinstalar → suelta su
ranura de Inicio, `flexPkgUninstall(id)` (transacción del gestor), olvida sus preferencias, refiltra, normaliza y guarda.

### 11.6 Apps descargadas en Inicio y caja (`PkgApps.h:280-420`)

* Tabla estable **`pkgpref`** (NVS bytes, 16 ranuras × (id[97] + flags 1) = **1568 B**): bit `0x01` anclada en Inicio, `0x02`
  oculta. Una ranura sin marcas se libera. Inicio guarda `128 + ranura` (nunca un índice de la lista, que se reordena).
* Al desinstalar o dejar de existir, la ranura deja de valer y la normalización la retira del escritorio.
* Estado "actualizando" se muestrea **una vez por cuadro** (no por icono: el mutex de la tienda podría bloquear la UI).

### 11.7 Referencias y migración

Constantes `AppDrawer.h:328-346`; `drwHitCell` `:421`; `drwFilter` `:464`; `drwBuildPage` `:513`; dibujo `:545-759`;
menú `:765-881`; confirmación `:887-916`; ficha `:919-993`; acciones `:1002-1114`; `drawerCanOpen` `:1117`; `drawerOpen`
`:1131`; animación `:1186`; toques `:1213-1290`; `drawerTick` `:1293`.
**Lógica pura**: `drwFilter` (+ `dexMatch`, `pkgAppNameCmp`), `drwHitCell`, `drwMaxScroll`/`drwClampScroll`, inercia
(`drwInertiaStep` con dt), reglas de habilitación del menú, `drwFavToggle`/`drwHideToggle` sobre bitmasks, tabla `pkgpref`.
Pruebas host existentes: `testCajaApps` (`:1337`), `testCajaDescargadas` (`:1544`), `testCajaDescargadasScroll` (`:1792`),
`testCajaDescargadasRegresion` (`:1859`), `testCajaUnificada` (`:1957`).
**LVGL**: contenedor a pantalla completa en `lv_layer_top` animado en `y` (800 → 0, 240 ms ease-out); dentro, `lv_textarea`
de búsqueda + `lv_keyboard` con mapa propio (solo minúsculas) y un contenedor con `LV_FLEX_FLOW_ROW_WRAP` y scroll vertical
con inercia (`scroll_throw` para aproximar el decaimiento de 160 ms). Long-press con `LV_EVENT_LONG_PRESSED` y
`long_press_time = 600`.
**Rarezas / mejoras**: búsqueda sensible a acentos y teclado sin dígitos/espacio/ñ (conviene plegar acentos); orden por bytes
(Cámara tras Clima; conviene `strcoll`-like con plegado); `FLEXDRW_DIAG = 1` (`PkgApps.h:62`) deja trazas por Serie en cada
apertura/scroll — en IDF a nivel `ESP_LOGD`; `gHomeLocked` no impide añadir/quitar de Inicio desde la caja.

---

## 12. Recientes (`ST_SWITCHER`, `FlexOS_Ultra_AppSwitcher.h`)

### 12.1 Cómo se llega

`sysRecents()` (botón Recientes de una app, toque abajo-derecha en Inicio, gesto mantenido de la barra, botón Recientes de la
caja): suspende la app actual (miniatura + guardado diferido) y abre el gestor (`activarMultitarea`). Vetado en kiosco;
hospedada en DeX → Recientes de DeX.

### 12.2 Modelo

* Una tarjeta por app viva (máx. `APP_N`), ordenadas por uso (0 = más reciente). Si la lista está llena, la más antigua se
  **cierra de verdad** (guardando su sesión).
* **Miniaturas** 150×250 RGB565 (73 KB en PSRAM), captura al vecino más cercano de la pantalla al suspender; se conservan
  las **4** más recientes (1 en modo visual eficiente); no se captura si la PSRAM libre < crítico + 73 KB, si la app es
  horizontal (`APP_LAND` o estaba en horizontal) o si el visor muestra algo **protegido**. Sin miniatura: tarjeta con icono.

### 12.3 Layout

* Fondo `blurBg` (o `TH_PAGE`). Título `"Recientes"` tamaño 3 centrado y = 30 `TH_ONWALL`.
* **Carrusel**: tarjetas 260×480 en y = 92, centro x = 240 + i·288 − scroll. Marco `TH_SURF` + borde `TH_BORDER` r22;
  imagen (x+8, y+8, 244×404); sin miniatura: rectángulo `TH_SURF2` r14 con icono 60 px en (cx−30, y+180). Nombre tamaño 2
  `TH_TXT` centrado en y+428 con **punto de cambios sin guardar** r4 `TH_WARN` a la derecha si procede; estado tamaño 1
  `TH_TXT2` en y+452: `"Activa"` (corriendo/reanudando), `"Pausada"`, `"Estado guardado"` (se le soltaron recursos).
* Vacío: `"Sin apps recientes"` tamaño 2 `TH_ONWALL2` y = 332.
* **"Cerrar todas"** (140, 600, 200×46) r23: activo `TH_SURF2` + borde `TH_BORDER` + texto `TH_ONWALL`; sin tarjetas `TH_SURF`
  + `TH_DIV` + `TH_DIS`.
* Pie: `"Desliza una tarjeta arriba para cerrar"` tamaño 1 `TH_ONWALL2` y = 772.
* **Aviso** (1800 ms): cápsula centrada en y = 512, alto 34, r17, `TH_WALLSURF` α235, texto tamaño 2 `TH_ONWALL`:
  `"Tarea en curso: no se cierra"` o `"Sin espacio para guardar"`.
* **Entrada**: 150 ms, *ease-out-back* `1 + 2.6·(p−1)³ + 1.6·(p−1)²`, escala de tarjetas 0.6 → 1 (sin animación en Modo
  seguro). En el original es un bucle bloqueante; en IDF `lv_anim` con `lv_anim_path_overshoot`.

### 12.4 Interacción (`swTick`)

* Al apoyar se decide el gesto: **horizontal** si |dx| > 12 px; **vertical** si |dy| > 14 px.
* Horizontal: desplaza el carrusel con el dedo (rebote de ±90 px más allá de los extremos); al soltar, **inercia**
  (`v ← v·0.90` por vuelta mientras |v| > 0.4) y **enganche** a la tarjeta más cercana (`scroll += (destino − scroll)·0.25`
  por vuelta).
* Vertical sobre una tarjeta: la tarjeta **sube** con el dedo (0..116 px); soltar con > 110 px → **cierra la app de verdad**
  (guarda su sesión, libera); si tiene trabajo esencial en segundo plano o no se pudo guardar, no se cierra y sale el aviso.
* **Mantener pulsada 480 ms** una tarjeta sin mover → **ficha** (§12.5).
* Toque: "Cerrar todas" → si hay apps con cambios sin guardar, confirmación; si no, cierra todas salvo las que tienen trabajo
  en segundo plano (aviso si quedó alguna). y > 740 → Inicio. Tarjeta central → **reanuda** la app (vuelve donde estaba);
  tarjeta lateral → la centra. Cualquier otro sitio → Inicio.

### 12.5 Ficha y confirmación (hoja modal)

Fondo `blurBg` + velo `TH_SCRIM` α150; hoja (28, 250, 424×300) r24 superficie sobre wallpaper (`TH_WALLSURF2` Vidrio /
`TH_WALLSURF` Plano). Botones 188×46 en y = 488: izquierda (44) `TH_SURF2`, derecha (248) `TH_DANGER` texto `TH_ONACC`.
Tocar fuera de la hoja (y < 250 o > 550) la cierra.
* **Ficha**: icono 48 px (48, 268); nombre tamaño 3 (112, 270); estado tamaño 1 (112, 298). Filas desde y = 334 cada 26 px
  (etiqueta tamaño 1 `TH_ONWALL2` a la izquierda, valor tamaño 2 `TH_ONWALL` alineado a x = 432): `"Consumo estimado"`
  (PSRAM medida o `"No disponible"`), `"Clase"` (`"Ligera"`/`"Media"`/`"Pesada"`), `"Miniatura"` (73 KB o `"Sin
  captura"`), `"Última actividad"` (`"hace %u s"`, `"hace %u min"`, `"hace %u h"` o `"No disponible"`), `"Cambios sin
  guardar"` (`"Sí"` en `TH_WARN` / `"No"`). Botones `"Volver"` / `"Cerrar"`.
* **Confirmar cerrar todas**: `"Cerrar todas las apps"` tamaño 2; `"%d app%s tiene%s cambios sin guardar."`;
  `"Flex OS intentará guardarlos antes de cerrar;"`; `"la que no pueda guardarse se queda abierta."` (tamaño 1);
  `"Cancelar"` / `"Cerrar todas"`.

### 12.6 Referencias y migración

`swPush`/miniaturas `AppSwitcher.h:68-120`; `swCloseCard` `:143`; `swCloseAll` `:154`; dibujo `:263-333`; hoja `:347-454`;
`activarMultitarea` `:467`; `swTick` `:498`. Ciclo de vida y memoria en `Core.h` (02 §8.5-8.6).
**Lógica pura**: lista LRU de tarjetas (`swPush`, `swDropCard`, recorte de miniaturas), decisión de gesto (12/14 px),
cierre por umbral, texto de estado y de "última actividad". Prueba host existente: `testMultitareaMemoria` (`:6102`).
**LVGL**: contenedor horizontal con snap center y scroll con momento; cada tarjeta con `lv_image` (miniatura como
`lv_draw_buf` de 150×250) y arrastre vertical propio; `LV_EVENT_LONG_PRESSED` con 480 ms. **No copiar**: inercia y enganche
por vuelta del bucle (dependen de la cadencia) ni la animación de entrada bloqueante.

---

## 13. Navegación del sistema desde el shell

Botones Atrás/Inicio/Recientes, barra de gestos, transiciones de apertura/cierre desde el icono y pantalla completa están en
**02 §8.6-8.10**. Lo propio del shell:
* **Origen de la animación de apertura** (`getIconRect`, `AppFramework.h:207`): el icono **real** en la página visible
  (buscando su ranura en `homeOrder`); si la app está en el dock, su posición en el dock (ids 12..15 literales, ver §6.3); si
  se abrió desde la caja, el rectángulo del icono tocado en la caja (`gIconOvrApp/X/Y/S`, válido solo para esa app y hasta
  volver a Inicio); si no está en la página visible, la casilla que le tocaría por id.
* **`touchDropAll()`** en cada cambio de pantalla (`AppFramework.h:1179`): el dedo que sigue apoyado no genera un toque en la
  pantalla nueva (§14.4).
* `enterHomeState()` (`Core.h:1003`): cierra Personalizar inicio guardando, caduca el origen prestado por la caja, cierra la
  cortina, `ST_HOME`, recompone si sucio. `enterHome()` además cancela transiciones y vuelca el escritorio.

---

## 14. Motor táctil y clasificación de gestos (`FlexOS_Ultra_Touch.h`, `HAL.h:531-638`)

### 14.1 Lectura del GT911

* `gtPoll()` en **cada vuelta**: lee el estado `0x814E` (bit7 = frame nuevo, bits 3..0 = nº de dedos); si hay dedo, lee el
  primer punto `0x8150`; limpia el estado. Devuelve **1** tocando, **0** soltado, **−1** sin dato nuevo. Coordenadas nativas
  0..479 × 0..799 (con `SWAP/FLIP` configurables, hoy 0). Actualiza `gtFingers`/`gtFingersMs` (cuenta de dedos).
* Recuperación del bus tras lecturas fallidas seguidas (nueve pulsos de reloj, reset del GT911 70 ms como mucho una vez por
  segundo), nunca apaga el táctil.
* `gtPollMulti()`: hasta 5 puntos con **id de seguimiento** (byte 7 del bloque); solo para teclado y pellizco.
* **IDF**: `components/flex_touch` ya publica frames multipunto con id en su propia tarea (`flex_touch_get_frame`, caducidad
  100 ms). El clasificador de abajo debe ejecutarse sobre esos frames, antes del indev de LVGL.

### 14.2 Estructura `Touch T` (`Types.h:61-66`) y reglas

`down, pressed (flanco de bajada), released (flanco de subida), tap, moved, swipeUp/Down/Left/Right, x, y, startX, startY,
dx, dy, downMs, lastMs`.
* **Apoyo**: primer frame con dedo → `pressed`, `start = punto`, `downMs`.
* **moved**: > 12 px en x o y desde el inicio.
* **Suelta**: frame con 0 dedos, **o 90 ms** sin ningún frame con el dedo apoyado.

### 14.3 Clasificación al soltar (`tDoRelease`, `Touch.h:332`)

| Resultado | Condición |
|---|---|
| **tap** | `|dx| < 16` y `|dy| < 16` y duración < **550 ms** → además `x, y` **se reponen al punto de apoyo** (el destino se decide al apoyar, como LVGL/Android) |
| **swipeUp / swipeDown** | `|dy| > 55` y `|dy| ≥ |dx|` (signo de dy) |
| **swipeLeft / swipeRight** | `|dx| > 55` y `|dx| > |dy|` |
| (nada) | resto (arrastre corto, pulsación larga) |

### 14.4 Tragado de episodios (`touchDropAll`, `Touch.h:45-57`, `:376-391`)

Al cambiar de pantalla se anulan todos los eventos y se ignoran los siguientes **hasta que el dedo se levante de verdad**
(frame de 0 dedos o 90 ms sin frame con contacto; un poll sin dato nuevo no cuenta). Overlays no modales (banner) usan
`touchHoldBack` para quedarse un episodio sin romper los flancos.

### 14.5 Suspensión por doble toque (`suspGestureUpdate`, `Touch.h:233-304`)

Trabaja sobre la **cuenta de dedos** (no sobre `T`), cuenta válida 90 ms.
* Episodio = desde que baja el primer dedo hasta que se levantan todos. Confirmado de **2 dedos** si hay **2 polls
  consecutivos** con n ≥ 2 (`SUSP_TAP_FRAMES`); 3+ dedos invalida.
* Toque válido: duración ≤ **600 ms** (`SUSP_TAP_MAX_MS`).
* Doble toque: segundo toque dentro de **450 ms** (`SUSP_TAP_WINDOW_MS`) y separado ≥ **45 ms** (`SUSP_TAP_GAP_MS`).
* 2 dedos ×2 → **suspender** (vetado en kiosco, tecleando o tras un pellizco de una app). 1 dedo ×2 → **despertar** (solo
  escuchado con la pantalla suspendida).
* Tragado: suspendido → todo; despierto → los episodios confirmados de 2 dedos (salvo tecleando o si una app es dueña de los
  dos dedos).
* Fundido de backlight: 6 puntos cada 10 ms (≈ 170 ms de 100 a 0), no bloqueante; `DISPOFF` tras llegar a 0.

### 14.6 Orden de filtros en `flexPollTouch` (`Touch.h:341-432`)

1. Restaurar el estado real si un overlay retuvo el episodio anterior.
2. `gtPoll`; **zona excluida del kiosco** → el punto se convierte en "sin dato".
3. Actualización de `T` (apoyo / movimiento / suelta).
4. Tragado de episodio heredado (`touchDropAll`).
5. Detector de suspensión.
6. Detector de pellizco (§10.9) → si se lo traga, se anulan todos los eventos (incluido `down`).
7. Si la suspensión se lo traga, ídem.

### 14.7 Tabla de umbrales del shell

| Gesto | Umbral | Dónde |
|---|---|---|
| Tap | < 16 px, < 550 ms | `Touch.h:337` |
| Swipe | > 55 px en el eje dominante | `Touch.h:338-339` |
| "Movido" | > 12 px | `Touch.h:370` |
| Suelta por silencio | 90 ms | `Touch.h:374` |
| Bordes del sistema | arriba 30 px, izquierda 26 px, derecha 26 px | `Touch.h:41-43` |
| Pulsación larga icono Inicio → menú | > 1000 ms, < 12 px | `HomeCfg.h:1258` |
| Pulsación larga hueco Inicio → Personalizar | > 650 ms, ≤ 12 px | `HomeCfg.h:1285` |
| Pulsación larga en la caja → menú | > 600 ms, < 12 px | `AppDrawer.h:340` |
| Pulsación larga tarjeta Recientes → ficha | ≥ 480 ms sin gesto (≤ 12 x, ≤ 14 y) | `AppSwitcher.h:66` |
| Pulsación larga tarjeta Personalizar → reordenar | > 650 ms, ≤ 12 px | `HomeCfg.h:905` |
| Salida de kiosco | > 1000 ms, < 12 px | `Lock.h:510` |
| Abrir caja | swipeUp con inicio en y > 96 | `HomeCfg.h:1290` |
| Deslizar páginas | ≥ 18 px y ≥ 2× vertical; flick < 320 ms y > 42 px; o > 120 px | `Home.h:1013-1016` |
| Desbloqueo sin clave | > 266 px o swipeUp | `Home.h:1489` |
| Verificación desde bloqueo | > 60 px | `Home.h:1480` |
| Edición: *dwell* / borde | 400 ms / ≤ 34 px durante 700 ms | `Home.h:2176`, `:2035-2036` |
| Personalizar: arrastre / cambio | 14 px / > 80 px | `HomeCfg.h:952`, `:962` |
| Recientes: horizontal / vertical / cierre | > 12 px / > 14 px / > 110 px (tope 116) | `AppSwitcher.h:512-538` |
| Caja: arrastre / parada de inercia | 16 px / 24 px/s | `AppDrawer.h:1263`, `:1288` |
| Barra de gestos | 44 / 12 / 30 px; −0.35 px/ms; 300 ms | 02 §8.9 |
| Pellizco | ver §10.9 | `HomeCfg.h:1123-1127` |
| Doble toque suspensión | ver §14.5 | `Types.h:325-330` |

### 14.8 Notas de migración del táctil

* **Lógica pura reutilizable**: `tDoRelease` (clasificador), máquina de apoyo/suelta con timeout, tragado de episodio,
  detector de doble toque (entrada: nº de dedos + tiempo), pellizco (entrada: dos puntos con id + tiempo), barra de gestos
  (`handleiOSGestures`). Pruebas host existentes: `testTactoGlobal` (`ino_compile.cpp:10143`), `testTactoVisorYBordes`
  (`:9563`), `testPulsacionLargaVidrio` (`:10047`).
* **Riesgo**: LVGL tiene su propio reconocimiento (`LV_INDEV_DEF_SCROLL_LIMIT`, `long_press_time`, `gesture_limit`, *throw*).
  Hay que fijar esos valores por indev y, sobre todo, mantener una **capa de arbitraje previa** (kiosco, tragado,
  suspensión, pellizco, bordes del sistema, barra de gestos) que decida si el frame llega a LVGL. Los widgets LVGL no deben
  ver el contacto que abrió/cerró una pantalla (equivalente a `touchDropAll`: `lv_indev_reset(indev, NULL)` + ignorar hasta
  soltar).

---

## 15. Registro de apps `APP_REG` (`AppFramework.h:739-793`, `Icons.h:70-75`, `Session.h:345-366`)

`APP_N = 19`. El **índice es el id estable** que viaja a NVS (favoritas, ocultas, candados, `homeOrder`, kiosco). Una app
nueva se añade **al final**. Estructura `FlexApp { enter, tick, flags, cat, dflt, hooks }`.

**Banderas**: `APP_CUSTOM_HEADER = 1` (cabecera propia), `APP_OWN_TOUCH = 2` (gestiona sus toques), `APP_LAND = 4`
(dibuja en horizontal), `APP_FLEX = 8` (maqueta contra el lienzo real; admite ventana DeX y pantalla completa),
`APP_BG_KEEP = 16` (trabajo real en segundo plano: no se cierra con "Cerrar todas" ni por memoria mientras dure),
`APP_IMMERSIVE = 32` (ofrece "Pantalla completa" en el menú). **Categorías** (`APP_CAT_NAME`): 0 `"Esenciales"`,
1 `"Multimedia"`, 2 `"Productividad"`, 3 `"Sistema"`, 4 `"Ocio"` (EN: Essentials, Media, Productivity, System, Fun).
**`dflt`**: 1 = favorita de fábrica (rejilla), 0 = no.

| id | `IC_*` | ES | EN | FR | PT | IT | Banderas | Cat. | Fáb. | Peso | Ganchos de ciclo de vida | Fondo del icono |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `IC_RELOJ` | Reloj | Clock | Horloge | Relógio | Orologio | FLEX | Esenciales | sí | ligera | — | `#F5F5F7` |
| 1 | `IC_GALERIA` | Galería | Gallery | Galerie | Galeria | Galleria | FLEX, BG_KEEP | Multimedia | sí | pesada | backLayer, backScreen, suspend, resume, close, bgWork, shed, dirty | `#FFFFFF` |
| 2 | `IC_MULTIMEDIA` | Multimedia | Media | Multimédia | Multimídia | Multimedia | CUSTOM_HEADER, OWN_TOUCH | Multimedia | sí | pesada | backLayer, backScreen, suspend, resume, close, saveSess, loadSess, shed | `#1B5FD9` |
| 3 | `IC_ALMACEN` | Almacenamiento | Storage | Stockage | Armazenamento | Archivi | FLEX | Sistema | sí | ligera | backScreen, suspend, resume, close | `#3B7BD9` |
| 4 | `IC_MODOPC` | Modo PC | PC Mode | Mode PC | Modo PC | Modo PC | CUSTOM_HEADER | Sistema | sí | pesada | suspend, resume, close | `#1E3A6E` |
| 5 | `IC_NOTAS` | Notas | Notes | Notes | Notas | Note | CUSTOM_HEADER, OWN_TOUCH | Productividad | sí | ligera | backLayer, backScreen, suspend, resume, close, saveSess, loadSess, dirty | `#E8A75A` |
| 6 | `IC_NAV` | Navegador | Browser | Navigateur | Navegador | Browser | FLEX, OWN_TOUCH, **IMMERSIVE** | Esenciales | sí | pesada | backLayer, suspend, resume, close, shed | `#2E9BE6` |
| 7 | `IC_BRUJULA` | Flex Compass | Flex Compass | Flex Compass | Flex Compass | Flex Compass | CUSTOM_HEADER, OWN_TOUCH | Esenciales | sí | ligera | backLayer, suspend, resume, close, shed | `#102A60` |
| 8 | `IC_PAINT` | Paint | Paint | Dessin | Paint | Disegno | CUSTOM_HEADER, OWN_TOUCH | Ocio | sí | media | backScreen, suspend, resume, close, saveSess, loadSess, dirty | `#F1E7D2` |
| 9 | `IC_JUEGOS` | Juegos | Games | Jeux | Jogos | Giochi | OWN_TOUCH, CUSTOM_HEADER, **LAND** | Ocio | sí | media | suspend, resume, close | `#8E1E1E` |
| 10 | `IC_AJUSTES` | Ajustes | Settings | Réglages | Ajustes | Impostazioni | CUSTOM_HEADER | Sistema | no | ligera | backScreen, suspend, resume, saveSess, loadSess, bgWork | `#8A8F98` |
| 11 | `IC_CALC` | Calculadora | Calculator | Calculatrice | Calculadora | Calcolatrice | FLEX | Productividad | no | ligera | resume, saveSess, loadSess | `#3A3A3C` |
| 12 | `IC_CALEND` | Calendario | Calendar | Calendrier | Calendário | Calendario | FLEX | Productividad | no | ligera | resume | `#FFFFFF` |
| 13 | `IC_CAMARA` | Cámara | Camera | Appareil | Câmera | Fotocamera | CUSTOM_HEADER, OWN_TOUCH | Multimedia | no | pesada | suspend, resume, close, shed | `#4A4A4E` |
| 14 | `IC_CLIMA` | Clima | Weather | Météo | Clima | Meteo | CUSTOM_HEADER, OWN_TOUCH | Esenciales | no | ligera | backScreen, suspend, resume, shed | `#307CE2` |
| 15 | `IC_FLEXSTORE` | Flex Store | Flex Store | Flex Store | Flex Store | Flex Store | CUSTOM_HEADER, OWN_TOUCH, FLEX | Sistema | sí | media | suspend, resume, close | `#695BE6` |
| 16 | `IC_FLEXPHONE` | Flex Phone | Flex Phone | Flex Phone | Flex Phone | Flex Phone | CUSTOM_HEADER, OWN_TOUCH, FLEX | Esenciales | no | ligera | backScreen, suspend, resume, close | `#2684FF` |
| 17 | `IC_DEVCARE` | Device Care | Device Care | Device Care | Device Care | Device Care | CUSTOM_HEADER, OWN_TOUCH, FLEX | Sistema | sí | ligera | backScreen, suspend, resume, close, saveSess, loadSess, bgWork | `#14847A` |
| 18 | `IC_MUSICA` | Música | Music | Musique | Música | Musica | FLEX, BG_KEEP | Multimedia | no | media | backLayer, backScreen, suspend, resume, close, bgWork, shed | `#EC4C6C` |

Glifos de cada icono: **02 §6.1**. Pesos de memoria (`APP_WEIGHT`, `Core.h:174`) y semántica de cada gancho: 02 §8.5-8.6.
Reglas especiales: **Ajustes nunca se puede ocultar** (`appCanHide`); Modo seguro solo permite 0, 3, 10, 11; la única app
`APP_IMMERSIVE` es el Navegador; la única `APP_LAND` es Juegos (sin miniatura en Recientes).
Favoritas de fábrica (bitmask) = ids 0-9, 15, 17 = `0x283FF`.

**Migración**: en IDF definir el registro como tabla `const` de C con los mismos ids (`enum flex_app_id`), nombres en tabla
de traducciones (no literales en código), y el dock como lista explícita. No usar números literales para referirse a apps
(ver bugs de 02 §2.7 y §6.3 de este documento).

---

## 16. Persistencia: todas las claves NVS de esta área

Namespace **`flexos`** salvo que se indique otro. "Def." = valor si la clave no existe.

| Clave | Tipo | Def. | Significado / rango | Escribe | Lee |
|---|---|---|---|---|---|
| `oobe` | bool | false | OOBE completado | `cfgSaveOobe` (fin de Flex Account) | `cfgLoad` |
| `lang` | int | 0 | 0 ES, 1 EN, 2 FR, 3 PT, 4 IT, 5 ZH (usa textos EN) | OOBE nombre, `cfgSaveOobe`, `cfgSavePrefs` | `cfgLoad` |
| `name` | string | `"FlexOS Ultra"` | nombre del equipo (OOBE ≤ 20; búfer 24) | OOBE | `cfgLoad` |
| `h24` | bool | false | formato 24 h | Ajustes | `cfgLoad` |
| `glass` | bool | false | material Liquid Glass | Ajustes/tema | `cfgLoad` |
| `glasslv` | int | 50 | intensidad del vidrio 0..100 | panel rápido | `cfgLoad` |
| `dark` | bool | true | apariencia oscura | Ajustes/tema | `cfgLoad` |
| `iconstyle` | int | 0 | 0 Plano, 1 Vidrio | Ajustes/tema | `cfgLoad` |
| `bright` | int | 80 | brillo 0..100 | Ajustes | `cfgLoad` |
| `locktype` | int | 0 | 0 deslizar, 1 PIN, 2 contraseña | `flexLockStore`/`Clear` | `cfgLoad`, `flexLockType` |
| `lockslt` | bytes[16] | — | sal PBKDF2 | `flexLockStore` | verificación |
| `lockhsh` | bytes[32] | — | hash PBKDF2-HMAC-SHA256 | `flexLockStore` | verificación |
| `lockitr` | uint32 | 12000 | iteraciones (válido 1..1 000 000) | `flexLockStore` | verificación |
| `locklen` | int | 0 | longitud del PIN (autoconfirmación) | `flexLockStore` | `flexLockLen` |
| `lockpin` / `lockpass` | string | — | **legado en claro**: solo se lee para migrar y se borra | — | `flexLockMigrate` |
| `lockfails` | int | 0 | intentos fallidos acumulados (≥ 0, tope 9999) | `lockFailsSave` | `cfgLoad` |
| `autolockms` | int | 60000 | inactividad: 30000/60000/300000/600000/1800000/0 | Ajustes | `cfgLoad` (normaliza) |
| `poffpin` | bool | false | apagado seguro con clave | Ajustes | `cfgLoad` |
| `applockm` | int | 0 | bitmask de candados por app (**leído como 16 bits: bug**) | `appLockSet` | `cfgLoad` |
| `kioskon` | bool | false | kiosco activo | `kioskSave` | `cfgLoad` |
| `kioskapp` | int | −1 | app clavada | `kioskSave` | `cfgLoad` |
| `kioskx`/`kiosky`/`kioskw`/`kioskh` | int | 0 | área excluida (w = 0 → ninguna) | `kioskSave` | `cfgLoad` |
| `lockwidgets` | int | 1 | bits LW_CLOCK 1, LW_WEATHER 2, LW_CAL 4, LW_NOTIF 8 | Ajustes | `cfgLoad` |
| `navmode` | int | 0 | 0 botones, 1 gestos | Ajustes | `cfgLoad` |
| `animstyle` | int | 0 | transición 0 zoom, 1 fundido, 2 deslizar | Ajustes | `cfgLoad` |
| `hordq` | bytes[100] | fábrica | escritorio, 5 páginas × paso 20 | `homeOrderSave` | `homeOrderLoad` |
| `hordp` | bytes[36] | — | **legado** 3×12 (solo lectura para migrar) | — | `homeOrderLoad` |
| `hord` | bytes[12] | — | **legado** 1×12 (solo lectura) | — | `homeOrderLoad` |
| `hpgn` | int | 3 (centinela −1) | nº de páginas 1..5 | `homeOrderSave` | `homeOrderLoad` |
| `hpmain` | int | 0 | página principal | `homeOrderSave` | `homeOrderLoad` |
| `hgrid` | int | 4×3 (centinela −1) | `(cols<<8)|rows`, cols 4..5, rows 3..4 | `homeOrderSave` | `homeOrderLoad` |
| `hicon` | int | 1 | tamaño de icono 0..2 | `homeOrderSave` | `homeOrderLoad` |
| `hflag` | int | nombres+indicadores+pellizco (centinela −1) | bit0 nombres, bit1 diseño bloqueado, bit2 indicadores, bit3 pellizco, bit4 reducir animaciones | `homeOrderSave` | `homeOrderLoad` |
| `hwg2` | bytes[157] | fábrica (Clima+Calendario) | widgets v2 | `homeOrderSave`, migración | `homeOrderLoad` |
| `hwg` | bytes[82] | — | **legado** widgets v1 (solo lectura) | — | `homeOrderLoad` |
| `appn` | int | 16 | apps que conocía el firmware que guardó | `homeOrderSave` (= 19) | `homeOrderLoad` |
| `appver` | int | 1 | versión del registro de apps (actual 3) | `homeOrderSave` | `homeOrderLoad` |
| `appfav` | int | fábrica (centinela −1) | bitmask favoritas | `homeOrderSave` | `homeOrderLoad` |
| `apphide` | int | 0 (centinela −1) | bitmask ocultas | `homeOrderSave` | `homeOrderLoad` |
| `wallh` | int | 0 | fondo de Inicio 0..7 o 200 (imagen) | `homeCfgSave` | `homeCfgLoad` |
| `walll` | int | 0 | fondo de Bloqueo 0..7 o 200 | `homeCfgSave` | `homeCfgLoad` |
| `wallfit` | int | 0 | encuadre 0 rellenar, 1 ajustar, 2 centrar | `homeCfgSave` | `homeCfgLoad` |
| `wallpal` | bool | false | aplicar paleta del fondo al sistema | `homeCfgSave` | `homeCfgLoad` |
| `hlook` | int | 0 | tema integrado 0..7 | `homeCfgSave` | `homeCfgLoad` |
| `wallpb` | bytes[80] | "" | ruta del JPEG del fondo | `homeCfgSave` | `homeCfgLoad` |
| `pkgpref` | bytes[1568] | ceros | 16 × (id[97] + flags) apps descargadas: ancladas/ocultas | `pkgPrefsSave` | `pkgPrefsLoad` |
| `cleanoff` | bool | false | el último apagado fue limpio (se borra al arrancar) | apagado | `setup` |
| `fxvpurge` | int | 0 | purga única de la carpeta segura retirada hecha | `setup` | `setup` |
| `apps3rd` | int | 0 | máscara legada de terceros (Modo seguro) | Modo seguro | Modo seguro |
| `kb*` | varios | — | preferencias del teclado (otro documento) | — | `cfgLoad` |

Otros namespaces: **`flexsafe`** (`fails` int, `cause` int); **`flexreset`** (`pending` bool, `ver` int, `stage` int,
`err` int); **`flexos_time`** (`epoch` u64, `lastsync` u64).

**Archivos LittleFS** usados por el shell: `/System/Sessions` y `/System/Cache` (creados al arrancar; sesiones de app con
cabecera `SessHdr {magic 0x584C4631, ver, app, len, crc32}`, escritura atómica `.tmp` + verificación + renombrado, máx.
1024 B, 02 §8.12); imágenes de fondo leídas de `/Imagenes`, `/Camara`, `/Descargas` (máx. 512 KB, JPEG baseline).

**Migración de NVS**: si se quiere que una placa que pase de Arduino a IDF **conserve** su configuración, el nuevo `flex_kv`
debe leer el mismo namespace y los mismos formatos binarios (blobs `hordq`, `hwg2`, `pkgpref`; `Preferences` de Arduino usa
`nvs_get_blob`/`nvs_get_i32`/`nvs_get_u8` para bool, `nvs_get_str`, `nvs_get_u32`, `nvs_get_u64` — tipos que deben
coincidir o la lectura falla). Si no se conserva, documentarlo como "primer arranque" y no leer claves legadas.

---

## 17. Cadenas e idiomas

* Idiomas (`NLANG = 6`): ES, EN, FR, PT, IT, ZH. Las tablas tienen **5 columnas** (ES, EN, FR, PT, IT); **chino usa la
  columna inglesa** (`LI()`) porque no hay glifos CJK.
* Tabla general `CH[S_NSTR][5]` (`Session.h:315-341`) — las usadas por esta área: `S_SELLANG`, `S_CONTINUE`, `S_YOURNAME`,
  `S_NAMEHINT`, `S_SWIPE` ("Desliza arriba para desbloquear" / "Swipe up to unlock" / "Glisse vers le haut" / "Deslize para
  desbloquear" / "Scorri per sbloccare"), `S_WEATHER` ("Clima"/"Weather"/"Météo"/"Clima"/"Meteo"), `S_NOEVENTS`
  ("(Sin eventos)"/"(No events)"/"(Aucun événement)"/"(Sem eventos)"/"(Nessun evento)"), `S_NOTIFS`
  ("Notificaciones"/"Notifications"/"Notifications"/"Notificações"/"Notifiche"), `S_NONOTIFS` ("(Sin notificaciones)"/
  "(No notifications)"/"(Aucune notification)"/"(Sem notificações)"/"(Nessuna notifica)"). Sin uso en el código actual:
  `S_START` ("Comenzar"…), `S_WELCOME` ("Bienvenido a"…), `S_BACK` ("Volver"…).
* Nombres de apps `APP[19][5]` (§15), categorías `APP_CAT_NAME` (§15), días y meses (`Session.h:369-396`), textos de clima
  `WXT` (`WeatherKit.h:117`).
* **Todo lo demás del shell está solo en español** y escrito en el código (menús, Personalizar inicio, caja, Recientes,
  clave, kiosco, Flex Account). En IDF conviene moverlo a la tabla de traducciones aunque de momento solo tenga ES, y
  decidir si se corrigen las tildes que faltan (lista en §3.4, §5.15, §10.10, §11.7).

---

## 18. Notas de migración globales

### 18.1 Lógica pura reutilizable (a `components/flex_portable` o un `flex_shell_model` sin LVGL) y sus pruebas

| Módulo | Contenido | Prueba host |
|---|---|---|
| Clave del sistema | `FlexOS_Passcode.cpp` íntegro (cambiar `Preferences` por `flex_kv`, `esp_fill_random`) | `tests/host/test_passcode.cpp` (portar) |
| Reloj | época UTC, Hinnant, `clkStr12/Bar`, fechas localizadas | nueva: fechas límite, 12/24 h |
| Modelo del escritorio | rejilla, ranuras, páginas, normalización, migraciones v1/v2, lectura de claves legadas | `testPaginasHome`, `testRejillaAutoPaginas`, `testDeslizarPaginas` |
| Widgets | máscaras, validación, colocación, cambio de página, normalización, (de)serialización v1/v2 | `testWidgetsDePagina` |
| Personalizar inicio | operaciones de páginas, reordenación, pellizco | `testPersonalizarInicio` |
| Caja | filtro, orden, hit-test, scroll/inercia, menú, `pkgpref` | `testCajaApps`, `testCajaDescargadas*`, `testCajaUnificada` |
| Recientes | lista LRU, miniaturas racionadas, decisiones de gesto | `testMultitareaMemoria` |
| Táctil | `tDoRelease`, timeout, tragado, doble toque, pellizco, barra de gestos | `testTactoGlobal`, `testTactoVisorYBordes`, `testPulsacionLargaVidrio` |
| Bloqueo reforzado | penalizaciones, cuenta atrás, sacudida, `lsuAfter` | `testDesbloqueoFluido` (+ nuevas) |
| Modo seguro / reset | contador, marcador transaccional | nueva |

Todas esas pruebas están en `tests/host/ino_compile.cpp` del árbol Arduino (compilan el .ino entero); al portar, extraer
cada bloque a un módulo C con su propio test en `FlexOS_Ultra_IDF/tests/host`.

### 18.2 Qué depende de Arduino / del motor y cómo hacerlo en IDF + LVGL

* **Composición manual por bandas** (`homeBuf`, `hpBuf`, `hpBg`, `hgBd`, máscaras, `lockBuf`, `blurBg`, `drwPage`,
  `authSnap`, `hcThumb`, miniaturas): desaparece; cada pantalla es un árbol de objetos LVGL. Lo que sí se conserva son las
  **reglas visuales**: el vidrio de los widgets muestra su fondo real durante el deslizamiento; los fondos velados son
  wallpaper + velo; las animaciones tienen las duraciones y curvas de este documento.
* **Animaciones bloqueantes** (`animateTo`, entrada de Recientes, splash, banda forense) → `lv_anim`.
* **Animaciones por cuadro** (muelle de edición, inercia/enganche de Recientes, temblor) → `lv_anim` por tiempo.
* **`Preferences`** → `flex_kv`; mantener una sola escritura por operación (nunca por cuadro).
* **PBKDF2 a plazos** → tarea de verificación + evento al bus; la UI muestra el último punto antes de empezar.
* **`WiFi.status()`** en la UI → estado publicado por `flex_wifi`.
* **Multitáctil** (`gtPollMulti`) → `flex_touch_get_frame()`.
* **Estado global `gState`** → gestor de pantallas con pila mínima y destinos explícitos (`lsuAfter`, `AccountReturn`,
  `drwPendApp`) en vez de variables sueltas.

### 18.3 Bugs y rarezas que **no** hay que copiar (resumen)

1. **Dock con ids literales 12..15** → hoy muestra Calendario, Cámara, Clima y Flex Store (Flex Store duplicada; Ajustes y
   Calculadora fuera de Inicio). Decidir el dock y hacerlo lista explícita (§6.3).
2. **`applockm` leído como `uint16_t`** → se pierden los candados de las apps 16-18 al reiniciar (§5.13).
3. **No se puede quitar la clave** (volver a "Deslizar") ni se pide la clave actual para cambiarla (§5.15).
4. Sin PSRAM el deslizamiento de páginas **no cambia de página** (§6.6).
5. Animaciones bloqueantes y dependientes de la cadencia (§18.2).
6. En el escritorio, el toque abajo-derecha abre Recientes también en modo Gestos (donde no hay botón).
7. Búsqueda de la caja sensible a acentos y orden por bytes (§11.7).
8. Batería fija al 82 %; calendario del bloqueo sin eventos reales; SSID calculado y no mostrado en el widget Wi-Fi.
9. Flex Account: URL de activación en el código; sondeo de `WiFi.status()` cada 80 ms; dos botones idénticos (§3.4).
10. Reiniciar durante el OOBE vuelve al idioma y lo resetea a español (§3.4).
11. Diagnóstico `FLEXDRW_DIAG = 1` activo (trazas por Serie en la caja).
12. Textos sin tildes y "kiosko"/"Kiosco" inconsistentes.
13. `gHomeLocked` no protege añadir/quitar de Inicio desde la caja.

### 18.4 Riesgos principales de la migración

1. **Arbitraje táctil**: el original encadena consumidores en un orden preciso (kiosco → tragado → suspensión → pellizco →
   isla/banner → centro de notificaciones → cortina → pantalla) con umbrales propios; LVGL trae su reconocimiento de scroll,
   gesto y pulsación larga. Sin una capa de arbitraje previa al indev aparecerán conflictos (página vs caja vs pulsación
   larga vs pellizco vs barra de gestos) y toques fantasma al cambiar de pantalla.
2. **Compatibilidad de datos e ids**: formatos binarios (`hordq` con paso 20, `hwg2` 157 B, `pkgpref` 1568 B), centinelas
   −1, migraciones de registro v1/v2 y tipos NVS de Arduino; los ids `IC_*` son contrato. Los bugs de ids literales (dock,
   `themeChanged`) y el truncado de `applockm` no deben heredarse.
3. **Rendimiento del vidrio en movimiento**: deslizar páginas con widgets de vidrio, la caja subiendo, el menú contextual y
   Recientes se diseñaron con cachés y bandas para sostener 30-60 fps en 480×800; en LVGL el desenfoque por objeto en cada
   cuadro no es viable: hace falta un *backdrop* pre-desenfocado compartido y materiales planos durante las animaciones.

---

## 19. Checklist de funcionalidades

| Funcionalidad | Archivo | Detalle clave |
|---|---|---|
| Secuencia de arranque | `FlexOS_Ultra.ino:349-579` | migrar clave → `cfgLoad` → FS → escritorio → reloj → splash; nada de red |
| Filtro de encendido 3 s | `FlexOS_Ultra_Power.h:1056` | 3000 ms sostenidos en 4200 ms; si no, deep sleep |
| SOS de panel / sin PSRAM | `FlexOS_Ultra.ino:370-380` | parpadeo backlight 150/150 ms |
| Banda forense | `FlexOS_Ultra_Home.h:262` | solo reinicio anormal; 2200 ms; textos exactos |
| Splash | `FlexOS_Ultra_Home.h:278-320` | 600/1400/600 ms; puntos cada 320 ms |
| Destino tras splash | `FlexOS_Ultra_Home.h:302-316` | seguro → OOBE → kiosco → bloqueo |
| Modo seguro en arranque | `FlexOS_Ultra_Session.h:133-210` | 3 reinicios anormales; 60 s estable; 4 apps permitidas |
| Marcador de restablecimiento | `FlexOS_Ultra_Session.h:226-298` | se borra al entrar en el OOBE |
| Reloj del sistema | `FlexOS_Ultra_Clock.h` | UTC−5 Lima; semilla 4-jul-2026 13:23; formatos barra/grande |
| OOBE idioma | `FlexOS_Ultra_Home.h:323-366` | 6 filas, "Chinese", cambio en vivo |
| OOBE nombre | `FlexOS_Ultra_Home.h:369-438` | QWERTY mayúsculas, 20 car., vacío → "FlexOS Ultra" |
| OOBE Flex Account | `FlexOS_Account_Bridge.h` | 4 estados, código de dispositivo, desvincular, 3 vías de entrada |
| Wi-Fi desde la cuenta | `FlexOS_Ultra_Network.h:726` | vuelve con el mismo destino |
| Pantalla de bloqueo | `FlexOS_Ultra_Home.h:502-545` | reloj grande, fecha, 3 widgets, asa, aviso de robo |
| Widgets del bloqueo | `FlexOS_Ultra_Prefs.h:138-142`, `AppWeather.h:1365` | bits `lockwidgets`, clima real, calendario maqueta |
| Desbloqueo físico | `FlexOS_Ultra_Home.h:1415-1500` | > 266 px o swipe; 200 ms ease-out |
| Paso a verificación | `FlexOS_Ultra_Home.h:1464-1482` | > 60 px; vuelve a la app tras suspensión |
| Selector PIN/Contraseña | `FlexOS_Ultra_Power.h:283` | dos botones 400×120 |
| PIN | `FlexOS_Ultra_Power.h:300-509` | 4..8 dígitos, autoconfirmación, teclado 3×4 |
| Contraseña | `FlexOS_Ultra_Power.h:357-577` | teclado del sistema, 4..63 bytes, slide 300 ms |
| Transición de seguridad | `FlexOS_Ultra_Lock.h:99-187` | 190 + 230 ms smoothstep |
| Verificación a plazos | `FlexOS_Passcode.cpp:217-284` | 1500 iteraciones/vuelta de 12000 |
| Sacudida al fallar | `FlexOS_Ultra_Lock.h:205-220` | 180 ms, 12 px, 3 ciclos |
| Espera progresiva | `FlexOS_Ultra_Lock.h:222-336` | 4-5 → 30 s; ≥ 6 → 5 min; persiste |
| Revelado del escritorio | `FlexOS_Ultra_Power.h:147-190` | 400 ms fundido + temblor 6 px |
| Salir de la verificación | `FlexOS_Ultra_Power.h:74-124` | vuelve al bloqueo/app/ajustes según destino |
| Hash de la clave + migración | `FlexOS_Passcode.cpp` | sal 16 B, PBKDF2 32 B, 3 pasos |
| Autobloqueo | `FlexOS_Ultra_Lock.h:343-396` | 6 opciones, def. 1 min, vetos |
| Suspensión y despertar | `FlexOS_Ultra_Touch.h:102-304` | doble toque 2/1 dedos, 450/45/600 ms |
| Bloqueo al despertar | `FlexOS_Ultra_Power.h:633` | compuesto a oscuras, vuelve a la app |
| Candado por app | `FlexOS_Ultra_Prefs.h:322-334` | verificación para poner/quitar/abrir; bug 16 bits |
| Modo kiosco | `FlexOS_Ultra_Lock.h:398-622`, `Touch.h:59-100` | área excluida, candado, salida 1 s, persiste |
| Escritorio: modelo | `FlexOS_Ultra_Home.h:45-212` | 5 páginas × 20, descargadas 128+n |
| Escritorio: rejilla | `FlexOS_Ultra_Home.h:641-676` | 4×3/5×3/4×4/5×4, iconos 60/72/84 |
| Barra de estado, dock, nav | `FlexOS_Ultra_Home.h:595-604`, `:910-947` | dock ids 12..15 (bug) |
| Indicadores de página | `FlexOS_Ultra_Home.h:757` | casita principal, punto que sigue al dedo |
| Prioridad de toques en Inicio | `FlexOS_Ultra_HomeCfg.h:1228-1325` | 1000/650 ms, caja y > 96, Recientes abajo-dcha |
| Deslizar páginas | `FlexOS_Ultra_Home.h:981-1400` | 18 px, 2:1, flick 320/42, 120 px, 190 ms |
| Destello de icono | `FlexOS_Ultra_Widgets.h:593-627` | solo iconos Vidrio, 500 ms, r 70 |
| Normalización del escritorio | `FlexOS_Ultra_Home.h:1646` | rescate, crear página, perder favorita si lleno |
| Escritorio de fábrica | `FlexOS_Ultra_Home.h:88-92`, `Widgets.h:486` | 12 apps + Clima + Calendario |
| Migración del registro v1/v2 | `FlexOS_Ultra_Home.h:93-124`, `:1832-1870` | mapas, candado de Code IDE no heredado |
| Persistencia del escritorio | `FlexOS_Ultra_Home.h:1540-1567` | `hordq`, `hpgn`, `hgrid`, `hflag`, `hwg2`… |
| Catálogo de widgets | `FlexOS_Ultra_Widgets.h:60-74` | 10 tipos útiles, id 7 retirado |
| Geometría de widgets | `FlexOS_Ultra_Widgets.h:174` | fila 0 = cabecera 72..192 |
| Dibujo de widgets | `FlexOS_Ultra_Widgets.h:186-271` | textos exactos por tipo; clima compacto/ancho |
| Datos de widgets | `FlexOS_Ultra_Widgets.h:118-156` | cada 2 s; almacenamiento cada 10 s; clima por generación |
| Validación de widgets | `FlexOS_Ultra_Widgets.h:337-482` | única puerta `homeWgPlaceOk` |
| Serialización de widgets | `FlexOS_Ultra_Widgets.h:497-539` | v2 157 B, v1 82 B migrable |
| Acciones de widgets | `FlexOS_Ultra_HomeCfg.h:1320-1323` | Cámara, Clima, Calendario abren su app |
| Modo edición | `FlexOS_Ultra_Home.h:1501-2197` | 89 %, ±2 px, dwell 400, borde 34 px/700 ms |
| Mover/redimensionar widgets | `FlexOS_Ultra_Home.h:2051-2144` | por celdas, asa ±22 px, insignia ±14 px |
| Menú contextual Inicio | `FlexOS_Ultra_AppDrawer.h:30-291` | 3-5 filas, 150 ms, filas inertes sin clave |
| Personalizar inicio: entrada/salida | `FlexOS_Ultra_HomeCfg.h:772-867` | 180 ms reducción; guarda al salir |
| Personalizar: páginas | `FlexOS_Ultra_HomeCfg.h:510-553`, `:889-967` | añadir, borrar, reordenar 650 ms, principal |
| Personalizar: fondo | `FlexOS_Ultra_HomeCfg.h:555-590`, `:980-1004` | 8 fondos, Inicio/Bloqueo/Ambos, paleta |
| Personalizar: mis imágenes | `FlexOS_Ultra_HomeCfg.h:431-450`, `:592-617` | 3 carpetas, 24 máx., errores exactos |
| Personalizar: temas | `FlexOS_Ultra_HomeCfg.h:402-427`, `:619-640` | 8 temas, aplicar/cancelar/restaurar |
| Personalizar: widgets | `FlexOS_Ultra_HomeCfg.h:642-680`, `:1036-1057` | vista previa real, añadir a la página |
| Personalizar: ajustes | `FlexOS_Ultra_HomeCfg.h:684-716`, `:1058-1085` | 10 filas |
| Personalizar: modales | `FlexOS_Ultra_HomeCfg.h:718-741`, `:872-888` | eliminar, restablecer, aviso |
| Pellizco | `FlexOS_Ultra_HomeCfg.h:1109-1206` | 140 px, −28 % y 50 px; +38 % para salir |
| Caja: apertura/cierre | `FlexOS_Ultra_AppDrawer.h:1117-1210` | 240 ms ease-out cúbica, acción tras bajar |
| Caja: rejilla y marcas | `FlexOS_Ultra_AppDrawer.h:645-724` | 4 col., 116 px, candado, oculta, estado paquete |
| Caja: búsqueda | `FlexOS_Ultra_AppDrawer.h:566-617`, `:1213-1249` | teclado minúsculas, 14 car., ojo de ocultas |
| Caja: filtro y orden | `FlexOS_Ultra_AppDrawer.h:464-503` | subcadena ASCII, orden por nombre mezclado |
| Caja: scroll con inercia | `FlexOS_Ultra_AppDrawer.h:1250-1290` | 16 px, decaimiento 160 ms, 24 px/s |
| Caja: menú contextual | `FlexOS_Ultra_AppDrawer.h:765-881` | Abrir, Inicio, Ocultar, Información, Desinstalar |
| Caja: ficha | `FlexOS_Ultra_AppDrawer.h:919-993` | nativa / descargada / error |
| Caja: desinstalar | `FlexOS_Ultra_AppDrawer.h:887-916`, `:1044-1060` | confirmación con nombre; transacción del gestor |
| Apps descargadas ancladas | `FlexOS_Ultra_PkgApps.h:280-420` | `pkgpref` 16 ranuras |
| Recientes: modelo y miniaturas | `FlexOS_Ultra_AppSwitcher.h:36-120` | 150×250, 4 máx., sin captura protegida/horizontal |
| Recientes: carrusel | `FlexOS_Ultra_AppSwitcher.h:263-333` | 260×480, paso 288, estado y punto sin guardar |
| Recientes: gestos | `FlexOS_Ultra_AppSwitcher.h:498-574` | 12/14 px, cierre > 110 px, 480 ms ficha |
| Recientes: cerrar todas | `FlexOS_Ultra_AppSwitcher.h:151-172` | respeta trabajo en segundo plano; confirma si hay cambios |
| Recientes: ficha | `FlexOS_Ultra_AppSwitcher.h:363-427` | consumo medido, clase, miniatura, actividad |
| Origen de la animación de apertura | `FlexOS_Ultra_AppFramework.h:207` | icono real / dock / caja |
| Tragar episodio al cambiar pantalla | `FlexOS_Ultra_AppFramework.h:1179`, `Touch.h:376` | hasta soltar de verdad |
| Lectura GT911 | `FlexOS_Ultra_HAL.h:531-638` | 1/0/−1, multipunto con id |
| Clasificación de gestos | `FlexOS_Ultra_Touch.h:332-340` | tap 16 px/550 ms; swipe 55 px |
| Orden de filtros táctiles | `FlexOS_Ultra_Touch.h:341-432` | kiosco → tragado → suspensión → pellizco |
| Arbitraje global del bucle | `FlexOS_Ultra.ino:670-1017` | orden de consumidores; ritmo 1/5 ms |
| Registro de apps | `FlexOS_Ultra_AppFramework.h:739-812`, `Icons.h:70-75` | 19 apps, banderas, categorías, fábrica |
| Nombres de apps (5 idiomas) | `FlexOS_Ultra_Session.h:345-366` | chino = inglés |
| Cadenas de interfaz | `FlexOS_Ultra_Session.h:308-342` | tabla `CH` |
| Claves NVS del shell | §16 | namespaces `flexos`, `flexsafe`, `flexreset`, `flexos_time` |
