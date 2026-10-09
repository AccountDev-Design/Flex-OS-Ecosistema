# 01b · Shell: panel rápido, notificaciones, barra de estado, navegación, teclado, energía, recuperación y núcleo

> **Estado del documento: PARCIAL.** Escritos §0-§5 (núcleo, táctil, barra de estado, navegación, panel rápido).
> Pendientes: notificaciones, teclado, energía, recuperación, kiosco, memoria. Se completan antes de migrar cada módulo.

> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical, MIPI-DSI,
> táctil GT911) la parte "shell B" de `FlexOS_Ultra/`: todo lo que el sistema dibuja **por encima** de las apps
> (panel rápido, banner y Centro de notificaciones, barra de estado, barra de navegación y gestos, teclado), lo que
> gobierna la energía (suspensión, apagado completo), la recuperación (Modo seguro, restablecimiento de fábrica,
> Modo Kiosco), el núcleo (bucle `loop()`, estados `ST_*`, sesiones, preferencias, idiomas, memoria y "Optimizar
> Flex OS"). El motor de dibujo propio **no se reutiliza**: aquí se describe *qué se ve* y *cómo se comporta*, con
> las medidas exactas del código.

**Fuentes leídas por completo:** `FlexOS_Ultra.ino`, `FlexOS_Ultra_Core.h`, `FlexOS_Ultra_QuickPanel.h`,
`FlexOS_Ultra_QuickPanelEdit.h`, `FlexOS_Ultra_Notif.h`, `FlexOS_Ultra_Keyboard.h`, `FlexOS_Ultra_KeyboardSettings.h`,
`FlexOS_Ultra_System.h`, `FlexOS_Ultra_Recovery.h`, `FlexOS_Ultra_Power.h`, `FlexOS_Ultra_Session.h`,
`FlexOS_Ultra_Prefs.h`, `FlexOS_Ultra_Text.h`, `FlexOS_Ultra_AppFramework.h` (barra de estado, navegación, transiciones,
ciclo de vida), y además — porque el comportamiento de esta área vive allí — `FlexOS_Ultra_QuickPanelGlass.h`
(render y gestos del panel), `FlexOS_FlexPhone_Overlay.h` (banner, Centro de notificaciones, No molestar),
`FlexOS_Ultra_Touch.h` (táctil, bordes del sistema, suspensión, kiosco), `FlexOS_Ultra_Shell.h` (enum `ST_*`),
la sección de kiosco y auto-bloqueo de `FlexOS_Ultra_Lock.h`, `FlexOS_Mem.h`, y las partes de `Types.h`, `Theme.h`,
`AppChrono.h` (cápsula), `Home.h`/`HomeCfg.h` (barra de estado y botones en Inicio), `AppDrawer.h` (menú contextual →
kiosco), `Conn.h` (subtítulos de red), `AppSettings.h` (filas que abren esta área) y `FlexOS_FS.cpp`
(`flexFsWriteBinAtomic`, `flexFsFactoryErase`).

**Documentos hermanos:** el material Liquid Glass, la tipografía, los iconos y el marco de app están en
`02_diseno_visual_liquid_glass.md` (se cita como **[02 §x]**). Bloqueo/OOBE/Inicio/Recientes/Caja de apps pertenecen a
otros documentos del shell; aquí solo se describe su contacto con esta área.

Las rutas `archivo:línea` son relativas a `FlexOS_Ultra/` salvo que se diga otra cosa.

---

## 0. Convenciones

### 0.1 Coordenadas y texto

* Pantalla física **480×800**, origen arriba-izquierda. `SCR_W=480`, `SCR_H=800` (`Types.h:289`).
* En los textos, la `y` de `drawText(x, y, ...)` es el **tope de las mayúsculas**, no la caja de línea [02 §0, §5.1].
* "Tamaño N" de fuente: N=1 es el mapa de bits 5×7 nítido (7 px de alto, avance 6 px); N≥2 es **Outfit Regular**
  escalada con caja de línea **8·N px** (2→16, 3→24, 4→32, 5→40, 6→48). Equivalencias LVGL: [02 §5.1].
* `drawTextC` centra en `x`, `drawTextR` alinea a la derecha en `x`, `drawTextClip(x,y,s,size,col,maxRight)` recorta
  por la derecha, `fgTextEllipsis` recorta con "…".

### 0.2 Mezcla y alfa

`mix565(a, b, t)`: `t=0` → `a`, `t=255` → `b` (misma escala que `opa` de LVGL). `fillRoundRectA(..., a)` = relleno
con opacidad `a/255`.

### 0.3 Tokens de color (`Theme.h:114-171`)

Toda la UI de esta área usa los tokens semánticos. `TH()` = paleta oscura si `gDark`, clara si no.

| Token | Oscuro | Claro | Uso en esta área |
|---|---|---|---|
| `TH_PAGE` / `TH_WIN` | `#12141C` | `#F4F7FB` | fondo de pantallas (Centro, Ajustes del teclado), velo del panel |
| `TH_SURF` | `#222632` | `#FFFFFF` | tarjetas planas, hoja de Notas |
| `TH_SURF2` | `#303648` | `#ECF0F8` | tarjeta secundaria, píldora DND apagada |
| `TH_GLASS` | `#303648` | `#F6F8FC` | tinte de vidrio de tarjetas |
| `TH_GLASS2` | `#28325A` | `#E0E8F6` | tinte de vidrio del panel rápido |
| `TH_TXT` | `#F0F2F8` | `#14161E` | texto principal |
| `TH_TXT2` | `#A0A6B6` | `#606674` | texto secundario |
| `TH_MUTE` | `#787E8E` | `#848A98` | ayudas, asas |
| `TH_NAV` | `#E8ECF6` | `#222632` | iconos de la barra de estado del marco de app |
| `TH_BORDER` | `#424A5E` | `#CAD2E0` | bordes |
| `TH_DIV` | `#343A4A` | `#E0E5EE` | divisores |
| `TH_DIS` | `#7C8292` | `#848A96` | filas desactivadas |
| `TH_TRACK` | `#383E56` | `#CAD0DC` | pista de deslizadores |
| `TH_SEL` | `#305CA8` | `#B2CEF6` | selección de texto |
| `TH_SCRIM` | `#080A12` | `#222836` | velo de apagado |
| `TH_SHADOW` | `#000000` | `#465064` | sombras |
| `TH_ONACC` | `#FFFFFF` | `#FFFFFF` | texto sobre acento |
| `TH_PRIM` | `#3C6EEB` | `#2D5FE1` | acento primario |
| `TH_DANGER` | `#C84646` | `#C43434` | acciones destructivas |
| `TH_OK` | `#5AC88C` | `#168C58` | etapa hecha |
| `TH_WARN` | `#F0B45A` | `#B0740C` | avisos |
| `TH_ERR` | `#EB5050` | `#CC2E2E` | subrayado ortográfico, zona excluida |
| `TH_ACCS` | `#8CB4FA` | `#285CBE` | acento de avisos del sistema en el Centro |
| `TH_KEYPANEL` | `#1E222E` | `#DEE2EC` | panel del teclado (plano) |
| `TH_KEYFACE` | `#343846` | `#FFFFFF` | tecla |
| `TH_KEYALT` | `#424656` | `#D6DBE6` | tecla de función |
| `TH_ONWALL` / `TH_ONWALL2` | `#FFFFFF` / `#D7DEEE` | igual | texto sobre fondo de pantalla |
| `TH_WALLSURF` / `TH_WALLPANEL` | `#2C365C` / `#24283A` | igual | superficies sobre fondo de pantalla |

`thCard()` = `TH_GLASS` con Liquid Glass, `TH_SURF` sin él; `thCard2()` = `TH_GLASS2` / `TH_SURF2` (`Theme.h:236-237`).
`wallAccent()` = acento del usuario (puede venir del fondo; ver [02 §2]).
`uiSurface(x,y,w,h,r,UIS_ELEVATED)` = superficie del sistema: vidrio con Liquid Glass, sólido plano sin él [02 §4].

### 0.4 Interruptores de compilación relevantes (`Types.h`)

`SUSPEND_ON=1`, `SUSPEND_LOCK_ON=1`, `POWEROFF_ON=1`, `POWEROFF_PIN_ON=1`, `PANEL_DCS_SLEEP_ON=1` (`:311-322`),
`KIOSK_ON=1` (`Touch.h:61`), `KB_SIZE_CONFIG_ON`, `KB_MULTITOUCH_ON`, `KB_TOOLBAR_ON`, `KB_CLIPBOARD_MULTI_ON`,
`KB_SETTINGS_ON`, `KB_AUTOCOMPLETE_ON`, `KB_ANIM_POLISH_ON` = 1 (`Types.h:184-190`), `LOCK_FAILS_ON`, `LOCK_SHAKE_ON`,
`AUTOLOCK_ON`, `CTXMENU_ON`, `APPLOCK_ON` = 1 (`Prefs.h:42-44, 90-91`). En IDF: todos activos; no hace falta
reproducir los interruptores salvo como `Kconfig` de depuración.

---

## 1. Núcleo: arranque, bucle principal y máquina de estados

### 1.1 Estados de pantalla `ST_*` (`Shell.h:186-216`)

`gState` dice qué pantalla manda. Valores (añadidos siempre al final; **no se persisten**, solo viven en RAM):

| Valor | Estado | Tick (`.ino:933-975`) | Qué es |
|---|---|---|---|
| 0 | `ST_SPLASH` | `splashTick` | Splash con fundido (0-600 ms entra, hasta 2000 quieto, 2000-2600 sale) |
| 1 | `ST_OOBE_LANG` | `oobeLangTick` | Asistente: idioma |
| 2 | `ST_OOBE_NAME` | `oobeNameTick` | Asistente: nombre del equipo |
| 3 | `ST_LOCK` | `lockTick` (+ repintado al cambiar el minuto) | Pantalla de bloqueo |
| 4 | `ST_HOME` | `homeTick` | Escritorio |
| 5 | `ST_APP` | `appTick` | Una app en primer plano (`gAppId`) |
| 6 | `ST_SWITCHER` | `swTick` | Recientes |
| 7 | `ST_LOCKSETUP` | `lsuTick` | Alta / verificación de PIN o contraseña |
| 8 | `ST_WIFI` | `wifiTick` | Ajustes → Wi-Fi |
| 9 | `ST_CTX` | `ctxTick` | Menú contextual de pulsación larga en Inicio |
| 10 | `ST_KIOSKSET` | `kioskSetTick` | Definir zona excluida del Modo Kiosco (§12) |
| 11 | `ST_POWEROFF_CONFIRM` | `poffTick` | "Desliza para apagar" (§10) |
| 12 | `ST_POWEROFF_ANIM` | `poffAnimTick` | Animación final de apagado (§10) |
| 13 | `ST_KBSET` | `kbsTick` | Ajustes del teclado (§8) |
| 14 | `ST_CONN` | `connTick` | Conectividad |
| 15 | `ST_FILES` | `filesTick` | Explorador de archivos |
| 16 | `ST_DRAWER` | `drawerTick` | Caja de aplicaciones |
| 17 | `ST_HOMECFG` | `hcTick` | Personalizar inicio |
| 18 | `ST_OOBE_ACCOUNT` | `accountOobeTick` | Asistente: Flex Account |
| 19 | `ST_FACTORY` | `frTick` | Restablecimiento de fábrica (§13) |
| 20 | `ST_SAFE` | `safeTick` | Modo seguro (§11) |
| 21 | `ST_THEFT` | `theftTick` | Protección contra robo |

Variables globales asociadas (`Shell.h:218-224`): `gState`, `splashStart`, `lockOff`, `lastLockOff`, `oobeSel`,
`gAppId`, `editMode` (Modo Edición del Inicio). **Capas que NO son estados** (se superponen a cualquiera): panel rápido
(`qsPanelY>0`), Centro de notificaciones (`fpcOpen()`), banner (`fpbVisible()`), suspensión (`gSuspOn`), transición de
app (`appTrVisible()`), OTA (`flexOtaOwnsScreen()`), Optimizar (`optActive()`), tarjeta del cronómetro, aviso de caída,
aprobación de teléfono (Flex Storage), barra transitoria de pantalla completa.

### 1.2 `setup()` — orden de arranque (`FlexOS_Ultra.ino:349-579`)

1. `Serial.begin(115200)`, `delay(60)`.
2. **`poffWakeGate()`** (§10.6): si se viene de deep sleep, exige 3 s de dedo sostenido ANTES de encender el panel; si
   no, vuelve a dormir sin que se vea nada.
3. `flexPanelInit()` con un reintento a los 150 ms; si falla: `"[FATAL] el panel DSI no responde (revisa cableado)"`
   y el backlight parpadea en bucle (150 ms on/off) para siempre. Sin PSRAM: `"[FATAL] sin PSRAM (activa 'PSRAM: Enabled' en el IDE)"`.
4. `flexTouchInit()` (fallo suave), **`safeBootEval()`** (§11.1), **`frLoadState()`** (§13.6).
5. Transporte del C6 (`wifiConfigureHostedTransport`, solo pines). Si NO hay restablecimiento pendiente ni Modo seguro:
   `bootInitRadioSafe()`, `flexBrowserBegin()`, `flexOtaBegin()`.
6. Si no hay restablecimiento pendiente: `flexLockMigrate()` (migración del PIN en claro a PBKDF2; mensajes
   `"[SEG] clave del bloqueo migrada a hash con sal"` / `"[SEG] no se pudo migrar la clave: se conserva la anterior"`).
7. **`cfgLoad()`** (§16).
8. `flexFsBegin()`; si monta: crea `/System/Sessions` y `/System/Cache`; si no hay restablecimiento/Modo seguro:
   `flexPkgBegin()`, `mlBegin()` (biblioteca de medios).
9. `flexAudioBegin()`, `dcBegin()` (Device Care, solo NVS), `tpBegin()` (robo, solo NVS).
10. **Si hay restablecimiento pendiente** (`gFrPending`): `setBacklight(gBright); frResumeAfterBoot(); return;` — no se
    carga nada más (§13).
11. Purga única de la "Carpeta segura" retirada (NVS `flexos/fxvpurge` = 1 tras hacerlo).
12. Si NO Modo seguro: `flexStoreBegin`, `flexAccountBegin`, `flexStorageBegin`, `flexCloudBegin`, `connBootRestore`
    (modo avión), `flexWeatherBegin`, `flexPhoneBegin`.
13. `setBacklight(gBright)`; si NO Modo seguro: `dcApplyFallPref()`, `tpApplyPref()`.
14. `homeOrderLoad()`, `homeCfgLoad()` (fondo/tema), **teclado**: `kbApplySize()`, `kbMtSurfaceReset()`,
    `clipLoadPinned()` y log `"[KB] tamano=%d (%dx%d gap=%d x=%d) cabe=%s"`.
15. Reloj: semilla de fábrica (sáb 4 jul 2026 13:23) → `clkLoadNvs()` (última hora conocida) → `clkUpdate()`.
16. Si se viene de deep sleep: lee y **borra** `flexos/cleanoff` → `gBootCleanOff`.
17. Banda forense (`showBootBanner`) solo si el reinicio fue anormal (no POWERON, no SW, no DEEPSLEEP).
18. Si Modo seguro: `safeEnter()` y fin. Si no: pantalla negra, `splashStart = millis()`, `gState = ST_SPLASH`.

Al acabar el splash (`Home.h:296-312`): Modo seguro → `safeEnter()`; sin OOBE → `enterOobeLang()`; kiosco activo →
`enterApp(kioskApp)` directamente; si no, bloqueo/Inicio (otro documento).

### 1.3 `loop()` — planificador y prioridad de dueños de pantalla (`FlexOS_Ultra.ino:670-1017`)

El bucle es **un solo hilo** (loopTask de Arduino) que corre todo lo de UI. Orden exacto de cada vuelta:

1. `flexFeedWdt()` (alimenta el TWDT; si se perdió la suscripción reintenta cada 5 s, `Power.h:1024-1042`),
   `loopRateTick()` (vueltas/s), **`flexPollTouch()`** (lee GT911, clasifica gesto, filtros de kiosco, suspensión,
   pellizco; §2).
2. `uiGlassBandGuard()` (`.ino:666`): la banda pre-desenfocada solo vale mientras su dueño (`ST_CTX`, tarjeta del
   cronómetro, menú de medios) siga a la vista.
3. **Restablecimiento en curso** (`gFrPending || gState==ST_FACTORY`): `clkUpdate(); frTick(); delay(pace); return;` —
   nadie más dibuja ni recibe toques.
4. Servicios por tiempo (`FLEXHITCH` solo mide tiempos si el diagnóstico está activo): `safeStableTick`,
   `sessAutosaveTick` (§14), `safeToastTick`, `memTick`, `memAlertTick` (§17), `suspFadeTick` (§9), `autoLockTick`
   (§9.5), `cronoOverlayTouch`, **`fpbTouch`** (banner, §6.4), `notifHandleTouch` (isla, §6.2), `flexOtaTouchBridge`,
   `imuServiceTick`, `dcSensorTick`, `tpSensorTick`, `tpLockPendingTick`, `tpIdleGuard`, `compassIdleGuard`,
   `faPendingTick`, `spaWatch`, `mlTick`, `webTick`, `musAudioTick`, `vwLockTick`, `gedBgTick`, `vedBgTick`,
   `cloudUiTick`; si NO Modo seguro: `wifiAutoReconnectTick`, `ntpTick`; `clkPersistTick`; `minChanged = clkUpdate()`.
5. **OTA a pantalla completa** (`flexOtaOwnsScreen()`): cierra Personalizar inicio si estaba abierto, `flexOtaRender()`,
   `return`.
6. **Optimizar Flex OS** (`optActive()`): `optTick(); flexOtaRender(); return;` (sin pausa).
7. Si NO Modo seguro: `flexWeatherTick`, `flexPhoneTick`.
8. **Centro de notificaciones** (`fpcGlobalHandle()` true): `kioskTick; notifTick; flexOtaRender; delay; return`.
9. **Panel rápido** (`qsGlobalHandle()` true): `kioskTick; uiTick` (anima la cortina)`; notifTick; flexOtaRender; delay; return`.
10. **Tarjeta del cronómetro** (`cronoCardVisible()`), **aviso de caída** (`faVisible()`), **aprobación de teléfono**
    (`spaVisible()`): cada uno se queda la vuelta (`*Tick(); flexOtaRender(); delay; return`); si cambió el minuto marca
    `gHomeDirty`.
11. `switch(gState)` → tick de la pantalla (tabla §1.1). En `ST_HOME`, al cambiar el minuto: si no hay cortina,
    edición, gesto de página ni transición → `renderHome(); showHome();`; si los hay, se aplaza con `gHomeDirty`.
12. `kioskTick` (gesto de salida del kiosco), `wgDataTick` (datos de widgets).
13. **Transición de app** (`appTrOwnsScreen()`): si `gState` ya no es Inicio ni App → `appTrCancel()`; si no,
    `appTrTick(); flexOtaRender(); return;` (sin pausa).
14. Repintado de widgets si `wgDirty` y estamos en Inicio quieto.
15. `uiTick()` (cortina a ~60 fps / ripple de icono / jiggle de edición), `notifTick()`, `fpbTick()` (banner),
    `cronoCapsuleTick()`, `flexOtaRender()` (última capa), `delay(loopPaceMs())`.

**Ritmo** (`loopPaceMs`, `.ino:647-659`): 1 ms si el dedo está apoyado / acaba de haber evento / menos de 400 ms desde
el último contacto / hay transición, cortina o ripple / `uiBusyNow()`; si no, **5 ms**.

**`uiTick`** (`.ino:584-625`): si hay transición, no hace nada; intervalo **16 ms** si la cortina está visible o hay
ripple en Inicio, **38 ms** en otro caso; cortina → `qsTick()`; en Inicio: `edRender()` (jiggle) o
`animateIconRipple()`; calla durante un gesto de página.

**Prioridad efectiva de dueños de pantalla** (de mayor a menor): restablecimiento > OTA > Optimizar > Centro de
notificaciones > panel rápido > tarjeta del cronómetro > aviso de caída > aprobación de teléfono > pantalla `ST_*` >
transición de app > banner (no modal, se estampa encima) > barra de navegación / candado de kiosco (estampados en
cada volcado).

> **Migración (LVGL):** un `lv_timer` o tarea de UI única que ejecute este mismo orden. Cada "dueño" se modela como
> un objeto modal en `lv_layer_top()` con `LV_OBJ_FLAG_CLICKABLE` que absorbe la entrada, y una **pila de prioridad**
> explícita (`flex_shell_owner_push/pop`) para que solo el de mayor prioridad reciba toques y los de abajo pausen su
> animación (equivalente a los `return` tempranos). Los servicios del paso 4 pasan a tareas/timers propios y publican
> eventos en `flex_bus`; la UI solo los consume. Cuidado con el **banner**: en Arduino se estampa en cada volcado; en
> LVGL va en `lv_layer_sys()` para quedar encima de todo excepto de los modales que lo suprimen (§6.4.2).

---

## 2. Táctil base y arbitraje de gestos (lo que necesita toda el área)

### 2.1 Estructura `Touch T` y clasificación (`Types.h:61-66`, `Touch.h:332-430`)

`T.down, pressed (flanco de bajada, 1 vuelta), released (flanco de subida), tap, moved, swipeUp/Down/Left/Right,
x, y, startX, startY, dx, dy, downMs, lastMs`.

* Bajada: `pressed=true`, `start=(x,y)`, `downMs=now`. `moved=true` si se aleja **>12 px** del inicio.
* Soltar (frame de 0 dedos, o **90 ms** sin frames): `dx,dy`; **tap** si `|dx|<16 && |dy|<16 && dur<550 ms` — y el tap
  se **localiza donde se apoyó** (`T.x=startX`); **swipe vertical** si `|dy|>55 && |dy|≥|dx|`; **horizontal** si
  `|dx|>55 && |dx|>|dy|`.

### 2.2 Bordes del sistema (`Touch.h:41-43`)

| Constante | Valor | Gesto |
|---|---|---|
| `SYS_EDGE_TOP_H` | 30 px | franja superior: cortina del panel rápido |
| `SYS_EDGE_LEFT_W` | 26 px | franja izquierda: Centro de notificaciones |
| `SYS_EDGE_RIGHT_W` | 26 px | franja derecha: panel rápido (segunda vía) |
| `GB_STRIP_H` | 44 px | franja inferior: barra de gestos (modo iOS) |
| `NAV_IMM_EDGE` | 26 px | borde inferior en pantalla completa |

Las pantallas con controles propios deben colocarlos **fuera** de estas franjas (como los *insets* de gestos de Android).

### 2.3 Propiedad del episodio táctil

* **`touchDropAll()`** (`AppFramework.h:1179-1191`): se llama en cada cambio de pantalla. Borra todos los flancos y
  arma `gTouchSwallow`: el contacto que siga apoyado **no existe** para la pantalla nueva hasta que el dedo se levante
  de verdad (frame de 0 dedos o 90 ms sin frames; `Touch.h:395-407`). Evita el "toque fantasma" (pulsar Inicio y abrir
  un icono). También apaga el destello de la barra de navegación.
* **`touchHoldBack()`** (`Touch.h:315-320`): un overlay no modal (banner, Centro, barra transitoria) se queda el
  episodio entero: borra los flancos de `T` para la pantalla de debajo **en esta vuelta** pero guarda el estado real
  del dedo para restaurarlo al principio de la siguiente (así el clasificador no ve un "nuevo dedo" a mitad de gesto).
* **Filtro de kiosco** (`Touch.h:351`, `Lock.h:493`): en `ST_APP` con kiosco activo, las coordenadas dentro de la zona
  excluida se convierten en "sin dato" (el candado de salida siempre gana).
* **Suspensión** (`suspGestureUpdate`, §9.2) y **pellizco del escritorio** (`hpzUpdate`) pueden anular TODOS los
  eventos (incluido `T.down`) de un episodio de 2 dedos.

> **Migración:** LVGL tiene un único `indev` de puntero. Hace falta una **capa de gestos del sistema** en el
> `read_cb` del indev (o justo antes): lee los puntos crudos del GT911 (hasta 5), ejecuta suspensión (2 dedos),
> pellizco, bordes y kiosco, y decide si el punto llega a LVGL o se consume. `touchDropAll` ≡ "esperar a
> `LV_INDEV_STATE_RELEASED` antes de entregar el siguiente `PRESSED`" (p. ej. `lv_indev_wait_release()`), y
> `touchHoldBack` ≡ que el overlay capture el objeto pulsado (`LV_OBJ_FLAG_PRESS_LOCK`) para que el arrastre no
> salte a otro objeto. Umbrales: `tap` <16 px y <550 ms (LVGL: `scroll_limit` y `long_press_time`; reproducir con
> los valores exactos).

---

## 3. Barra de estado

Hay **dos** barras de estado con la misma geometría, dibujadas por funciones distintas: la del **escritorio**
(`renderHomeInto`, `Home.h:911-930`) y la del **marco de app** (`appDrawChrome`, `AppFramework.h:237-261`). La hora y
la cápsula del cronómetro salen de la MISMA función `cronoBarClock(16, col)` (`AppChrono.h:291-312`).

### 3.1 Elementos y condiciones

| Elemento | Posición / tamaño | Color | Condición |
|---|---|---|---|
| **Hora** `clkStrBar` | texto tamaño 2 en (20, 16) | Inicio `TH_ONWALL`; app `TH_NAV` | siempre, salvo que la cápsula del cronómetro no quepa (entonces se oculta) |
| Formato hora | `"%d:%02d"` (24 h) o `"%d:%02d AM"/"PM"` (12 h, 0→12) (`Clock.h:192`) | | según `g24h` (NVS `h24`) |
| **Fecha corta** (solo Inicio) | tamaño 1 en (20, 40) | `TH_ONWALL2` | siempre en Inicio; formato `"%s, %d %s"` = `WD_SHORT`, día, `MO_SHORT` (ej. `sáb, 4 jul`) (`Home.h:451`) |
| **Wi-Fi** `drawWifi(414, 28, 11)` | centro (414,28): 3 arcos 225°-315° de radio 11, 7.26, 3.63, grosor 2 + punto r=2 | igual que la hora | **siempre**, esté o no conectado (rareza: no refleja el estado real) |
| **Batería** `drawBattery(434, 20, 30, 15, 82)` | doble contorno redondeado r=2 de 30×15 en (434,20); polo 2×5 en (464,25); relleno (3..) de `(30-6)*82/100 = 19` px | igual | **siempre al 82 %** fijo (no hay sensor de batería; el panel rápido evita a propósito mostrar porcentaje) |
| **Cápsula del cronómetro** | y=12, h=26, ancho fijo `8+18+6+textW("00:00" o "0:00:00",2)+11`; x = `20 + textW(hora,2) + 12`; si `x+ancho > 402` (borde izq. del Wi-Fi − 12) → x=20 y la hora se oculta | relleno sólido `wallAccent()`, radio h/2; glifo de cronómetro de 18 px y tiempo tamaño 2 color `CRONO_ONACC` | cronómetro corriendo o en pausa (`cronoActive()`); solo en superficies válidas (abajo) |
| **Píldora "Modo seguro"** (solo Inicio) | (146,56) 188×38 r=19 | `#BA7030`, texto `"Modo seguro"` tamaño 2 blanco centrado en y=66 | `gSafeMode`; toque en (136..344, 48..104) → pantalla de Modo seguro (`HomeCfg.h:1240`) |
| **Candado de kiosco** | (448,44) 24×24 r=7 (recuadro de estampado 444..479 × 40..71) | fondo `TH_PAGE` α200; arco y cuerpo `TH_TXT` | kiosco activo y `gState==ST_APP` (§12) |

`cronoBarSurface()` (`AppChrono.h:316-326`) decide dónde existe la cápsula: **0** (ninguna) si suspendido, horizontal,
hospedado en DeX, app que oculta la barra (`gAppHidesStatusBar`, visor de medios), pantalla completa o kiosco;
**1** = escritorio quieto (sin cortina ni edición); **2** = app con marco estándar (sin `APP_CUSTOM_HEADER`). La
cápsula se repinta sola cada segundo (`cronoCapsuleTick`); su toque abre la tarjeta del cronómetro (documento de
apps/Reloj).

### 3.2 Barra del marco de app

`appDrawChrome` **borra** primero (0,0,480,46) con `WIN_BG` (evita el "AM" doble al redibujar con fuente
proporcional), pinta hora/cápsula, Wi-Fi y batería con `TH_NAV`, y en **modo gestos** dibuja el indicador de inicio
`drawHomeIndicator(800, 180)`: píldora 130×5 r=2 en x=175, y=775, color `TH_ONWALL` α180 (`Home.h:459-464`). No se
pinta embebida en DeX ni en pantalla completa. Apps con `APP_CUSTOM_HEADER` pintan su propia barra (varias llaman
igualmente a `cronoBarClock(16, W)`).

### 3.3 Notas de migración

* Un componente `flex_statusbar` (objeto LVGL de 480×46) reutilizado por Inicio (texto `TH_ONWALL`, con fecha) y por el
  marco de app (texto `TH_NAV`, sin fecha). Se suscribe a: cambio de minuto, `g24h`, idioma, estado del cronómetro,
  Modo seguro, kiosco.
* **No copiar** el Wi-Fi siempre visible ni la batería fija al 82 %: el Wi-Fi debe reflejar `flex_wifi` (apagado /
  conectando / conectado) y la batería **ocultarse** mientras no exista medida real (coherente con la regla "nada
  falso" del propio panel rápido). Si se mantiene por fidelidad visual, documentarlo como decorativo.
* Rareza: en tema claro el indicador de inicio blanco (`TH_ONWALL`) sobre `TH_WIN` claro casi no se ve [02 §7.3]: usar
  `TH_NAV`.
* La cápsula y la hora se reparten el hueco: implementar con un `lv_obj` en fila con prioridad (si no cabe, ocultar la
  etiqueta de hora). Lógica pura reutilizable: `cronoCapsuleW`, `cronoCapsuleRight` y la decisión de `cronoBarClock`.

---

## 4. Navegación del sistema

### 4.1 Los tres botones lógicos (`Core.h:551-588`)

| Acción | Función | Qué hace |
|---|---|---|
| **Atrás** | `sysBack()` | Kiosco → nada. Solo en `ST_APP`: 1) `hooks->backLayer()` (cierra teclado/menú/diálogo propio) → si true, `touchDropAll` y fin; 2) `hooks->backScreen()` (retrocede una pantalla interna); 3) si no había nada, `appClose()` (suspende y vuelve a Inicio). |
| **Inicio** | `sysHome()` | Kiosco → nada. Hospedado en DeX → `gHostReq=1`. En app → `appClose()`. En otra pantalla distinta de Inicio → `enterHome(); touchDropAll()`. |
| **Recientes** | `sysRecents()` | Kiosco → nada. DeX → `gHostReq=3`. En app: vuelve a vertical, cancela una apertura pendiente o `appSuspend(app, land)` (toma miniatura), `immersiveLeave()`, `renderHome` si hacía falta; luego `appTrCancel()`, `swSyncFromLife()`, `activarMultitarea()` (abre `ST_SWITCHER`), `touchDropAll()`. |

`appClose()` (`Core.h:638-677`): `qsForceClose()`; kiosco → nada; DeX → `gHostReq=1`; fuerza vertical y recorte
completo; suspende (o cancela apertura pendiente); `immersiveLeave()`; recompone `homeBuf` si estaba sucio;
**`enterHomeState()`** (Inicio manda desde YA y acepta toques); `touchDropAll()`; `appTrBeginClose(outId)` (capa visual
no bloqueante). `enterApp(id)` (`Core.h:683-719`): cierra cortina; kiosco solo permite `kioskApp`; DeX → petición;
restablecimiento pendiente → nada; Modo seguro y app no permitida → `safeDenyApp` (§11.3); **admisión de memoria**
si la app estaba cerrada (§17.3); carga perezosa de sesión; decide reanudar (`resume`) o reconstruir (`enter`); estado
lógico inmediato `gAppId=id; gState=ST_APP`; `touchDropAll()`; `appTrBeginOpen`.

### 4.2 Modo "Botones" (`gNavMode = 0`, por defecto) — barra de 3 botones en apps

Propietario único: el sistema la **estampa** en cada volcado al panel (`navStampBar`, `AppFramework.h:1159-1171`);
ninguna app puede pintar encima ni hacerla parpadear.

**Visible** (`navBarVisible`, `:997-1009`) solo si: `gNavMode==0`, no DeX, no horizontal, no kiosco, `gState==ST_APP`
y la app no es `APP_LAND`. En pantalla completa es la barra transitoria (§4.5).

**Layout (`navBarPaint`, `:1020-1037`):** franja **y = 736..799 (NAV_H = 64)**.

| Elemento | Geometría | Color oscuro / claro |
|---|---|---|
| Fondo | rect (0,736,480,64) | `#0D0F16` / `#EEF1F7` |
| Línea superior | rect (0,736,480,1) | `#1E222E` / `#D6DBE4` |
| Atrás (triángulo relleno) | vértices (70,756), (88,746), (88,766) — centro x=80 | `#E8ECF5` / `#2C303C` |
| Inicio (anillo doble) | círculos r=12 y r=11 en (240,756) | igual |
| Recientes (cuadrado) | contorno redondeado (389,745) 22×22 r=4 — centro x=400 | igual |
| Destello de pulsación | círculo r=24 en (80/240/400, 756), color de primer plano α46 | visible mientras el dedo sigue encima y **130 ms** (`NAV_PRESS_MS`) tras soltar |

**Interacción** (`navBarHandle`, `Core.h:591-632`): zona = `T.y ≥ 736`; botón por tercios (`x<160` atrás, `<320`
inicio, resto recientes). Al apoyar: destello y la franja se queda el episodio. Al soltar **dentro** de la barra →
acción; si el dedo salió de la barra → se cancela. Un `tap` cuyo apoyo no se vio (vuelta larga) se resuelve donde se
apoyó. Cualquier toque en la franja no llega nunca a la app. `WIN_BOT = 736` (las apps terminan encima); Notas sube el
teclado `kbBotReserve = NAV_H`.

En **Inicio** (`renderHomeInto`, `Home.h:933-944`) se dibujan los mismos tres glifos en `TH_ONWALL` **sin fondo**,
pero solo **Recientes** responde (`T.tap && x>320 && y>728`, `HomeCfg.h:1292`); Atrás e Inicio no hacen nada en el
escritorio. En modo gestos Inicio pinta el indicador con α220.

### 4.3 Modo "Gestos iOS" (`gNavMode = 1`) — `handleiOSGestures` (`Core.h:1048-1114`)

| Constante | Valor |
|---|---|
| `GB_STRIP_H` | 44 px (el gesto debe **nacer** con `T.y > 756`) |
| `GB_CLAIM_DY` | 12 px hacia arriba: a partir de aquí la barra reclama el episodio (antes, la app sigue recibiendo el toque) |
| `GB_HOME_DY` | 30 px de recorrido mínimo |
| `GB_FLICK_VEL` | −0.35 px/ms (velocidad hacia arriba de un "flick") |
| `GB_RECENTS_MS` | 300 ms |
| `GB_VEL_TAU` | 40 ms (filtro exponencial de velocidad; `dt` acotado a 1..100 ms) |

Comportamiento: **flick** (dy>30, vel ≤ −0.35, antes de 300 ms) → resuelve **Inicio con el dedo todavía apoyado**
(`sysHome` en app, `enterHome` fuera); el resto del episodio se traga. Si no fue flick, al **soltar**: dy>30 y
duración ≥300 ms → **Recientes**; dy>30 y más rápido → Inicio. Guarda `gGbFireVel` (velocidad real) para que la
animación de cierre arranque con el impulso del dedo (§4.6). En kiosco devuelve false (la app no se congela, pero no
hay escape). En Inicio el mismo gesto se evalúa antes que los toques (`HomeCfg.h:1248`).

### 4.4 Chevron "atrás" de la cabecera estándar

En apps sin `APP_CUSTOM_HEADER`: `T.tap && T.y ≤ WIN_TOP(96) && T.x < 72` → `sysBack()` (`Core.h:992`). Cabecera:
chevron en (18..30, 50..66) grosor 2.4 + título tamaño 3 centrado en y=53 (`AppFramework.h:264-272`) [02 §7.2].

### 4.5 Pantalla completa inmersiva y barra transitoria (`AppFramework.h:98-122, 1039-1323`)

* Una app `APP_FLEX` puede pedir `immersiveRequest(app, st)` (0 normal, 1 vertical, 2 horizontal); se aplica en
  `appTick` antes de su tick (`immersiveApplyPending`): limpia el lienzo, (re)pinta el marco si sale, `touchDropAll`.
  Desde Inicio se ofrece en el menú contextual ("Pantalla completa" / "Pantalla completa horizontal") si la app declara
  `APP_IMMERSIVE`. El modo es de la **sesión**: suspender la conserva; `appTerminate` la borra.
* **Barra transitoria**: oculta al entrar; se revela con un deslizamiento que **nace** en los últimos **26 px** del
  borde inferior del lienzo (en horizontal, el borde inferior lógico) y recorre **14 px** hacia dentro; se oculta sola
  a los **3500 ms** sin tocarla. 4 botones en `cw·(2i+1)/8` (vertical: x=60, 180, 300, 420): atrás, inicio,
  recientes y **salir de pantalla completa** (glifo de 4 esquinas hacia dentro, r=9, k=5, grosor 2). Mismo fondo,
  línea y colores que la barra normal; destello r=24 α46 del pulsado. Atrás → `sysBack` (la app decide; el navegador
  sale de pantalla completa), salir → `immersiveRequest(app, 0)`.
* Se **estampa** sobre la app sin guardarse en el framebuffer (al ocultarse, el panel recibe las filas limpias).
  En kiosco no existe.

### 4.6 Transiciones de apertura/cierre (resumen; detalle en [02 §8.7])

`ATR_OPEN_MS=210`, `ATR_CLOSE_MS=190`, mínimo al re-dirigir 60 ms, **ease-out cúbico** `1-(1-u)^3`, radio 26 px
(pequeña) → 4 px (completa); la saliente se desvanece por debajo de p=0.35. Estilo (`gAnimStyle`, NVS `animstyle`):
0 = zoom desde el icono, 1 = fundido a pantalla completa, 2 = deslizar desde abajo. Duración de cierre tras un gesto:
`190 · max(0.45, 0.35/v)` si v>0.35 px/ms. Interrumpibles: el estado lógico cambia en el acto y la animación es solo
una capa; una generación (`gTrGen`) invalida finalizaciones obsoletas. Lógica pura: `appTrP`, `appTrRect`,
`appTrAlpha`, `appTrAim`, `appTrCloseMs` (`AppFramework.h:896-954`, `Core.h:805-811`).

### 4.7 Notas de migración

* La barra de 3 botones: objeto en `lv_layer_top()`, 480×64 en y=736, visible según `navBarVisible()` (suscrito a
  cambios de `gState`, app, `gNavMode`, kiosco, pantalla completa). Botones como `lv_button` sin estilo con estado
  `LV_STATE_PRESSED` para el destello (añadir 130 ms de cola con una animación de opacidad). Cancelar si el dedo sale
  (`LV_EVENT_PRESS_LOST`).
* Gestos iOS: no usar los gestos de LVGL (umbrales distintos y sin "reclamar a los 12 px"): implementarlo en la capa de
  gestos del sistema (§2.3) con la máquina de estados `gbReset/handleiOSGestures` portada tal cual (es lógica pura si se
  le pasan `(pressed, down, y, now)`; **prueba de host** recomendada: flick a 0.5 px/ms con 40 px → Inicio antes de
  soltar; 40 px en 400 ms → Recientes al soltar; 10 px → la app conserva el toque).
* Bug a no copiar: en Inicio, Atrás e Inicio de la barra de botones son decorativos (solo responde Recientes). Decidir
  explícitamente (p. ej. Inicio → página principal).

---

## 5. Panel rápido (cortina estilo One UI 8.5)

Archivos: modelo y catálogo `QuickPanel.h`; render, material y gestos `QuickPanelGlass.h`; edición, catálogo, tick y
punto de entrada `QuickPanelEdit.h`.

### 5.1 Propósito y cómo se llega

Capa **global** (no una pantalla del Inicio): puede abrirse sobre el escritorio o sobre cualquier app. Muestra la
hora, la fecha, el estado de red, botones de editar / apagar / ajustes y una cuadrícula configurable de controles
**con función real** (regla "NADA FALSO": solo entran controles cuyo backend existe en esta placa, `QuickPanel.h:49-59`).

**Se puede abrir** (`qsCanOpen`, `QuickPanelEdit.h:488-495`) solo si: vertical (`!gLand`), no hospedado en DeX, sin
Modo Edición del Inicio, sin kiosco, sin OTA a la vista, y `gState` es `ST_HOME` o `ST_APP`. Si deja de poder estar
abierta con la cortina fuera → `qsForceClose()`.

**Gestos de apertura** (`qsGlobalHandle`, `QuickPanelEdit.h:506-579`):

| Vía | Condición exacta | Comportamiento |
|---|---|---|
| **Borde superior** | `T.pressed && T.startY < 30` (`SYS_EDGE_TOP_H`) | La cortina se agarra en el **mismo cuadro del apoyo** y sigue al dedo 1:1 (`qsPanelY = base + (T.y − y0)`). Un toque sin mover (≤6 px) en esa franja se traga y deja la cortina cerrada. |
| **Borde derecho** | `T.pressed && startX > 454 && startY > 30 && (startX − T.x) > 28 && (startX − T.x) > |T.y − startY|` | Abre sola con la animación (`qsAnimTo(800)`), sin seguir al dedo. **Bug:** exige `T.pressed` (solo el cuadro del apoyo, cuando `T.x == startX`), así que la condición de recorrido nunca se cumple y esta vía **no se dispara nunca** (el Centro de notificaciones corrigió el mismo fallo usando `T.down`). En IDF: evaluarlo con el dedo **apoyado y moviéndose**. |

Al abrir sobre una app se toma una **captura** del framebuffer (`qsCaptureApp`, 768 KB en PSRAM); si no hay memoria,
la cortina **no se abre** y la app conserva el gesto. Se carga la configuración (`qpLoad`), se resetean scrolls y se
fija el alto de la tarjeta a `qpGroupH(qpGrows)`.

**Otras entradas que lo cierran a la fuerza** (`qsForceClose`, `QuickPanelEdit.h:441-462`): abrir/cerrar app, volver a
Inicio, bloquear, suspender, apagar, Modo PC, Recientes, kiosco, OTA. Cierra sin animación, **descarta la edición en
curso entera** (no guarda), cancela un arrastre de intensidad del vidrio, libera todos los buffers y limpia los flancos
del toque.

### 5.2 Geometría base (`QuickPanel.h:86-117`)

| Constante | Valor | Significado |
|---|---|---|
| `QP_MX` | 16 | margen lateral |
| `QP_CONT_W` | 448 | ancho útil |
| `QP_GAP` | 12 | separación entre columnas |
| `QP_CW` | 103 | ancho de columna (4 columnas: 16 + 4·103 + 3·12 + 16 = 480) |
| `qpColX(c)` | 16, 131, 246, 361 | x de cada columna |
| `qpSpanW(w)` | 103 / 218 / 448 | ancho de un bloque de 1 / 2 / 4 columnas |
| `QP_HDR_H` | 116 | cabecera fija |
| `QP_FOOT_H` | 34 | franja inferior fija (asa de cierre) |
| Vista con scroll | y = 116 .. 765 (650 px) | `QP_VIEW_Y0/Y1/H` |
| `QP_RH1` / `QP_RH2` | 74 / 160 | alto de bloque de 1 / 2 filas |
| `QP_VGAP` | 12 | separación vertical entre bloques |
| `QP_RAD` / `QP_RAD_S` | 26 / 20 | radio de la tarjeta grande / de los módulos |
| `QP_GPAD` | 14 | relleno interior de la tarjeta de círculos |
| `QP_TROW` | 98 | alto de una fila de círculos |
| `QP_TCIRC` | 62 | diámetro de un círculo 1×1 |
| `QP_HANDLE_H` | 22 | franja del asa de la tarjeta |
| `qpGroupH(r)` | 14 + 98·r + 22 → 2:232, 3:330, 4:428, 5:526 | alto de la tarjeta según filas visibles (2..5) |
| `QP_TCOLW` | 105 | columna interior de la tarjeta; centros x = 82, 187, 292, 397 |
| `QP_TOUCH_MIN` | 44 | área táctil mínima |

### 5.3 Catálogo de controles (`QS_REG`, `QuickPanel.h:533-601`)

Ids **estables** (van a NVS; nunca se reordenan, los nuevos al final). Tipo: **T** = interruptor con estado real,
**A** = acción (nunca finge estado), **S** = deslizador. Tamaños: 1×1 = círculo dentro de la tarjeta; 2×1 = cápsula;
4×1 = módulo ancho. Orientaciones H (icono a la izquierda) / V (icono arriba). "Larga" = acción de pulsación larga
(480 ms).

| id | `name` (círculo) | `title` (cápsula) | Tipo | Tamaños | Cat. | Disponible si | Estado ON | Toque | Larga | Subtítulo |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 `QSID_WIFI` | `Wi-Fi` | `Wi-Fi` | T | 1×1, 2×1 | Conectividad | `FLEXOS_ENABLE_WIFI` (core 3.2.1+) | radio encendida (`gWifiDriverOn`) | conmuta Wi-Fi (no si modo avión) | Conectividad | `connWifiSub` (abajo) |
| 1 `QSID_AIRPLANE` | `Modo avión` | `Modo avión` | T | 1×1, 2×1 | Conectividad | siempre | `gAirplane` | conmuta; al activar apaga BLE y Wi-Fi y guarda | Conectividad | `Activado` / `Desactivado` |
| 2 `QSID_BLE` | `Bluetooth` | `Bluetooth` | T | 1×1, 2×1 | Conectividad | `SOC_BLE_SUPPORTED` (**en el P4: no existe**) | `gBleOn` | conmuta (no si modo avión) | Conectividad | `connBleSub` |
| 3 `QSID_BRIGHT` | `Brillo` | `Brillo` | S | 4×1 | Pantalla | siempre | — | (deslizador) | Ajustes | `"%d%%"` |
| 4 `QSID_THEME` | `Tema` | `Modo oscuro` | T | 1×1, 2×1 | Pantalla | siempre | **tema CLARO** (`!gDark`) | `gDark = !gDark; themeChanged()` | Ajustes | `Oscuro` / `Claro` |
| 5 `QSID_GLASS` | `Vidrio` | `Liquid Glass` | T | 1×1, 2×1 | Pantalla | siempre | `uiGlass` | conmuta Liquid Glass; `themeChanged()` | Ajustes | `Liquid Glass` / `Plano` |
| 6 `QSID_POWERSAVE` | `Ahorro` | `Ahorro Ultra` | T | 1×1, 2×1 | Sistema | siempre | `qsPower` | `setCpuFrequencyMhz(160 ó 360)` | Ajustes | `160 MHz` / `360 MHz` |
| 7 `QSID_SETTINGS` | `Ajustes` | `Ajustes` | A | 1×1, 2×1 | Sistema | siempre | — | abre Ajustes | — | — |
| 8 `QSID_CONN` | `Conexiones` | `Conectividad` | A | 1×1, 2×1 | Conectividad | siempre | — | abre Ajustes y luego Conectividad (`connEnter`) | — | — |
| 9 `QSID_DEX` | `Modo PC` | `Modo PC` | A | 1×1, 2×1 | Sistema | no hospedado | — | abre Modo PC | — | — |
| 10 `QSID_RETIRED_10` | `""` | `""` | A | 1×1 | Sistema | **nunca** | — | — | — | — |
| 11 `QSID_OTA` | `Actualizar` | `Actualizaciones` | A | 1×1, 2×1 | Sistema | siempre | — | cierra cortina y `flexOtaOpenSettings()` | — | `Versión %s` (o `?`) |
| 12 `QSID_FILES` | `Archivos` | `Archivos` | A | 1×1, 2×1 | Herramientas | LittleFS montado | — | abre Almacenamiento y luego el explorador (`filesEnter`) | — | — |
| 13 `QSID_RETIRED_13` | `""` | `""` | A | 1×1 | Herramientas | **nunca** | — | — | — | — |
| 14 `QSID_CAMERA` | `Cámara` | `Cámara` | A | 1×1, 2×1 | Herramientas | siempre | — | abre Cámara | — | — |
| 15 `QSID_GALLERY` | `Galería` | `Galería` | A | 1×1, 2×1 | Herramientas | siempre | — | abre Galería | — | — |
| 16 `QSID_CRONO` | `Cronómetro` | `Cronómetro` | T | 1×1, 2×1 | Herramientas | siempre | `gCronoSt == CRONO_RUN` | pausa si corre, si no arranca | — | `En marcha` / `En pausa` / `Detenido` |
| 17 `QSID_LOCK` | `Bloquear` | `Bloquear ahora` | A | 1×1, 2×1 | Sistema | hay PIN/contraseña (`gLockType≠0`) | — | cierra cortina y **suspende** la pantalla (al despertar sale el bloqueo, §9) | — | — |
| 18 `QSID_POWEROFF` | `Apagar` | `Apagar` | A | 1×1, 2×1 | Sistema | `POWEROFF_ON` | — | cierra cortina y abre "desliza para apagar" (§10) | — | — |
| 19 `QSID_NTP` | `Hora` | `Sincronizar hora` | A | 1×1, 2×1 | Sistema | Wi-Fi habilitado y sin modo avión | — | `ntpRequestSync(true)` (no hace nada si ya está en curso, en modo avión o sin red) | — | — |
| 20 `QSID_VOLUME` | `Volumen` | `Volumen` | S | 4×1 | Sistema | el ES8311 contestó (`flexAudioAvailable`) | — | (deslizador, escribe el registro del códec) | Ajustes | `"%d%%"` |
| 21 `QSID_MUTE` | `Silencio` | `Silenciar` | T | 1×1, 2×1 | Sistema | audio disponible | `flexAudioMuted()` | conmuta silencio | Ajustes | `Silenciado` / `Con sonido` |
| 22 `QSID_DND` | `No molestar` | `No molestar` | T | 1×1, 2×1 | Sistema | siempre | `gDnd` | conmuta No molestar (NVS `flexphone/dnd`) | Ajustes | `Sin avisos` / `Avisos, sin sonido` / `Avisos normales` |
| 23 `QSID_GLASSFX` | `Vidrio` | `Intensidad del vidrio` | S | 4×1 | Pantalla | siempre; **solo se ve con Liquid Glass** (`qpCtlShown`) | — | toque = botón restablecer (nivel 50) | — | `Sutil` (<35) / `Normal` / `Intenso` (>65) + `" %d%%"` |

Categorías del catálogo (`QP_CAT_NAME`): `"Conectividad"`, `"Pantalla"`, `"Sistema"`, `"Herramientas"`.

Subtítulos de red (`Conn.h:146-176`): Wi-Fi → `(No disponible)` (sin Wi-Fi compilado), `(Modo avión)`,
`(Desactivado)`, `(<SSID>)` o `(Conectado)` si hay red, `(Conectando...)`, `(Buscando redes...)`, `(No conectado)`.
BLE → `(No disponible)`, `(Desactivado al compilar)`, `(Modo avión)`, `(Visible como "FlexOS")`, `(Desactivado)`.

Iconos (`QuickPanel.h:350-510`, vectoriales, firma `(cx, cy, s, col)`): Wi-Fi (2 arcos + punto), avión (alas delta,
fuselaje, morro, cola), BLE (runa), sol (círculo + 8 rayos), luna (círculo menos círculo desplazado), vidrio (2 rects
solapados), vidrio+reflejo, restablecer (flecha circular), batería con rayo, engranaje (8 dientes + hueco), señal (4
barras), monitor, actualizar (flecha abajo + base), carpeta, cámara, imagen (marco + sol + montañas), cronómetro,
candado, encendido (IEC 5009: arco −68°..248° + barra), reloj, lápiz, más, menos, altavoz (ondas según volumen real:
>5 una, >45 dos), altavoz tachado, campana tachada (DND). El recorte de los glifos con hueco toma el color **del píxel
real ya compuesto** (`qpIcoBgAt`). En LVGL: usar una fuente de iconos o imágenes vectoriales con fondo transparente
(no hace falta "recortar" con color).

> **Rarezas del catálogo a no copiar:** (1) el control "Modo oscuro" está **encendido cuando el tema es CLARO**
> (`qpStTheme` = `!gDark`): el título contradice al estado; en IDF el ON debe significar oscuro (o titularlo
> "Tema claro"). (2) "Ahorro Ultra" no se persiste (vuelve a 360 MHz en cada arranque) y cambia la frecuencia con
> `setCpuFrequencyMhz`; en IDF usar `esp_pm_configure` y decidir si se guarda.

### 5.4 Disposición de fábrica (`QP_FACTORY`, `QuickPanel.h:683-708`) y su posición resultante

Orden: `WIFI 2×1`, `AIRPLANE 2×1`, círculos `THEME, POWERSAVE, GLASS, CRONO, NTP, DND, LOCK, FILES, CAMERA, GALLERY,
SETTINGS` (1×1), `BRIGHT 4×1`, `VOLUME 4×1`, `DEX 2×1`, `OTA 2×1`, `CONN 2×1`. Orientación H, visibles,
`qpGrows = 3`. Los no disponibles se saltan (sin hueco).

Con todo disponible (11 círculos, audio presente), coordenadas en pantalla con scroll 0 (contenido empieza en y=116):

| Bloque | x | y | w×h |
|---|---|---|---|
| Wi-Fi (cápsula) | 16 | 116 | 218×74 |
| Modo avión (cápsula) | 246 | 116 | 218×74 |
| Tarjeta de círculos (3 filas) | 16 | 202 | 448×330 |
| Brillo (deslizador) | 16 | 544 | 448×74 |
| Volumen (deslizador) | 16 | 630 | 448×74 |
| Modo PC / Actualizaciones | 16 / 246 | 716 | 218×74 |
| Conectividad | 16 | 802 (fuera de la vista: requiere scroll; scroll máx. 110 px) | 218×74 |

### 5.5 Motor de maquetación (`qpLayout`, `QuickPanel.h:871-927`) — lógica pura

1. Los elementos **1×1** no van en el flujo: se recogen en orden en `qpTiles[]` y se dibujan **dentro de la tarjeta
   expandible**. La tarjeta se emite en la posición de flujo del **primer** 1×1 de la lista (mover ese círculo en el
   editor mueve la tarjeta entera); si todos los 1×1 están detrás, la tarjeta va al final.
2. El resto (2×1, 4×1, 2×2) se empaqueta por **filas de 4 columnas**: si no cabe, salta de fila; alto de fila =
   máximo de sus bloques; `QP_VGAP` entre filas.
3. En edición se añade al final el bloque **"Añadir un control"** (448×74).
4. `qpContentH` = alto total; scroll máximo = `max(0, contentH − 650)`.
5. Alto de la tarjeta: entre `qpGroupH(2)` y `qpGroupH(min(5, max(2, filas reales)))`; scroll interno máximo
   `filas·98 − (alto − 36)`.

Solo cuentan elementos visibles **y mostrables ahora** (`qpCtlShown`).

### 5.6 Layout y aspecto de cada parte (`QuickPanelGlass.h`)

**Fondo de la cortina**: el fondo de lo que hay debajo (escritorio o captura de la app) reducido a 1/4 (120×200),
desenfocado (caja de radio 1..3, según nivel de intensidad), teñido con `TH_GLASS2` al 40/255 (solo Liquid Glass),
velado con `TH_PAGE` a α = 152 + ajuste por intensidad (136..184; **255 opaco en estilo Plano**) y con un brillo
`TH_SURF2` extra (18→0) en el 20 % superior; se expande bilinealmente a 480×800 (`qpGlassBuild/qpGlassRows`,
`:741-815`). En LVGL: snapshot del fondo + blur [02 §4] o una imagen pre-desenfocada; el resultado visual es un velo
casi opaco del color de página con algo de color del fondo.

**Material de cada superficie** (`qpGlassSurface`, `:52-125`): tinte adaptativo por luminancia (mezcla base ±12 según
diferencia de luminancia con el fondo), especular blanco que decrece en el 45 % superior (máx. 70 px), sombreado negro
en el 55 % inferior (máx. 90 px), borde de 1 px claro arriba/oscuro abajo con peso distinto por lado. Mezclas base
(`qpMixAdj`, `:146-155`): tarjetas/módulos **128**, círculo apagado **112**, acento **178**, cabeceras de editor/catálogo
**168**; con nivel de intensidad ≠50 se desplazan `d·12/50` (hacia sutil) o `d·24/50` (hacia intenso); **en Plano son
255** (sólido). Sombra de tarjetas: 3 líneas bajo el borde inferior, alfa `46−12k`. Colores: `qpCard = thCard2()`,
`qpTileOff = mix(thCard2, TH_TXT, 34)`, `qpCapOn = mix(thCard2, TH_PRIM, 70)`.

**Cabecera fija (0..115)** (`qpDrawHeader`, `:202-224`):

| Elemento | Geometría | Estilo |
|---|---|---|
| Hora | tamaño **6** en (16, 22) | `TH_TXT`; formato de la barra de estado |
| Fecha corta | tamaño 2 en (16 + ancho_hora + 14, 54), recortada en x=304 | `TH_TXT2`; ej. `sáb, 4 jul` |
| Estado de red | tamaño 1 en (16, 84), recortado en x=304 | `TH_TXT2`; `Modo avión activo` o subtítulo Wi-Fi |
| Botón lápiz (editar) | círculo r=22 centrado en (338, 50) | vidrio `qpCard`; glifo 26 px `TH_TXT` |
| Botón apagar | círculo r=22 en (390, 50) + contorno `mix(TH_BORDER, TH_DANGER, 140)` | glifo encendido 26 px `TH_DANGER` |
| Botón ajustes | círculo r=22 en (442, 50) | glifo engranaje 26 px `TH_TXT` |

No se muestra batería (no hay sensor). Zona táctil de cada botón: cuadrado de ±24 px alrededor del centro.

**Asa de cierre (franja 766..799)**: píldora 88×6 r=3 en (196, 780), `TH_MUTE` (`qpDrawFooter`).

**Borde móvil de la cortina** (mientras no está abierta del todo, `qsRender`, `:1101-1110`): sombra de 18 filas bajo
el borde con alfa `70·(1 − k/18)` de `TH_SHADOW`, y asa 56×5 r=2 en (212, borde − 14), `TH_MUTE`. Por encima del borde
se ve el panel compuesto; por debajo, el fondo original.

**Cápsula / módulo 2×1** (`qpDrawModule`, `:266-300`): superficie r=20 con tinte `qpCapOn` (encendido) o `qpCard`.
Círculo de icono r=22: tinte `TH_PRIM` (encendido, mezcla 178) o `qpTileOff` (112); glifo 30 px `TH_ONACC`/`TH_TXT2`.
* Orientación **H**: icono en (x+36, y+h/2); título tamaño 2 en x+70 (y = centro−17 con subtítulo, centro−8 sin él),
  subtítulo tamaño 1 en centro+5, `TH_TXT2`, ambos recortados en x+w−12.
* Orientación **V**: icono en (x+w/2, y+36); título tamaño 1 centrado en y+66; subtítulo en y+80.
* Destello al tocar: velo `TH_TXT` α 110 → 0 en **200 ms** (`QP_FLASH_MS`).

**Deslizador 4×1** (`qpDrawSliderBody`, `:237-263`): pista de alto `th = h−20 = 54` (mín. 40) en y+10, radio th/2,
vidrio `TH_TRACK`; relleno desde la izquierda de ancho `th + (w−th)·pct/100` (nunca menor que el diámetro) en vidrio
`wallAccent()` (mezcla 178); icono 30 px en el centro del extremo izquierdo (`TH_ONACC` sobre el relleno); valor
tamaño 2 alineado a la derecha en x+w−20, centrado verticalmente, `TH_ONACC` si el relleno pasa de w−70, si no
`TH_TXT2`. El deslizador de **intensidad del vidrio** reserva a la derecha un **botón cuadrado de restablecer** de
th×th (vidrio `TH_TRACK`, icono restablecer 26 px `TH_MUTE` si ya está en 50, `TH_TXT` si no); su pista mide
`w − th − 8`.

**Tarjeta de círculos** (`qpDrawGroup`, `:305-385`): superficie 448×alto r=26 `qpCard`; contenido recortado al
interior (y+14 .. y+h−22, x+2 .. x+w−3), con scroll interno. Por círculo k (fila k/4, col k%4): centro
`(82/187/292/397, gyTop + fila·98 + 31)`; círculo r=31 vidrio `TH_PRIM`(ON)/`qpTileOff`; glifo 32 px; nombre tamaño 1
centrado en cy+39 (`TH_TXT` si ON, `TH_TXT2` si no), recortado a 99 px; subtítulo tamaño 1 `TH_TXT2` en cy+53 si
existe. Asa inferior: píldora 56×6 r=3 en (x+w/2−28, y+h−14) `TH_MUTE`. Indicador de scroll interno (solo si hay más
filas de las visibles): barra 3 px de ancho en x+w−8, alto `inner²/total` (mín. 24), `TH_MUTE`.

### 5.7 Interacción con el panel abierto — máquina de gestos (`qpPanelTouch`, `:1446-1714`)

Al apoyar se decide QUÉ superficie manda y no cambia hasta soltar (`qpG`: `QG_NONE, QG_PENDING, QG_CURTAIN, QG_SCROLL,
QG_GSCROLL, QG_RESIZE, QG_SLIDER, QG_EDDRAG, QG_CATSCROLL`). Umbrales: `QP_DRAG_TH = 8 px` vertical (16 px en
horizontal), `QP_LONG_MS = 480`, goma elástica en extremos `×0.42`.

| Dónde se apoya | Modo | Al mover | Al soltar sin mover |
|---|---|---|---|
| Cortina a medio abrir (`qsPanelY < 800`) | `CURTAIN` (toda la superficie es la cortina) | sigue al dedo 1:1 | ver "soltar la cortina" |
| Cabecera (y<116) sobre un botón | `PENDING` | si se mueve >8 px → pasa a `CURTAIN` (arrastrar desde la cabecera cierra) | lápiz → **editor**; apagar → cierra cortina y abre "desliza para apagar"; engranaje → Ajustes |
| Cabecera fuera de botones | `CURTAIN` | arrastra la cortina | no cierra (tocar la cabecera no cierra) |
| Franja inferior (y>766) | `CURTAIN` | arrastra | **cierra** (anima a 0) |
| Asa de la tarjeta (desde 10 px encima de la franja del asa hasta 14 px debajo del borde, todo el ancho) | `RESIZE` | el alto sigue al dedo 1:1 con goma fuera de rango (tope ±40 px) | "snap" a filas completas + guardado diferido |
| Deslizador (pista) | `SLIDER` | valor = `(x − bx − th/2)·100 / (w − th)`, 0..100, **en vivo** | guarda prefs (diferido) |
| Botón restablecer del deslizador de intensidad | `PENDING` | | restablece nivel 50 |
| Tarjeta de círculos / módulo / vacío | `PENDING` | >8 px vertical (o 16 horizontal): si nació en la tarjeta y la tarjeta tiene más filas → `GSCROLL`, si no `SCROLL` | **toque**: círculo o módulo → ejecuta el control (+ destello); en el vacío **no cierra** |
| Igual, mantenido 480 ms sin mover | | | **pulsación larga** → acción secundaria (`detail`, normalmente abre Ajustes/Conectividad) |

**Deslizadores en vivo** (`:1491-1563`):
* **Brillo**: `setBacklight(v)` en cada movimiento (PWM real, mínimo efectivo 5 %); recompone solo la banda del slider.
* **Volumen**: `flexAudioSetVolume(v)` en cada movimiento.
* **Intensidad del vidrio**: mientras el dedo está abajo solo se mueve el indicador, cuantizado a pasos de **5**
  (`GLASS_LVL_STEP`); al **soltar** se aplica una vez (`qpGlassFxCommit`, `:1179-1193`): ajusta los parámetros del
  material, invalida cachés de vidrio, recompone el escritorio y la cortina, guarda prefs (diferido) y, si había una app
  debajo, la repinta al cerrar la cortina (`themeChanged(false)`).
* Al soltar cualquier deslizador: `qpSavePrefs = true` → en el siguiente `qsTick`, **después de publicar el cuadro**:
  `cfgSavePrefs(); flexAudioSavePrefs();` (la escritura en flash nunca cae dentro del gesto).

**Soltar la cortina** (`:1594-1611`): `qsVel` = velocidad filtrada (τ 45 ms, `dt` 1..100). Si `qsVel > 0.45 px/ms` →
abrir del todo; `< −0.45` → cerrar; si no, abrir si `qsPanelY ≥ 40 %` de 800 (320 px), si no cerrar. Toque sin mover
con el panel abierto: solo cierra si nació en la franja inferior.

**Scroll del contenido y de la tarjeta**: 1:1 con goma en los extremos; al soltar, inercia con decaimiento
exponencial τ = **190 ms** y rebote de vuelta al límite con τ = **90 ms** (`qpScrollAnimStep`, `:1302-1342`).

### 5.8 Animaciones (todas por reloj, no bloqueantes)

| Animación | Duración | Curva | Ref. |
|---|---|---|---|
| Abrir / cerrar cortina | `150 + distancia·150/800` ms (150..300) | ease-in-out cúbica (`p<0.5 ? 4p³ : 1−4(1−p)³`) | `qsAnimTo :1211`, `qsAnimStep Edit:389` |
| Tocar durante la animación | cancela y devuelve el control al dedo desde la posición actual | — | `Edit:513-521` |
| "Snap" del alto de la tarjeta | `140 + 2·distancia` ms, máx. 420 | ease-out-back con rebote pequeño (`c1 = 0.55`), acotado a los límites | `qpGroupSnap :1266`, `qpGroupAnimStep :1284` |
| Destello de control | 200 ms | lineal α 110→0 | `:168` |
| Destello de rechazo (editor) | 350 ms | lineal α 120→0 `TH_DANGER` | `:418` |
| Inercia / rebote de scroll | τ 190 / 90 ms | exponencial | `:1302` |

Al cerrarse del todo (`qsSettleClosed`, `:1195-1210`): se vuelca el fondo original, se liberan captura, vidrio y
panel compuesto (hasta ~2,3 MB de PSRAM: `qsBuf` 768 KB + `qsGlassFull` 768 KB + `qsAppSnap` 768 KB + 48 KB).

### 5.9 Modo edición ("Editar panel", `QuickPanelEdit.h:30-311`)

Se entra con el **lápiz**. Trabaja sobre una **copia** (`qpEdIt`); nada se guarda hasta "Listo"; un reinicio a mitad
deja la configuración anterior intacta. No se entra si el OTA posee la pantalla.

**Cabecera del editor** (96 px, `qpDrawEditHeader`, `Glass:486-496`): banda de vidrio `TH_GLASS2` (mezcla 168) de
480×96; `"Cancelar"` tamaño 2 en (16, 20) `TH_TXT2`; `"Editar panel"` tamaño 2 centrado en y=20 `TH_TXT`; `"Listo"`
tamaño 2 alineado a la derecha en x=464, y=20, `TH_PRIM`; `"Restablecer diseño"` tamaño 1 centrado en y=56 `TH_TXT2`;
divisor 1 px `TH_DIV` en y=95. El cuerpo ocupa y=96..799 (sin franja de asa).

Zonas táctiles de la cabecera: y=6..50 → `x<150` **Cancelar** (descarta la copia), `x>330` **Listo**
(`qpEditCommit`: normaliza; si quedó vacío → fábrica; guarda NVS una vez; vuelve al panel); y=50..96 y
x=130..350 → **Restablecer diseño** (carga la fábrica en la copia; lo vivo no cambia hasta "Listo").

**Adornos por bloque** (`qpDrawEditChrome`, `Glass:401-422`):

| Adorno | Módulo / cápsula | Círculo de la tarjeta | Zona táctil |
|---|---|---|---|
| **"−" quitar** | círculo r=14 `TH_DANGER` en la esquina (x+4, y+4) + signo menos 22 px `TH_ONACC` | círculo r=12 en (cx−27, cy−27), menos 20 px | módulo: `x ≤ bx+26 && y ≤ by+26`; círculo: `x ≤ cx−11 && y ≤ cy−11` |
| **Asa de redimensión** (solo si el control admite otro tamaño) | barra 6×32 r=3 `TH_PRIM` en el borde derecho (x+w−10, y+h/2−16) + barrita interior 4×16 | barra 6×24 en (cx+44, cy−12) | módulo: `x ≥ bx+bw−26`; círculo: `x ≥ cx+30` y `|y−cy| ≤ 24` |
| **Rotar orientación** (solo controles H+V, solo módulos) | círculo r=13 en (x+6, y+h−6) `mix(qpCard, TH_TXT, 70)` + arco y punta de flecha | — | `x ≤ bx+30 && y ≥ by+bh−30` → alterna H/V |
| Rechazo | velo `TH_DANGER` α120→0 en 350 ms sobre el bloque | | |

**Gestos del editor**:
* **Quitar**: inmediato (`qpEditRemove`); si es el último elemento se rechaza con destello (el panel nunca queda vacío).
* **Redimensionar**: arrastre horizontal desde el asa; cada **46 px** salta al siguiente tamaño válido en esa dirección
  en el orden `1×1 → 2×1 → 4×1 → 2×2` (`qpNextSize`); si no hay → destello de rechazo. Un círculo que pasa a 2×1 sale
  de la tarjeta y se convierte en cápsula.
* **Mover**: mantener **320 ms** (`QP_EDLONG_MS`) sobre un bloque o círculo → se levanta un **fantasma** que sigue al
  dedo (encima de todo, con sombra de 4 líneas α `96−20k`; círculo = r=31 `mix(qpCard,TH_PRIM,90)` + nombre; módulo =
  su render + contorno `TH_PRIM`) y en el sitio original queda un **hueco de inserción** (relleno `TH_PRIM` α60 +
  contorno `TH_PRIM`). Al pasar sobre otro elemento se **reordena en tiempo real** (`qpEditMove`). Cerca de los bordes
  (y < 156 o y > 730) hay auto-scroll a **0.45 px/ms**.
* **Scroll** del editor: arrastre vertical >8 px.
* **"Añadir un control"** (último bloque): vidrio `mix(qpCard, TH_PRIM, 70)` + contorno `mix(TH_BORDER,TH_PRIM,150)`
  r=20, icono "+" 28 px en (x+w/2−92, centro) y texto `"Añadir un control"` tamaño 2 → abre el **catálogo**.

### 5.10 Catálogo "Añadir un control" (`qpDrawCatalog Glass:523-564`, `qpCatTouch Edit:326-377`)

* Lista: **solo** los controles mostrables ahora y que no estén ya en la copia (`qpCatBuild`), en orden de id.
* Cabecera fija 96 px (vidrio `TH_GLASS2`, mezcla 168): `"Atrás"` tamaño 2 en (16, 22) `TH_TXT2`; `"Añadir un control"`
  tamaño 2 centrado en y=22; `"Solo se listan controles con función real"` tamaño 1 centrado en y=56 `TH_TXT2`;
  divisor en y=95. Hoja de vidrio `TH_GLASS2` (mezcla 128) bajo la cabecera.
* Rejilla de 4 columnas: centros x = 67, 182, 297, 412; fila de **112 px**; primera fila con centro en
  y = 96 + 12 + 31 − scroll; círculo r=31 (`TH_PRIM` si seleccionado, `qpTileOff` si no), glifo 32 px, nombre tamaño 1
  en cy+39 (`TH_TXT`), nombre de categoría tamaño 1 en cy+53 (`TH_TXT2`), ambos recortados a 101 px. Alto de contenido
  `filas·112 + 24`; vista 704 px. Vacío: `"No queda ningún control disponible"` tamaño 2 `TH_MUTE` centrado en y=156.
* Toque en la cabecera con `x<150` → vuelve al editor sin perder lo editado. Toque en un control → se añade al final
  con su primer tamaño permitido y orientación H (si la admite), se vuelve al editor con el scroll al final. Scroll
  vertical >8 px con goma; al soltar se acota.

### 5.11 Persistencia (`QuickPanel.h:656-829`)

* NVS **namespace propio `flexqs`**, clave **`qp1`**, blob de **124 bytes** (`QP_BLOB_N = 4 + 24·5`):
  `[0]='Q'`, `[1]=versión (QP_CFG_VER = 2)`, `[2]=n elementos (≤24)`, `[3]=qpGrows`, y 24 registros fijos de
  `{id, w, h, ori, vis}` (1 byte cada uno; `ori` 1 = H, 2 = V).
* Carga perezosa una vez por arranque (`qpLoad`). Blob ausente, de tamaño distinto, sin 'Q', de versión 0 o futura,
  o con n>24 → **fábrica** (`"[QP] configuracion ausente o invalida: valores de fabrica"`).
* `qpNormalize` (puerta única): quita ids desconocidos, duplicados y **no disponibles ahora**; corrige tamaños y
  orientaciones no permitidos; acota filas a 2..5. `qpAdoptNew(verGuardada)`: al subir `QP_CFG_VER`, añade al final los
  controles de fábrica nuevos sin tocar lo del usuario (v1→v2 añadió No molestar).
* Se escribe **solo** al pulsar "Listo" y tras un "snap" del alto de la tarjeta (diferido a `qsTick`).
* El restablecimiento de fábrica borra `flexqs` (§13).

> **Rareza:** `qpNormalize` trata como "no disponible" controles que dependen de un **estado** (Bloquear sin PIN,
> Sincronizar hora en modo avión). Si el usuario pulsa "Listo" en ese momento, el control desaparece del blob para
> siempre. En IDF separar "disponible por hardware" (se normaliza) de "visible ahora" (solo se oculta al dibujar).

### 5.12 Rendimiento / buffers (contexto para la migración)

El panel compone por **bandas sucias** en un buffer propio y solo publica las filas que cambian; la composición es
perezosa (solo las filas que la cortina deja ver). Durante el arrastre no se dibuja nada nuevo: es una copia de filas.
Cuesta ~2,3 MB de PSRAM mientras está abierto y lo libera al cerrarse. Hay perfilado (`QP_PROF = 1`) que imprime
`"[QP] %s: %lu cuadros, medio %lu us, peor %lu us, lentos(>%d ms) %lu"` al cerrar.

### 5.13 Notas de migración del panel rápido

* **Lógica pura reutilizable tal cual** (extraer a `flex_portable/qs_model.c` con **pruebas de host**): `QS_REG`
  (como tabla de datos con punteros a funciones de backend), `qpFirstSize`, `qpSizeAllowed`, `qpNextSize`, `qpFactory`,
  `qpNormalize`, `qpAdoptNew`, `qpSerialize`/`qpDeserialize` (formato **byte a byte** para leer el blob `flexqs/qp1`
  existente), `qpLayout` (devuelve rectángulos), `qpGroupSnap` (cálculo del alto objetivo), `qpEditMove/Remove/Add`.
  Pruebas: blob v1 sin DND → se adopta DND al final; blob con id 30 → descartado; duplicados; 2×2 de un control que
  no lo admite → corregido a su primer tamaño; layout de fábrica → coordenadas de §5.4; arrastrar el primer 1×1 mueve
  la tarjeta.
* **LVGL**: la cortina es un `lv_obj` de 480×800 en `lv_layer_top()` cuyo `y` se anima (`lv_anim` con
  `lv_anim_path_ease_in_out` y la duración de §5.8) o sigue al dedo (`LV_EVENT_PRESSING` con la capa de gestos).
  Contenido en un contenedor con scroll (`LV_SCROLL_MOMENTUM`, elasticidad ON) de 116..765; la tarjeta de círculos es
  otro contenedor con scroll propio y alto animado; el asa usa `LV_EVENT_PRESSING`. Hay que resolver el **scroll
  anidado** con la misma regla de propiedad (si nace en la tarjeta y la tarjeta puede desplazarse, se desplaza la
  tarjeta) — en LVGL: `LV_OBJ_FLAG_SCROLL_CHAIN` desactivado en la tarjeta.
* Material: el vidrio del panel se calcula **una vez** desde el fondo (snapshot + blur reducido) y se reutiliza; no
  usar `lv_obj` con blur por cuadro. Liberar el snapshot al cerrar (equivalente a `qpFreeBuffers`) y registrar el
  panel en el "soltar memoria" (§17.5).
* Guardados: brillo/volumen/intensidad/alto de tarjeta → `flex_cfg_set_*` (cola del escritor único de
  `flex_storage`), nunca dentro del evento de arrastre.
* No copiar: la vía del borde derecho rota (§5.1), el "Modo oscuro" invertido (§5.3), la normalización por estado
  (§5.11).

---

## Teclado del sistema

> Fuentes: `FlexOS_Ultra_Keyboard.h` (todo), `FlexOS_Ultra_KeyboardSettings.h` (todo), `FlexOS_Ultra_Prefs.h:147-249`,
> `FlexOS_Ultra_Power.h:276-578` (pantalla de contraseña), `FlexOS_Ultra_Lock.h:37-76` (colores de la clave),
> `FlexOS_Ultra_AppNotes.h:376-531` (sesión de Notas), `FlexOS_Ultra_Types.h:184-190` (interruptores `KB_*_ON`, todos a 1),
> y los usos en `Network.h` (Wi-Fi), `AppWeather.h` (buscador), `FileKit.h` (nombre de archivo) y `FlexOS_Browser_Bridge.h`.
>
> **Implementación ESP-IDF:** `components/flex_ui/src/widgets/flex_kb_layout.{h,c}` (lógica pura, sin LVGL) y
> `components/flex_ui/src/widgets/flex_kb.{h,c}` (widget LVGL). Pruebas: `tests/host/test_kb.c` (contra el código Arduino
> extraído, bit a bit) y la escena `teclado` del simulador (`sim/scenes/scene_kb.c`).
> **Estado: NO PROBADO EN HARDWARE REAL** (solo host y simulador LVGL en el PC).

### T.1 Quién lo usa y con qué variante

El teclado es UN recurso del sistema con una sola geometría, un solo hit-test y una sola tabla de mapas; cada superficie
cambia solo los colores, la etiqueta de la tecla enter, la reserva inferior y los extras.

| Superficie | Colores | Enter | Extras (barra + chips) | `kbBotReserve` | Fn al tocar | Acentos |
|---|---|---|---|---|---|---|
| Notas (`noteEditorEnter`) | sistema (`kbCol*`) | `ent` (salto de línea) | sí | `NAV_H` = 64 si hay barra | sí (vía rápida) | sí |
| Clave al **crear** (`lsuEnter`, `lsuVerify=false`) | página: `PAGE_BG` / `SET_CARD_BG` / `SET_TXT_HI` | `OK` | **no** (seguridad) | 0 | no | no |
| Clave al **verificar** (`lsuVerify=true`) | sobre fondo: `TH_WALLPANEL` / `TH_WALLSURF` / `TH_ONWALL` | `OK` | **no** | 0 | no | no |
| Wi-Fi (`wifiRenderPass`) | sistema | `Conectar` | no | 0 | no | no |
| Ajustes del teclado → editor de atajos | sistema | `OK` | no | 0 | no | no |
| Clima (buscador), FileKit (nombre) | sistema (FileKit: tinte `TH_KEYPANEL`) | `wt(WT_SEARCH)` / `Guardar` | no | 0 | no | no |

Regla de seguridad (Keyboard.h:136-140, 155-158): en las pantallas de clave **no** hay barra de herramientas, chips,
portapapeles, ajustes ni reserva de barra de navegación: serían una vía de escape.

### T.2 Geometría (Fase A, `kbApplySize`, NVS `kbsize`)

Rejilla de 10×3 teclas + fila de 6 teclas de función. Todo sale de 4 números; la rejilla se centra en 480
(`KB_X = (480 - (10·KW + 9·GAP)) / 2`, mínimo 2).

| `kbsize` | Tecla KW×KH | GAP | `KB_X` | Rejilla | `KB_Y` (reserva 0) | Panel (`kbPanelTop`) | Fila fn (`kbFuncY`) | Alto del panel |
|---|---|---|---|---|---|---|---|---|
| 0 Compacto | 43×50 | 4 | 7 | 466 | 578 | 574 | 740 | 226 |
| 1 Normal (defecto) | 45×60 | 2 | 6 | 468 | 546 | 542 | 732 | 258 |
| 2 Grande | 45×72 | 2 | 6 | 468 | 498 | 494 | 720 | 306 |

* `KB_Y = kbRowsTop() = 800 − kbBotReserve − 4·(KH+GAP) − 6`. Los extras crecen **hacia arriba**: no empujan las teclas.
* `kbPanelTop() = KB_Y − 4 − kbTopH()`, `kbTopH = barra (56) + chips (32)`; `kbToolbarY = kbPanelTop + 4`;
  `kbChipsY = kbToolbarY + barra`. Notas con reserva 64 y extras (Normal): `KB_Y = 482`, panel en 390, barra en 394,
  chips en 450.
* Tecla `(fila r, columna c)`: `x = KB_X + c·(KW+GAP)`, `y = KB_Y + r·(KH+GAP)`.
* Fila de función: pesos `{0.135, 0.125, 0.110, 0.420, 0.100, 0.110}` (shift, capa, idioma, espacio, borrar, enter) sobre
  `usable = rejilla − 5·GAP`; `w = (int)(usable·peso + 0.5f)` (mín. 20), `x` acumulado con GAP. Resultado `(x, w)`:
  Compacto `(7,60) (71,56) (131,49) (184,187) (375,45) (424,49)`; Normal y Grande `(6,62) (70,57) (129,50) (181,192)
  (375,46) (423,50)`. Ningún producto cae cerca de .5 (prueba de host), así que el resultado no depende de FMA.
* **Hit-test sin franjas muertas** (`kbCellAt`): el área de cada tecla es su **paso completo** (tecla + separación):
  `c = (px−KB_X)/(KW+GAP)`, `r = (py−KB_Y)/(KH+GAP)`. Fila de función (`kbFRowHit`): `fy−GAP ≤ py ≤ fy+KH+GAP`,
  `px ≥ KB_X−GAP`, y cada tecla se queda con la separación de su derecha. En los 2-4 px donde se solapan la fila 3 y la
  fila de función, la ruta rápida da la letra y la ruta "al soltar" da la función (como Arduino).
* `kbSizeCheck`: rejilla y fila dentro de 480, fila de función por encima de `800 − reserva − 4` y panel por debajo de
  y = 120. Se cumple en los tres tamaños, con y sin reserva y con extras.
* El navegador (DeX) estrecha `KW` si el lienzo es menor que la rejilla (`brKbGeomBegin`); no aplica en vertical a pantalla
  completa (pendiente si se migra DeX).

### T.3 Capas, resolución de tecla y fila de función

Mapas (`Keyboard.h:44-59`, copiados tal cual):

| Capa | Fila 1 | Fila 2 | Fila 3 |
|---|---|---|---|
| `LAYOUT_ES` | `q w e r t y u i o p` | `a s d f g h j k l ñ` | `z x c v b n m , . ?` |
| `LAYOUT_EN` | `q w e r t y u i o p` | `a s d f g h j k l ;` | `z x c v b n m , . ?` |
| `LAYOUT_NUM` | `1 2 3 4 5 6 7 8 9 0` | `@ # $ % & - _ ( ) /` | `* " ' : ; ! ? + = .` |
| `LAYOUT_EMOJI` | `:) :D :( ;) :P xD :o :\| <3 :3` | `^^ o_o >:( :'( B) -_- =) D: :v :c` | `uwu :* <_< >_> (y) !! :] [: T_T o/` |

* La capa "emoji" son **emoticonos de texto ASCII**: la fuente Outfit (ASCII + Latin-1) los dibuja todos. No hace falta
  fuente de emoji (ni la versión Arduino la tenía).
* `kbResolveKey`: con shift, `a-z → A-Z`, `ñ → Ñ` (`C3 B1 → C3 91`) y, solo en `?123`, `( → {` y `) → }`. Escribir una
  variante **apaga el shift** (shift de una pulsación); un símbolo sin variante no lo consume. Las etiquetas se pintan ya
  resueltas (en mayúscula con shift).
* Fila de función: **shift** (conmuta; pintada con `kbColFnOn` cuando está activo), **capa** (letras → `?123` → emoji →
  letras del idioma; etiqueta `?123` / `emoji` / `ABC`), **idioma** (`ES`/`EN`; si la capa activa es de letras cambia
  de mapa), **espacio** (etiqueta `espacio`, inserta `" "`), **`<-`** (borra **un carácter UTF-8 completo**, nunca medio
  byte de `ñ`), **enter** (etiqueta por superficie).
* Estado de sesión de Notas: capa codificada 0..3 (ES, EN, NUM, EMOJI) + flags idioma/shift (`noteCaptureKbState`).

### T.4 Colores y material

| Elemento | Sistema (Notas, Wi-Fi…) | Clave al crear | Clave al verificar |
|---|---|---|---|
| Panel, Liquid Glass | vidrio con tinte `TH_GLASS` | vidrio, tinte `TH_GLASS` | vidrio sobre el fondo, tinte `TH_WALLPANEL` |
| Panel, Plano | `TH_KEYPANEL` | `TH_PAGE` | `TH_WALLPANEL` |
| Tecla / texto | `TH_KEYFACE` / `TH_TXT` | `TH_SURF` / `TH_TXT` | `TH_WALLSURF` / `TH_ONWALL` |
| Tecla de función / texto | `TH_KEYALT` / `TH_TXT` | ídem | ídem (Arduino usa `kbFKey` también aquí) |
| Shift activo / texto | `TH_PRIM` / `TH_ONACC` | ídem | ídem |
| Tecla pulsada | `mix565(TH_PRIM, TH_KEYFACE, 60)` | ídem | ídem |

* Las **teclas son siempre planas** (`fillRoundRect`, radio 6) incluso con Liquid Glass: el vidrio es solo el panel (radio 0,
  de `kbPanelTop` al borde inferior).
* Contraste alto (`kbhicon`, solo superficies del sistema para panel/teclas): tecla y panel negros, texto blanco, función
  `rgb(24,24,24)`, shift y pulsada ámbar `rgb(255,210,0)` con texto negro, borde blanco.
* Opacidad del panel (`kbopa`, 40..100, solo sistema): por debajo de 100 el panel pasa a relleno **plano con alfa**
  (`opa·255/100`) aunque esté Liquid Glass, con el color `kbColPanel()`.
* Estilo (`kbstyle`): 0 redondeada (r 6), 1 cuadrada (r 0), 2 contorno (sin relleno, borde 1 px `TH_BORDER`; la pulsada sí se
  rellena). Aplica a todas las superficies.
* Tipografía (`kbfont`): talla 1/2/3 → `FLEX_FONT_S1/S2/S3` (Outfit 11/13/19) para las teclas, `min(talla, 2)` para la fila de
  función; el **tope de las mayúsculas** en `y + h/2 − dy` con `dy = 4/8/12`.
* En IDF: `TH_PRIM` → `flex_accent()` (acento del sistema; con las preferencias por defecto es el mismo `primary`).

### T.5 Entrada, destello y animaciones

* **Escritura rápida** (`kbfast`, por defecto activada): la tecla se escribe al **tocar**, sin esperar a soltar, y se ve
  hundida mientras el dedo está encima. Sin ella, se escribe al **soltar** si el gesto es un *tap* (|dx|,|dy| < 16 px y
  < 550 ms, localizado donde se apoyó) y la tecla destella al apoyar como única señal.
* Teclas de función: al soltar (tap) — confirmar o borrar tiene que salir de un toque deliberado; en Notas, con escritura
  rápida, también al tocar.
* **Destello** (`kbfx` = 60/100/160 ms, defecto 100): la tecla escrita se pinta con el color de pulsada durante ese tiempo y
  vuelve sola (por reloj, sin repintar el teclado entero). Las teclas de función no destellan.
* **Entrada**: deslizando desde abajo, **300 ms lineal**, `y = top + (int)((1 − t/300)·kbh)`, con `kbh = 800 − KB_Y`
  (clave, Wi-Fi) o `800 − kbPanelTop` (Notas). Mientras dura **no se atiende ningún toque**.
* **Sacudida** (clave equivocada, FASE 1): se desplazan solo las teclas en horizontal dentro del panel, que no se mueve.
* **Acentos** (Notas): pulsación larga en `a e i o u` de las capas de letras (umbral `kblp` 350/500/700 ms) → ventana
  elevada (`uiSurface` r10) con 40×46 por variante (r8, talla 3) encima de la tecla:
  `á à â ã`, `é è ê`, `í ì î`, `ó ò ô õ`, `ú ù û ü`. Soltar en una variante la escribe (si la vía rápida ya escribió la
  vocal, primero se borra); soltar fuera escribe la vocal si no estaba escrita.

### T.6 Preferencias (NVS `flexos`, mismas claves que Arduino)

| Clave | Tipo | Defecto | Normalización |
|---|---|---|---|
| `kbsize` | i32 | 1 | 0..2, si no 1 |
| `kbfast` | bool | true | — |
| `kbtool` / `kbpred` | bool | true / true | — |
| `kbspell` / `kbemoji` / `kbhicon` | bool | false | — |
| `kbopa` | i32 | 100 | acotado 40..100 |
| `kbstyle` / `kbfont` | i32 | 0 / 1 | 0..2 |
| `kblp` | i32 | 500 | 350/500/700 |
| `kbfx` | i32 | 100 | 60/100/160 |
| `kbsyms` | blob 4 B | 0,1,2,3 | índices 0..15 de `@ # $ % & * + = / \ ( ) [ ] < >` |
| `kbscabr` / `kbscexp` | blob 8×10 / 8×24 | `xq→porque`, `q→que`, `tb→también`, `pf→por favor` | si el tamaño no cuadra, de fábrica |

El widget las lee al crearse; `flex_kb_reload_prefs()` las reaplica al momento (Arduino: sin reiniciar).

### T.7 API IDF (`flex_kb.h`)

```c
lv_obj_t *flex_kb_create(lv_obj_t *parent, const flex_kb_cfg_t *cfg);   // parent: 480x800 en (0,0)
// cfg: flags FLEX_KB_F_WALLPAPER | _PAGE | _LANG_EN | _EXTRAS | _FN_ON_PRESS | _ACCENTS | _NO_FAST,
//      layout inicial, backdrop (FLAT/HOME/LOCK), bottom_reserve, enter_label,
//      on_text(kb, utf8, user), on_backspace(kb, user), on_enter(kb, user), user
void flex_kb_set_layout / flex_kb_get_layout, flex_kb_set_shift / _get_shift, flex_kb_set_lang_es / _get_lang_es
int32_t flex_kb_height(kb | NULL), flex_kb_top(kb), flex_kb_keys_y(kb); const flex_kb_geom_t *flex_kb_geom(kb)
void flex_kb_slide_in(kb); bool flex_kb_is_animating(kb)
void flex_kb_set_shift_x(kb, dx); lv_obj_t *flex_kb_keys_obj(kb)     // sacudida
void flex_kb_set_input_enabled(kb, en); void flex_kb_reload_prefs(kb)
```

* El teclado **no guarda texto**: avisa por callbacks. `flex_kb_buf_append()` / `flex_kb_buf_backspace()` son las
  operaciones de buffer de la clave y del editor de atajos (`lsuPassAppend`, `kbsBackField`).
* Objetos: panel (`flex_surface`, recibe todos los toques y resuelve con `flex_kb_cell_at`/`flex_kb_frow_hit`) →
  contenedor de teclas (lo que se mueve en la sacudida) → 36 teclas (`flex_surface` con material forzado a Plano) con su
  etiqueta. `scroll_chain` desactivado: teclear no desplaza la pantalla de debajo.
* Los callbacks se llaman al final de cada evento: si uno borra el teclado, usar `lv_obj_delete_async`.

### T.8 Pendiente (API preparada) y cómo se hará

* **Barra de herramientas (Fase C, `FLEX_KB_F_EXTRAS` + `kbtool`)**: 5 botones circulares r21 en `x = 48 + 96·i`,
  `y = kbToolbarY + 26`, relleno `kbColKey`, icono `kbColKeyTxt` (carita, globo, portapapeles, engranaje, tres puntos;
  hit `|px − x| ≤ 26`, `kbToolbarY ≤ py ≤ +52`). Emoji y globo se resuelven dentro del teclado (capa emoji / tecla idioma);
  portapapeles, ajustes y "más" irán a un callback `on_tool` porque abren pantallas de la app. Hoy el flag no dibuja nada ni
  reserva alto.
* **Chips (Fase F)**: franja de 32 px en `kbChipsY`; en capas de letras hasta 3 sugerencias de `flex_kb_suggest()` (ya
  implementado y probado contra Arduino: atajos del usuario, diccionario local por prefijo con plegado de tildes, emoticono
  sugerido), en `?123` los 4 símbolos de `kbsyms`. Necesita que la app pase el texto antes del cursor
  (`flex_kb_set_context`); aceptar un chip = `on_backspace` por cada carácter de la palabra + `on_text(palabra)` + `" "`.
  Fundido de entrada 140 ms; divisores 1×16 `TH_DIV` a 160.
* **Portapapeles de 12 ranuras (Fase D)**: lógica pura (`clipPush`, fijadas en NVS `clip0..clip11`) + panel de 2 columnas;
  es de la app Notas, no del widget.
* **Escritura rápida multitáctil (Fase B)**: con un solo `indev` de puntero, LVGL solo ve un dedo; la versión actual
  escribe al tocar (lo mismo para un dedo). El *rollover* de varios dedos necesita la capa de gestos del sistema (§2.3):
  el `read_cb` del táctil entrega los puntos del GT911 con su *track id* y el teclado dispara una tecla por id nuevo
  (`kbMtPoll`); el diagnóstico "el panel ha dado N dedos" sale de ahí.
* Revisión ortográfica (subrayado de Notas) usa `flex_kb_dict_has()` (ya implementado).

### T.9 Decisiones y discrepancias con Arduino

* **Teclas de función sobre el fondo**: Arduino las pinta con `kbFKey` (colores del sistema, `TH_KEYALT`/`TH_TXT`) también
  en la clave al verificar, aunque el resto del teclado use `TH_WALL*`. Se reproduce tal cual (en tema claro quedan teclas
  claras sobre el panel oscuro). Si se quiere coherencia total, es un cambio de una línea en `compute_colors()`.
* **Estilo contorno + shift activo**: Arduino no rellena la tecla shift activa en este estilo (texto `TH_ONACC` sin fondo);
  se reproduce.
* **Notas, soltar sin tap**: `handleKeyRelease` de Notas escribía también al soltar tras un arrastre; el widget usa la regla
  de la clave (solo un *tap* escribe) en todas las superficies.
* **Solape de 2-4 px fila 3 / fila de función**: Arduino podía escribir la letra al tocar y además ejecutar la función al
  soltar; aquí un toque solo hace una cosa.
* **Acento del sistema**: `TH_PRIM` → `flex_accent()` (igual con preferencias por defecto).
* **Tipografía**: Outfit LVGL 11/13/19 px para las tallas 1/2/3 (la talla 1 de Arduino era el bitmap 5×7).
* El panel en vidrio sobre el fondo usa el backdrop ya desenfocado (`FLEX_BD_LOCK`); Arduino volvía a desenfocar lo que
  había debajo (el fondo ya borroso): diferencia de matiz mínima.

---
