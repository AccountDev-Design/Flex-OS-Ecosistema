# 03b · Apps (continuación): Ajustes, hora/NTP, Clima, Juegos (Jumper) y apps descargadas

> **Documento de continuación de `03_apps_productividad.md`** (área "apps1"). Aquel cubre §0-§10 (convenciones, Reloj,
> Cronómetro, Calculadora, Calendario, kit de archivos, Notas, Paint, Archivos, Almacenamiento, Flex Storage). Este cubre
> **§11-§19**: Ajustes (todas las categorías y opciones), hora del sistema y NTP, Clima (motor Open-Meteo, app y
> widgets), Juegos → Jumper, el modelo de apps descargadas que pinta la Caja de aplicaciones, y los apartados
> consolidados del área entera (persistencia, servicios/tareas, riesgos y checklist de §1-§15).
>
> Especificación funcional para reconstruir en **ESP-IDF 5.5 + FreeRTOS + LVGL 9.6** (480×800 vertical, MIPI-DSI,
> táctil GT911). Se describe *qué se ve* y *cómo se comporta*, con las medidas exactas del código; el motor de dibujo
> Arduino **no** se reutiliza.

**Fuentes leídas por completo:** `FlexOS_Ultra_AppSettings.h`, `FlexOS_Ultra_NTP.h`, `FlexOS_Ultra_Clock.h`,
`FlexOS_Ultra_Prefs.h`, `FlexOS_Weather.h`, `FlexOS_Weather.cpp`, `FlexOS_Ultra_WeatherKit.h`, `FlexOS_Ultra_AppWeather.h`,
`FlexOS_Ultra_AppGames.h`, `FlexOS_Jumper.h`, `FlexOS_Jumper_Level.h`, `FlexOS_Ultra_PkgApps.h`.
**Leídas en parte (lo que toca a esta área):** `FlexOS_Ultra.ino` (`setup()` `:476-525`, `loop()` `:670-1017`),
`FlexOS_Ultra_AppFramework.h` (registro y ganchos `:672-768`, `navBarVisible` `:997`), `FlexOS_Ultra_Core.h` (`appTick`
`:975-995`, `navBarHandle` `:591`), `FlexOS_Ultra_Conn.h` (subtítulos Wi-Fi/BLE, `"airpl"`), `FlexOS_Account_Bridge.h`
(`accountSettingsText`), `FlexOS_OTA.cpp` (`flexOtaStatusText`), `FlexOS_Ultra_Theft.h` (`theftRowValue`),
`FlexOS_Ultra_Recovery.h` (`frEnterWizard`), `FlexOS_Ultra_KeyboardSettings.h` (filas), `FlexOS_Ultra_HAL.h`
(`setBacklight`), `FlexOS_Ultra_System.h` (`themeChanged`), `FlexOS_Ultra_Widgets.h` (widgets de Clima),
`FlexOS_Ultra_AppDrawer.h` (uso de `pkgApps`), `FlexOS_Store_Bridge.h` (apertura de paquetes), `FlexOS_Package.h`,
`tests/host/test_weather.cpp` y `tests/host/ino_compile.cpp` (pruebas existentes, en la raíz del repositorio).

Convenciones: las de **03 §0** (gestos `T.tap/T.pressed/...`, marco de app, registro, sesión, idiomas) y **02** (tokens
`TH_*`, material Liquid Glass, tipografía "tamaño N", framework). Rutas `archivo:línea` relativas a `FlexOS_Ultra/`
salvo que se diga otra cosa. Colores en hex de los valores de 8 bits del código (`rgb565(r,g,b)`), cuantizados luego a
RGB565. Equivalencia de tamaños de letra (02 §5.1): tamaño 1 = mapa de bits 5×7 (avance 6 px); tamaño N ≥ 2 = Outfit
con caja de línea `8·N` px y em ≈ `6.27·N` px (2 → 13 px, 3 → 19 px, 4 → 25 px, 5 → 31 px, 6 → 38 px, 11 → 69 px).

---

## Índice (continuación)

11. Ajustes (todas las categorías y opciones)
12. Hora del sistema y NTP
13. Clima: motor Open-Meteo, app y widgets
14. Juegos: Jumper
15. Apps descargadas: modelo de la Caja de aplicaciones
16. Persistencia consolidada del área (§1-§15)
17. Servicios, tareas y orden en `loop()`
18. Riesgos de migración y rarezas que no hay que copiar
19. Checklist de funcionalidades (§1-§15)

---

## 11. Ajustes

### 11.1 Propósito, registro y cómo se llega

* App id **10** `IC_AJUSTES`, nombre `"Ajustes"` (EN "Settings", FR "Réglages", PT "Ajustes", IT "Impostazioni"),
  banderas **`APP_CUSTOM_HEADER`** (sin `APP_OWN_TOUCH`: la barra de navegación y los gestos iOS los atiende el sistema
  antes que la app, `Core.h:986-990`), categoría Sistema, en el **dock** de fábrica (`AppFramework.h:757`).
  Ganchos `H_SETTINGS = { backLayer NULL, backScreen settingsHandleBack, suspend setSuspend, resume setResume, close
  NULL, saveSess setSaveSess, loadSess setLoadSess, bgWork setBgWork, shed NULL, dirty NULL }` (`AppFramework.h:679`).
* **Nunca se puede ocultar** (`appCanHide`, `Prefs.h:124`) y es una de las cuatro apps del **Modo seguro**.
* Entradas:
  * icono en Inicio / dock / Caja de aplicaciones (transición estándar de apertura, 02 §8.7);
  * `settingsJumpSecurity()` (`AppSettings.h:594`): abre directamente **"Seguridad y privacidad"** (`setView = 1,
    setSel = 6, setScroll = 0`), cerrando antes la app activa. La usan Galería/Multimedia/Música cuando el usuario quiere
    proteger medios y aún no tiene PIN (`MediaKit.h:403`);
  * `settingsJumpAccount()` (`:601`): abre **"General"** (donde está la fila Flex Account). La usa Flex Cloud cuando no
    hay cuenta o hay que volver a vincular (`CloudKit.h:892`);
  * menú del Modo seguro, opción 3 → `enterApp(IC_AJUSTES)` (`Recovery.h:684`);
  * vuelta desde las subpantallas que abre (Flex Account `accountFinish` → `gState = ST_APP; settingsRender()`, teclado,
    conectividad, Wi-Fi, clave, protección contra robo).
* **Dos niveles de navegación** en una variable propia `setView` (0 = lista de categorías, 1 = pantalla de una
  categoría), **no** en `gState`: así funciona igual dentro de una ventana de Modo PC (DeX), donde `dexHostRun` descarta
  los cambios de `gState` de la app hospedada (`:94-98`). Hospedada solo se omite la animación.

### 11.2 Elementos comunes de las dos pantallas

* La app pinta **la página entera 480×800** sobre `TH_PAGE` (`settingsPaintPage`, `:482`), incluida una barra de estado
  propia (`settingsDrawChromeDark`, `:453`; se omite hospedada):
  * hora `clkStrBar` (12 h → `"1:23 PM"`, 24 h → `"13:23"`) tamaño 2 `TH_TXT` en (16, 16);
  * fecha corta `buildShortDate` (`"sáb, 4 jul"`) tamaño 1 `TH_TXT` en (16 + ancho_hora + 14, 20);
  * glifo Wi-Fi `drawWifi(414, 28, 11)` y batería `drawBattery(434, 20, 30×15, 82 %)` en `TH_TXT` — **ambos fijos** (no
    reflejan el estado real; ver §18);
  * glifos de la barra de navegación (modo Botones: triángulo en x≈80, círculo r12 en (240, 756), cuadrado 22×22 r4 en
    (389, 745); modo Gestos: indicador de 210 px `TH_NAV`). En modo Botones la barra real la **estampa el sistema** encima
    en cada volcado (`navStampBar` dentro de `flxFlush`, 01b §4.2): los glifos propios quedan tapados.
* Todo repintado se compone en el back buffer y se publica entero (`settingsRender`, `:497`); durante un arrastre solo se
  recompone la **banda con scroll** (`settingsRenderBandOnly`, `:505`) con recorte exclusivo `[top, bot)`.
* **Tarjetas**: con Liquid Glass `drawGlassCardFlat(x, y, w, h, r, TH_GLASS sobre TH_PAGE)` (vidrio cacheado: el
  material se mantiene durante el desplazamiento, 02 §4.6); sin vidrio `fillRoundRect(..., TH_SURF)`. Alias de la app:
  `PAGE_BG = TH_PAGE`, `SET_CARD_BG = TH_SURF`, `SET_CARD_GLASS = TH_GLASS`, `SET_TXT_HI = TH_TXT`, `SET_TXT_LO =
  TH_TXT2`, `SET_TXT_MUTE = TH_MUTE`, `SET_CHEV = TH_MUTE`, `SET_SIDE_SUB = TH_TXT2`, `SET_NAVPILL = TH_NAV` (`:68-76`).
* **Chevron de fila** (">"): dos segmentos AA de grosor 2.0 `TH_MUTE`: `(chx-3, chy-6)→(chx+3, chy)→(chx-3, chy+6)`
  con `chx = 446`.
* **Texto recortado** `drawTextClip(x, y, s, size, col, maxRight)` (`:111`): corta por la derecha **sin puntos
  suspensivos** al llegar a `maxRight` (tamaño 1: carácter de 6 px; tamaño ≥ 2: avance del glifo escalado).

### 11.3 Pantalla 1: lista de categorías (`setView = 0`, `settingsListContent`, `:425`)

| Elemento | Posición / tamaño (px) | Estilo |
|---|---|---|
| Título `"Ajustes"` | (16, 40) | tamaño 4, `TH_TXT` |
| Banda con scroll | y 104..729 (`SET_LIST_TOP = 104`, `SET_LIST_BOT = 800-70 = 730`, exclusivo) | recorte |
| Tarjeta de categoría *i* (0..11) | x 14, y `104 + i·74 − listScroll`, 452×66, radio 16 | vidrio/plano (§11.2) |
| Icono de categoría | caja 32×32 en (30, y+17) | color de acento `SET_ACCENT[i]` (02 §7.10) |
| Nombre `SET_CAT[i]` | (76, y+14), recorte en x = 436 | tamaño 2 `TH_TXT` |
| Subtítulo `SET_SUB[i]` | (76, y+38), recorte en x = 436 | tamaño 1 `TH_TXT2` |
| Chevron | (446, y+33) | §11.2 |

Alto del contenido `setListH = 12·74 + 10 = 898` → scroll máx. `898 − 626 = 272`. Las tarjetas fuera de la banda no se
dibujan, pero su rango Y se registra igual (`setCatY0/Y1`) para el hit-test.

**Textos exactos** (literales del código, **sin traducción** y con las tildes tal como están; `:78-92`):

| # | `SET_CAT` (título) | `SET_SUB` (subtítulo en la lista) | `SET_DESC` (bajo el título de su pantalla) | Acento | Icono (`drawSetCatIcon`) |
|---|---|---|---|---|---|
| 0 | `General` | `Idioma, fecha, hora` | `Configura las opciones basicas del sistema.` | `#4678EB` | engranaje (8 dientes, hueco del color de la tarjeta) |
| 1 | `Pantalla` | `Brillo, fondo, tema` | `Brillo, fondo de pantalla y modo oscuro.` | `#F0AA32` | sol (8 rayos) |
| 2 | `Sonido` | `Volumen, tonos` | `Volumen, tonos y notificaciones.` | `#4678EB` | altavoz + arco |
| 3 | `Red e Internet` | `WiFi, Bluetooth` | `Conexiones de red (offline por ahora).` | `#3C96EB` | Wi-Fi |
| 4 | `Dispositivos` | `GPIO, perifericos` | `GPIO, modulos y perifericos.` | `#50B478` | cubo isométrico |
| 5 | `Personalización` | `Temas, iconos` | `Temas, iconos y estilo del sistema.` | `#5A6EEB` | pincel |
| 6 | `Seguridad y privacidad` | `Bloqueo y permisos` | `Bloqueo, permisos y privacidad.` | `#5A5F6E` | candado |
| 7 | `Batería` | `Ahorro de energia` | `Estado de la bateria y ahorro de energia.` | `#50BE6E` | batería |
| 8 | `Almacenamiento` | `Interna, SD` | `Memoria interna y tarjeta SD.` | `#965AD2` | discos apilados |
| 9 | `Desarrollador` | `Opciones dev` | `Herramientas y diagnostico de desarrollo.` | `#464B5A` | `</>` |
| 10 | `Sistema` | `Sistema, logs` | `Informacion del sistema y registros.` | `#4678EB` | info (i) |
| 11 | `Acerca de` | `Version, creditos` | `Version, hardware y creditos de FlexOS.` | `#4678EB` | info (i) |

### 11.4 Pantalla 2: una categoría (`setView = 1`)

**Cabecera** (`settingsDrawDetailHead`, `:446`): chevron "<" `TH_TXT` de grosor 2.6: `(30, 46)→(18, 58)→(30, 70)`;
título `SET_CAT[setSel]` tamaño 4 `TH_TXT` en (52, 42) recortado en x = 468; descripción `SET_DESC[setSel]` tamaño 1
`TH_TXT2` en (52, 94) recortada en x = 468.

**Banda con scroll**: y 128..729 (`DLIST_TOP = 128`, `DLIST_BOT = 730`). El contenido arranca en `y = 128 + 6 −
setScroll`; alto `setContentH = (y_final − base) + 10`.

**Fila** `setRowCard(y, icono, color, título, valor, chevron)` (`:239`): paso **64 px**; tarjeta `(14, y, 452, 56)`
radio 14 (vidrio/plano); glifo `drawRowGlyph` centrado en (40, y+28) con el color de la fila; título tamaño 2 `TH_TXT`
en (66, y+10); valor tamaño 1 `TH_TXT2` en (66, y+34) (ambos recortados en x = 436); chevron opcional en (446, y+28).
Cada fila registra su rango `[y, y+64)` en `setRowY0/Y1` (máx. 16 filas por pantalla); el índice de la fila es el
**orden de llamada** (es lo que usa `settingsRowAction`).

Glifos de fila (02 §6.5): `RI_GLOBE` globo, `RI_CAL` calendario, `RI_CLOCK` reloj, `RI_PIN` chincheta, `RI_REFRESH`
flecha circular, `RI_CLOUD` nube, `RI_RESET` flecha circular inversa, `RI_DOT` punto r4.

**Título de sección**: `y += 8`, texto tamaño 2 `TH_TXT` en (14, y), `y += 30`. **Ayuda**: `y += 8`, texto tamaño 1
`TH_MUTE` en (14, y), `y += 24`.

**Bloque "Dispositivo"** (`drawDeviceInfo`, `:257`): `"Dispositivo"` tamaño 2 `TH_TXT` en (14, y), `y += 30`; luego
filas de información (`drawInfoLine`, `:252`) con etiqueta tamaño 1 `TH_TXT2` en x = 14 y valor tamaño 1 `TH_TXT`
**alineado a la derecha en x = 468**, paso 24:

| Etiqueta | Valor | Fuente |
|---|---|---|
| `Nombre` | `cfgName` (NVS `"name"`, def. `"FlexOS Ultra"`) | real |
| `Modelo` | `ESP32-P4 DevKit` | fijo |
| `Version` | `FlexOS Ultra 1.0` | fijo |
| `Actividad` | `buildUptime`: `"%lud %luh %lum"` / `"%luh %lum"` / `"%lum"` | real (`millis`) |
| `RAM` | `"%u KB libre"` = `esp_get_free_heap_size()/1024` | real (incluye PSRAM, ver §18) |
| `PSRAM` | `"%u / %u MB"` = **libre / total** | real |
| `Flash` | `16 MB` | fijo |
| `CPU` | `RISC-V dual 360 MHz` | fijo |

### 11.5 Contenido y acción de cada categoría (`settingsDetailContent`, `:273`; `settingsRowAction`, `:634`)

Notación: **idx** = índice de fila; ✓ = lleva chevron; "—" = sin acción al tocar. Toda acción que cambia el aspecto del
escritorio pone `gHomeDirty = true` (la caché del Inicio se recompone antes de volver a mostrarse).

#### 0 · General

| idx | Glifo / color | Título | Valor | > | Acción al tocar | Persistencia |
|---|---|---|---|---|---|---|
| 0 | GLOBE `#3C8CEB` | `Idioma` | `Español` / `English` / `Français` / `Português` / `Italiano` / `Chinese` (`LANG_ENDONYM`, ZH → literal `"Chinese"`) | ✓ | `cfgLang = (cfgLang+1) % 6` (cicla ES→EN→FR→PT→IT→ZH→ES); guarda; repinta **toda** la pantalla | `"lang"` I32 def. 0 |
| 1 | CAL `#EB5A5A` | `Fecha y hora` | `"%02d/%02d/%04d  %d:%02d %s"` (día/mes/año, dos espacios, **siempre 12 h con AM/PM**) | ✓ | **— (no hace nada)** | — |
| 2 | CLOCK `#5A78E6` | `Formato de hora` | `24 horas` / `12 horas` | ✓ | `g24h = !g24h`; guarda; repinta la banda | `"h24"` bool def. false |
| 3 | PIN `#E65050` | `Zona horaria` | `GMT-05:00 Lima (sin DST)` | — | — (zona fija por diseño) | — |
| 4 | CLOUD `#3CAADC` (o `#DC5050` si NVS de la hora falla) | `Sincronización automática` | `ntpStateText` (§12.6) | — | — | — |
| 5 | CLOCK `#5A78E6` | `Última sincronización` | `ntpLastSyncText` (§12.6) | — | — | — |
| 6 | REFRESH `#50B478` | `Sincronizar ahora` | `Sincronizando...` si hay consulta en vuelo, si no `Consultar el servidor de hora` | ✓ | `ntpRequestSync(true)` (salta la espera progresiva); repinta la banda | — |
| 7 | REFRESH `#3CA0E6` | `Actualizaciones` | `flexOtaStatusText()`: `Buscando actualizaciones...` / `El sistema esta al dia` / `"<versión> disponible"` / texto de error OTA / `Buscar actualizacion` / fase (`Preparando...`, `Finalizando...`, `Reiniciando...`…) / `No disponible` (OTA compilado fuera) | ✓ | `flexOtaOpenSettings()` → capa OTA de ajustes (área OTA) | — |
| 8 | CLOUD `#78A0E6` | `Copias de seguridad` | `Proximamente` | ✓ | **—** | — |
| 9 | RESET `#DC5050` | `Restablecer` | `Opciones de fabrica` | ✓ | `frEnterWizard()` → asistente de restablecimiento `ST_FACTORY` (área recuperación). Con un OTA en curso muestra en su lugar `"Actualizacion en curso"` (tamaño 2) / `"Espera a que termine para restablecer"` (tamaño 1) y se retira solo | — |
| — | sección | `Cuenta` | | | | |
| 10 | DOT `#695BE6` | `Flex Account` | `accountSettingsText`: `Vinculacion en curso` / dirección Flex / etiqueta de enlace (`flexAccountLinkLabel`) / `"<dirección> · sin conexión"` / `Sin cuenta vinculada` | ✓ | `accountSettingsEnter()` → pantalla Flex Account `ST_OOBE_ACCOUNT` con retorno `ACC_RET_SETTINGS` (01a §3.3); al terminar vuelve a esta pantalla | — |
| — | bloque | `Dispositivo` (§11.4) | | | | |

Nota de orden: la fila Flex Account se añadió **al final** a propósito para no mover los índices 0, 2, 6, 7 que usa
`settingsRowAction` (`:303-307`). En LVGL las acciones se atan a cada objeto fila y el problema desaparece.

#### 1 · Pantalla

| idx | Glifo / color | Título | Valor | > | Acción | Persistencia |
|---|---|---|---|---|---|---|
| 0 | DOT `#F0AA32` | `Brillo` | `"%d%%"` (`gBright`) | ✓ | `gBright += 25`; si > 100 → 25 (desde el 80 de fábrica: 80→25→50→75→100→25…); `setBacklight` (PWM `map(pct, 0..100 → 25..255)`, mínimo 5 %); guarda; repinta la banda (no marca el Inicio) | `"bright"` I32 def. 80 |
| 1 | DOT `#5A6EEB` | `Estilo` | `Liquid Glass` / `Plano` | ✓ | `uiGlass = !uiGlass; themeChanged()` (guarda, invalida cachés visuales y repinta, 02 §2.7) | `"glass"` bool def. false |
| 2 | DOT `#7882DC` (oscuro) / `#F0AA32` (claro) | `Modo de apariencia` | `Oscuro` / `Claro` | ✓ | `gDark = !gDark; themeChanged()` | `"dark"` bool def. true |
| 3 | DOT `#64B4F0` | `Barra de navegacion` | `Botones` / `Gestos iOS` | ✓ | `gNavMode` 0↔1; guarda; marca Inicio; repinta todo | `"navmode"` I32 def. 0 |
| — | ayuda | `Toca una fila para cambiarla` | | | | |
| — | sección | `Bloqueo` | | | | |
| 4 | CLOCK `#5A78E6` | `Reloj grande` | `Activado` / `Desactivado` | ✓ | `gLockWidgets ^= LW_CLOCK (0x01)`; guarda | `"lockwidgets"` I32 def. 1 (`LW_CLOCK`) |
| 5 | CLOUD `#5AAAEB` | `Clima` | ídem | ✓ | `^= LW_WEATHER (0x02)` | ídem |
| 6 | CAL `#EB6E5A` | `Calendario` | ídem | ✓ | `^= LW_CAL (0x04)` | ídem |
| 7 | DOT `#E6B45A` | `Notificaciones` | ídem | ✓ | `^= LW_NOTIF (0x08)` | ídem |
| — | ayuda | `Elige los widgets de la pantalla de bloqueo` | | | | |

Los widgets del bloqueo se describen en 01a §4.3. "Estilo" e "Intensidad del vidrio" también están en el Panel rápido
(01b §5.3); cualquier cambio de uno se refleja en el otro porque comparten la variable.

#### 2 · Sonido (`:374-387`)

Con códec disponible (`flexAudioAvailable()`): fila `Volumen` → `"%d%%"` + `" (silenciado)"` si está silenciado; fila
`Salida` → `Altavoz (ES8311)`. Sin códec: `Volumen` → `Sin salida de audio`; `Motivo` → `flexAudioError()`. Glifo DOT
`#4678E1`, con chevron, **sin acción**. Ayuda final `Mas opciones proximamente`. (El volumen se cambia en el Panel
rápido, otra área.)

#### 3 · Red e Internet (`:341-354`, datos reales)

| idx | Glifo / color | Título | Valor | > | Acción |
|---|---|---|---|---|---|
| 0 | GLOBE `#3C96EB` | `Conectividad` | `Modo avión activo` si `gAirplane`, si no `Wifi, BLE y modo avión` | ✓ | `connEnter()` → pantalla `ST_CONN` ("Conectividad": tarjetas `Wifi`, `BLE`, `Modo avión`; salir con toque en x<60, y<60) — área de red |
| 1 | CLOUD `#4678E1` | `Wi-Fi` | `connWifiSub` sin paréntesis: `No disponible` / `Modo avión` / `Desactivado` / `<SSID real>` / `Conectado` / `Conectando...` / `Buscando redes...` / `No conectado` | ✓ | `wifiSettingsEnter()` → pantalla `ST_WIFI` (área de red) |
| 2 | DOT `#5A6EEB` | `Bluetooth (BLE)` | `connBleSub` sin paréntesis: `No disponible` (chip sin BLE: **es el caso del ESP32-P4**) / `Desactivado al compilar` / `Modo avión` / `Visible como "FlexOS"` / `Desactivado` | ✓ | si BLE compilado y sin modo avión: `flexBleStop()`/`flexBleStart()` y repinta; si no, nada |
| — | ayuda | `Los interruptores encienden la radio de verdad` | | | |

Persistencia relacionada: modo avión NVS `"airpl"` bool def. false (lo escribe la pantalla Conectividad).

#### 4 · Dispositivos (maqueta)

Filas DOT `#4678E1` con chevron y sin acción: `GPIO` → `Configurable`; `Perifericos` → `Ninguno`. Ayuda `Mas opciones
proximamente`.

#### 5 · Personalización

| idx | Glifo / color | Título | Valor | > | Acción | Persistencia |
|---|---|---|---|---|---|---|
| 0 | DOT `#5A6EEB` | `Personalizar UI` | `Liquid Glass` / `Plano` | ✓ | igual que Pantalla › Estilo (`uiGlass`, `themeChanged`) — **duplicado** | `"glass"` |
| 1 | DOT `#965AD2` | `Iconos` | `Vidrio` / `Plano` | ✓ | `gIconStyle` 0↔1; guarda; marca Inicio | `"iconstyle"` I32 def. 0 |
| 2 | DOT `#5AC8A0` | `Transiciones` | `Zoom` / `Fundido` / `Deslizar` | ✓ | `gAnimStyle = (gAnimStyle+1) % 3` (0 zoom → 1 fundido → 2 deslizar → 0); guarda | `"animstyle"` I32 def. 0 |
| 3 | DOT `#EB963C` | `Teclado` | `Compacto` / `Normal` / `Grande` (`gKbSize`) | ✓ | `kbsEnter()` → **Ajustes del teclado** `ST_KBSET` (01b §8) | claves `kb*` (§16) |
| — | ayuda | `Toca una fila para cambiar su estilo` | | | | |

Las transiciones de app se describen en 02 §8.7. La fila Teclado existe porque `KB_SETTINGS_ON = 1` (`Types.h:188`).
Resumen de la pantalla del teclado (detalle en 01b §8): filas `Idiomas y tipos`, `Texto predictivo`, `Revisión ortográfica
básica`, `Sugerir emojis`, `Atajos de texto` (abreviación → expansión), `Barra de herramientas del teclado`, `Teclado de
contraste alto`, `Escritura rápida (multitoque)`, `Diseño y tamaño`, `Deslizar, tocar y respuesta táctil`, `Entrada de
voz` (inactiva: "Necesita reconocimiento en la nube"), `Guardar capturas en portapapeles` (inactiva), `Restablecer ajustes
del teclado`, `Sobre teclado`; subpantallas de idiomas, tamaño/diseño/fuente/transparencia, animación y pulsación larga,
símbolos personalizados (4) y atajos (8).

#### 6 · Seguridad y privacidad

| idx | Glifo / color | Título | Valor | > | Acción | Persistencia |
|---|---|---|---|---|---|---|
| 0 | DOT `#DC7878` | `Bloqueo` | `PIN configurado` / `Contraseña configurada` / `Deslizar` (según `gLockType` 1/2/0) | ✓ | `lsuEnter()` → crear/cambiar clave `ST_LOCKSETUP` (01a §5.2) | `"locktype"` I32 def. 0 (la escribe esa pantalla) |
| 1 | CLOCK `#7896EB` | `Bloqueo de inactividad` | `30 segundos` / `1 minuto` / `5 minutos` / `10 minutos` / `30 minutos` / `Nunca` | ✓ | cicla `AUTOLOCK_OPTS = {30000, 60000, 300000, 600000, 1800000, 0}` ms; **reinicia el temporizador** (`gLastTouchMs = millis()`); guarda | `"autolockms"` I32 def. 60000 (un valor que no esté en la lista se normaliza a 60000) |
| 2 | DOT `#E6785A` | `Protección contra robo` | `theftRowValue`: `Monitoreando movimiento…` / `Protección pausada` / `Bloqueado por seguridad` / `Desactivada` | ✓ | `theftEnter()` → pantalla `ST_THEFT` (área sensores) | (propia de esa área) |
| 3 | DOT `#DC7878` | `Apagado seguro` | `Configura antes un PIN` (sin clave) / `Activado` / `Desactivado` | ✓ | sin clave: nada; con clave: `gPoffPin = !gPoffPin`; guarda (solo afecta al apagado completo, nunca a la suspensión) | `"poffpin"` bool def. false |
| — | ayuda | `Toca para configurar PIN o contraseña` | | | | |

La fila 3 existe porque `POWEROFF_ON && POWEROFF_PIN_ON` = 1 (`Types.h:315-316`).

#### 7 · Batería (maqueta)

Filas DOT `#4678E1` sin acción: `Nivel` → **`82%` fijo**; `Ahorro` → `Desactivado`. Ayuda `Mas opciones proximamente`.
(No hay medida de batería en el firmware; ver §18.)

#### 8 · Almacenamiento (datos reales, `:392-400`)

`Memoria interna` → `"<usado> / <total>"` (`flexFsFmtSize`, p. ej. `"1.2 MB / 8.0 MB"`) o `No montada`; `Papelera` →
tamaño real de `/Papelera`. Sin acción (la app Almacenamiento, 03 §9, es la vista completa). Ayuda `Mas opciones
proximamente`.

#### 9 · Desarrollador (maqueta)

`Depuracion` → `En pantalla`; `Banda reinicio` → `Solo crash`. Sin acción. Ayuda `Mas opciones proximamente`.

#### 10 · Sistema (maqueta)

`Version` → `FlexOS 1.0`; `Logs` → `Puerto serie`. Sin acción. Ayuda `Mas opciones proximamente`.

#### 11 · Acerca de

Bloque `Dispositivo` (§11.4) y dos líneas tamaño 1 `TH_MUTE`: `FlexOS Ultra - desde cero` y `para ESP32-P4 - 2026`
(paso 22 px). Sin filas tocables.

(La rama `case 6` de la parte genérica, `:390`, es **código muerto**: Seguridad tiene su propia rama.)

### 11.6 Interacción (`settingsTick`, `:697-743`)

1. **Volver** (solo en la pantalla 2):
   * *tap* con `x < 52` (`SET_BACK_W`) y `20 ≤ y ≤ 84` → `settingsHandleBack()`;
   * **deslizar desde el borde izquierdo**: si el apoyo es `T.startX < 28` se arma; **al soltar**, si `x − startX > 70`
     y `|y − startY| < 90` → atrás. Se decide al soltar para que un scroll que empiece cerca del borde no lo dispare;
   * botón Atrás del sistema / gesto → gancho `backScreen = settingsHandleBack`: devuelve `false` en la lista (el sistema
     cierra la app) y `true` si volvió de una categoría.
2. ***Tap*** sin arrastre con `y` dentro de la banda activa: en la lista → abre la categoría cuya tarjeta contiene `y`
   (`settingsOpenCat`); en una categoría → `settingsRowAction(setSel, fila)` por rango registrado. Recordatorio: la `y` de
   un *tap* es la del **apoyo** (03 §0.1).
3. **Scroll** de la vista activa: ancla en `T.pressed` si `top−24 ≤ y ≤ bot`; con `T.down`, contenido desbordado y el
   apoyo dentro de esa zona, se convierte en arrastre al superar **6 px**; el contenido sigue al dedo (sin inercia ni
   rebote), acotado a `[0, máx]`; cada cambio repinta solo la banda. Al soltar un arrastre → `sessMarkDirty(IC_AJUSTES)`.
4. **Sin estado pulsado** visual en filas/tarjetas.
5. No hay refresco periódico: la hora de la barra propia, la fila "Fecha y hora" y el estado NTP **solo se actualizan al
   repintar por un toque** (ver §18).

### 11.7 Transición entre pantallas (`settingsAnimate`, `:539-590`)

* Abrir categoría (`dir = +1`) y volver (`dir = −1`), **270 ms** (`SET_NAV_MS`), curva **ease-in-out cúbica**
  (`p<0.5 ? 4p³ : 1 − 4(1−p)³`), interpolada por tiempo real.
* Se pintan las dos páginas completas en dos lienzos PSRAM (480×800×2 = 750 KB cada uno, reservados la primera vez y
  conservados). Solo se mueve la banda **y 40..739** (`SET_ANIM_Y0 = 40`, `SET_ANIM_Y1 = 800-60`): barra de estado y de
  navegación quietas.
* **Empuje con paralaje**: la página que queda debajo recorre solo el **28 %** del ancho (134 px).
  * Abrir: la lista sale de 0 → −134 px; la categoría entra de +480 → 0 y va **encima**.
  * Volver: la categoría sale de 0 → +480 **encima**; la lista vuelve de −134 → 0.
* **Sombra** de 8 px a la izquierda del borde de la página de encima: columna `k = 1..8` mezclada con `TH_SHADOW` con
  alfa `effShadow(70 − 8k)`.
* Hospedada en Modo PC o sin PSRAM: cambio instantáneo (la función nunca se pierde, solo la animación). Al terminar se
  repinta la página destino limpia.

### 11.8 Ciclo de vida y sesión (`:745-786`)

* `settingsEnter`: `appLoadSessionOnce(IC_AJUSTES)` (la primera apertura tras el arranque recupera categoría y scrolls;
  si la sesión decía "categoría" se abre directamente ahí, sin animación), corta arrastres, repinta.
* `setSuspend`: corta arrastre y gesto de borde. `setResume`: idem + repinta (misma categoría, mismo scroll).
* Sesión `ajustes.bin` v1 (`/System/Sessions/`, formato 02 §8.12): `SetSessV1 { i16 view, sel, scroll, listScroll; }`
  (8 B). Se ignora entera si `sel ∉ [0, 11]`; `view` ≠ 1 → 0; scrolls negativos → 0. Se marca sucia al abrir/cerrar
  categoría y al terminar un arrastre. **No duplica** valores de configuración (esos viven en NVS).
* `setBgWork()` = `flexOtaBusy()`: mientras el OTA lanzado desde aquí tenga trabajo de red, Ajustes **no se cierra** ni
  por "Cerrar todo" ni por el límite de sesiones (es el único proceso en segundo plano real, `:783-786`).

### 11.9 Persistencia que toca Ajustes

Todo en NVS espacio **`flexos`** vía `cfgSavePrefs()` (`Prefs.h:290-306`), que **reescribe de una vez** `lang, h24, glass,
glasslv, dark, iconstyle, bright, lockwidgets, navmode, animstyle, autolockms, poffpin` + las 15 claves del teclado. Tabla
completa en §16. `themeChanged(save)` (`System.h:417`) llama a `cfgSavePrefs()` y además invalida `homeBuf`, cortina,
tarjeta de vidrio cacheada, banda pre-desenfocada, fondo de Modo PC y miniaturas de Recientes, y repinta la pantalla actual.

### 11.10 Casos límite

* Ajustes es la "salida" del sistema: sin ocultar, abrible en Modo seguro, sin candado de app en la práctica (si se le
  pone candado, se pide la clave como a cualquier app).
* Idioma ZH: el valor muestra `Chinese` y toda la interfaz cae a inglés (la fuente no tiene glifos CJK; `LI()`).
* BLE en ESP32-P4: siempre `No disponible` (el SoC no tiene radio BLE; `FLEXOS_BLE_HW = 0`, `.ino:216-224`).
* Brillo: el ciclo de 25 en 25 no permite fijar valores intermedios desde Ajustes (sí desde el deslizador del Panel
  rápido, 5..100).
* Máximo 16 filas registradas por pantalla (`SET_ROW_MAX`); General usa 11.

### 11.11 Migración a IDF/LVGL

* **Estructura**: una pantalla LVGL con dos `lv_obj` de página (lista y categoría) dentro de un contenedor 480×(800 −
  barra de estado); navegación con `lv_screen_load_anim`-equivalente propio: animar `x` de las dos páginas (paralaje 28 %)
  con `lv_anim` de 270 ms y `lv_anim_path_ease_in_out` (o la cúbica exacta con `lv_anim_path_custom`), página entrante con
  sombra izquierda (`shadow_width` 8, `shadow_ofs_x` −4). Gesto de borde: `LV_EVENT_PRESSED` con `x < 28` + decisión en
  `LV_EVENT_RELEASED` (dx > 70, |dy| < 90).
* **Filas**: componente `flex_settings_row` (icono 24×24 + título 13 px + valor 10-11 px + chevron) con `user_data` =
  callback de acción. Las listas son `lv_obj` con `LV_FLEX_FLOW_COLUMN` y scroll vertical (sin inercia en el original;
  LVGL añade momento por defecto: aceptable, o quitar `LV_OBJ_FLAG_SCROLL_MOMENTUM` para ser fiel).
* **Barra de estado**: usar la barra global del sistema (no copiar el Wi-Fi/batería fijos de la app).
* **Valores vivos**: suscribirse a eventos del bus (`TIME_MINUTE`, `NTP_STATE_CHANGED`, `OTA_STATE_CHANGED`,
  `ACCOUNT_CHANGED`, `WIFI_STATE_CHANGED`, `AUDIO_CHANGED`) y actualizar solo la etiqueta afectada; corrige el "valor
  congelado" del original.
* **Persistencia**: la IDF ya tiene `flex_kv` + `flex_cfg_get/set_*` (`components/flex_storage`, usado por `flex_theme.c`,
  `flex_i18n.c`, `flex_lock.c`): cada fila escribe **solo su clave** (no reescribir 30 claves por toque). Mismos nombres
  y tipos para leer NVS dejada por la versión Arduino.
* **Lógica pura con pruebas host**: ciclos de valores (idioma 6, brillo +25 con vuelta a 25, autobloqueo 6 opciones +
  normalización, transiciones 3, bits de `lockwidgets`), textos derivados (`connWifiSub` sin paréntesis,
  `accountSettingsText`, `buildUptime`), validación de la sesión `SetSessV1`.
* **No copiar**: la fila "Fecha y hora" con chevron sin acción (o implementar ajuste manual de fecha/hora), el 82 %
  de batería, los valores "maqueta" de Dispositivos/Desarrollador/Sistema (marcarlos como no disponibles), la duplicación
  Estilo/Personalizar UI (mantenerla si se quiere 1:1, pero que ambas filas lean el mismo estado).

---

## 12. Hora del sistema y NTP

### 12.1 Modelo del reloj (`FlexOS_Ultra_Clock.h:30-157`) — lógica pura

* **Fuente de verdad única**: una época UTC en segundos `clkEpochRef` anclada a un `millis()` `clkRefMs`
  (`clkAnchored`). `clkNowUtc() = clkEpochRef + (millis() − clkRefMs)/1000` (resta sin signo: correcta con el
  desbordamiento de `millis`).
* **Zona horaria fija**: Lima/Perú **UTC−5 sin horario de verano** (`FLEXOS_TZ_OFFSET_SEC = −18000`). No hay selector.
* **Semilla de fábrica**: sábado **4 de julio de 2026, 13:23 hora local** (`clkSeedFactory`, época 1783189380).
* **Suelo de cordura** `FLEXOS_CLK_MIN_EPOCH = 1767225600` (1 ene 2026 00:00 UTC): ninguna época menor se acepta de NTP
  ni de NVS.
* `clkUpdate()` (cada vuelta de `loop()`, `.ino:771`): siembra si no hay ancla; **re-ancla cada hora** (`clkSetEpoch(
  clkNowUtc())`, mantiene el delta de `millis` pequeño); calcula hora local; si cambió el minuto rellena `rtcY, rtcMo,
  rtcD, rtcWd (0 = domingo), rtcH, rtcMin` y devuelve `true` → `gMinChanged` (lo usan Reloj, Calendario, Caja, Modo
  PC…). Conversión días↔fecha civil con el algoritmo de **Howard Hinnant** (`clkDaysFromCivil`/`clkCivilFromDays`,
  aritmética entera, cualquier año), día de la semana `(días + 4) mod 7` (1970-01-01 fue jueves).
* `clkSetEpoch(utc)`: fija ancla, fuerza repintado del minuto (`clkLastMin = −1`) y **avisa al motor del clima**
  (`flexWeatherSetClock(utc)`, §13.2).
* `clkSetDate(addDays)`: compatibilidad (desplaza la fecha N días); sin llamantes en esta área.

### 12.2 Formatos de hora y fecha

| Función | Salida | Uso |
|---|---|---|
| `clkStr12` (`Clock.h:181`) | 24 h `"13:23"`; 12 h `"1:23"` (**sin AM/PM**: el reloj vectorial gigante solo tiene dígitos y `:`) | reloj grande del bloqueo y app Reloj |
| `clkStrBar` (`:193`) | 24 h `"13:23"`; 12 h `"1:23 PM"` / `"12:05 AM"` | barras de estado (búfer ≥ 12 B) |
| `settingsDateTimeStr` (`AppSettings.h:135`) | `"04/07/2026  1:23 PM"` (siempre 12 h) | Ajustes › Fecha y hora |
| `ntpLastSyncText` (`NTP.h:285`) | `"Nunca"`, `"Hoy 13:24"`, `"Ayer 09:10"`, `"03/07/2026 22:00"` (siempre 24 h) | Ajustes › Última sincronización |
| `buildUptime` (`AppSettings.h:139`) | `"2d 3h 15m"`, `"3h 15m"`, `"15m"` | Ajustes › Actividad |
| `buildShortDate` / `buildLongDate` (`Home.h`) | `"sáb, 4 jul"` / `"sábado, 4 de julio"` (localizadas) | barras, Reloj, Calendario (03 §1) |

### 12.3 Cliente SNTP propio (`NTP.h:126-189`)

* **No** usa `configTime()`/SNTP de lwIP (ese re-sondea solo y no se puede acotar): socket UDP propio que se abre y se
  **cierra** en cada consulta.
* Paquete de 48 B: `pkt[0] = 0xE3` (LI = 3, VN = 4, modo 3 cliente), `pkt[1] = 0` (estrato), `pkt[2] = 6` (sondeo),
  `pkt[3] = 0xEC` (precisión), `pkt[12..15] = 0x31 0x4E 0x31 0x34` (id de referencia), resto 0. Puerto 123, puerto
  local efímero.
* Respuesta: se esperan ≥ 48 B sondeando `parsePacket()` cada **20 ms** (`vTaskDelay`) hasta **4000 ms** por servidor;
  se leen los **segundos** del sello de transmisión (bytes 40..43, big-endian), se resta `2208988800` (1900→1970) y se
  descarta si `< FLEXOS_CLK_MIN_EPOCH`. **Precisión ±1 s** (sin fracción ni compensación de ida y vuelta).
* Servidores en orden, gana el primero que responde: **`pool.ntp.org`**, **`time.google.com`**,
  **`time.cloudflare.com`**. Si el Wi-Fi cae a mitad, no se insiste.
* Tarea **`ntp`** de un solo uso: `xTaskCreatePinnedToCore(ntpTask, "ntp", 4096, NULL, prioridad 1, NULL, núcleo 1)`;
  al terminar escribe resultado en variables `volatile` y se borra (`vTaskDelete(NULL)`). Éxito → `clkSetEpoch(utc)`,
  `gNtpLastSyncUtc = utc`, `gNtpFromNvs = false`, `gNtpFails = 0`, `NTPS_OK`, `clkSaveNvs()`. Fallo → `gNtpFails++`
  (tope 8), `NTPS_FAIL`. Si la tarea no se puede crear → `NTPS_FAIL` inmediato.

### 12.4 Planificación (máquina de estados, `ntpRequestSync` `:203`, `ntpTick` `:220`)

Estado: `gNtpState ∈ {NTPS_NEVER=0, NTPS_OK, NTPS_FAIL, NTPS_BUSY}`, `gNtpBusy`, `gNtpDoneMs` (fin del último intento),
`gNtpNextMs` (próximo intento permitido; 0 = "nunca programado"), `gNtpFails`.

```
           ntpOnWifiUp(): fails=0, next = now+1500
NEVER/OK/FAIL ──(due && online && !airplane)──► BUSY (tarea ntp) ──ok──► OK   (next = done + 6 h)
                     ▲   ntpRequestSync(true) salta la espera     └─fallo─► FAIL (next = done + backoff)
```

* `ntpRequestSync(userAsked)`: no hace nada si ya hay consulta, en **modo avión** o **sin red** (`gNetOnline`); si no la
  pidió el usuario, respeta `gNtpNextMs`. "Sincronizar ahora" (Ajustes) y el control `QSID_NTP` del Panel rápido pasan
  `true`. **No bloquea** nunca.
* `ntpTick()` (cada vuelta, salvo Modo seguro, `.ino:768`): si un intento acaba de terminar, programa el siguiente:
  éxito → **+6 h** (`NTP_PERIOD_MS`); fallo → espera progresiva `30 s << (fallos−1)` con techo **30 min**:
  1.º 30 s, 2.º 60 s, 3.º 2 min, 4.º 4 min, 5.º 8 min, 6.º 16 min, 7.º y siguientes 30 min. Sin red o en modo avión no
  hace nada. Primera sincronización si `gNtpNextMs == 0`: solo con `gState ∈ {ST_HOME, ST_LOCK}` y `millis() ≥ 1500`.
* `ntpOnWifiUp()` (lo llaman las dos tareas de conexión Wi-Fi al confirmar enlace, `Network.h:398, 437`): reinicia
  fallos y programa un intento a **+1500 ms** (`NTP_FIRST_DELAY`).
* Perder el Wi-Fi **no** para el reloj (sigue el ancla).

### 12.5 Persistencia de la hora (`NTP.h:78-124, 250-258`)

* NVS espacio **`flexos_time`** (propio, distinto de `flexos`): clave **`epoch`** (U64, época UTC actual) y
  **`lastsync`** (U64, época de la última sincronización correcta). Se guarda la UTC, nunca la local.
* `clkSaveNvs()`: solo si hay ancla y `now ≥ MIN_EPOCH`. Se llama tras cada sincronización correcta (desde la tarea
  `ntp`), **una vez por hora** (`clkPersistTick`, `.ino:770`; la primera vez en la primera vuelta de `loop()`) y en el
  **apagado limpio** (`poffSaveCleanFlag`, `Power.h:931`).
* `clkLoadNvs()` (en `setup()`, `.ino:520-525`, tras `clkSeedFactory()`): abre en solo lectura; si el espacio no existe
  (placa recién flasheada) reintenta en lectura/escritura (lo crea). Solo si eso también falla → `gTimeNvsOk = false`
  (Ajustes muestra el error). Si `epoch ≥ MIN_EPOCH` → `clkSetEpoch(epoch)`, `gNtpLastSyncUtc = lastsync`,
  `gNtpFromNvs = true` (hora **aproximada**: el equipo estuvo apagado sin contar). Después `clkUpdate()`.

### 12.6 Textos de estado (Ajustes › General)

`ntpStateText` (`NTP.h:262`), por orden de prioridad:

| Condición | Texto exacto |
|---|---|
| Build sin Wi-Fi por core inseguro | `Requiere core ESP32 3.2.1` |
| Build sin Wi-Fi | `Wi-Fi desactivado en este build` |
| NVS de la hora no disponible | `Error: almacenamiento NVS no disponible` (y el icono de la fila pasa a `#DC5050`) |
| Consulta en vuelo | `Sincronizando...` |
| `NTPS_OK` | `Sincronizado - UTC-5 (Lima)` |
| `NTPS_FAIL` + modo avión | `Modo avión activo` |
| `NTPS_FAIL` + con red | `Sin respuesta del servidor` |
| `NTPS_FAIL` + sin red | `Sin conexión Wi-Fi` |
| Nunca + hora de NVS | `Hora aproximada (sin sincronizar)` |
| Nunca + modo avión | `Modo avión activo` |
| Nunca + sin red | `Esperando Wi-Fi` |
| Nunca + con red | `Pendiente` |

`ntpLastSyncText`: `Nunca` si `lastsync < MIN_EPOCH`; mismo día local → `"Hoy %02d:%02d"`; día anterior →
`"Ayer %02d:%02d"`; si no `"%02d/%02d/%04d %02d:%02d"` (d/m/a).

### 12.7 Casos límite

* Sin red jamás: el reloj arranca en la semilla (2026-07-04 13:23) o en la última hora guardada, y avanza con `millis`.
* El sello NTP se acepta tal cual (sin RTT): error de hasta ~1 s + latencia.
* `gNtpUserAsked` se guarda pero no se usa (no hay aviso de "hora sincronizada" para el usuario).
* Fecha anterior a 2026 o servidor que responde ceros → rechazada (`MIN_EPOCH`).

### 12.8 Migración a IDF

* **Servicio `flex_time`** (componente propio): ancla = reloj POSIX del sistema. Al sincronizar, `settimeofday()`;
  `TZ` con `setenv("TZ", "<-05>5", 1); tzset();` y `localtime_r` para los campos `rtc*`. Publicar eventos
  `TIME_MINUTE` (cambio de minuto, sustituye a `gMinChanged`), `TIME_DAY`, `TIME_SET`, `NTP_STATE_CHANGED`.
* **SNTP**: dos opciones válidas. (a) `esp_netif_sntp` (IDF 5.5) con `esp_sntp_set_sync_interval(6 h)`, los tres
  servidores y `sync_cb` → estado OK; el reintento progresivo hay que emularlo parando/arrancando el cliente desde el
  servicio. (b) Mantener el cliente propio con sockets BSD de lwIP en una tarea (el paquete y el parseo son lógica pura).
  En ambos casos: nunca en el hilo de LVGL; solo con Wi-Fi arriba y sin modo avión; "Sincronizar ahora" fuerza.
* **Persistencia**: mismo espacio `flexos_time`, claves `epoch`/`lastsync` U64 (`flex_kv` U64), guardado horario y al
  apagar; en el arranque, sembrar con NVS antes de la UI.
* **Lógica pura → `tests/host`**: conversiones Hinnant (fechas límite, bisiestos, épocas negativas), día de la semana,
  re-anclaje sin salto, cálculo de la espera progresiva (tabla de §12.4), selección de `ntpStateText`, formato de
  `ntpLastSyncText` (Hoy/Ayer/fecha), construcción y validación del paquete SNTP (bytes 0-3, 12-15, 40-43, filtro).
* **No copiar**: la carrera de `clkSetEpoch` llamada desde la tarea `ntp` mientras la UI lee las dos palabras del ancla
  (en IDF lo resuelve `settimeofday`/`time()`); el guardado de la **semilla de fábrica** en NVS en la primera vuelta
  (tras un primer arranque sin red, el siguiente arranque la presenta como "Hora aproximada"); el re-anclaje horario que
  fuerza un falso "cambio de minuto".
